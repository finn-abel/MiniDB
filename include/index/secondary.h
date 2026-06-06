#ifndef MINIDB_INDEX_SECONDARY_H
#define MINIDB_INDEX_SECONDARY_H

#include <stdint.h>

#include "common.h"
#include "record.h"
#include "rid.h"
#include "schema.h"
#include "value.h"

typedef DBStatus (*SecondaryIndexRIDCallback)(RID rid, void *context);

/*
 * Builds a secondary index file from the authoritative table rows.
 *
 * The secondary index stores only key values and RIDs. Duplicate key values are
 * represented by multiple entries, so regular non-unique indexes work without
 * changing the primary-key B+ tree.
 */
DBStatus secondary_index_build(
    const char *index_file,
    const char *table_file,
    const Schema *schema,
    const uint16_t *column_indexes,
    uint16_t column_count
);

/*
 * Scans one indexed column inside the secondary index and calls callback for
 * each RID whose key satisfies the condition.
 */
DBStatus secondary_index_scan_condition(
    const char *index_file,
    uint16_t condition_index_column,
    const WhereCondition *condition,
    SecondaryIndexRIDCallback callback,
    void *context
);

#endif
