#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "catalog.h"
#include "common.h"
#include "db.h"
#include "schema.h"
#include "value.h"

static void cleanup_db_dir(const char *path) {
    char catalog_path[MAX_DB_PATH];
    char users_path[MAX_DB_PATH];
    char posts_path[MAX_DB_PATH];
    char tables_dir[MAX_DB_PATH];

    snprintf(catalog_path, sizeof(catalog_path), "%s/catalog.db", path);
    snprintf(users_path, sizeof(users_path), "%s/tables/users.tbl", path);
    snprintf(posts_path, sizeof(posts_path), "%s/tables/posts.tbl", path);
    snprintf(tables_dir, sizeof(tables_dir), "%s/tables", path);

    remove(users_path);
    remove(posts_path);
    remove(catalog_path);
    rmdir(tables_dir);
    rmdir(path);
}

static void setup_db(DB *db, const char *path) {
    cleanup_db_dir(path);

    assert(db_open(db, path) == DB_OK);
}

static void setup_users_schema(Schema *schema) {
    assert(schema_init(schema, "users") == DB_OK);
    assert(schema_add_column(schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(schema, "name", VALUE_TEXT, true, false) == DB_OK);
}

static void setup_posts_schema(Schema *schema) {
    assert(schema_init(schema, "posts") == DB_OK);
    assert(schema_add_column(schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(schema, "title", VALUE_TEXT, true, false) == DB_OK);
}

static void write_catalog_file(const char *path, const char *contents) {
    char catalog_path[MAX_DB_PATH];
    FILE *file;

    snprintf(catalog_path, sizeof(catalog_path), "%s/catalog.db", path);

    file = fopen(catalog_path, "w");
    assert(file != NULL);
    assert(fputs(contents, file) >= 0);
    assert(fclose(file) == 0);
}

static void test_catalog_load_empty_when_missing(void) {
    const char *path = "test_catalog_load_empty";

    DB db;

    setup_db(&db, path);

    assert(db.catalog.table_count == 0);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_catalog_create_table(void) {
    const char *path = "test_catalog_create_table";

    DB db;
    Schema schema;
    FILE *table_file;

    setup_db(&db, path);
    setup_users_schema(&schema);

    assert(catalog_create_table(&db, &schema) == DB_OK);

    assert(db.catalog.table_count == 1);
    assert(strcmp(db.catalog.tables[0].table_name, "users") == 0);

    table_file = fopen("test_catalog_create_table/tables/users.tbl", "rb");
    assert(table_file != NULL);
    assert(fclose(table_file) == 0);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_catalog_create_table_rejects_duplicate(void) {
    const char *path = "test_catalog_duplicate";

    DB db;
    Schema schema;

    setup_db(&db, path);
    setup_users_schema(&schema);

    assert(catalog_create_table(&db, &schema) == DB_OK);
    assert(catalog_create_table(&db, &schema) == DB_ERROR);
    assert(db.catalog.table_count == 1);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_catalog_create_table_rejects_null_inputs(void) {
    const char *path = "test_catalog_create_null";

    DB db;
    Schema schema;

    setup_db(&db, path);
    setup_users_schema(&schema);

    assert(catalog_create_table(NULL, &schema) == DB_ERROR);
    assert(catalog_create_table(&db, NULL) == DB_ERROR);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_catalog_get_schema(void) {
    const char *path = "test_catalog_get_schema";

    DB db;
    Schema schema;
    Schema copy;

    setup_db(&db, path);
    setup_users_schema(&schema);

    assert(catalog_create_table(&db, &schema) == DB_OK);
    assert(catalog_get_schema(&db, "users", &copy) == DB_OK);

    assert(strcmp(copy.table_name, "users") == 0);
    assert(copy.column_count == 2);
    assert(strcmp(copy.columns[0].name, "id") == 0);
    assert(copy.columns[0].type == VALUE_INT);
    assert(strcmp(copy.columns[1].name, "name") == 0);
    assert(copy.columns[1].type == VALUE_TEXT);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_catalog_get_schema_missing_table(void) {
    const char *path = "test_catalog_missing";

    DB db;
    Schema copy;

    setup_db(&db, path);

    assert(catalog_get_schema(&db, "missing", &copy) == DB_NOT_FOUND);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_catalog_get_schema_rejects_null_inputs(void) {
    const char *path = "test_catalog_get_null";

    DB db;
    Schema copy;

    setup_db(&db, path);

    assert(catalog_get_schema(NULL, "users", &copy) == DB_ERROR);
    assert(catalog_get_schema(&db, NULL, &copy) == DB_ERROR);
    assert(catalog_get_schema(&db, "users", NULL) == DB_ERROR);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_catalog_table_exists(void) {
    const char *path = "test_catalog_exists";

    DB db;
    Schema schema;

    setup_db(&db, path);
    setup_users_schema(&schema);

    assert(catalog_table_exists(&db, "users") == false);

    assert(catalog_create_table(&db, &schema) == DB_OK);

    assert(catalog_table_exists(&db, "users") == true);
    assert(catalog_table_exists(&db, "missing") == false);
    assert(catalog_table_exists(NULL, "users") == false);
    assert(catalog_table_exists(&db, NULL) == false);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_catalog_list_tables(void) {
    const char *path = "test_catalog_list";

    DB db;
    Schema users;
    Schema posts;
    FILE *out;
    char buffer[128];

    setup_db(&db, path);
    setup_users_schema(&users);
    setup_posts_schema(&posts);

    assert(catalog_create_table(&db, &users) == DB_OK);
    assert(catalog_create_table(&db, &posts) == DB_OK);

    out = tmpfile();
    assert(out != NULL);

    catalog_list_tables(&db, out);

    rewind(out);
    assert(fgets(buffer, sizeof(buffer), out) != NULL);
    assert(strcmp(buffer, "users\n") == 0);
    assert(fgets(buffer, sizeof(buffer), out) != NULL);
    assert(strcmp(buffer, "posts\n") == 0);
    assert(fgets(buffer, sizeof(buffer), out) == NULL);
    assert(fclose(out) == 0);

    catalog_list_tables(NULL, stdout);
    catalog_list_tables(&db, NULL);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_catalog_save_and_load(void) {
    const char *path = "test_catalog_save_load";

    DB first_db;
    DB second_db;
    Schema schema;
    Schema copy;

    setup_db(&first_db, path);
    setup_users_schema(&schema);

    assert(catalog_create_table(&first_db, &schema) == DB_OK);
    assert(db_close(&first_db) == DB_OK);

    assert(db_open(&second_db, path) == DB_OK);
    assert(second_db.catalog.table_count == 1);
    assert(catalog_get_schema(&second_db, "users", &copy) == DB_OK);
    assert(copy.column_count == 2);
    assert(strcmp(copy.columns[0].name, "id") == 0);
    assert(copy.columns[0].type == VALUE_INT);
    assert(copy.columns[0].not_null == false);
    assert(copy.columns[0].primary_key == false);

    assert(db_close(&second_db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_catalog_load_rejects_bad_type(void) {
    const char *path = "test_catalog_bad_type";

    DB db;

    setup_db(&db, path);
    assert(db_close(&db) == DB_OK);

    write_catalog_file(path, "TABLE users\nCOLUMNS 1\nid FLOAT\nEND\n");

    assert(db_open(&db, path) == DB_TYPE_ERROR);

    cleanup_db_dir(path);
}

static void test_catalog_load_rejects_missing_end(void) {
    const char *path = "test_catalog_missing_end";

    DB db;

    setup_db(&db, path);
    assert(db_close(&db) == DB_OK);

    write_catalog_file(path, "TABLE users\nCOLUMNS 1\nid INT\n");

    assert(db_open(&db, path) == DB_PARSE_ERROR);

    cleanup_db_dir(path);
}

int main(void) {
    test_catalog_load_empty_when_missing();
    test_catalog_create_table();
    test_catalog_create_table_rejects_duplicate();
    test_catalog_create_table_rejects_null_inputs();
    test_catalog_get_schema();
    test_catalog_get_schema_missing_table();
    test_catalog_get_schema_rejects_null_inputs();
    test_catalog_table_exists();
    test_catalog_list_tables();
    test_catalog_save_and_load();
    test_catalog_load_rejects_bad_type();
    test_catalog_load_rejects_missing_end();

    printf("All catalog tests passed.\n");

    return 0;
}
