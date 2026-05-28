#include <stdint.h>
#include <stdlib.h>

#include "common.h"
#include "page.h"
#include "pager.h"
#include "record.h"
#include "rid.h"
#include "row.h"

DBStatus record_insert(const char *table_file, Row *row, RID *out_rid) {
    if (table_file == NULL || row == NULL || out_rid == NULL) {
        return DB_ERROR;
    }

    Pager pager;

    DBStatus status = pager_open(&pager, table_file);

    if (status != DB_OK) {
        return status;
    }

    uint8_t *row_bytes = NULL;
    uint32_t row_len = 0;

    /*
     * Convert the row into raw bytes.
     * Pages only store bytes, not Row structs.
     */
    status = row_serialize(row, &row_bytes, &row_len);

    if (status != DB_OK) {
        pager_close(&pager);
        return status;
    }

    uint8_t page_buffer[PAGE_SIZE];

    /*
     * First try to insert into an existing page with enough free space.
     */
    for (uint32_t page_id = 0; page_id < pager_num_pages(&pager); page_id++) {
        status = pager_read_page(&pager, page_id, page_buffer);

        if (status != DB_OK) {
            free(row_bytes);
            pager_close(&pager);
            return status;
        }

        /*
         * page_insert will return DB_FULL if this page cannot fit the row.
         */
        uint16_t slot_id = 0;
        status = page_insert(page_buffer, row_bytes, row_len, &slot_id);

        if (status == DB_OK) {
            status = pager_write_page(&pager, page_id, page_buffer);

            if (status != DB_OK) {
                free(row_bytes);
                pager_close(&pager);
                return status;
            }

            out_rid->page_id = page_id;
            out_rid->slot_id = slot_id;

            free(row_bytes);
            return pager_close(&pager);
        }

        /*
         * DB_FULL just means this page did not have enough room.
         * Keep searching the next page.
         */
        if (status != DB_FULL) {
            free(row_bytes);
            pager_close(&pager);
            return status;
        }
    }

    /*
     * No existing page had enough space.
     * Create a new page at the end of the file.
     */
    uint32_t new_page_id = pager_num_pages(&pager);

    status = page_init(page_buffer, new_page_id);

    if (status != DB_OK) {
        free(row_bytes);
        pager_close(&pager);
        return status;
    }

    uint16_t slot_id = 0;

    status = page_insert(page_buffer, row_bytes, row_len, &slot_id);

    if (status != DB_OK) {
        free(row_bytes);
        pager_close(&pager);
        return status;
    }

    uint32_t allocated_page_id = 0;

    status = pager_allocate_page(&pager, page_buffer, &allocated_page_id);

    if (status != DB_OK) {
        free(row_bytes);
        pager_close(&pager);
        return status;
    }

    out_rid->page_id = allocated_page_id;
    out_rid->slot_id = slot_id;

    free(row_bytes);

    return pager_close(&pager);
}

DBStatus record_get(const char *table_file, RID rid, Row *out_row) {
    if (table_file == NULL || out_row == NULL) {
        return DB_ERROR;
    }

    Pager pager;

    DBStatus status = pager_open(&pager, table_file);

    if (status != DB_OK) {
        return status;
    }

    uint8_t page_buffer[PAGE_SIZE];

    /*
     * Load the page where the row should live.
     */
    status = pager_read_page(&pager, rid.page_id, page_buffer);

    if (status != DB_OK) {
        pager_close(&pager);
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
        pager_close(&pager);
        return status;
    }

    /*
     * Convert the raw bytes back into a Row.
     * out_row owns the deserialized values.
     */
    status = row_deserialize(row_bytes, row_len, out_row);

    if (status != DB_OK) {
        pager_close(&pager);
        return status;
    }

    return pager_close(&pager);
}

DBStatus record_delete(const char *table_file, RID rid) {
    if (table_file == NULL) {
        return DB_ERROR;
    }

    Pager pager;

    DBStatus status = pager_open(&pager, table_file);

    if (status != DB_OK) {
        return status;
    }

    uint8_t page_buffer[PAGE_SIZE];

    /*
     * Read the page containing the row.
     */
    status = pager_read_page(&pager, rid.page_id, page_buffer);

    if (status != DB_OK) {
        pager_close(&pager);
        return status;
    }

    /*
     * Mark the slot as deleted.
     */
    status = page_delete(page_buffer, rid.slot_id);

    if (status != DB_OK) {
        pager_close(&pager);
        return status;
    }

    /*
     * Write the modified page back to disk.
     */
    status = pager_write_page(&pager, rid.page_id, page_buffer);

    if (status != DB_OK) {
        pager_close(&pager);
        return status;
    }

    return pager_close(&pager);
}

DBStatus record_scan(
    const char *table_file,
    RecordScanCallback callback,
    void *context
) {
    if (table_file == NULL || callback == NULL) {
        return DB_ERROR;
    }

    Pager pager;

    DBStatus status = pager_open(&pager, table_file);

    if (status != DB_OK) {
        return status;
    }

    uint8_t page_buffer[PAGE_SIZE];
    uint32_t total_pages = pager_num_pages(&pager);

    /*
     * Visit every page in the table file.
     */
    for (uint32_t page_id = 0; page_id < total_pages; page_id++) {
        status = pager_read_page(&pager, page_id, page_buffer);

        if (status != DB_OK) {
            pager_close(&pager);
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
                pager_close(&pager);
                return status;
            }

            Row row;

            /*
             * Convert the row bytes back into a temporary Row.
             */
            status = row_deserialize(row_bytes, row_len, &row);

            if (status != DB_OK) {
                pager_close(&pager);
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
                pager_close(&pager);
                return status;
            }
        }
    }

    return pager_close(&pager);
}
