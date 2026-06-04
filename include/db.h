#ifndef DB_H
#define DB_H

#include <stdbool.h>

#include "catalog.h"
#include "common.h"

/*
 * DB represents an opened database.
 * It owns the database folder path and the loaded catalog.
 * The catalog remembers what tables exist.
 */
struct DB {
    char path[MAX_DB_PATH];
    Catalog catalog;
    bool is_open;
};

/*
 * Opens a database folder.
 * Creates the database folder and tables folder if needed.
 * Loads the catalog from mydb/catalog.db.
 */
DBStatus db_open(DB *db, const char *path);

/*
 * Saves the catalog and closes the database.
 * This does not close table pagers; tables should be closed separately.
 * After closing, the DB is reset to an empty state.
 */
DBStatus db_close(DB *db);

#endif