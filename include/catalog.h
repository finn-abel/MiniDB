#ifndef CATALOG_H
#define CATALOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "schema.h"

/*
 * Forward declaration so catalog functions can accept DB pointers.
 * The full DB struct is defined in db.h.
 */
typedef struct DB DB;

#define MAX_CATALOG_TABLES 64
#define MAX_CATALOG_INDEXES 64

/*
 * Explicit secondary index metadata.
 *
 * Primary-key indexes are still derived from Schema column flags. CREATE INDEX
 * entries live here because they need stable names and must survive db_open.
 * Explicit indexes store one or more key columns. They are non-unique by
 * default and can contain duplicate key values that point to different RIDs.
 */
typedef struct {
    char index_name[MAX_INDEX_NAME];
    char table_name[MAX_TABLE_NAME];
    uint16_t column_count;
    char column_names[MAX_COLUMNS][MAX_COLUMN_NAME];
    bool unique;
} CatalogIndex;

/*
 * The Catalog stores all known table schemas in memory.
 * It is loaded from mydb/catalog.db when the database opens.
 * It is saved back to disk when the database closes.
 */
typedef struct {
    uint16_t table_count;
    Schema tables[MAX_CATALOG_TABLES];
    uint16_t index_count;
    CatalogIndex indexes[MAX_CATALOG_INDEXES];
} Catalog;

/*
 * Loads the catalog file from disk into db->catalog.
 * If catalog.db does not exist yet, the catalog starts empty.
 * This should be called during db_open.
 */
DBStatus catalog_load(DB *db);

/*
 * Saves db->catalog to mydb/catalog.db.
 * The current format is simple readable text.
 * This should be called during db_close and after CREATE TABLE later.
 */
DBStatus catalog_save(const DB *db);

/*
 * Adds a new table schema to the catalog.
 * Also creates the matching table file in mydb/tables/.
 * Returns an error if the table already exists.
 */
DBStatus catalog_create_table(DB *db, const Schema *schema);

/*
 * Adds a CREATE INDEX entry to the catalog and persists it.
 * The executor builds the B+ tree file before this metadata is committed.
 */
DBStatus catalog_create_index(DB *db, const CatalogIndex *index);

/*
 * Copies a table schema out of the catalog by table name.
 * Returns DB_OK if the table exists.
 * Returns DB_NOT_FOUND if the table does not exist.
 */
DBStatus catalog_get_schema(
    const DB *db,
    const char *table_name,
    Schema *out_schema
);

/*
 * Checks whether a table exists in the catalog.
 * Returns true if a schema with this table name exists.
 * Returns false otherwise.
 */
bool catalog_table_exists(const DB *db, const char *table_name);

bool catalog_index_exists(const DB *db, const char *index_name);

DBStatus catalog_find_index_for_column(
    const DB *db,
    const char *table_name,
    const char *column_name,
    CatalogIndex *out_index
);

/*
 * Prints all table names in the catalog.
 * This will be useful later for the .tables command.
 * If there are no tables, nothing is printed.
 */
void catalog_list_tables(const DB *db, FILE *out);

#endif
