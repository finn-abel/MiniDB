#ifndef MINIDB_TRANSACTION_WAL_H
#define MINIDB_TRANSACTION_WAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "rid.h"

typedef enum {
    WAL_RECORD_BEGIN = 1,
    WAL_RECORD_INSERT = 2,
    WAL_RECORD_DELETE = 3,
    WAL_RECORD_COMMIT = 4
} WalRecordType;

typedef struct {
    FILE *file;
    char file_path[MAX_DB_PATH];
    bool is_open;
} WAL;

/*
 * Opens or creates a WAL file.
 */
DBStatus wal_open(WAL *wal, const char *file_path);

/*
 * Flushes and closes a WAL file.
 */
DBStatus wal_close(WAL *wal);

/*
 * Appends BEGIN txn_id.
 */
DBStatus wal_log_begin(WAL *wal, uint64_t txn_id);

/*
 * Appends INSERT table rid row_bytes.
 */
DBStatus wal_log_insert(
    WAL *wal,
    uint64_t txn_id,
    const char *table_file,
    RID rid,
    const uint8_t *row_bytes,
    uint32_t row_len
);

/*
 * Appends DELETE table rid old_row_bytes.
 */
DBStatus wal_log_delete(
    WAL *wal,
    uint64_t txn_id,
    const char *table_file,
    RID rid,
    const uint8_t *old_row_bytes,
    uint32_t old_row_len
);

/*
 * Appends COMMIT txn_id.
 */
DBStatus wal_log_commit(WAL *wal, uint64_t txn_id);

/*
 * Replays committed INSERT/DELETE records and ignores uncommitted records.
 */
DBStatus wal_recover(WAL *wal);

#endif
