#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "row.h"
#include "schema.h"
#include "value.h"

static void test_schema_init(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(strcmp(schema.table_name, "users") == 0);
    assert(schema.column_count == 0);
}

static void test_schema_init_rejects_null_inputs(void) {
    Schema schema;

    assert(schema_init(NULL, "users") == DB_ERROR);
    assert(schema_init(&schema, NULL) == DB_ERROR);
}

static void test_schema_init_rejects_empty_table_name(void) {
    Schema schema;

    assert(schema_init(&schema, "") == DB_ERROR);
}

static void test_schema_rejects_unsafe_identifiers(void) {
    Schema schema;

    assert(schema_init(&schema, "../users") == DB_ERROR);
    assert(schema_init(&schema, "9users") == DB_ERROR);
    assert(schema_init(&schema, "users") == DB_OK);
    assert(schema_add_column(&schema, "profile/name", VALUE_TEXT, false, false) == DB_ERROR);
}

static void test_schema_add_column_int(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);

    assert(schema.column_count == 1);
    assert(strcmp(schema.columns[0].name, "id") == 0);
    assert(schema.columns[0].type == VALUE_INT);
    assert(schema.columns[0].not_null == true);
    assert(schema.columns[0].primary_key == true);
}

static void test_schema_add_column_text(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "name", VALUE_TEXT, false, false) == DB_OK);

    assert(schema.column_count == 1);
    assert(strcmp(schema.columns[0].name, "name") == 0);
    assert(schema.columns[0].type == VALUE_TEXT);
    assert(schema.columns[0].not_null == false);
    assert(schema.columns[0].primary_key == false);
}

static void test_schema_add_multiple_columns(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(&schema, "name", VALUE_TEXT, true, false) == DB_OK);
    assert(schema_add_column(&schema, "age", VALUE_INT, false, false) == DB_OK);

    assert(schema.column_count == 3);

    assert(strcmp(schema.columns[0].name, "id") == 0);
    assert(strcmp(schema.columns[1].name, "name") == 0);
    assert(strcmp(schema.columns[2].name, "age") == 0);
}

static void test_schema_add_column_rejects_null_inputs(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(NULL, "id", VALUE_INT, true, true) == DB_ERROR);
    assert(schema_add_column(&schema, NULL, VALUE_INT, true, true) == DB_ERROR);
}

static void test_schema_add_column_rejects_empty_name(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "", VALUE_INT, true, true) == DB_ERROR);
}

static void test_schema_add_column_rejects_invalid_type(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", (ValueType)99, true, true) == DB_TYPE_ERROR);
}

static void test_schema_add_column_rejects_duplicate_name(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(&schema, "id", VALUE_INT, true, false) == DB_ERROR);
}

static void test_schema_add_column_rejects_when_full(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    for (uint16_t i = 0; i < MAX_COLUMNS; i++) {
        char name[32];

        snprintf(name, sizeof(name), "col_%u", i);

        assert(schema_add_column(&schema, name, VALUE_INT, false, false) == DB_OK);
    }

    assert(schema.column_count == MAX_COLUMNS);
    assert(schema_add_column(&schema, "extra", VALUE_INT, false, false) == DB_FULL);
}

static void test_schema_get_column_index_first_column(void) {
    Schema schema;
    uint16_t index = 99;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(&schema, "name", VALUE_TEXT, true, false) == DB_OK);

    assert(schema_get_column_index(&schema, "id", &index) == DB_OK);
    assert(index == 0);
}

static void test_schema_get_column_index_later_column(void) {
    Schema schema;
    uint16_t index = 99;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(&schema, "name", VALUE_TEXT, true, false) == DB_OK);

    assert(schema_get_column_index(&schema, "name", &index) == DB_OK);
    assert(index == 1);
}

static void test_schema_get_column_index_missing_column(void) {
    Schema schema;
    uint16_t index = 99;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);

    assert(schema_get_column_index(&schema, "name", &index) == DB_NOT_FOUND);
}

static void test_schema_get_column_index_rejects_null_inputs(void) {
    Schema schema;
    uint16_t index = 0;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_get_column_index(NULL, "id", &index) == DB_ERROR);
    assert(schema_get_column_index(&schema, NULL, &index) == DB_ERROR);
    assert(schema_get_column_index(&schema, "id", NULL) == DB_ERROR);
}

static void test_schema_get_column_type_int(void) {
    Schema schema;
    ValueType type = VALUE_TEXT;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);

    assert(schema_get_column_type(&schema, "id", &type) == DB_OK);
    assert(type == VALUE_INT);
}

static void test_schema_get_column_type_text(void) {
    Schema schema;
    ValueType type = VALUE_INT;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "name", VALUE_TEXT, true, false) == DB_OK);

    assert(schema_get_column_type(&schema, "name", &type) == DB_OK);
    assert(type == VALUE_TEXT);
}

static void test_schema_get_column_type_missing_column(void) {
    Schema schema;
    ValueType type = VALUE_INT;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);

    assert(schema_get_column_type(&schema, "name", &type) == DB_NOT_FOUND);
}

static void test_schema_get_column_type_rejects_null_inputs(void) {
    Schema schema;
    ValueType type = VALUE_INT;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_get_column_type(NULL, "id", &type) == DB_ERROR);
    assert(schema_get_column_type(&schema, NULL, &type) == DB_ERROR);
    assert(schema_get_column_type(&schema, "id", NULL) == DB_ERROR);
}

static void test_schema_validate_row_valid_row(void) {
    Schema schema;
    Row row;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(&schema, "name", VALUE_TEXT, true, false) == DB_OK);
    assert(schema_add_column(&schema, "age", VALUE_INT, false, false) == DB_OK);

    assert(row_create(&row, 3) == DB_OK);

    row.values[0] = value_int(1);
    assert(value_text(&row.values[1], "Finn") == DB_OK);
    row.values[2] = value_int(20);

    assert(schema_validate_row(&schema, &row) == DB_OK);

    row_free(&row);
}

static void test_schema_validate_row_rejects_wrong_value_count(void) {
    Schema schema;
    Row row;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(&schema, "name", VALUE_TEXT, true, false) == DB_OK);

    assert(row_create(&row, 1) == DB_OK);

    row.values[0] = value_int(1);

    assert(schema_validate_row(&schema, &row) == DB_ERROR);

    row_free(&row);
}

static void test_schema_validate_row_rejects_wrong_type(void) {
    Schema schema;
    Row row;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);

    assert(row_create(&row, 1) == DB_OK);

    assert(value_text(&row.values[0], "not an int") == DB_OK);

    assert(schema_validate_row(&schema, &row) == DB_TYPE_ERROR);

    row_free(&row);
}

static void test_schema_validate_row_rejects_null_text_when_not_null(void) {
    Schema schema;
    Row row;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "name", VALUE_TEXT, true, false) == DB_OK);

    assert(row_create(&row, 1) == DB_OK);

    row.values[0].type = VALUE_TEXT;
    row.values[0].text_value = NULL;

    assert(schema_validate_row(&schema, &row) == DB_ERROR);

    row_free(&row);
}

static void test_schema_validate_row_allows_null_text_when_nullable(void) {
    Schema schema;
    Row row;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "name", VALUE_TEXT, false, false) == DB_OK);

    assert(row_create(&row, 1) == DB_OK);

    row.values[0].type = VALUE_TEXT;
    row.values[0].text_value = NULL;

    assert(schema_validate_row(&schema, &row) == DB_OK);

    row_free(&row);
}

static void test_schema_validate_row_rejects_null_inputs(void) {
    Schema schema;
    Row row;

    assert(schema_init(&schema, "users") == DB_OK);
    assert(row_create(&row, 0) == DB_OK);

    assert(schema_validate_row(NULL, &row) == DB_ERROR);
    assert(schema_validate_row(&schema, NULL) == DB_ERROR);

    row_free(&row);
}

static void test_row_matches_condition_int(void) {
    Schema schema;
    Row row;
    WhereCondition condition;
    bool matches = false;

    assert(schema_init(&schema, "users") == DB_OK);
    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(&schema, "age", VALUE_INT, false, false) == DB_OK);

    assert(row_create(&row, 2) == DB_OK);
    row.values[0] = value_int(1);
    row.values[1] = value_int(20);

    memset(&condition, 0, sizeof(condition));
    strcpy(condition.column_name, "age");
    condition.operator_type = SQL_OPERATOR_GREATER;
    condition.value = value_int(18);

    assert(row_matches_condition(&row, &schema, &condition, &matches) == DB_OK);
    assert(matches == true);

    condition.operator_type = SQL_OPERATOR_LESS_EQUAL;

    assert(row_matches_condition(&row, &schema, &condition, &matches) == DB_OK);
    assert(matches == false);

    row_free(&row);
}

static void test_row_matches_condition_text(void) {
    Schema schema;
    Row row;
    WhereCondition condition;
    bool matches = false;

    assert(schema_init(&schema, "users") == DB_OK);
    assert(schema_add_column(&schema, "name", VALUE_TEXT, false, false) == DB_OK);

    assert(row_create(&row, 1) == DB_OK);
    assert(value_text(&row.values[0], "Finn") == DB_OK);

    memset(&condition, 0, sizeof(condition));
    strcpy(condition.column_name, "name");
    condition.operator_type = SQL_OPERATOR_EQUAL;
    assert(value_text(&condition.value, "Finn") == DB_OK);

    assert(row_matches_condition(&row, &schema, &condition, &matches) == DB_OK);
    assert(matches == true);

    condition.operator_type = SQL_OPERATOR_NOT_EQUAL;

    assert(row_matches_condition(&row, &schema, &condition, &matches) == DB_OK);
    assert(matches == false);

    value_free(&condition.value);
    row_free(&row);
}

static void test_row_matches_condition_missing_column(void) {
    Schema schema;
    Row row;
    WhereCondition condition;
    bool matches = false;

    assert(schema_init(&schema, "users") == DB_OK);
    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);

    assert(row_create(&row, 1) == DB_OK);
    row.values[0] = value_int(1);

    memset(&condition, 0, sizeof(condition));
    strcpy(condition.column_name, "age");
    condition.operator_type = SQL_OPERATOR_EQUAL;
    condition.value = value_int(20);

    assert(row_matches_condition(&row, &schema, &condition, &matches) == DB_NOT_FOUND);

    row_free(&row);
}

static void test_row_matches_condition_rejects_null_inputs(void) {
    Schema schema;
    Row row;
    WhereCondition condition;
    bool matches = false;

    assert(schema_init(&schema, "users") == DB_OK);
    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);

    assert(row_create(&row, 1) == DB_OK);
    row.values[0] = value_int(1);

    memset(&condition, 0, sizeof(condition));
    strcpy(condition.column_name, "id");
    condition.operator_type = SQL_OPERATOR_EQUAL;
    condition.value = value_int(1);

    assert(row_matches_condition(NULL, &schema, &condition, &matches) == DB_ERROR);
    assert(row_matches_condition(&row, NULL, &condition, &matches) == DB_ERROR);
    assert(row_matches_condition(&row, &schema, NULL, &matches) == DB_ERROR);
    assert(row_matches_condition(&row, &schema, &condition, NULL) == DB_ERROR);

    row_free(&row);
}

static void test_schema_print_empty_schema(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    char buffer[128];

    FILE *out = fmemopen(buffer, sizeof(buffer), "w");
    assert(out != NULL);

    schema_print(&schema, out);
    fclose(out);

    assert(strcmp(buffer, "users ()") == 0);
}

static void test_schema_print_with_columns(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(&schema, "name", VALUE_TEXT, true, false) == DB_OK);
    assert(schema_add_column(&schema, "age", VALUE_INT, false, false) == DB_OK);

    char buffer[256];

    FILE *out = fmemopen(buffer, sizeof(buffer), "w");
    assert(out != NULL);

    schema_print(&schema, out);
    fclose(out);

    assert(strcmp(
        buffer,
        "users (id INT PRIMARY KEY NOT NULL, name TEXT NOT NULL, age INT)"
    ) == 0);
}

static void test_schema_print_null_inputs(void) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);

    schema_print(NULL, stdout);
    schema_print(&schema, NULL);
}

int main(void) {
    test_schema_init();
    test_schema_init_rejects_null_inputs();
    test_schema_init_rejects_empty_table_name();
    test_schema_rejects_unsafe_identifiers();
    test_schema_add_column_int();
    test_schema_add_column_text();
    test_schema_add_multiple_columns();
    test_schema_add_column_rejects_null_inputs();
    test_schema_add_column_rejects_empty_name();
    test_schema_add_column_rejects_invalid_type();
    test_schema_add_column_rejects_duplicate_name();
    test_schema_add_column_rejects_when_full();
    test_schema_get_column_index_first_column();
    test_schema_get_column_index_later_column();
    test_schema_get_column_index_missing_column();
    test_schema_get_column_index_rejects_null_inputs();
    test_schema_get_column_type_int();
    test_schema_get_column_type_text();
    test_schema_get_column_type_missing_column();
    test_schema_get_column_type_rejects_null_inputs();
    test_schema_validate_row_valid_row();
    test_schema_validate_row_rejects_wrong_value_count();
    test_schema_validate_row_rejects_wrong_type();
    test_schema_validate_row_rejects_null_text_when_not_null();
    test_schema_validate_row_allows_null_text_when_nullable();
    test_schema_validate_row_rejects_null_inputs();
    test_row_matches_condition_int();
    test_row_matches_condition_text();
    test_row_matches_condition_missing_column();
    test_row_matches_condition_rejects_null_inputs();
    test_schema_print_empty_schema();
    test_schema_print_with_columns();
    test_schema_print_null_inputs();

    printf("All schema tests passed.\n");

    return 0;
}
