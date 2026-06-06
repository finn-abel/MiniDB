#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "index/secondary.h"
#include "record.h"
#include "rid.h"
#include "row.h"
#include "schema.h"
#include "sql/ast.h"
#include "value.h"

typedef struct {
    uint16_t count;
    RID rids[8];
} RIDList;

static void cleanup_file(const char *path) {
    remove(path);
}

static Row make_user_row(int32_t id, const char *name, int32_t age) {
    Row row;

    assert(row_create(&row, 3) == DB_OK);
    row.values[0] = value_int(id);
    assert(value_text(&row.values[1], name) == DB_OK);
    row.values[2] = value_int(age);

    return row;
}

static void make_user_schema(Schema *schema) {
    assert(schema_init(schema, "users") == DB_OK);
    assert(schema_add_column(schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(schema, "name", VALUE_TEXT, true, false) == DB_OK);
    assert(schema_add_column(schema, "age", VALUE_INT, false, false) == DB_OK);
}

static void seed_users(const char *table_file, RID *first, RID *second, RID *third) {
    Row finn = make_user_row(1, "Finn", 20);
    Row alex = make_user_row(2, "Alex", 17);
    Row sam = make_user_row(3, "Sam", 17);

    assert(record_insert(table_file, &finn, first) == DB_OK);
    assert(record_insert(table_file, &alex, second) == DB_OK);
    assert(record_insert(table_file, &sam, third) == DB_OK);

    row_free(&finn);
    row_free(&alex);
    row_free(&sam);
}

static DBStatus collect_rid_callback(RID rid, void *context) {
    RIDList *list = context;

    assert(list != NULL);
    assert(list->count < 8);

    list->rids[list->count] = rid;
    list->count++;

    return DB_OK;
}

static DBStatus failing_rid_callback(RID rid, void *context) {
    (void)rid;
    (void)context;

    return DB_ERROR;
}

static bool rid_list_contains(const RIDList *list, RID rid) {
    for (uint16_t i = 0; i < list->count; i++) {
        if (rid_equal(&list->rids[i], &rid)) {
            return true;
        }
    }

    return false;
}

static void test_secondary_index_build_and_scan_duplicate_int_keys(void) {
    const char *table_file = "test_secondary_duplicate.tbl";
    const char *index_file = "test_secondary_duplicate.sidx";
    Schema schema;
    RID finn;
    RID alex;
    RID sam;
    RIDList list = {0};
    uint16_t age_column = 2;
    Value age = value_int(17);
    WhereCondition condition;

    cleanup_file(table_file);
    cleanup_file(index_file);
    make_user_schema(&schema);
    seed_users(table_file, &finn, &alex, &sam);

    assert(secondary_index_build(index_file, table_file, &schema, &age_column, 1) == DB_OK);
    assert(ast_where_init(&condition, "age", SQL_OPERATOR_EQUAL, &age) == DB_OK);

    assert(secondary_index_scan_condition(
        index_file,
        0,
        &condition,
        collect_rid_callback,
        &list
    ) == DB_OK);

    assert(list.count == 2);
    assert(rid_list_contains(&list, alex));
    assert(rid_list_contains(&list, sam));
    assert(!rid_list_contains(&list, finn));

    ast_where_free(&condition);
    cleanup_file(table_file);
    cleanup_file(index_file);
}

static void test_secondary_index_scan_text_key(void) {
    const char *table_file = "test_secondary_text.tbl";
    const char *index_file = "test_secondary_text.sidx";
    Schema schema;
    RID finn;
    RID alex;
    RID sam;
    RIDList list = {0};
    uint16_t name_column = 1;
    Value name;
    WhereCondition condition;

    cleanup_file(table_file);
    cleanup_file(index_file);
    make_user_schema(&schema);
    seed_users(table_file, &finn, &alex, &sam);

    assert(value_text(&name, "Finn") == DB_OK);
    assert(secondary_index_build(index_file, table_file, &schema, &name_column, 1) == DB_OK);
    assert(ast_where_init(&condition, "name", SQL_OPERATOR_EQUAL, &name) == DB_OK);

    assert(secondary_index_scan_condition(
        index_file,
        0,
        &condition,
        collect_rid_callback,
        &list
    ) == DB_OK);

    assert(list.count == 1);
    assert(rid_list_contains(&list, finn));
    assert(!rid_list_contains(&list, alex));
    assert(!rid_list_contains(&list, sam));

    ast_where_free(&condition);
    value_free(&name);
    cleanup_file(table_file);
    cleanup_file(index_file);
}

static void test_secondary_index_scan_range_key(void) {
    const char *table_file = "test_secondary_range.tbl";
    const char *index_file = "test_secondary_range.sidx";
    Schema schema;
    RID finn;
    RID alex;
    RID sam;
    RIDList list = {0};
    uint16_t age_column = 2;
    Value age = value_int(18);
    WhereCondition condition;

    cleanup_file(table_file);
    cleanup_file(index_file);
    make_user_schema(&schema);
    seed_users(table_file, &finn, &alex, &sam);

    assert(secondary_index_build(index_file, table_file, &schema, &age_column, 1) == DB_OK);
    assert(ast_where_init(&condition, "age", SQL_OPERATOR_GREATER_EQUAL, &age) == DB_OK);

    assert(secondary_index_scan_condition(
        index_file,
        0,
        &condition,
        collect_rid_callback,
        &list
    ) == DB_OK);

    assert(list.count == 1);
    assert(rid_list_contains(&list, finn));
    assert(!rid_list_contains(&list, alex));
    assert(!rid_list_contains(&list, sam));

    ast_where_free(&condition);
    cleanup_file(table_file);
    cleanup_file(index_file);
}

static void test_secondary_index_scan_composite_second_column(void) {
    const char *table_file = "test_secondary_composite.tbl";
    const char *index_file = "test_secondary_composite.sidx";
    Schema schema;
    RID finn;
    RID alex;
    RID sam;
    RIDList list = {0};
    uint16_t columns[2] = {2, 1};
    Value name;
    WhereCondition condition;

    cleanup_file(table_file);
    cleanup_file(index_file);
    make_user_schema(&schema);
    seed_users(table_file, &finn, &alex, &sam);

    assert(value_text(&name, "Sam") == DB_OK);
    assert(secondary_index_build(index_file, table_file, &schema, columns, 2) == DB_OK);
    assert(ast_where_init(&condition, "name", SQL_OPERATOR_EQUAL, &name) == DB_OK);

    assert(secondary_index_scan_condition(
        index_file,
        1,
        &condition,
        collect_rid_callback,
        &list
    ) == DB_OK);

    assert(list.count == 1);
    assert(rid_list_contains(&list, sam));
    assert(!rid_list_contains(&list, finn));
    assert(!rid_list_contains(&list, alex));

    ast_where_free(&condition);
    value_free(&name);
    cleanup_file(table_file);
    cleanup_file(index_file);
}

static void test_secondary_index_build_rejects_invalid_inputs(void) {
    const char *table_file = "test_secondary_invalid_build.tbl";
    const char *index_file = "test_secondary_invalid_build.sidx";
    Schema schema;
    RID first;
    RID second;
    RID third;
    uint16_t age_column = 2;
    uint16_t missing_column = 99;

    cleanup_file(table_file);
    cleanup_file(index_file);
    make_user_schema(&schema);
    seed_users(table_file, &first, &second, &third);

    assert(secondary_index_build(NULL, table_file, &schema, &age_column, 1) == DB_ERROR);
    assert(secondary_index_build(index_file, NULL, &schema, &age_column, 1) == DB_ERROR);
    assert(secondary_index_build(index_file, table_file, NULL, &age_column, 1) == DB_ERROR);
    assert(secondary_index_build(index_file, table_file, &schema, NULL, 1) == DB_ERROR);
    assert(secondary_index_build(index_file, table_file, &schema, &age_column, 0) == DB_ERROR);
    assert(secondary_index_build(index_file, table_file, &schema, &missing_column, 1) == DB_ERROR);

    cleanup_file(table_file);
    cleanup_file(index_file);
}

static void test_secondary_index_scan_rejects_invalid_inputs(void) {
    const char *table_file = "test_secondary_invalid_scan.tbl";
    const char *index_file = "test_secondary_invalid_scan.sidx";
    Schema schema;
    RID first;
    RID second;
    RID third;
    RIDList list = {0};
    uint16_t age_column = 2;
    Value age = value_int(17);
    WhereCondition condition;

    cleanup_file(table_file);
    cleanup_file(index_file);
    make_user_schema(&schema);
    seed_users(table_file, &first, &second, &third);

    assert(secondary_index_build(index_file, table_file, &schema, &age_column, 1) == DB_OK);
    assert(ast_where_init(&condition, "age", SQL_OPERATOR_EQUAL, &age) == DB_OK);

    assert(secondary_index_scan_condition(
        NULL,
        0,
        &condition,
        collect_rid_callback,
        &list
    ) == DB_ERROR);
    assert(secondary_index_scan_condition(
        index_file,
        0,
        NULL,
        collect_rid_callback,
        &list
    ) == DB_ERROR);
    assert(secondary_index_scan_condition(
        index_file,
        0,
        &condition,
        NULL,
        &list
    ) == DB_ERROR);
    assert(secondary_index_scan_condition(
        "test_secondary_missing.sidx",
        0,
        &condition,
        collect_rid_callback,
        &list
    ) == DB_IO_ERROR);
    assert(secondary_index_scan_condition(
        index_file,
        1,
        &condition,
        collect_rid_callback,
        &list
    ) == DB_ERROR);

    ast_where_free(&condition);
    cleanup_file(table_file);
    cleanup_file(index_file);
}

static void test_secondary_index_scan_no_matches(void) {
    const char *table_file = "test_secondary_no_matches.tbl";
    const char *index_file = "test_secondary_no_matches.sidx";
    Schema schema;
    RID first;
    RID second;
    RID third;
    RIDList list = {0};
    uint16_t age_column = 2;
    Value age = value_int(99);
    WhereCondition condition;

    cleanup_file(table_file);
    cleanup_file(index_file);
    make_user_schema(&schema);
    seed_users(table_file, &first, &second, &third);

    assert(secondary_index_build(index_file, table_file, &schema, &age_column, 1) == DB_OK);
    assert(ast_where_init(&condition, "age", SQL_OPERATOR_EQUAL, &age) == DB_OK);

    assert(secondary_index_scan_condition(
        index_file,
        0,
        &condition,
        collect_rid_callback,
        &list
    ) == DB_OK);

    assert(list.count == 0);

    ast_where_free(&condition);
    cleanup_file(table_file);
    cleanup_file(index_file);
}

static void test_secondary_index_scan_propagates_callback_error(void) {
    const char *table_file = "test_secondary_callback_error.tbl";
    const char *index_file = "test_secondary_callback_error.sidx";
    Schema schema;
    RID first;
    RID second;
    RID third;
    uint16_t age_column = 2;
    Value age = value_int(17);
    WhereCondition condition;

    cleanup_file(table_file);
    cleanup_file(index_file);
    make_user_schema(&schema);
    seed_users(table_file, &first, &second, &third);

    assert(secondary_index_build(index_file, table_file, &schema, &age_column, 1) == DB_OK);
    assert(ast_where_init(&condition, "age", SQL_OPERATOR_EQUAL, &age) == DB_OK);

    assert(secondary_index_scan_condition(
        index_file,
        0,
        &condition,
        failing_rid_callback,
        NULL
    ) == DB_ERROR);

    ast_where_free(&condition);
    cleanup_file(table_file);
    cleanup_file(index_file);
}

int main(void) {
    test_secondary_index_build_and_scan_duplicate_int_keys();
    test_secondary_index_scan_text_key();
    test_secondary_index_scan_range_key();
    test_secondary_index_scan_composite_second_column();
    test_secondary_index_build_rejects_invalid_inputs();
    test_secondary_index_scan_rejects_invalid_inputs();
    test_secondary_index_scan_no_matches();
    test_secondary_index_scan_propagates_callback_error();

    printf("All secondary index tests passed.\n");

    return 0;
}
