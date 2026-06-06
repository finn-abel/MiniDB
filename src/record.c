#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "buffer/buffer_pool.h"
#include "common.h"
#include "page.h"
#include "record.h"
#include "rid.h"
#include "row.h"
#include "storage/free_space.h"

static uint32_t record_required_page_space(uint32_t row_len) {
    /*
     * A new row normally needs row bytes plus one slot entry. If page_insert
     * can reuse a deleted slot, this is conservative but still correct.
     */
    return row_len + sizeof(PageSlot);
}

DBStatus record_insert(const char *table_file, Row *row, RID *out_rid) {
    if (table_file == NULL || row == NULL || out_rid == NULL) {
        return DB_ERROR;
    }

    uint8_t *row_bytes = NULL;
    uint32_t row_len = 0;

    /*
     * Convert the row into raw bytes.
     * Pages only store bytes, not Row structs.
     */
    DBStatus status = row_serialize(row, &row_bytes, &row_len);

    if (status != DB_OK) {
        return status;
    }

    uint32_t required_space = record_required_page_space(row_len);

    /*
     * Ask the free-space map for candidate pages instead of scanning every
     * page in the table file. free_space_find_page lazily rebuilds the map if
     * this table has not been opened through table_open yet.
     */
    uint32_t candidate_page_id = 0;
    DBStatus find_status = free_space_find_page(
        table_file,
        required_space,
        &candidate_page_id
    );

    while (find_status == DB_OK) {
        uint8_t *page_buffer = NULL;
        uint32_t page_id = candidate_page_id;

        /*
         * Fetch only the candidate page chosen by the FSM. This is the main
         * win over the previous "read every page until one works" approach.
         */
        status = buffer_pool_fetch_page(table_file, page_id, &page_buffer);

        if (status != DB_OK) {
            free(row_bytes);
            return status;
        }

        /*
         * page_insert will return DB_FULL if this page cannot fit the row.
         */
        uint16_t slot_id = 0;
        status = page_insert(page_buffer, row_bytes, row_len, &slot_id);

        if (status == DB_OK) {
            /*
             * The page changed in memory, so update the FSM before unpinning.
             * If the update fails, unpin as clean because the caller will see
             * an error and the changed page should not be flushed.
             */
            status = free_space_update(
                table_file,
                page_id,
                page_insertable_space(page_buffer)
            );

            if (status != DB_OK) {
                buffer_pool_unpin_page(table_file, page_id, false);
                free(row_bytes);
                return status;
            }

            status = buffer_pool_unpin_page(table_file, page_id, true);

            if (status != DB_OK) {
                free(row_bytes);
                return status;
            }

            status = buffer_pool_flush_page(table_file, page_id);

            if (status != DB_OK) {
                free(row_bytes);
                return status;
            }

            out_rid->page_id = page_id;
            out_rid->slot_id = slot_id;

            free(row_bytes);
            return DB_OK;
        }

        /*
         * DB_FULL means the FSM candidate did not actually fit the row. That
         * can happen when the map is stale or when our required-space estimate
         * was conservative around deleted-slot reuse.
         */
        if (status != DB_FULL) {
            buffer_pool_unpin_page(table_file, page_id, false);
            free(row_bytes);
            return status;
        }

        /*
         * If the map was stale, refresh this page's free-space value and ask
         * the FSM for another candidate.
         */
        status = free_space_update(
            table_file,
            page_id,
            page_insertable_space(page_buffer)
        );

        if (status != DB_OK) {
            buffer_pool_unpin_page(table_file, page_id, false);
            free(row_bytes);
            return status;
        }

        status = buffer_pool_unpin_page(table_file, page_id, false);

        if (status != DB_OK) {
            free(row_bytes);
            return status;
        }

        find_status = free_space_find_page(
            table_file,
            required_space,
            &candidate_page_id
        );
    }

    if (find_status != DB_NOT_FOUND) {
        free(row_bytes);
        return find_status;
    }

    /*
     * No existing page had enough space.
     * Create a new page at the end of the file.
     */
    uint32_t page_count = 0;

    status = buffer_pool_page_count(table_file, &page_count);

    if (status != DB_OK) {
        free(row_bytes);
        return status;
    }

    uint32_t new_page_id = page_count;
    uint8_t new_page_buffer[PAGE_SIZE];

    /*
     * Build the page in a stack buffer first. This preserves the old behavior
     * for rows that are too large: fail before allocating an empty disk page.
     */
    status = page_init(new_page_buffer, new_page_id);

    if (status != DB_OK) {
        free(row_bytes);
        return status;
    }

    uint16_t slot_id = 0;
    status = page_insert(new_page_buffer, row_bytes, row_len, &slot_id);

    if (status != DB_OK) {
        free(row_bytes);
        return status;
    }

    uint8_t *page_buffer = NULL;

    /*
     * Now reserve and pin the real page in the buffer pool, then copy the
     * already-validated page image into the cached frame.
     */
    status = buffer_pool_new_page(table_file, &new_page_id, &page_buffer);

    if (status != DB_OK) {
        free(row_bytes);
        return status;
    }

    memcpy(page_buffer, new_page_buffer, PAGE_SIZE);

    status = free_space_update(
        table_file,
        new_page_id,
        page_insertable_space(page_buffer)
    );

    if (status != DB_OK) {
        buffer_pool_unpin_page(table_file, new_page_id, false);
        free(row_bytes);
        return status;
    }

    status = buffer_pool_unpin_page(table_file, new_page_id, true);

    if (status != DB_OK) {
        free(row_bytes);
        return status;
    }

    status = buffer_pool_flush_page(table_file, new_page_id);

    if (status != DB_OK) {
        free(row_bytes);
        return status;
    }

    out_rid->page_id = new_page_id;
    out_rid->slot_id = slot_id;

    free(row_bytes);

    return DB_OK;
}

DBStatus record_get(const char *table_file, RID rid, Row *out_row) {
    if (table_file == NULL || out_row == NULL) {
        return DB_ERROR;
    }

    uint8_t *page_buffer = NULL;

    /*
     * Fetch the page where the row should live.
     */
    DBStatus status = buffer_pool_fetch_page(table_file, rid.page_id, &page_buffer);

    if (status != DB_OK) {
        return status;
    }

    uint8_t *row_bytes = NULL;
    uint32_t row_len = 0;

    /*
     * Get the row bytes from the slot.
     * This pointer points inside page_buffer.
     */
    status = page_get(page_buffer, rid.slot_id, &row_bytes, &row_len);

    if (status != DB_OK) {
        buffer_pool_unpin_page(table_file, rid.page_id, false);
        return status;
    }

    /*
     * Convert the raw bytes back into a Row.
     * out_row owns the deserialized values.
     */
    status = row_deserialize(row_bytes, row_len, out_row);

    DBStatus unpin_status = buffer_pool_unpin_page(table_file, rid.page_id, false);

    if (status != DB_OK) {
        return status;
    }

    return unpin_status;
}

DBStatus record_delete(const char *table_file, RID rid) {
    if (table_file == NULL) {
        return DB_ERROR;
    }

    uint8_t *page_buffer = NULL;

    /*
     * Fetch the page containing the row.
     */
    DBStatus status = buffer_pool_fetch_page(table_file, rid.page_id, &page_buffer);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Mark the slot as deleted.
     */
    status = page_delete(page_buffer, rid.slot_id);

    if (status != DB_OK) {
        buffer_pool_unpin_page(table_file, rid.page_id, false);
        return status;
    }

    /*
     * Keep the in-memory free-space map in sync with the changed page.
     * Deleted row bytes are not compacted yet, so this usually reflects slot
     * reuse rather than reclaimed row storage.
     */
    status = free_space_update(
        table_file,
        rid.page_id,
        page_insertable_space(page_buffer)
    );

    if (status != DB_OK) {
        buffer_pool_unpin_page(table_file, rid.page_id, false);
        return status;
    }

    /*
     * Mark the cached page dirty and flush it to keep record_delete durable.
     */
    status = buffer_pool_unpin_page(table_file, rid.page_id, true);

    if (status != DB_OK) {
        return status;
    }

    return buffer_pool_flush_page(table_file, rid.page_id);
}

DBStatus record_scan(
    const char *table_file,
    RecordScanCallback callback,
    void *context
) {
    if (table_file == NULL || callback == NULL) {
        return DB_ERROR;
    }

    uint32_t total_pages = 0;
    DBStatus status = buffer_pool_page_count(table_file, &total_pages);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Visit every page in the table file.
     */
    for (uint32_t page_id = 0; page_id < total_pages; page_id++) {
        uint8_t *page_buffer = NULL;

        status = buffer_pool_fetch_page(table_file, page_id, &page_buffer);

        if (status != DB_OK) {
            return status;
        }

        uint16_t slot_count = page_slot_count(page_buffer);

        /*
         * Visit every active slot in the page.
         */
        for (uint16_t slot_id = 0; slot_id < slot_count; slot_id++) {
            if (!page_slot_is_active(page_buffer, slot_id)) {
                continue;
            }

            uint8_t *row_bytes = NULL;
            uint32_t row_len = 0;

            status = page_get(page_buffer, slot_id, &row_bytes, &row_len);

            if (status != DB_OK) {
                buffer_pool_unpin_page(table_file, page_id, false);
                return status;
            }

            Row row;

            /*
             * Convert the row bytes back into a temporary Row.
             */
            status = row_deserialize(row_bytes, row_len, &row);

            if (status != DB_OK) {
                buffer_pool_unpin_page(table_file, page_id, false);
                return status;
            }

            RID rid;

            rid.page_id = page_id;
            rid.slot_id = slot_id;

            /*
             * The callback can inspect the row and RID.
             * The row is freed after the callback returns.
             */
            status = callback(&row, rid, context);

            row_free(&row);

            if (status != DB_OK) {
                buffer_pool_unpin_page(table_file, page_id, false);
                return status;
            }
        }

        status = buffer_pool_unpin_page(table_file, page_id, false);

        if (status != DB_OK) {
            return status;
        }
    }

    return DB_OK;
}
