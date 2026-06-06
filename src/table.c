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
#include "storage/free_space.h"
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

    /*
     * Build the in-memory free-space map from the table's current pages.
     * Direct record-manager tests can still rebuild lazily on first use.
     */
    status = free_space_rebuild(table->file_path);

    if (status != DB_OK) {
        pager_close(&table->pager);
        memset(table, 0, sizeof(Table));
        return status;
    }

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

    return record_insert(table->file_path, row, out_rid);
}

DBStatus table_get(Table *table, RID rid, Row *out_row) {
    if (table_check_open(table) != DB_OK || out_row == NULL) {
        return DB_ERROR;
    }

    DBStatus status = record_get(table->file_path, rid, out_row);

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

    return record_delete(table->file_path, rid);
}

DBStatus table_scan(
    Table *table,
    RecordScanCallback callback,
    void *context
) {
    if (table_check_open(table) != DB_OK || callback == NULL) {
        return DB_ERROR;
    }

    return record_scan(table->file_path, callback, context);
}
