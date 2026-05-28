#ifndef TABLE_H
#define TABLE_H

#include <stdbool.h>

#include "common.h"
#include "db.h"
#include "pager.h"
#include "record.h"
#include "rid.h"
#include "row.h"
#include "schema.h"

/*
 * A Table represents one opened table.
 * It owns the table schema, the table file path,
 * and an open Pager for reading/writing table pages.
 */
typedef struct {
    Schema schema;
    char file_path[MAX_DB_PATH];
    Pager pager;
    bool is_open;
} Table;

/*
 * Opens a table using the provided schema.
 * The table file path is built from the database path and table name.
 * For now, the schema must be passed in because there is no catalog yet.
 */
DBStatus table_open(Table *table, const DB *db, const Schema *schema);

/*
 * Closes the table's pager.
 * After this succeeds, the table is no longer open.
 * This should be called when the table is no longer needed.
 */
DBStatus table_close(Table *table);

/*
 * Inserts a row into the table.
 * The row is first validated against the table schema.
 * out_rid stores the physical location of the inserted row.
 */
DBStatus table_insert(Table *table, Row *row, RID *out_rid);

/*
 * Reads a row from the table using its RID.
 * out_row owns its values after this succeeds.
 * The caller must later call row_free on out_row.
 */
DBStatus table_get(Table *table, RID rid, Row *out_row);

/*
 * Deletes a row from the table using its RID.
 * This marks the matching page slot as deleted.
 * It does not compact the page yet.
 */
DBStatus table_delete(Table *table, RID rid);

/*
 * Scans every active row in the table.
 * For each row, the callback receives the row and its RID.
 * If the callback returns an error, scanning stops early.
 */
DBStatus table_scan(
    Table *table,
    RecordScanCallback callback,
    void *context
);

#endif
