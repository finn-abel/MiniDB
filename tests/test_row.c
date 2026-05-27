#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "row.h"
#include "value.h"

static void test_row_create_with_values(void) {
    Row row;

    assert(row_create(&row, 3) == DB_OK);

    assert(row.value_count == 3);
    assert(row.values != NULL);

    row_free(&row);
}

static void test_row_create_empty(void) {
    Row row;

    assert(row_create(&row, 0) == DB_OK);

    assert(row.value_count == 0);
    assert(row.values == NULL);

    row_free(&row);
}

static void test_row_create_null(void) {
    assert(row_create(NULL, 3) == DB_ERROR);
}

static void test_row_free_null(void) {
    row_free(NULL);
}

static void test_row_free_resets_row(void) {
    Row row;

    assert(row_create(&row, 2) == DB_OK);

    row.values[0] = value_int(10);
    assert(value_text(&row.values[1], "Finn") == DB_OK);

    row_free(&row);

    assert(row.value_count == 0);
    assert(row.values == NULL);
}

static void test_row_get_value_valid_index(void) {
    Row row;

    assert(row_create(&row, 2) == DB_OK);

    row.values[0] = value_int(10);
    row.values[1] = value_int(20);

    Value *value = row_get_value(&row, 1);

    assert(value != NULL);
    assert(value->type == VALUE_INT);
    assert(value->int_value == 20);

    row_free(&row);
}

static void test_row_get_value_invalid_index(void) {
    Row row;

    assert(row_create(&row, 1) == DB_OK);

    row.values[0] = value_int(10);

    assert(row_get_value(&row, 1) == NULL);
    assert(row_get_value(NULL, 0) == NULL);

    row_free(&row);
}

static void test_row_get_value_const_valid_index(void) {
    Row row;

    assert(row_create(&row, 2) == DB_OK);

    row.values[0] = value_int(10);
    row.values[1] = value_int(20);

    const Value *value = row_get_value_const(&row, 0);

    assert(value != NULL);
    assert(value->type == VALUE_INT);
    assert(value->int_value == 10);

    row_free(&row);
}

static void test_row_get_value_const_invalid_index(void) {
    Row row;

    assert(row_create(&row, 1) == DB_OK);

    row.values[0] = value_int(10);

    assert(row_get_value_const(&row, 1) == NULL);
    assert(row_get_value_const(NULL, 0) == NULL);

    row_free(&row);
}

static void test_row_print_mixed_row(void) {
    Row row;

    assert(row_create(&row, 3) == DB_OK);

    row.values[0] = value_int(1);
    assert(value_text(&row.values[1], "Finn") == DB_OK);
    row.values[2] = value_int(20);

    char buffer[128];

    FILE *out = fmemopen(buffer, sizeof(buffer), "w");
    assert(out != NULL);

    row_print(&row, out);
    fclose(out);

    assert(strcmp(buffer, "[1, \"Finn\", 20]") == 0);

    row_free(&row);
}

static void test_row_print_empty_row(void) {
    Row row;

    assert(row_create(&row, 0) == DB_OK);

    char buffer[64];

    FILE *out = fmemopen(buffer, sizeof(buffer), "w");
    assert(out != NULL);

    row_print(&row, out);
    fclose(out);

    assert(strcmp(buffer, "[]") == 0);

    row_free(&row);
}

static void test_row_print_null_inputs(void) {
    Row row;

    assert(row_create(&row, 1) == DB_OK);

    row.values[0] = value_int(10);

    row_print(NULL, stdout);
    row_print(&row, NULL);

    row_free(&row);
}

int main(void) {
    test_row_create_with_values();
    test_row_create_empty();
    test_row_create_null();
    test_row_free_null();
    test_row_free_resets_row();
    test_row_get_value_valid_index();
    test_row_get_value_invalid_index();
    test_row_get_value_const_valid_index();
    test_row_get_value_const_invalid_index();
    test_row_print_mixed_row();
    test_row_print_empty_row();
    test_row_print_null_inputs();

    printf("All row tests passed.\n");

    return 0;
}
