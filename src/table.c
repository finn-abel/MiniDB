#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catalog.h"
#include "common.h"
#include "db.h"
#include "page.h"
#include "pager.h"
#include "record.h"
#include "rid.h"
#include "row.h"
#include "schema.h"
#include "table.h"

static DBStatus table_build_file_path(
    const DB *db,
    const char *table_name,
    char *out_path,
    uint32_t out_size
) {
    if (db == NULL || table_name == NULL || out_path == NULL || out_size == 0) {
        return DB_ERROR;
    }

    /*
     * Each table gets one table file.
     * Example:
     * mydb/tables/users.tbl
     */
    int written = snprintf(
        out_path,
        out_size,
        "%s/tables/%s.tbl",
        db->path,
        table_name
    );

    if (written < 0 || written >= (int)out_size) {
        return DB_ERROR;
    }

    return DB_OK;
}

static DBStatus table_check_open(const Table *table) {
    if (table == NULL || !table->is_open || table->pager.file == NULL) {
        return DB_ERROR;
    }

    return DB_OK;
}

DBStatus table_open(Table *table, const DB *db, const char *table_name) {
    if (table == NULL || db == NULL || table_name == NULL) {
        return DB_ERROR;
    }

    /*
     * Start from a clean state so failed opens do not leave stale data behind.
     */
    memset(table, 0, sizeof(Table));

    /*
     * Load the table schema from the catalog.
     */
    DBStatus status = catalog_get_schema(
        db,
        table_name,
        &table->schema
    );

    if (status != DB_OK) {
        return status;
    }

    status = table_build_file_path(
        db,
        table_name,
        table->file_path,
        sizeof(table->file_path)
    );

    if (status != DB_OK) {
        memset(table, 0, sizeof(Table));
        return status;
    }

    /*
     * Open the table's data file.
     */
    status = pager_open(&table->pager, table->file_path);

    if (status != DB_OK) {
        memset(table, 0, sizeof(Table));
        return status;
    }

    table->is_open = true;

    return DB_OK;
}

DBStatus table_close(Table *table) {
    if (table == NULL) {
        return DB_ERROR;
    }

    if (!table->is_open) {
        return DB_OK;
    }

    DBStatus status = pager_close(&table->pager);

    /*
     * Clear the table after close so it cannot accidentally be reused.
     */
    memset(table, 0, sizeof(Table));

    return status;
}

DBStatus table_insert(Table *table, Row *row, RID *out_rid) {
    if (table_check_open(table) != DB_OK || row == NULL || out_rid == NULL) {
        return DB_ERROR;
    }

    /*
     * The table owns the schema, so row validation happens here.
     */
    DBStatus status = schema_validate_row(&table->schema, row);

    if (status != DB_OK) {
        return status;
    }

    uint8_t *row_bytes = NULL;
    uint32_t row_len = 0;

    /*
     * Convert the row into raw bytes before putting it into a page.
     */
    status = row_serialize(row, &row_bytes, &row_len);

    if (status != DB_OK) {
        return status;
    }

    uint8_t page_buffer[PAGE_SIZE];

    /*
     * Try every existing page first.
     */
    for (uint32_t page_id = 0; page_id < pager_num_pages(&table->pager); page_id++) {
        status = pager_read_page(&table->pager, page_id, page_buffer);

        if (status != DB_OK) {
            free(row_bytes);
            return status;
        }

        uint16_t slot_id = 0;

        status = page_insert(page_buffer, row_bytes, row_len, &slot_id);

        if (status == DB_OK) {
            status = pager_write_page(&table->pager, page_id, page_buffer);

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
         * DB_FULL means this page had no room.
         * That is not fatal; keep searching.
         */
        if (status != DB_FULL) {
            free(row_bytes);
            return status;
        }
    }

    /*
     * No existing page had space, so create a new page.
     */
    uint32_t new_page_id = pager_num_pages(&table->pager);

    status = page_init(page_buffer, new_page_id);

    if (status != DB_OK) {
        free(row_bytes);
        return status;
    }

    uint16_t slot_id = 0;

    status = page_insert(page_buffer, row_bytes, row_len, &slot_id);

    if (status != DB_OK) {
        free(row_bytes);
        return status;
    }

    uint32_t allocated_page_id = 0;

    status = pager_allocate_page(&table->pager, page_buffer, &allocated_page_id);

    if (status != DB_OK) {
        free(row_bytes);
        return status;
    }

    out_rid->page_id = allocated_page_id;
    out_rid->slot_id = slot_id;

    free(row_bytes);

    return DB_OK;
}

DBStatus table_get(Table *table, RID rid, Row *out_row) {
    if (table_check_open(table) != DB_OK || out_row == NULL) {
        return DB_ERROR;
    }

    uint8_t page_buffer[PAGE_SIZE];

    /*
     * Read the page where the RID says the row should live.
     */
    DBStatus status = pager_read_page(&table->pager, rid.page_id, page_buffer);

    if (status != DB_OK) {
        return status;
    }

    uint8_t *row_bytes = NULL;
    uint32_t row_len = 0;

    /*
     * Get the row bytes from the page slot.
     */
    status = page_get(page_buffer, rid.slot_id, &row_bytes, &row_len);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Convert the raw row bytes back into a Row.
     */
    status = row_deserialize(row_bytes, row_len, out_row);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Since this table has a schema, validate the loaded row too.
     * If this fails, the table file may contain corrupted or old-format data.
     */
    return schema_validate_row(&table->schema, out_row);
}

DBStatus table_delete(Table *table, RID rid) {
    if (table_check_open(table) != DB_OK) {
        return DB_ERROR;
    }

    uint8_t page_buffer[PAGE_SIZE];

    /*
     * Read the page containing the row.
     */
    DBStatus status = pager_read_page(&table->pager, rid.page_id, page_buffer);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Mark the slot as deleted.
     */
    status = page_delete(page_buffer, rid.slot_id);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Persist the modified page.
     */
    return pager_write_page(&table->pager, rid.page_id, page_buffer);
}

DBStatus table_scan(
    Table *table,
    RecordScanCallback callback,
    void *context
) {
    if (table_check_open(table) != DB_OK || callback == NULL) {
        return DB_ERROR;
    }

    uint8_t page_buffer[PAGE_SIZE];
    uint32_t total_pages = pager_num_pages(&table->pager);

    /*
     * Scan each page in the table file.
     */
    for (uint32_t page_id = 0; page_id < total_pages; page_id++) {
        DBStatus status = pager_read_page(&table->pager, page_id, page_buffer);

        if (status != DB_OK) {
            return status;
        }

        uint16_t slot_count = page_slot_count(page_buffer);

        /*
         * Scan each active slot in the page.
         */
        for (uint16_t slot_id = 0; slot_id < slot_count; slot_id++) {
            if (!page_slot_is_active(page_buffer, slot_id)) {
                continue;
            }

            uint8_t *row_bytes = NULL;
            uint32_t row_len = 0;

            status = page_get(page_buffer, slot_id, &row_bytes, &row_len);

            if (status != DB_OK) {
                return status;
            }

            Row row;

            /*
             * Deserialize the row for the callback.
             */
            status = row_deserialize(row_bytes, row_len, &row);

            if (status != DB_OK) {
                return status;
            }

            /*
             * Validate scanned rows against the table schema.
             */
            status = schema_validate_row(&table->schema, &row);

            if (status != DB_OK) {
                row_free(&row);
                return status;
            }

            RID rid;

            rid.page_id = page_id;
            rid.slot_id = slot_id;

            /*
             * The row is only valid during this callback.
             */
            status = callback(&row, rid, context);

            row_free(&row);

            if (status != DB_OK) {
                return status;
            }
        }
    }

    return DB_OK;
}