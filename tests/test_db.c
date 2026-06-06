#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "catalog.h"
#include "common.h"
#include "db.h"
#include "schema.h"
#include "value.h"

static bool path_is_dir(const char *path) {
    struct stat info;

    if (stat(path, &info) != 0) {
        return false;
    }

    return S_ISDIR(info.st_mode);
}

static void cleanup_db_dir(const char *path) {
    char catalog_path[MAX_DB_PATH];
    char table_path[MAX_DB_PATH];
    char tables_dir[MAX_DB_PATH];
    char wal_path[MAX_DB_PATH];

    snprintf(catalog_path, sizeof(catalog_path), "%s/catalog.db", path);
    snprintf(table_path, sizeof(table_path), "%s/tables/users.tbl", path);
    snprintf(tables_dir, sizeof(tables_dir), "%s/tables", path);
    snprintf(wal_path, sizeof(wal_path), "%s/minidb.wal", path);

    remove(table_path);
    remove(wal_path);
    remove(catalog_path);
    rmdir(tables_dir);
    rmdir(path);
}

static void setup_schema(Schema *schema) {
    assert(schema_init(schema, "users") == DB_OK);
    assert(schema_add_column(schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(schema, "name", VALUE_TEXT, false, false) == DB_OK);
}

static void test_db_open_new_database(void) {
    const char *path = "test_db_open_new";

    DB db;

    cleanup_db_dir(path);

    assert(db_open(&db, path) == DB_OK);

    assert(db.is_open == true);
    assert(strcmp(db.path, path) == 0);
    assert(db.catalog.table_count == 0);
    assert(db.wal.is_open == true);
    assert(db.transaction.wal == &db.wal);
    assert(db.transaction.state == TRANSACTION_STATE_IDLE);
    assert(path_is_dir(path) == true);
    assert(path_is_dir("test_db_open_new/tables") == true);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_db_open_existing_database_loads_catalog(void) {
    const char *path = "test_db_open_existing";

    DB first_db;
    DB second_db;
    Schema schema;

    cleanup_db_dir(path);

    assert(db_open(&first_db, path) == DB_OK);
    setup_schema(&schema);
    assert(catalog_create_table(&first_db, &schema) == DB_OK);
    assert(db_close(&first_db) == DB_OK);

    assert(db_open(&second_db, path) == DB_OK);

    assert(second_db.is_open == true);
    assert(second_db.catalog.table_count == 1);
    assert(catalog_table_exists(&second_db, "users") == true);

    assert(db_close(&second_db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_db_open_rejects_null_inputs(void) {
    DB db;

    assert(db_open(NULL, "test_db_null") == DB_ERROR);
    assert(db_open(&db, NULL) == DB_ERROR);
}

static void test_db_open_rejects_empty_path(void) {
    DB db;

    assert(db_open(&db, "") == DB_ERROR);
}

static void test_db_open_rejects_long_path(void) {
    DB db;
    char path[MAX_DB_PATH + 1];

    memset(path, 'a', sizeof(path));
    path[sizeof(path) - 1] = '\0';

    assert(db_open(&db, path) == DB_ERROR);
}

static void test_db_open_rejects_file_path(void) {
    const char *path = "test_db_open_file";

    DB db;
    FILE *file;

    remove(path);

    file = fopen(path, "w");
    assert(file != NULL);
    assert(fclose(file) == 0);

    assert(db_open(&db, path) == DB_ERROR);
    assert(db.is_open == false);
    assert(db.path[0] == '\0');

    remove(path);
}

static void test_db_close_null(void) {
    assert(db_close(NULL) == DB_ERROR);
}

static void test_db_close_unopened_database(void) {
    DB db;

    memset(&db, 0, sizeof(DB));

    assert(db_close(&db) == DB_OK);
}

static void test_db_close_resets_database(void) {
    const char *path = "test_db_close_resets";

    DB db;

    cleanup_db_dir(path);

    assert(db_open(&db, path) == DB_OK);
    assert(db_close(&db) == DB_OK);

    assert(db.is_open == false);
    assert(db.path[0] == '\0');
    assert(db.catalog.table_count == 0);
    assert(db.wal.is_open == false);
    assert(db.transaction.state == TRANSACTION_STATE_IDLE);

    cleanup_db_dir(path);
}

int main(void) {
    test_db_open_new_database();
    test_db_open_existing_database_loads_catalog();
    test_db_open_rejects_null_inputs();
    test_db_open_rejects_empty_path();
    test_db_open_rejects_long_path();
    test_db_open_rejects_file_path();
    test_db_close_null();
    test_db_close_unopened_database();
    test_db_close_resets_database();

    printf("All db tests passed.\n");

    return 0;
}
