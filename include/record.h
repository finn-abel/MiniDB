#ifndef RECORD_H
#define RECORD_H

#include "common.h"
#include "rid.h"
#include "row.h"
#include "transaction/transaction.h"

/*
 * Callback type used by record_scan.
 * The callback receives each active row and its RID.
 * The row is only valid during the callback.
 */
typedef DBStatus (*RecordScanCallback)(const Row *row, RID rid, void *context);

/*
 * Inserts a row into a table file.
 * The row is serialized, inserted into a page, and written to disk.
 * out_rid stores the physical location of the inserted row.
 */
DBStatus record_insert(const char *table_file, Row *row, RID *out_rid);

/*
 * WAL-aware insert used by the SQL execution path.
 */
DBStatus record_insert_logged(
    const char *table_file,
    Row *row,
    RID *out_rid,
    Transaction *transaction
);

/*
 * Reads a row from a table file using its RID.
 * The row bytes are loaded from the correct page and slot.
 * out_row owns its values after this succeeds.
 */
DBStatus record_get(const char *table_file, RID rid, Row *out_row);

/*
 * Updates a row at an existing RID.
 * If the new bytes no longer fit in the old slot, the old row is deleted and
 * the new row is inserted elsewhere. out_rid stores the final row location.
 */
DBStatus record_update(const char *table_file, RID rid, Row *row, RID *out_rid);

/*
 * WAL-aware update used by the SQL execution path.
 */
DBStatus record_update_logged(
    const char *table_file,
    RID rid,
    Row *row,
    RID *out_rid,
    Transaction *transaction
);

/*
 * Deletes a row from a table file using its RID.
 * This marks the page slot as deleted.
 * It does not compact the page or reclaim row bytes yet.
 */
DBStatus record_delete(const char *table_file, RID rid);

/*
 * WAL-aware delete used by the SQL execution path.
 */
DBStatus record_delete_logged(
    const char *table_file,
    RID rid,
    Transaction *transaction
);

/*
 * Scans every active row in a table file.
 * For each active row, the callback is called with the row and RID.
 * If the callback returns an error, the scan stops early.
 */
DBStatus record_scan(
    const char *table_file,
    RecordScanCallback callback,
    void *context
);

#endif
