#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "value.h"

static void test_value_int(void) {
    Value value = value_int(42);

    assert(value.type == VALUE_INT);
    assert(value.int_value == 42);
}

static void test_value_text(void) {
    Value value;

    assert(value_text(&value, "Finn") == DB_OK);

    assert(value.type == VALUE_TEXT);
    assert(strcmp(value.text_value, "Finn") == 0);

    value_free(&value);
}

static void test_value_text_empty_string(void) {
    Value value;

    assert(value_text(&value, "") == DB_OK);

    assert(value.type == VALUE_TEXT);
    assert(strcmp(value.text_value, "") == 0);

    value_free(&value);
}

static void test_value_text_rejects_null_inputs(void) {
    Value value;

    assert(value_text(NULL, "Finn") == DB_ERROR);
    assert(value_text(&value, NULL) == DB_ERROR);
}

static void test_value_free_null(void) {
    value_free(NULL);
}

static void test_value_free_text(void) {
    Value value;

    assert(value_text(&value, "Finn") == DB_OK);

    value_free(&value);

    assert(value.text_value == NULL);
}

static void test_value_free_int(void) {
    Value value = value_int(42);

    value_free(&value);

    assert(value.type == VALUE_INT);
    assert(value.int_value == 42);
}

static void test_value_print_int(void) {
    Value value = value_int(42);

    char buffer[64];

    FILE *out = fmemopen(buffer, sizeof(buffer), "w");
    assert(out != NULL);

    value_print(&value, out);
    fclose(out);

    assert(strcmp(buffer, "42") == 0);
}

static void test_value_print_text(void) {
    Value value;

    assert(value_text(&value, "Finn") == DB_OK);

    char buffer[64];

    FILE *out = fmemopen(buffer, sizeof(buffer), "w");
    assert(out != NULL);

    value_print(&value, out);
    fclose(out);

    assert(strcmp(buffer, "\"Finn\"") == 0);

    value_free(&value);
}

static void test_value_print_null_inputs(void) {
    Value value = value_int(42);

    value_print(NULL, stdout);
    value_print(&value, NULL);
}

static void test_value_compare_int_less_than(void) {
    Value left = value_int(10);
    Value right = value_int(20);
    int result = 0;

    assert(value_compare(&left, &right, &result) == DB_OK);
    assert(result == -1);
}

static void test_value_compare_int_greater_than(void) {
    Value left = value_int(20);
    Value right = value_int(10);
    int result = 0;

    assert(value_compare(&left, &right, &result) == DB_OK);
    assert(result == 1);
}

static void test_value_compare_int_equal(void) {
    Value left = value_int(20);
    Value right = value_int(20);
    int result = 0;

    assert(value_compare(&left, &right, &result) == DB_OK);
    assert(result == 0);
}

static void test_value_compare_text_less_than(void) {
    Value left;
    Value right;
    int result = 0;

    assert(value_text(&left, "Apple") == DB_OK);
    assert(value_text(&right, "Banana") == DB_OK);

    assert(value_compare(&left, &right, &result) == DB_OK);
    assert(result == -1);

    value_free(&left);
    value_free(&right);
}

static void test_value_compare_text_greater_than(void) {
    Value left;
    Value right;
    int result = 0;

    assert(value_text(&left, "Banana") == DB_OK);
    assert(value_text(&right, "Apple") == DB_OK);

    assert(value_compare(&left, &right, &result) == DB_OK);
    assert(result == 1);

    value_free(&left);
    value_free(&right);
}

static void test_value_compare_text_equal(void) {
    Value left;
    Value right;
    int result = 0;

    assert(value_text(&left, "Finn") == DB_OK);
    assert(value_text(&right, "Finn") == DB_OK);

    assert(value_compare(&left, &right, &result) == DB_OK);
    assert(result == 0);

    value_free(&left);
    value_free(&right);
}

static void test_value_compare_rejects_null_inputs(void) {
    Value value = value_int(42);
    int result = 0;

    assert(value_compare(NULL, &value, &result) == DB_ERROR);
    assert(value_compare(&value, NULL, &result) == DB_ERROR);
    assert(value_compare(&value, &value, NULL) == DB_ERROR);
}

static void test_value_compare_rejects_different_types(void) {
    Value left = value_int(42);
    Value right;
    int result = 0;

    assert(value_text(&right, "Finn") == DB_OK);

    assert(value_compare(&left, &right, &result) == DB_TYPE_ERROR);

    value_free(&right);
}

int main(void) {
    test_value_int();
    test_value_text();
    test_value_text_empty_string();
    test_value_text_rejects_null_inputs();
    test_value_free_null();
    test_value_free_text();
    test_value_free_int();
    test_value_print_int();
    test_value_print_text();
    test_value_print_null_inputs();
    test_value_compare_int_less_than();
    test_value_compare_int_greater_than();
    test_value_compare_int_equal();
    test_value_compare_text_less_than();
    test_value_compare_text_greater_than();
    test_value_compare_text_equal();
    test_value_compare_rejects_null_inputs();
    test_value_compare_rejects_different_types();

    printf("All value tests passed.\n");

    return 0;
}
