#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "catalog.h"
#include "common.h"
#include "db.h"
#include "record.h"
#include "rid.h"
#include "row.h"
#include "schema.h"
#include "table.h"
#include "value.h"

typedef struct {
    uint32_t count;
    RID last_rid;
} ScanContext;

static void cleanup_db_dir(const char *path) {
    char table_path[MAX_DB_PATH];
    char tables_dir[MAX_DB_PATH];
    char wal_path[MAX_DB_PATH];

    snprintf(table_path, sizeof(table_path), "%s/tables/users.tbl", path);
    snprintf(tables_dir, sizeof(tables_dir), "%s/tables", path);
    snprintf(wal_path, sizeof(wal_path), "%s/minidb.wal", path);

    char catalog_path[MAX_DB_PATH];

    snprintf(catalog_path, sizeof(catalog_path), "%s/catalog.db", path);

    remove(table_path);
    remove(wal_path);
    remove(catalog_path);
    rmdir(tables_dir);
    rmdir(path);
}

static void setup_db(DB *db, const char *path) {
    cleanup_db_dir(path);

    assert(db_open(db, path) == DB_OK);
}

static void setup_schema(DB *db, Schema *schema) {
    assert(schema_init(schema, "users") == DB_OK);

    assert(schema_add_column(schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(schema, "name", VALUE_TEXT, true, false) == DB_OK);
    assert(schema_add_column(schema, "age", VALUE_INT, false, false) == DB_OK);

    assert(catalog_create_table(db, schema) == DB_OK);
}

static Row make_test_row(int32_t id, const char *name, int32_t age) {
    Row row;

    assert(row_create(&row, 3) == DB_OK);

    row.values[0] = value_int(id);
    assert(value_text(&row.values[1], name) == DB_OK);
    row.values[2] = value_int(age);

    return row;
}

static void assert_test_row_matches(const Row *row, int32_t id, const char *name, int32_t age) {
    assert(row != NULL);
    assert(row->value_count == 3);

    assert(row->values[0].type == VALUE_INT);
    assert(row->values[0].int_value == id);

    assert(row->values[1].type == VALUE_TEXT);
    assert(strcmp(row->values[1].text_value, name) == 0);

    assert(row->values[2].type == VALUE_INT);
    assert(row->values[2].int_value == age);
}

static DBStatus count_scan_callback(const Row *row, RID rid, void *context) {
    ScanContext *scan_context = context;

    assert(row != NULL);
    assert(scan_context != NULL);

    scan_context->count++;
    scan_context->last_rid = rid;

    return DB_OK;
}

static DBStatus failing_scan_callback(const Row *row, RID rid, void *context) {
    (void)row;
    (void)rid;
    (void)context;

    return DB_ERROR;
}

static void test_table_open(void) {
    const char *path = "test_table_open_db";

    DB db;
    Schema schema;
    Table table;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table.is_open == true);
    assert(strcmp(table.schema.table_name, "users") == 0);
    assert(strcmp(table.file_path, "test_table_open_db/tables/users.tbl") == 0);
    assert(table.pager.file != NULL);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_open_rejects_null_inputs(void) {
    const char *path = "test_table_open_null_db";

    DB db;
    Schema schema;
    Table table;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    assert(table_open(NULL, &db, "users") == DB_ERROR);
    assert(table_open(&table, NULL, "users") == DB_ERROR);
    assert(table_open(&table, &db, NULL) == DB_ERROR);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_close_null(void) {
    assert(table_close(NULL) == DB_ERROR);
}

static void test_table_open_missing_table(void) {
    const char *path = "test_table_open_missing_db";

    DB db;
    Table table;

    setup_db(&db, path);

    assert(table_open(&table, &db, "missing") == DB_NOT_FOUND);
    assert(table.is_open == false);

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_close_unopened_table(void) {
    Table table;

    memset(&table, 0, sizeof(Table));

    assert(table_close(&table) == DB_OK);
}

static void test_table_close_resets_table(void) {
    const char *path = "test_table_close_resets_db";

    DB db;
    Schema schema;
    Table table;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    assert(table_open(&table, &db, "users") == DB_OK);
    assert(table_close(&table) == DB_OK);

    assert(table.is_open == false);
    assert(table.pager.file == NULL);
    assert(table.file_path[0] == '\0');

    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_insert_single_row(void) {
    const char *path = "test_table_insert_single_db";

    DB db;
    Schema schema;
    Table table;
    Row row;
    RID rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    row = make_test_row(1, "Finn", 20);

    assert(table_open(&table, &db, "users") == DB_OK);
    assert(table_insert(&table, &row, &rid) == DB_OK);

    assert(rid.page_id == 0);
    assert(rid.slot_id == 0);

    row_free(&row);
    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_insert_multiple_rows(void) {
    const char *path = "test_table_insert_multiple_db";

    DB db;
    Schema schema;
    Table table;

    Row first;
    Row second;

    RID first_rid;
    RID second_rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    first = make_test_row(1, "Finn", 20);
    second = make_test_row(2, "Alex", 21);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_insert(&table, &first, &first_rid) == DB_OK);
    assert(table_insert(&table, &second, &second_rid) == DB_OK);

    assert(first_rid.page_id == 0);
    assert(first_rid.slot_id == 0);

    assert(second_rid.page_id == 0);
    assert(second_rid.slot_id == 1);

    row_free(&first);
    row_free(&second);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_insert_rejects_null_inputs(void) {
    const char *path = "test_table_insert_null_db";

    DB db;
    Schema schema;
    Table table;
    Row row;
    RID rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    row = make_test_row(1, "Finn", 20);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_insert(NULL, &row, &rid) == DB_ERROR);
    assert(table_insert(&table, NULL, &rid) == DB_ERROR);
    assert(table_insert(&table, &row, NULL) == DB_ERROR);

    row_free(&row);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_insert_rejects_closed_table(void) {
    Table table;
    Row row;
    RID rid;

    memset(&table, 0, sizeof(Table));

    row = make_test_row(1, "Finn", 20);

    assert(table_insert(&table, &row, &rid) == DB_ERROR);

    row_free(&row);
}

static void test_table_insert_rejects_wrong_value_count(void) {
    const char *path = "test_table_insert_wrong_count_db";

    DB db;
    Schema schema;
    Table table;
    Row row;
    RID rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    assert(row_create(&row, 1) == DB_OK);
    row.values[0] = value_int(1);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_insert(&table, &row, &rid) == DB_ERROR);

    row_free(&row);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_insert_rejects_wrong_type(void) {
    const char *path = "test_table_insert_wrong_type_db";

    DB db;
    Schema schema;
    Table table;
    Row row;
    RID rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    assert(row_create(&row, 3) == DB_OK);

    assert(value_text(&row.values[0], "not an int") == DB_OK);
    assert(value_text(&row.values[1], "Finn") == DB_OK);
    row.values[2] = value_int(20);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_insert(&table, &row, &rid) == DB_TYPE_ERROR);

    row_free(&row);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_get_existing_row(void) {
    const char *path = "test_table_get_existing_db";

    DB db;
    Schema schema;
    Table table;
    Row original;
    Row copy;
    RID rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    original = make_test_row(1, "Finn", 20);

    assert(table_open(&table, &db, "users") == DB_OK);
    assert(table_insert(&table, &original, &rid) == DB_OK);

    assert(table_get(&table, rid, &copy) == DB_OK);
    assert_test_row_matches(&copy, 1, "Finn", 20);

    row_free(&original);
    row_free(&copy);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_get_second_row(void) {
    const char *path = "test_table_get_second_db";

    DB db;
    Schema schema;
    Table table;

    Row first;
    Row second;
    Row copy;

    RID first_rid;
    RID second_rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    first = make_test_row(1, "Finn", 20);
    second = make_test_row(2, "Alex", 21);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_insert(&table, &first, &first_rid) == DB_OK);
    assert(table_insert(&table, &second, &second_rid) == DB_OK);

    assert(table_get(&table, second_rid, &copy) == DB_OK);
    assert_test_row_matches(&copy, 2, "Alex", 21);

    row_free(&first);
    row_free(&second);
    row_free(&copy);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_get_rejects_null_inputs(void) {
    const char *path = "test_table_get_null_db";

    DB db;
    Schema schema;
    Table table;
    Row out_row;
    RID rid = {0, 0};

    setup_db(&db, path);
    setup_schema(&db, &schema);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_get(NULL, rid, &out_row) == DB_ERROR);
    assert(table_get(&table, rid, NULL) == DB_ERROR);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_get_rejects_closed_table(void) {
    Table table;
    Row out_row;
    RID rid = {0, 0};

    memset(&table, 0, sizeof(Table));

    assert(table_get(&table, rid, &out_row) == DB_ERROR);
}

static void test_table_get_missing_page(void) {
    const char *path = "test_table_get_missing_page_db";

    DB db;
    Schema schema;
    Table table;
    Row out_row;
    RID rid = {0, 0};

    setup_db(&db, path);
    setup_schema(&db, &schema);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_get(&table, rid, &out_row) == DB_NOT_FOUND);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_get_missing_slot(void) {
    const char *path = "test_table_get_missing_slot_db";

    DB db;
    Schema schema;
    Table table;
    Row row;
    Row out_row;

    RID rid;
    RID missing_rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    row = make_test_row(1, "Finn", 20);

    assert(table_open(&table, &db, "users") == DB_OK);
    assert(table_insert(&table, &row, &rid) == DB_OK);

    missing_rid.page_id = rid.page_id;
    missing_rid.slot_id = 99;

    assert(table_get(&table, missing_rid, &out_row) == DB_NOT_FOUND);

    row_free(&row);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_delete_existing_row(void) {
    const char *path = "test_table_delete_existing_db";

    DB db;
    Schema schema;
    Table table;
    Row row;
    Row out_row;
    RID rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    row = make_test_row(1, "Finn", 20);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_insert(&table, &row, &rid) == DB_OK);
    assert(table_delete(&table, rid) == DB_OK);
    assert(table_get(&table, rid, &out_row) == DB_NOT_FOUND);

    row_free(&row);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_delete_rejects_closed_table(void) {
    Table table;
    RID rid = {0, 0};

    memset(&table, 0, sizeof(Table));

    assert(table_delete(&table, rid) == DB_ERROR);
}

static void test_table_delete_missing_page(void) {
    const char *path = "test_table_delete_missing_page_db";

    DB db;
    Schema schema;
    Table table;
    RID rid = {0, 0};

    setup_db(&db, path);
    setup_schema(&db, &schema);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_delete(&table, rid) == DB_NOT_FOUND);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_delete_missing_slot(void) {
    const char *path = "test_table_delete_missing_slot_db";

    DB db;
    Schema schema;
    Table table;
    Row row;

    RID rid;
    RID missing_rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    row = make_test_row(1, "Finn", 20);

    assert(table_open(&table, &db, "users") == DB_OK);
    assert(table_insert(&table, &row, &rid) == DB_OK);

    missing_rid.page_id = rid.page_id;
    missing_rid.slot_id = 99;

    assert(table_delete(&table, missing_rid) == DB_NOT_FOUND);

    row_free(&row);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_delete_already_deleted_row(void) {
    const char *path = "test_table_delete_twice_db";

    DB db;
    Schema schema;
    Table table;
    Row row;
    RID rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    row = make_test_row(1, "Finn", 20);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_insert(&table, &row, &rid) == DB_OK);
    assert(table_delete(&table, rid) == DB_OK);
    assert(table_delete(&table, rid) == DB_NOT_FOUND);

    row_free(&row);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_insert_reuses_deleted_slot(void) {
    const char *path = "test_table_reuse_deleted_slot_db";

    DB db;
    Schema schema;
    Table table;

    Row first;
    Row second;

    RID first_rid;
    RID second_rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    first = make_test_row(1, "Finn", 20);
    second = make_test_row(2, "Alex", 21);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_insert(&table, &first, &first_rid) == DB_OK);
    assert(table_delete(&table, first_rid) == DB_OK);
    assert(table_insert(&table, &second, &second_rid) == DB_OK);

    assert(second_rid.page_id == first_rid.page_id);
    assert(second_rid.slot_id == first_rid.slot_id);

    row_free(&first);
    row_free(&second);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_persists_after_reopen(void) {
    const char *path = "test_table_persist_db";

    DB db;
    Schema schema;
    Table first_table;
    Table second_table;

    Row original;
    Row copy;
    RID rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    original = make_test_row(1, "Finn", 20);

    assert(table_open(&first_table, &db, "users") == DB_OK);
    assert(table_insert(&first_table, &original, &rid) == DB_OK);
    assert(table_close(&first_table) == DB_OK);

    assert(table_open(&second_table, &db, "users") == DB_OK);

    assert(table_get(&second_table, rid, &copy) == DB_OK);
    assert_test_row_matches(&copy, 1, "Finn", 20);

    row_free(&original);
    row_free(&copy);

    assert(table_close(&second_table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_scan_empty_table(void) {
    const char *path = "test_table_scan_empty_db";

    DB db;
    Schema schema;
    Table table;
    ScanContext context;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    context.count = 0;
    context.last_rid.page_id = 0;
    context.last_rid.slot_id = 0;

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_scan(&table, count_scan_callback, &context) == DB_OK);
    assert(context.count == 0);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_scan_counts_active_rows(void) {
    const char *path = "test_table_scan_counts_db";

    DB db;
    Schema schema;
    Table table;
    ScanContext context;

    Row first;
    Row second;
    Row third;

    RID first_rid;
    RID second_rid;
    RID third_rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    first = make_test_row(1, "Finn", 20);
    second = make_test_row(2, "Alex", 21);
    third = make_test_row(3, "Sam", 22);

    context.count = 0;
    context.last_rid.page_id = 0;
    context.last_rid.slot_id = 0;

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_insert(&table, &first, &first_rid) == DB_OK);
    assert(table_insert(&table, &second, &second_rid) == DB_OK);
    assert(table_insert(&table, &third, &third_rid) == DB_OK);

    assert(table_scan(&table, count_scan_callback, &context) == DB_OK);

    assert(context.count == 3);

    row_free(&first);
    row_free(&second);
    row_free(&third);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_scan_skips_deleted_rows(void) {
    const char *path = "test_table_scan_skips_deleted_db";

    DB db;
    Schema schema;
    Table table;
    ScanContext context;

    Row first;
    Row second;

    RID first_rid;
    RID second_rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    first = make_test_row(1, "Finn", 20);
    second = make_test_row(2, "Alex", 21);

    context.count = 0;
    context.last_rid.page_id = 0;
    context.last_rid.slot_id = 0;

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_insert(&table, &first, &first_rid) == DB_OK);
    assert(table_insert(&table, &second, &second_rid) == DB_OK);

    assert(table_delete(&table, first_rid) == DB_OK);

    assert(table_scan(&table, count_scan_callback, &context) == DB_OK);

    assert(context.count == 1);
    assert(context.last_rid.page_id == second_rid.page_id);
    assert(context.last_rid.slot_id == second_rid.slot_id);

    row_free(&first);
    row_free(&second);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_scan_rejects_null_inputs(void) {
    const char *path = "test_table_scan_null_db";

    DB db;
    Schema schema;
    Table table;
    ScanContext context;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    assert(table_open(&table, &db, "users") == DB_OK);

    assert(table_scan(NULL, count_scan_callback, &context) == DB_ERROR);
    assert(table_scan(&table, NULL, &context) == DB_ERROR);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

static void test_table_scan_rejects_closed_table(void) {
    Table table;
    ScanContext context;

    memset(&table, 0, sizeof(Table));

    assert(table_scan(&table, count_scan_callback, &context) == DB_ERROR);
}

static void test_table_scan_returns_callback_error(void) {
    const char *path = "test_table_scan_callback_error_db";

    DB db;
    Schema schema;
    Table table;
    Row row;
    RID rid;

    setup_db(&db, path);
    setup_schema(&db, &schema);

    row = make_test_row(1, "Finn", 20);

    assert(table_open(&table, &db, "users") == DB_OK);
    assert(table_insert(&table, &row, &rid) == DB_OK);

    assert(table_scan(&table, failing_scan_callback, NULL) == DB_ERROR);

    row_free(&row);

    assert(table_close(&table) == DB_OK);
    assert(db_close(&db) == DB_OK);

    cleanup_db_dir(path);
}

int main(void) {
    test_table_open();
    test_table_open_rejects_null_inputs();
    test_table_open_missing_table();
    test_table_close_null();
    test_table_close_unopened_table();
    test_table_close_resets_table();

    test_table_insert_single_row();
    test_table_insert_multiple_rows();
    test_table_insert_rejects_null_inputs();
    test_table_insert_rejects_closed_table();
    test_table_insert_rejects_wrong_value_count();
    test_table_insert_rejects_wrong_type();

    test_table_get_existing_row();
    test_table_get_second_row();
    test_table_get_rejects_null_inputs();
    test_table_get_rejects_closed_table();
    test_table_get_missing_page();
    test_table_get_missing_slot();

    test_table_delete_existing_row();
    test_table_delete_rejects_closed_table();
    test_table_delete_missing_page();
    test_table_delete_missing_slot();
    test_table_delete_already_deleted_row();
    test_table_insert_reuses_deleted_slot();

    test_table_persists_after_reopen();

    test_table_scan_empty_table();
    test_table_scan_counts_active_rows();
    test_table_scan_skips_deleted_rows();
    test_table_scan_rejects_null_inputs();
    test_table_scan_rejects_closed_table();
    test_table_scan_returns_callback_error();

    printf("All table tests passed.\n");

    return 0;
}
