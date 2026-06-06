#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "buffer/buffer_pool.h"
#include "catalog.h"
#include "common.h"
#include "db.h"
#include "transaction/transaction.h"
#include "transaction/wal.h"

static DBStatus db_create_dir_if_needed(const char *path) {
    if (path == NULL) {
        return DB_ERROR;
    }

    struct stat info;

    /*
     * If the path already exists, make sure it is a directory.
     */
    if (stat(path, &info) == 0) {
        if (S_ISDIR(info.st_mode)) {
            return DB_OK;
        }

        return DB_ERROR;
    }

    /*
     * If stat failed for a reason other than "does not exist",
     * treat it as an I/O error.
     */
    if (errno != ENOENT) {
        return DB_IO_ERROR;
    }

    /*
     * Create the missing directory.
     */
    if (mkdir(path, 0755) != 0) {
        return DB_IO_ERROR;
    }

    return DB_OK;
}

static DBStatus db_create_tables_dir(const DB *db) {
    if (db == NULL) {
        return DB_ERROR;
    }

    char tables_path[MAX_DB_PATH];

    /*
     * Table files live inside:
     * mydb/tables/
     */
    int written = snprintf(
        tables_path,
        sizeof(tables_path),
        "%s/tables",
        db->path
    );

    if (written < 0 || written >= (int)sizeof(tables_path)) {
        return DB_ERROR;
    }

    return db_create_dir_if_needed(tables_path);
}

static DBStatus db_wal_path(const DB *db, char *out_path, size_t out_size) {
    if (db == NULL || out_path == NULL || out_size == 0) {
        return DB_ERROR;
    }

    /*
     * Keep the WAL beside catalog.db at the database root. Table files may be
     * many, but there is one log stream for all table mutations in this DB.
     */
    int written = snprintf(out_path, out_size, "%s/minidb.wal", db->path);

    if (written < 0 || written >= (int)out_size) {
        return DB_ERROR;
    }

    return DB_OK;
}

DBStatus db_open(DB *db, const char *path) {
    if (db == NULL || path == NULL) {
        return DB_ERROR;
    }

    /*
     * Start from a clean state.
     */
    memset(db, 0, sizeof(DB));

    if (strlen(path) == 0 || strlen(path) >= MAX_DB_PATH) {
        return DB_ERROR;
    }

    /*
     * Store the database folder path.
     */
    strncpy(db->path, path, MAX_DB_PATH - 1);
    db->path[MAX_DB_PATH - 1] = '\0';

    /*
     * Create the main database folder if needed.
     */
    DBStatus status = db_create_dir_if_needed(db->path);

    if (status != DB_OK) {
        memset(db, 0, sizeof(DB));
        return status;
    }

    /*
     * Create the table file directory if needed.
     */
    status = db_create_tables_dir(db);

    if (status != DB_OK) {
        memset(db, 0, sizeof(DB));
        return status;
    }

    char wal_path[MAX_DB_PATH];

    status = db_wal_path(db, wal_path, sizeof(wal_path));

    if (status != DB_OK) {
        memset(db, 0, sizeof(DB));
        return status;
    }

    /*
     * Recovery runs before normal statement execution. The first WAL version
     * replays committed row changes and ignores uncommitted ones.
     */
    status = wal_open(&db->wal, wal_path);

    if (status != DB_OK) {
        memset(db, 0, sizeof(DB));
        return status;
    }

    /*
     * Recovery must happen before catalog/table operations for new statements.
     * If the last process died after logging a committed row change but before
     * flushing the page file, wal_recover brings table pages back up to date.
     */
    status = wal_recover(&db->wal);

    if (status != DB_OK) {
        wal_close(&db->wal);
        memset(db, 0, sizeof(DB));
        return status;
    }

    status = transaction_init(&db->transaction);

    if (status != DB_OK) {
        wal_close(&db->wal);
        memset(db, 0, sizeof(DB));
        return status;
    }

    /*
     * The transaction layer owns BEGIN/COMMIT records; record.c owns row-level
     * INSERT/DELETE records while a transaction is active.
     */
    status = transaction_attach_wal(&db->transaction, &db->wal);

    if (status != DB_OK) {
        wal_close(&db->wal);
        memset(db, 0, sizeof(DB));
        return status;
    }

    /*
     * Load catalog metadata from disk.
     */
    /*
     * The catalog is loaded after WAL recovery. The current WAL records only
     * replay row changes, not CREATE TABLE metadata, so catalog persistence
     * still follows the existing catalog_load/catalog_save path.
     */
    status = catalog_load(db);

    if (status != DB_OK) {
        wal_close(&db->wal);
        memset(db, 0, sizeof(DB));
        return status;
    }

    db->is_open = true;

    return DB_OK;
}

DBStatus db_close(DB *db) {
    if (db == NULL) {
        return DB_ERROR;
    }

    if (!db->is_open) {
        return DB_OK;
    }

    /*
     * Table pages may be dirty in the buffer pool. Flush them before saving
     * catalog metadata and resetting DB state.
     */
    DBStatus status = buffer_pool_flush_all();

    if (status != DB_OK) {
        memset(db, 0, sizeof(DB));
        return status;
    }

    /*
     * Save table metadata before closing the database, then close the WAL.
     * Try to close the WAL even if catalog_save reports an error.
     */
    status = catalog_save(db);
    DBStatus wal_status = wal_close(&db->wal);

    if (status == DB_OK && wal_status != DB_OK) {
        status = wal_status;
    }

    /*
     * Reset the DB even if saving failed.
     * The caller still receives the save error.
     */
    memset(db, 0, sizeof(DB));

    return status;
}
