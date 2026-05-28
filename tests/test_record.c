#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "record.h"
#include "rid.h"
#include "row.h"
#include "value.h"

typedef struct {
    uint32_t count;
    RID last_rid;
} ScanContext;

static void cleanup_file(const char *path) {
    remove(path);
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

static void test_record_insert_single_row(void) {
    const char *path = "test_record_insert_single.db";
    cleanup_file(path);

    Row row = make_test_row(1, "Finn", 20);
    RID rid;

    assert(record_insert(path, &row, &rid) == DB_OK);

    assert(rid.page_id == 0);
    assert(rid.slot_id == 0);

    row_free(&row);
    cleanup_file(path);
}

static void test_record_insert_multiple_rows(void) {
    const char *path = "test_record_insert_multiple.db";
    cleanup_file(path);

    Row first = make_test_row(1, "Finn", 20);
    Row second = make_test_row(2, "Alex", 21);

    RID first_rid;
    RID second_rid;

    assert(record_insert(path, &first, &first_rid) == DB_OK);
    assert(record_insert(path, &second, &second_rid) == DB_OK);

    assert(first_rid.page_id == 0);
    assert(first_rid.slot_id == 0);

    assert(second_rid.page_id == 0);
    assert(second_rid.slot_id == 1);

    row_free(&first);
    row_free(&second);
    cleanup_file(path);
}

static void test_record_insert_rejects_null_inputs(void) {
    const char *path = "test_record_insert_null.db";
    cleanup_file(path);

    Row row = make_test_row(1, "Finn", 20);
    RID rid;

    assert(record_insert(NULL, &row, &rid) == DB_ERROR);
    assert(record_insert(path, NULL, &rid) == DB_ERROR);
    assert(record_insert(path, &row, NULL) == DB_ERROR);

    row_free(&row);
    cleanup_file(path);
}

static void test_record_get_existing_row(void) {
    const char *path = "test_record_get_existing.db";
    cleanup_file(path);

    Row original = make_test_row(1, "Finn", 20);
    RID rid;

    assert(record_insert(path, &original, &rid) == DB_OK);

    Row copy;

    assert(record_get(path, rid, &copy) == DB_OK);
    assert_test_row_matches(&copy, 1, "Finn", 20);

    row_free(&original);
    row_free(&copy);
    cleanup_file(path);
}

static void test_record_get_second_row(void) {
    const char *path = "test_record_get_second.db";
    cleanup_file(path);

    Row first = make_test_row(1, "Finn", 20);
    Row second = make_test_row(2, "Alex", 21);

    RID first_rid;
    RID second_rid;

    assert(record_insert(path, &first, &first_rid) == DB_OK);
    assert(record_insert(path, &second, &second_rid) == DB_OK);

    Row copy;

    assert(record_get(path, second_rid, &copy) == DB_OK);
    assert_test_row_matches(&copy, 2, "Alex", 21);

    row_free(&first);
    row_free(&second);
    row_free(&copy);
    cleanup_file(path);
}

static void test_record_get_rejects_null_inputs(void) {
    const char *path = "test_record_get_null.db";
    cleanup_file(path);

    RID rid = {0, 0};
    Row row;

    assert(record_get(NULL, rid, &row) == DB_ERROR);
    assert(record_get(path, rid, NULL) == DB_ERROR);

    cleanup_file(path);
}

static void test_record_get_missing_page(void) {
    const char *path = "test_record_get_missing_page.db";
    cleanup_file(path);

    RID rid = {0, 0};
    Row row;

    assert(record_get(path, rid, &row) == DB_NOT_FOUND);

    cleanup_file(path);
}

static void test_record_get_missing_slot(void) {
    const char *path = "test_record_get_missing_slot.db";
    cleanup_file(path);

    Row original = make_test_row(1, "Finn", 20);
    RID rid;

    assert(record_insert(path, &original, &rid) == DB_OK);

    RID missing_rid = {rid.page_id, 99};
    Row copy;

    assert(record_get(path, missing_rid, &copy) == DB_NOT_FOUND);

    row_free(&original);
    cleanup_file(path);
}

static void test_record_delete_existing_row(void) {
    const char *path = "test_record_delete_existing.db";
    cleanup_file(path);

    Row row = make_test_row(1, "Finn", 20);
    RID rid;

    assert(record_insert(path, &row, &rid) == DB_OK);
    assert(record_delete(path, rid) == DB_OK);

    Row copy;

    assert(record_get(path, rid, &copy) == DB_NOT_FOUND);

    row_free(&row);
    cleanup_file(path);
}

static void test_record_delete_rejects_null_file(void) {
    RID rid = {0, 0};

    assert(record_delete(NULL, rid) == DB_ERROR);
}

static void test_record_delete_missing_page(void) {
    const char *path = "test_record_delete_missing_page.db";
    cleanup_file(path);

    RID rid = {0, 0};

    assert(record_delete(path, rid) == DB_NOT_FOUND);

    cleanup_file(path);
}

static void test_record_delete_missing_slot(void) {
    const char *path = "test_record_delete_missing_slot.db";
    cleanup_file(path);

    Row row = make_test_row(1, "Finn", 20);
    RID rid;

    assert(record_insert(path, &row, &rid) == DB_OK);

    RID missing_rid = {rid.page_id, 99};

    assert(record_delete(path, missing_rid) == DB_NOT_FOUND);

    row_free(&row);
    cleanup_file(path);
}

static void test_record_delete_already_deleted_row(void) {
    const char *path = "test_record_delete_twice.db";
    cleanup_file(path);

    Row row = make_test_row(1, "Finn", 20);
    RID rid;

    assert(record_insert(path, &row, &rid) == DB_OK);

    assert(record_delete(path, rid) == DB_OK);
    assert(record_delete(path, rid) == DB_NOT_FOUND);

    row_free(&row);
    cleanup_file(path);
}

static void test_record_insert_reuses_deleted_slot(void) {
    const char *path = "test_record_reuse_deleted_slot.db";
    cleanup_file(path);

    Row first = make_test_row(1, "Finn", 20);
    Row second = make_test_row(2, "Alex", 21);

    RID first_rid;
    RID second_rid;

    assert(record_insert(path, &first, &first_rid) == DB_OK);
    assert(record_delete(path, first_rid) == DB_OK);
    assert(record_insert(path, &second, &second_rid) == DB_OK);

    assert(second_rid.page_id == first_rid.page_id);
    assert(second_rid.slot_id == first_rid.slot_id);

    row_free(&first);
    row_free(&second);
    cleanup_file(path);
}

static void test_record_scan_empty_file(void) {
    const char *path = "test_record_scan_empty.db";
    cleanup_file(path);

    ScanContext context;

    context.count = 0;
    context.last_rid.page_id = 0;
    context.last_rid.slot_id = 0;

    assert(record_scan(path, count_scan_callback, &context) == DB_OK);
    assert(context.count == 0);

    cleanup_file(path);
}

static void test_record_scan_counts_active_rows(void) {
    const char *path = "test_record_scan_counts.db";
    cleanup_file(path);

    Row first = make_test_row(1, "Finn", 20);
    Row second = make_test_row(2, "Alex", 21);
    Row third = make_test_row(3, "Sam", 22);

    RID first_rid;
    RID second_rid;
    RID third_rid;

    assert(record_insert(path, &first, &first_rid) == DB_OK);
    assert(record_insert(path, &second, &second_rid) == DB_OK);
    assert(record_insert(path, &third, &third_rid) == DB_OK);

    ScanContext context;

    context.count = 0;
    context.last_rid.page_id = 0;
    context.last_rid.slot_id = 0;

    assert(record_scan(path, count_scan_callback, &context) == DB_OK);

    assert(context.count == 3);

    row_free(&first);
    row_free(&second);
    row_free(&third);
    cleanup_file(path);
}

static void test_record_scan_skips_deleted_rows(void) {
    const char *path = "test_record_scan_skips_deleted.db";
    cleanup_file(path);

    Row first = make_test_row(1, "Finn", 20);
    Row second = make_test_row(2, "Alex", 21);

    RID first_rid;
    RID second_rid;

    assert(record_insert(path, &first, &first_rid) == DB_OK);
    assert(record_insert(path, &second, &second_rid) == DB_OK);

    assert(record_delete(path, first_rid) == DB_OK);

    ScanContext context;

    context.count = 0;
    context.last_rid.page_id = 0;
    context.last_rid.slot_id = 0;

    assert(record_scan(path, count_scan_callback, &context) == DB_OK);

    assert(context.count == 1);
    assert(context.last_rid.page_id == second_rid.page_id);
    assert(context.last_rid.slot_id == second_rid.slot_id);

    row_free(&first);
    row_free(&second);
    cleanup_file(path);
}

static void test_record_scan_rejects_null_inputs(void) {
    const char *path = "test_record_scan_null.db";
    cleanup_file(path);

    ScanContext context;

    assert(record_scan(NULL, count_scan_callback, &context) == DB_ERROR);
    assert(record_scan(path, NULL, &context) == DB_ERROR);

    cleanup_file(path);
}

static void test_record_scan_returns_callback_error(void) {
    const char *path = "test_record_scan_callback_error.db";
    cleanup_file(path);

    Row row = make_test_row(1, "Finn", 20);
    RID rid;

    assert(record_insert(path, &row, &rid) == DB_OK);

    assert(record_scan(path, failing_scan_callback, NULL) == DB_ERROR);

    row_free(&row);
    cleanup_file(path);
}

int main(void) {
    test_record_insert_single_row();
    test_record_insert_multiple_rows();
    test_record_insert_rejects_null_inputs();
    test_record_get_existing_row();
    test_record_get_second_row();
    test_record_get_rejects_null_inputs();
    test_record_get_missing_page();
    test_record_get_missing_slot();
    test_record_delete_existing_row();
    test_record_delete_rejects_null_file();
    test_record_delete_missing_page();
    test_record_delete_missing_slot();
    test_record_delete_already_deleted_row();
    test_record_insert_reuses_deleted_slot();
    test_record_scan_empty_file();
    test_record_scan_counts_active_rows();
    test_record_scan_skips_deleted_rows();
    test_record_scan_rejects_null_inputs();
    test_record_scan_returns_callback_error();

    printf("All record tests passed.\n");

    return 0;
}
