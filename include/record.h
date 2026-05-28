#ifndef RECORD_H
#define RECORD_H

#include "common.h"
#include "rid.h"
#include "row.h"

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
 * Reads a row from a table file using its RID.
 * The row bytes are loaded from the correct page and slot.
 * out_row owns its values after this succeeds.
 */
DBStatus record_get(const char *table_file, RID rid, Row *out_row);

/*
 * Deletes a row from a table file using its RID.
 * This marks the page slot as deleted.
 * It does not compact the page or reclaim row bytes yet.
 */
DBStatus record_delete(const char *table_file, RID rid);

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
