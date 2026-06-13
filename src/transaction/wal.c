#include <stdlib.h>
#include <string.h>

#include "buffer/buffer_pool.h"
#include "page.h"
#include "storage/free_space.h"
#include "transaction/wal.h"

typedef struct {
    /*
     * Recovery first collects committed transaction ids into this simple set.
     * A linear scan is fine for the first WAL because test logs are tiny and
     * the API can stay stable if this later becomes a hash table.
     */
    uint64_t *ids;
    uint32_t count;
    uint32_t capacity;
} WalTxnSet;

typedef struct {
    /*
     * In-memory representation of one binary WAL record.
     * BEGIN/COMMIT use only type + txn_id. INSERT/DELETE also carry table,
     * physical RID, and serialized row bytes.
     */
    WalRecordType type;
    uint64_t txn_id;
    char table_file[MAX_DB_PATH];
    RID rid;
    uint8_t *row_bytes;
    uint32_t row_len;
} WalRecord;

static bool wal_is_open(const WAL *wal) {
    return wal != NULL && wal->is_open && wal->file != NULL;
}

static bool wal_table_file_is_valid(const WAL *wal, const char *table_file) {
    if (!wal_is_open(wal) || table_file == NULL) {
        return false;
    }

    const char *filename = table_file;
    const char *wal_slash = strrchr(wal->file_path, '/');

    if (wal_slash != NULL) {
        size_t directory_len = (size_t)(wal_slash - wal->file_path);
        const char suffix[] = "/tables/";
        size_t prefix_len = directory_len + sizeof(suffix) - 1;

        if (
            prefix_len >= MAX_DB_PATH ||
            strncmp(table_file, wal->file_path, directory_len) != 0 ||
            strncmp(table_file + directory_len, suffix, sizeof(suffix) - 1) != 0
        ) {
            return false;
        }

        filename = table_file + prefix_len;
    } else if (strchr(table_file, '/') != NULL) {
        return false;
    }

    size_t filename_len = strlen(filename);
    const char extension[] = ".tbl";

    if (
        filename_len <= sizeof(extension) - 1 ||
        strcmp(filename + filename_len - (sizeof(extension) - 1), extension) != 0
    ) {
        return false;
    }

    size_t name_len = filename_len - (sizeof(extension) - 1);

    if (name_len >= MAX_TABLE_NAME) {
        return false;
    }

    char table_name[MAX_TABLE_NAME];
    memcpy(table_name, filename, name_len);
    table_name[name_len] = '\0';

    return db_identifier_is_valid(table_name, MAX_TABLE_NAME);
}

static DBStatus wal_write_bytes(WAL *wal, const void *bytes, size_t len) {
    if (!wal_is_open(wal) || bytes == NULL || len == 0) {
        return DB_ERROR;
    }

    /*
     * Write each logical field as one item. That keeps the call sites simple:
     * either the whole field was appended, or the log write failed.
     */
    if (fwrite(bytes, len, 1, wal->file) != 1) {
        return DB_IO_ERROR;
    }

    return DB_OK;
}

static DBStatus wal_read_bytes(FILE *file, void *bytes, size_t len, bool *out_eof) {
    if (file == NULL || bytes == NULL || out_eof == NULL || len == 0) {
        return DB_ERROR;
    }

    *out_eof = false;

    /*
     * The reader mirrors wal_write_bytes: each field must be read in full.
     */
    if (fread(bytes, len, 1, file) == 1) {
        return DB_OK;
    }

    if (feof(file)) {
        /*
         * EOF is reported separately from the status so callers can distinguish
         * "clean end of log" from other DB_NOT_FOUND-style situations.
         */
        *out_eof = true;
        return DB_NOT_FOUND;
    }

    return DB_IO_ERROR;
}

static DBStatus wal_read_required(FILE *file, void *bytes, size_t len) {
    bool eof = false;
    DBStatus status = wal_read_bytes(file, bytes, len, &eof);

    return status == DB_NOT_FOUND && eof ? DB_IO_ERROR : status;
}

static DBStatus wal_flush(WAL *wal) {
    if (!wal_is_open(wal)) {
        return DB_ERROR;
    }

    /*
     * This first WAL uses fflush as its durability boundary. A production
     * system would also fsync or use platform-specific durable-write calls.
     */
    if (fflush(wal->file) != 0) {
        return DB_IO_ERROR;
    }

    return DB_OK;
}

static DBStatus wal_write_record_header(
    WAL *wal,
    WalRecordType type,
    uint64_t txn_id
) {
    /*
     * Every on-disk record starts with:
     *   uint8_t  record type
     *   uint64_t transaction id
     *
     * Variable-length payload fields follow only for row operations.
     */
    uint8_t record_type = (uint8_t)type;
    DBStatus status = wal_write_bytes(wal, &record_type, sizeof(record_type));

    if (status != DB_OK) {
        return status;
    }

    return wal_write_bytes(wal, &txn_id, sizeof(txn_id));
}

static DBStatus wal_write_table_name(WAL *wal, const char *table_file) {
    if (
        !wal_table_file_is_valid(wal, table_file) ||
        strlen(table_file) >= MAX_DB_PATH
    ) {
        return DB_ERROR;
    }

    /*
     * Store table file paths as length-prefixed bytes so recovery can read the
     * exact path without relying on null terminators in the log.
     */
    uint16_t table_len = (uint16_t)strlen(table_file);
    DBStatus status = wal_write_bytes(wal, &table_len, sizeof(table_len));

    if (status != DB_OK) {
        return status;
    }

    return wal_write_bytes(wal, table_file, table_len);
}

static DBStatus wal_log_row_record(
    WAL *wal,
    WalRecordType type,
    uint64_t txn_id,
    const char *table_file,
    RID rid,
    const uint8_t *row_bytes,
    uint32_t row_len
) {
    if (
        !wal_is_open(wal) ||
        table_file == NULL ||
        row_bytes == NULL ||
        row_len == 0 ||
        row_len > PAGE_SIZE ||
        (type != WAL_RECORD_INSERT && type != WAL_RECORD_DELETE)
    ) {
        return DB_ERROR;
    }

    /*
     * Row-operation payload:
     *   table path length + table path bytes
     *   RID page id + RID slot id
     *   row byte length + row bytes
     */
    DBStatus status = wal_write_record_header(wal, type, txn_id);

    if (status != DB_OK) {
        return status;
    }

    status = wal_write_table_name(wal, table_file);

    if (status != DB_OK) {
        return status;
    }

    status = wal_write_bytes(wal, &rid.page_id, sizeof(rid.page_id));

    if (status != DB_OK) {
        return status;
    }

    status = wal_write_bytes(wal, &rid.slot_id, sizeof(rid.slot_id));

    if (status != DB_OK) {
        return status;
    }

    status = wal_write_bytes(wal, &row_len, sizeof(row_len));

    if (status != DB_OK) {
        return status;
    }

    /*
     * The row bytes are already in the same serialized format stored inside
     * table pages, so recovery can replay them without knowing the schema.
     */
    status = wal_write_bytes(wal, row_bytes, row_len);

    if (status != DB_OK) {
        return status;
    }

    return wal_flush(wal);
}

static void wal_record_free(WalRecord *record) {
    if (record == NULL) {
        return;
    }

    /*
     * wal_read_record allocates row_bytes only for INSERT/DELETE records.
     * BEGIN/COMMIT records leave this pointer NULL, which free accepts.
     */
    free(record->row_bytes);
    record->row_bytes = NULL;
    record->row_len = 0;
}

static DBStatus wal_read_record(WAL *wal, WalRecord *record, bool *out_eof) {
    if (!wal_is_open(wal) || record == NULL || out_eof == NULL) {
        return DB_ERROR;
    }

    /* Clean EOF is valid only before a new record; partial records are errors. */
    memset(record, 0, sizeof(WalRecord));

    uint8_t record_type = 0;
    DBStatus status = wal_read_bytes(
        wal->file,
        &record_type,
        sizeof(record_type),
        out_eof
    );

    if (status != DB_OK) {
        return status;
    }

    record->type = (WalRecordType)record_type;

    status = wal_read_required(wal->file, &record->txn_id, sizeof(record->txn_id));

    if (status != DB_OK) {
        return status;
    }

    /*
     * BEGIN and COMMIT are header-only records. They affect recovery decisions
     * but do not replay page bytes themselves.
     */
    if (record->type == WAL_RECORD_BEGIN || record->type == WAL_RECORD_COMMIT) {
        return DB_OK;
    }

    if (record->type != WAL_RECORD_INSERT && record->type != WAL_RECORD_DELETE) {
        return DB_ERROR;
    }

    uint16_t table_len = 0;
    status = wal_read_required(wal->file, &table_len, sizeof(table_len));

    if (status != DB_OK) {
        return status;
    }

    if (table_len == 0 || table_len >= MAX_DB_PATH) {
        return DB_ERROR;
    }

    status = wal_read_required(wal->file, record->table_file, table_len);

    if (status != DB_OK) {
        return status;
    }

    /*
     * The on-disk table path is not null-terminated. Add the terminator only in
     * the in-memory WalRecord struct.
     */
    record->table_file[table_len] = '\0';

    if (!wal_table_file_is_valid(wal, record->table_file)) {
        return DB_ERROR;
    }

    status = wal_read_required(
        wal->file,
        &record->rid.page_id,
        sizeof(record->rid.page_id)
    );

    if (status != DB_OK) {
        return status;
    }

    status = wal_read_required(
        wal->file,
        &record->rid.slot_id,
        sizeof(record->rid.slot_id)
    );

    if (status != DB_OK) {
        return status;
    }

    status = wal_read_required(wal->file, &record->row_len, sizeof(record->row_len));

    if (status != DB_OK) {
        return status;
    }

    if (record->row_len == 0 || record->row_len > PAGE_SIZE) {
        return DB_ERROR;
    }

    /*
     * Own a heap copy because recovery may unpin pages and continue scanning
     * the log after this record is read.
     */
    record->row_bytes = malloc(record->row_len);

    if (record->row_bytes == NULL) {
        return DB_ERROR;
    }

    status = wal_read_required(wal->file, record->row_bytes, record->row_len);

    if (status != DB_OK) {
        wal_record_free(record);
        return status;
    }

    return DB_OK;
}

static bool wal_txn_set_contains(const WalTxnSet *set, uint64_t txn_id) {
    if (set == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < set->count; i++) {
        if (set->ids[i] == txn_id) {
            return true;
        }
    }

    return false;
}

static DBStatus wal_txn_set_add(WalTxnSet *set, uint64_t txn_id) {
    if (set == NULL) {
        return DB_ERROR;
    }

    if (wal_txn_set_contains(set, txn_id)) {
        /*
         * Multiple COMMIT records for the same transaction should not duplicate
         * the committed set. That also makes recovery tolerant of repeated logs
         * in simple tests.
         */
        return DB_OK;
    }

    if (set->count == set->capacity) {
        /*
         * This set is private to one recovery run, so realloc-based growth is
         * enough and keeps the type tiny.
         */
        uint32_t next_capacity = set->capacity == 0 ? 8 : set->capacity * 2;
        uint64_t *next_ids = realloc(set->ids, next_capacity * sizeof(uint64_t));

        if (next_ids == NULL) {
            return DB_ERROR;
        }

        set->ids = next_ids;
        set->capacity = next_capacity;
    }

    set->ids[set->count] = txn_id;
    set->count++;

    return DB_OK;
}

static void wal_txn_set_free(WalTxnSet *set) {
    if (set == NULL) {
        return;
    }

    free(set->ids);
    set->ids = NULL;
    set->count = 0;
    set->capacity = 0;
}

static DBStatus wal_rewind(WAL *wal) {
    if (!wal_is_open(wal)) {
        return DB_ERROR;
    }

    if (fflush(wal->file) != 0) {
        return DB_IO_ERROR;
    }

    /*
     * Recovery makes multiple passes over the same FILE. Clear EOF/error flags
     * before seeking so the next pass can read normally.
     */
    clearerr(wal->file);

    if (fseek(wal->file, 0, SEEK_SET) != 0) {
        return DB_IO_ERROR;
    }

    return DB_OK;
}

static DBStatus wal_collect_committed(WAL *wal, WalTxnSet *committed) {
    DBStatus status = wal_rewind(wal);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Pass 1: discover winners. Any transaction with a COMMIT record is replayed;
     * transactions without COMMIT are ignored during pass 2.
     */
    while (true) {
        WalRecord record;
        bool eof = false;

        status = wal_read_record(wal, &record, &eof);

        if (status == DB_NOT_FOUND && eof) {
            return DB_OK;
        }

        if (status != DB_OK) {
            return status;
        }

        if (record.type == WAL_RECORD_COMMIT) {
            /*
             * BEGIN is useful for log readability/future validation, but the
             * first recovery algorithm only needs to know which txn ids reached
             * COMMIT.
             */
            status = wal_txn_set_add(committed, record.txn_id);
        }

        wal_record_free(&record);

        if (status != DB_OK) {
            return status;
        }
    }
}

static DBStatus wal_replay_ensure_page(const char *table_file, uint32_t page_id) {
    uint32_t page_count = 0;
    DBStatus status = buffer_pool_page_count(table_file, &page_count);

    if (status != DB_OK) {
        return status;
    }

    if (page_id > page_count) {
        return DB_ERROR;
    }

    /*
     * WAL replay may run after a crash where the log reached disk but the page
     * file did not. Allocate missing pages up to the logged RID's page id.
     */
    while (page_count <= page_id) {
        uint8_t *page = NULL;
        uint32_t new_page_id = 0;

        status = buffer_pool_new_page(table_file, &new_page_id, &page);

        if (status != DB_OK) {
            return status;
        }

        /*
         * Allocated pages start as zero bytes from the buffer pool. Initialize
         * them as normal data pages so page_insert/page_delete can operate.
         */
        status = page_init(page, new_page_id);

        if (status == DB_OK) {
            status = free_space_update(
                table_file,
                new_page_id,
                page_insertable_space(page)
            );
        }

        DBStatus unpin_status = buffer_pool_unpin_page(table_file, new_page_id, true);

        if (status != DB_OK) {
            return status;
        }

        if (unpin_status != DB_OK) {
            return unpin_status;
        }

        page_count++;
    }

    return DB_OK;
}

static DBStatus wal_replay_insert(const WalRecord *record) {
    DBStatus status = wal_replay_ensure_page(record->table_file, record->rid.page_id);

    if (status != DB_OK) {
        return status;
    }

    uint8_t *page = NULL;
    status = buffer_pool_fetch_page(record->table_file, record->rid.page_id, &page);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Replaying the same committed log more than once should be harmless.
     * If the target slot is already active, assume this INSERT was replayed.
     */
    uint8_t *existing_row = NULL;
    uint32_t existing_len = 0;
    status = page_get(page, record->rid.slot_id, &existing_row, &existing_len);

    if (status == DB_OK) {
        /*
         * This slot already contains a row. For redo recovery, that means the
         * insert has already been applied, so do not insert a duplicate.
         */
        return buffer_pool_unpin_page(record->table_file, record->rid.page_id, false);
    }

    if (status != DB_NOT_FOUND) {
        buffer_pool_unpin_page(record->table_file, record->rid.page_id, false);
        return status;
    }

    uint16_t slot_id = 0;
    status = page_insert(page, record->row_bytes, record->row_len, &slot_id);

    if (status != DB_OK) {
        buffer_pool_unpin_page(record->table_file, record->rid.page_id, false);
        return status;
    }

    if (slot_id != record->rid.slot_id) {
        /*
         * Physical WAL promises to restore the exact RID. If page_insert picks
         * a different slot, the page state no longer matches the log.
         */
        buffer_pool_unpin_page(record->table_file, record->rid.page_id, false);
        return DB_ERROR;
    }

    status = free_space_update(
        record->table_file,
        record->rid.page_id,
        page_insertable_space(page)
    );

    if (status != DB_OK) {
        buffer_pool_unpin_page(record->table_file, record->rid.page_id, false);
        return status;
    }

    status = buffer_pool_unpin_page(record->table_file, record->rid.page_id, true);

    if (status != DB_OK) {
        return status;
    }

    return buffer_pool_flush_page(record->table_file, record->rid.page_id);
}

static DBStatus wal_replay_delete(const WalRecord *record) {
    uint8_t *page = NULL;
    DBStatus status = buffer_pool_fetch_page(
        record->table_file,
        record->rid.page_id,
        &page
    );

    if (status == DB_NOT_FOUND) {
        /*
         * If the page file never reached disk, an already-deleted row is a
         * successful outcome for redo.
         */
        return DB_OK;
    }

    if (status != DB_OK) {
        return status;
    }

    /*
     * DELETE replay is also idempotent: missing page/slot means the row is
     * already absent, so recovery can continue.
     */
    status = page_delete(page, record->rid.slot_id);

    if (status == DB_NOT_FOUND) {
        /*
         * The slot is already missing/deleted. Treat this as an already-applied
         * delete so repeated recovery remains safe.
         */
        return buffer_pool_unpin_page(record->table_file, record->rid.page_id, false);
    }

    if (status != DB_OK) {
        buffer_pool_unpin_page(record->table_file, record->rid.page_id, false);
        return status;
    }

    status = free_space_update(
        record->table_file,
        record->rid.page_id,
        page_insertable_space(page)
    );

    if (status != DB_OK) {
        buffer_pool_unpin_page(record->table_file, record->rid.page_id, false);
        return status;
    }

    status = buffer_pool_unpin_page(record->table_file, record->rid.page_id, true);

    if (status != DB_OK) {
        return status;
    }

    return buffer_pool_flush_page(record->table_file, record->rid.page_id);
}

static DBStatus wal_replay_committed(WAL *wal, const WalTxnSet *committed) {
    DBStatus status = wal_rewind(wal);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Pass 2: replay only row records belonging to committed transactions,
     * preserving original log order.
     */
    while (true) {
        WalRecord record;
        bool eof = false;

        status = wal_read_record(wal, &record, &eof);

        if (status == DB_NOT_FOUND && eof) {
            return DB_OK;
        }

        if (status != DB_OK) {
            return status;
        }

        if (wal_txn_set_contains(committed, record.txn_id)) {
            /*
             * BEGIN/COMMIT records are skipped here; only row records change
             * table pages during redo.
             */
            if (record.type == WAL_RECORD_INSERT) {
                status = wal_replay_insert(&record);
            } else if (record.type == WAL_RECORD_DELETE) {
                status = wal_replay_delete(&record);
            }
        }

        wal_record_free(&record);

        if (status != DB_OK) {
            return status;
        }
    }
}

DBStatus wal_open(WAL *wal, const char *file_path) {
    if (wal == NULL || file_path == NULL || strlen(file_path) >= MAX_DB_PATH) {
        return DB_ERROR;
    }

    memset(wal, 0, sizeof(WAL));

    /*
     * a+b creates the log if needed and keeps writes append-only. Recovery uses
     * fseek to read from the beginning.
     */
    FILE *file = fopen(file_path, "a+b");

    if (file == NULL) {
        return DB_IO_ERROR;
    }

    wal->file = file;
    strncpy(wal->file_path, file_path, MAX_DB_PATH - 1);
    wal->file_path[MAX_DB_PATH - 1] = '\0';
    wal->is_open = true;

    return DB_OK;
}

DBStatus wal_close(WAL *wal) {
    if (!wal_is_open(wal)) {
        return DB_ERROR;
    }

    DBStatus status = wal_flush(wal);

    if (fclose(wal->file) != 0 && status == DB_OK) {
        status = DB_IO_ERROR;
    }

    wal->file = NULL;
    wal->file_path[0] = '\0';
    wal->is_open = false;

    return status;
}

DBStatus wal_log_begin(WAL *wal, uint64_t txn_id) {
    if (!wal_is_open(wal) || txn_id == 0) {
        return DB_ERROR;
    }

    /*
     * BEGIN is not strictly needed for this redo-only recovery pass, but it
     * makes the log stream explicit and prepares for validation/rollback work.
     */
    DBStatus status = wal_write_record_header(wal, WAL_RECORD_BEGIN, txn_id);

    if (status != DB_OK) {
        return status;
    }

    return wal_flush(wal);
}

DBStatus wal_log_insert(
    WAL *wal,
    uint64_t txn_id,
    const char *table_file,
    RID rid,
    const uint8_t *row_bytes,
    uint32_t row_len
) {
    if (txn_id == 0) {
        return DB_ERROR;
    }

    return wal_log_row_record(
        wal,
        WAL_RECORD_INSERT,
        txn_id,
        table_file,
        rid,
        row_bytes,
        row_len
    );
}

DBStatus wal_log_delete(
    WAL *wal,
    uint64_t txn_id,
    const char *table_file,
    RID rid,
    const uint8_t *old_row_bytes,
    uint32_t old_row_len
) {
    if (txn_id == 0) {
        return DB_ERROR;
    }

    return wal_log_row_record(
        wal,
        WAL_RECORD_DELETE,
        txn_id,
        table_file,
        rid,
        old_row_bytes,
        old_row_len
    );
}

DBStatus wal_log_commit(WAL *wal, uint64_t txn_id) {
    if (!wal_is_open(wal) || txn_id == 0) {
        return DB_ERROR;
    }

    /*
     * COMMIT is the winner marker. Recovery ignores all row records for a
     * transaction unless this record is present.
     */
    DBStatus status = wal_write_record_header(wal, WAL_RECORD_COMMIT, txn_id);

    if (status != DB_OK) {
        return status;
    }

    return wal_flush(wal);
}

DBStatus wal_recover(WAL *wal) {
    if (!wal_is_open(wal)) {
        return DB_ERROR;
    }

    WalTxnSet committed;
    committed.ids = NULL;
    committed.count = 0;
    committed.capacity = 0;

    /*
     * Two-pass recovery keeps the first version simple:
     * 1. collect committed transaction ids
     * 2. replay only row records belonging to those ids
     */
    DBStatus status = wal_collect_committed(wal, &committed);

    if (status == DB_OK) {
        status = wal_replay_committed(wal, &committed);
    }

    wal_txn_set_free(&committed);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Make recovered pages durable before db_open returns control to normal
     * execution.
     */
    return buffer_pool_flush_all();
}
