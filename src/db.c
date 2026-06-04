#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "buffer/buffer_pool.h"
#include "catalog.h"
#include "common.h"
#include "db.h"

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

    /*
     * Load catalog metadata from disk.
     */
    status = catalog_load(db);

    if (status != DB_OK) {
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
     * Save table metadata before closing the database.
     */
    status = catalog_save(db);

    /*
     * Reset the DB even if saving failed.
     * The caller still receives the save error.
     */
    memset(db, 0, sizeof(DB));

    return status;
}
