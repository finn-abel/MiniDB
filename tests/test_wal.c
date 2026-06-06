#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "record.h"
#include "rid.h"
#include "row.h"
#include "transaction/wal.h"
#include "value.h"

static void cleanup_file(const char *path) {
    remove(path);
}

static void cleanup_table_sidecar(const char *table_file) {
    char fsm_path[MAX_DB_PATH + 5];
    int written = snprintf(fsm_path, sizeof(fsm_path), "%s.fsm", table_file);

    assert(written > 0 && (size_t)written < sizeof(fsm_path));

    remove(fsm_path);
}

static void cleanup_table(const char *table_file) {
    cleanup_file(table_file);
    cleanup_table_sidecar(table_file);
}

static void make_test_row(Row *row, int32_t id, const char *name) {
    assert(row_create(row, 2) == DB_OK);
    row->values[0] = value_int(id);
    assert(value_text(&row->values[1], name) == DB_OK);
}

static void serialize_test_row(
    Row *row,
    uint8_t **out_bytes,
    uint32_t *out_len
) {
    assert(row_serialize(row, out_bytes, out_len) == DB_OK);
    assert(*out_bytes != NULL);
    assert(*out_len > 0);
}

static void assert_row_matches(const Row *row, int32_t id, const char *name) {
    assert(row != NULL);
    assert(row->value_count == 2);
    assert(row->values[0].type == VALUE_INT);
    assert(row->values[0].int_value == id);
    assert(row->values[1].type == VALUE_TEXT);
    assert(row->values[1].text_value != NULL);
    assert(strcmp(row->values[1].text_value, name) == 0);
}

static void test_wal_open_close(void) {
    const char *wal_path = "test_wal_open.log";
    WAL wal;

    cleanup_file(wal_path);

    assert(wal_open(&wal, wal_path) == DB_OK);
    assert(wal.is_open == true);
    assert(wal_close(&wal) == DB_OK);
    assert(wal.is_open == false);

    cleanup_file(wal_path);
}

static void test_wal_recover_replays_committed_insert(void) {
    const char *wal_path = "test_wal_insert.log";
    const char *table_file = "test_wal_insert.tbl";
    WAL wal;
    Row row;
    Row recovered;
    RID rid = {0, 0};
    uint8_t *row_bytes = NULL;
    uint32_t row_len = 0;

    cleanup_file(wal_path);
    cleanup_table(table_file);

    make_test_row(&row, 1, "Finn");
    serialize_test_row(&row, &row_bytes, &row_len);

    assert(wal_open(&wal, wal_path) == DB_OK);
    assert(wal_log_begin(&wal, 1) == DB_OK);
    assert(wal_log_insert(&wal, 1, table_file, rid, row_bytes, row_len) == DB_OK);
    assert(wal_log_commit(&wal, 1) == DB_OK);
    assert(wal_recover(&wal) == DB_OK);
    assert(wal_close(&wal) == DB_OK);

    assert(record_get(table_file, rid, &recovered) == DB_OK);
    assert_row_matches(&recovered, 1, "Finn");

    row_free(&recovered);
    row_free(&row);
    free(row_bytes);
    cleanup_file(wal_path);
    cleanup_table(table_file);
}

static void test_wal_recover_ignores_uncommitted_insert(void) {
    const char *wal_path = "test_wal_uncommitted.log";
    const char *table_file = "test_wal_uncommitted.tbl";
    WAL wal;
    Row row;
    Row recovered;
    RID rid = {0, 0};
    uint8_t *row_bytes = NULL;
    uint32_t row_len = 0;

    cleanup_file(wal_path);
    cleanup_table(table_file);

    make_test_row(&row, 2, "Ada");
    serialize_test_row(&row, &row_bytes, &row_len);

    assert(wal_open(&wal, wal_path) == DB_OK);
    assert(wal_log_begin(&wal, 2) == DB_OK);
    assert(wal_log_insert(&wal, 2, table_file, rid, row_bytes, row_len) == DB_OK);
    assert(wal_recover(&wal) == DB_OK);
    assert(wal_close(&wal) == DB_OK);

    assert(record_get(table_file, rid, &recovered) == DB_NOT_FOUND);

    row_free(&row);
    free(row_bytes);
    cleanup_file(wal_path);
    cleanup_table(table_file);
}

static void test_wal_recover_replays_committed_delete(void) {
    const char *wal_path = "test_wal_delete.log";
    const char *table_file = "test_wal_delete.tbl";
    WAL wal;
    Row row;
    Row recovered;
    RID rid = {0, 0};
    uint8_t *row_bytes = NULL;
    uint32_t row_len = 0;

    cleanup_file(wal_path);
    cleanup_table(table_file);

    make_test_row(&row, 3, "Grace");
    serialize_test_row(&row, &row_bytes, &row_len);

    assert(wal_open(&wal, wal_path) == DB_OK);
    assert(wal_log_begin(&wal, 3) == DB_OK);
    assert(wal_log_insert(&wal, 3, table_file, rid, row_bytes, row_len) == DB_OK);
    assert(wal_log_commit(&wal, 3) == DB_OK);
    assert(wal_log_begin(&wal, 4) == DB_OK);
    assert(wal_log_delete(&wal, 4, table_file, rid, row_bytes, row_len) == DB_OK);
    assert(wal_log_commit(&wal, 4) == DB_OK);
    assert(wal_recover(&wal) == DB_OK);
    assert(wal_close(&wal) == DB_OK);

    assert(record_get(table_file, rid, &recovered) == DB_NOT_FOUND);

    row_free(&row);
    free(row_bytes);
    cleanup_file(wal_path);
    cleanup_table(table_file);
}

static void test_wal_recover_ignores_uncommitted_delete(void) {
    const char *wal_path = "test_wal_uncommitted_delete.log";
    const char *table_file = "test_wal_uncommitted_delete.tbl";
    WAL wal;
    Row row;
    Row recovered;
    RID rid = {0, 0};
    uint8_t *row_bytes = NULL;
    uint32_t row_len = 0;

    cleanup_file(wal_path);
    cleanup_table(table_file);

    make_test_row(&row, 4, "Linus");
    serialize_test_row(&row, &row_bytes, &row_len);

    assert(wal_open(&wal, wal_path) == DB_OK);
    assert(wal_log_begin(&wal, 5) == DB_OK);
    assert(wal_log_insert(&wal, 5, table_file, rid, row_bytes, row_len) == DB_OK);
    assert(wal_log_commit(&wal, 5) == DB_OK);
    assert(wal_log_begin(&wal, 6) == DB_OK);
    assert(wal_log_delete(&wal, 6, table_file, rid, row_bytes, row_len) == DB_OK);
    assert(wal_recover(&wal) == DB_OK);
    assert(wal_close(&wal) == DB_OK);

    assert(record_get(table_file, rid, &recovered) == DB_OK);
    assert_row_matches(&recovered, 4, "Linus");

    row_free(&recovered);
    row_free(&row);
    free(row_bytes);
    cleanup_file(wal_path);
    cleanup_table(table_file);
}

static void test_wal_recover_is_idempotent_for_insert(void) {
    const char *wal_path = "test_wal_idempotent.log";
    const char *table_file = "test_wal_idempotent.tbl";
    WAL wal;
    Row row;
    Row recovered;
    RID rid = {0, 0};
    uint8_t *row_bytes = NULL;
    uint32_t row_len = 0;

    cleanup_file(wal_path);
    cleanup_table(table_file);

    make_test_row(&row, 5, "Mina");
    serialize_test_row(&row, &row_bytes, &row_len);

    assert(wal_open(&wal, wal_path) == DB_OK);
    assert(wal_log_begin(&wal, 7) == DB_OK);
    assert(wal_log_insert(&wal, 7, table_file, rid, row_bytes, row_len) == DB_OK);
    assert(wal_log_commit(&wal, 7) == DB_OK);
    assert(wal_recover(&wal) == DB_OK);
    assert(wal_recover(&wal) == DB_OK);
    assert(wal_close(&wal) == DB_OK);

    assert(record_get(table_file, rid, &recovered) == DB_OK);
    assert_row_matches(&recovered, 5, "Mina");

    row_free(&recovered);
    row_free(&row);
    free(row_bytes);
    cleanup_file(wal_path);
    cleanup_table(table_file);
}

static void test_wal_rejects_invalid_inputs(void) {
    const char *wal_path = "test_wal_invalid.log";
    const char *table_file = "test_wal_invalid.tbl";
    WAL wal;
    RID rid = {0, 0};
    uint8_t row_bytes[] = {1, 2, 3};

    cleanup_file(wal_path);
    cleanup_table(table_file);

    assert(wal_open(NULL, wal_path) == DB_ERROR);
    assert(wal_open(&wal, NULL) == DB_ERROR);
    assert(wal_open(&wal, wal_path) == DB_OK);
    assert(wal_log_begin(NULL, 1) == DB_ERROR);
    assert(wal_log_begin(&wal, 0) == DB_ERROR);
    assert(wal_log_insert(&wal, 0, table_file, rid, row_bytes, sizeof(row_bytes)) == DB_ERROR);
    assert(wal_log_insert(&wal, 1, NULL, rid, row_bytes, sizeof(row_bytes)) == DB_ERROR);
    assert(wal_log_insert(&wal, 1, table_file, rid, NULL, sizeof(row_bytes)) == DB_ERROR);
    assert(wal_log_insert(&wal, 1, table_file, rid, row_bytes, 0) == DB_ERROR);
    assert(wal_log_delete(&wal, 0, table_file, rid, row_bytes, sizeof(row_bytes)) == DB_ERROR);
    assert(wal_log_commit(&wal, 0) == DB_ERROR);
    assert(wal_close(&wal) == DB_OK);
    assert(wal_recover(&wal) == DB_ERROR);
    assert(wal_close(&wal) == DB_ERROR);

    cleanup_file(wal_path);
    cleanup_table(table_file);
}

int main(void) {
    test_wal_open_close();
    test_wal_recover_replays_committed_insert();
    test_wal_recover_ignores_uncommitted_insert();
    test_wal_recover_replays_committed_delete();
    test_wal_recover_ignores_uncommitted_delete();
    test_wal_recover_is_idempotent_for_insert();
    test_wal_rejects_invalid_inputs();

    printf("All WAL tests passed.\n");

    return 0;
}
