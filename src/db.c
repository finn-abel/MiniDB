#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "db.h"

static bool path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool path_is_directory(const char *path) {
    struct stat st;

    if (stat(path, &st) != 0) {
        return false;
    }

    return S_ISDIR(st.st_mode);
}

DBStatus db_open(DB *db, const char *path) {
    if (db == NULL || path == NULL) {
        return DB_ERROR;
    }

    size_t path_len = strlen(path);

    if (path_len == 0 || path_len >= MAX_DB_PATH) {
        return DB_ERROR;
    }

    /*
     * If the path already exists, make sure it is a directory.
     */
    if (path_exists(path)) {
        if (!path_is_directory(path)) {
            fprintf(stderr, "Error: '%s' exists but is not a directory.\n", path);
            return DB_IO_ERROR;
        }
    } else {
        /*
         * Create the database directory.
         *
         * 0755 means:
         * - owner can read/write/execute
         * - group can read/execute
         * - others can read/execute
         */
        if (mkdir(path, 0755) != 0) {
            fprintf(stderr, "Error: could not create database directory '%s': %s\n",
                    path,
                    strerror(errno));
            return DB_IO_ERROR;
        }
    }

    strncpy(db->path, path, MAX_DB_PATH);
    db->path[MAX_DB_PATH - 1] = '\0';

    return DB_OK;
}

DBStatus db_close(DB *db) {
    if (db == NULL) {
        return DB_ERROR;
    }

    /*
     * Nothing to free yet.
     *
     * Later, this function may flush dirty pages, close table files,
     * write catalog metadata, close the WAL, and release buffer memory.
     */
    db->path[0] = '\0';

    return DB_OK;
}
