#ifndef MINIDB_DB_H
#define MINIDB_DB_H

#include "common.h"

/*
 * DB represents an open MiniDB database.
 *
 * Right now, it only stores the database folder path.
 * Later, this struct will own the catalog, buffer pool, table manager,
 * and other top-level database systems.
 */
typedef struct {
    char path[MAX_DB_PATH];
} DB;

/*
 * Opens a database at the given path.
 *
 * If the folder does not exist, MiniDB should create it.
 */
DBStatus db_open(DB *db, const char *path);

/*
 * Closes the database.
 *
 * Right now there is no real cleanup to do, but this function gives the
 * project the correct structure for later steps.
 */
DBStatus db_close(DB *db);

#endif
