#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "buffer/buffer_pool.h"
#include "catalog.h"
#include "common.h"
#include "db.h"
#include "index/btree.h"
#include "record.h"
#include "row.h"
#include "schema.h"
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

static DBStatus db_create_indexes_dir(const DB *db) {
    if (db == NULL) {
        return DB_ERROR;
    }

    char indexes_path[MAX_DB_PATH];

    /*
     * Primary-key indexes live beside table files but under their own folder
     * so table data and access-path data can be rebuilt independently.
     */
    int written = snprintf(
        indexes_path,
        sizeof(indexes_path),
        "%s/indexes",
        db->path
    );

    if (written < 0 || written >= (int)sizeof(indexes_path)) {
        return DB_ERROR;
    }

    return db_create_dir_if_needed(indexes_path);
}

static DBStatus db_table_file_path(
    const DB *db,
    const char *table_name,
    char *out_path,
    size_t out_size
) {
    if (db == NULL || table_name == NULL || out_path == NULL || out_size == 0) {
        return DB_ERROR;
    }

    int written = snprintf(out_path, out_size, "%s/tables/%s.tbl", db->path, table_name);

    if (written < 0 || written >= (int)out_size) {
        return DB_ERROR;
    }

    return DB_OK;
}

static DBStatus db_primary_key_index_path(
    const DB *db,
    const char *table_name,
    char *out_path,
    size_t out_size
) {
    if (db == NULL || table_name == NULL || out_path == NULL || out_size == 0) {
        return DB_ERROR;
    }

    /*
     * Keep this convention in sync with catalog/executor. There is one
     * primary-key B+ tree per table that declares an INT PRIMARY KEY.
     */
    int written = snprintf(
        out_path,
        out_size,
        "%s/indexes/%s_pk.btree",
        db->path,
        table_name
    );

    if (written < 0 || written >= (int)out_size) {
        return DB_ERROR;
    }

    return DB_OK;
}

static DBStatus db_secondary_index_path(
    const DB *db,
    const char *index_name,
    char *out_path,
    size_t out_size
) {
    if (db == NULL || index_name == NULL || out_path == NULL || out_size == 0) {
        return DB_ERROR;
    }

    int written = snprintf(
        out_path,
        out_size,
        "%s/indexes/%s.btree",
        db->path,
        index_name
    );

    if (written < 0 || written >= (int)out_size) {
        return DB_ERROR;
    }

    return DB_OK;
}

typedef struct {
    /*
     * record_scan only gives us row bytes and RIDs. The rebuild context carries
     * the opened B+ tree plus the schema position of the primary-key value.
     */
    BTree *tree;
    const Schema *schema;
    uint16_t key_column_index;
} DBIndexRebuildContext;

static DBStatus db_index_rebuild_callback(const Row *row, RID rid, void *context) {
    DBIndexRebuildContext *rebuild = context;

    if (row == NULL || rebuild == NULL) {
        return DB_ERROR;
    }

    const Value *key = row_get_value_const(row, rebuild->key_column_index);

    if (key == NULL || key->type != VALUE_INT) {
        return DB_ERROR;
    }

    return btree_insert(rebuild->tree, key->int_value, rid);
}

static DBStatus db_rebuild_int_index(
    const DB *db,
    const Schema *schema,
    uint16_t key_column_index,
    const char *index_path
) {
    /*
     * Rebuild starts from a blank index file and replays the authoritative
     * table rows. The buffer pool may still have cached pages for this path
     * from an earlier open, so discard them before truncating the file.
     */
    DBStatus status = buffer_pool_discard_file(index_path);

    if (status != DB_OK) {
        return status;
    }

    FILE *index_file = fopen(index_path, "wb");

    if (index_file == NULL) {
        return DB_IO_ERROR;
    }

    if (fclose(index_file) != 0) {
        return DB_IO_ERROR;
    }

    BTree tree;

    status = btree_open(&tree, index_path);

    if (status != DB_OK) {
        return status;
    }

    char table_file[MAX_DB_PATH];
    status = db_table_file_path(db, schema->table_name, table_file, sizeof(table_file));

    if (status == DB_OK) {
        DBIndexRebuildContext context;

        context.tree = &tree;
        context.schema = schema;
        context.key_column_index = key_column_index;

        status = record_scan(table_file, db_index_rebuild_callback, &context);
    }

    DBStatus close_status = btree_close(&tree);

    if (status != DB_OK) {
        return status;
    }

    return close_status;
}

static DBStatus db_rebuild_primary_key_index(const DB *db, const Schema *schema) {
    uint16_t primary_key_index = 0;
    DBStatus status = schema_get_primary_key_index(schema, &primary_key_index);

    if (status == DB_NOT_FOUND) {
        return DB_OK;
    }

    if (status != DB_OK) {
        return status;
    }

    char index_path[MAX_DB_PATH];
    status = db_primary_key_index_path(
        db,
        schema->table_name,
        index_path,
        sizeof(index_path)
    );

    if (status != DB_OK) {
        return status;
    }

    return db_rebuild_int_index(db, schema, primary_key_index, index_path);
}

static DBStatus db_rebuild_secondary_index(
    const DB *db,
    const CatalogIndex *index
) {
    Schema schema;
    uint16_t column_index = 0;

    DBStatus status = catalog_get_schema(db, index->table_name, &schema);

    if (status != DB_OK) {
        return status;
    }

    status = schema_get_column_index(&schema, index->column_name, &column_index);

    if (status != DB_OK) {
        return status;
    }

    char index_path[MAX_DB_PATH];
    status = db_secondary_index_path(
        db,
        index->index_name,
        index_path,
        sizeof(index_path)
    );

    if (status != DB_OK) {
        return status;
    }

    return db_rebuild_int_index(db, &schema, column_index, index_path);
}

static DBStatus db_rebuild_primary_key_indexes(const DB *db) {
    if (db == NULL) {
        return DB_ERROR;
    }

    for (uint16_t i = 0; i < db->catalog.table_count; i++) {
        /*
         * WAL recovery currently restores table pages, not index pages. Rebuild
         * each primary-key index from table contents after recovery/catalog load
         * so the access path matches the durable rows.
         */
        DBStatus status = db_rebuild_primary_key_index(db, &db->catalog.tables[i]);

        if (status != DB_OK) {
            return status;
        }
    }

    return DB_OK;
}

static DBStatus db_rebuild_secondary_indexes(const DB *db) {
    if (db == NULL) {
        return DB_ERROR;
    }

    for (uint16_t i = 0; i < db->catalog.index_count; i++) {
        /*
         * Explicit indexes are also derived from table rows. Rebuilding at
         * open keeps them aligned after WAL recovery or a previous crash.
         */
        DBStatus status = db_rebuild_secondary_index(db, &db->catalog.indexes[i]);

        if (status != DB_OK) {
            return status;
        }
    }

    return DB_OK;
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

    status = db_create_indexes_dir(db);

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

    status = db_rebuild_primary_key_indexes(db);

    if (status != DB_OK) {
        wal_close(&db->wal);
        memset(db, 0, sizeof(DB));
        return status;
    }

    status = db_rebuild_secondary_indexes(db);

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
