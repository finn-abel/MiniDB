#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "sql/ast.h"
#include "value.h"

static void test_ast_statement_init(void) {
    Statement statement;

    assert(ast_statement_init(&statement, STATEMENT_SELECT) == DB_OK);

    assert(statement.type == STATEMENT_SELECT);
    assert(statement.select.table_name[0] == '\0');
    assert(statement.select.selected_column_count == 0);
    assert(statement.select.has_where == false);
}

static void test_ast_statement_init_rejects_null(void) {
    assert(ast_statement_init(NULL, STATEMENT_SELECT) == DB_ERROR);
}

static void test_ast_create_table_statement(void) {
    CreateTableStatement statement;

    assert(ast_create_table_init(&statement, "users") == DB_OK);
    assert(ast_create_table_add_column(&statement, "id", VALUE_INT) == DB_OK);
    assert(ast_create_table_add_column(&statement, "name", VALUE_TEXT) == DB_OK);

    assert(strcmp(statement.table_name, "users") == 0);
    assert(statement.column_count == 2);
    assert(strcmp(statement.columns[0].name, "id") == 0);
    assert(statement.columns[0].type == VALUE_INT);
    assert(statement.columns[0].not_null == false);
    assert(statement.columns[0].primary_key == false);
    assert(strcmp(statement.columns[1].name, "name") == 0);
    assert(statement.columns[1].type == VALUE_TEXT);
}

static void test_ast_create_table_rejects_null_inputs(void) {
    CreateTableStatement statement;

    assert(ast_create_table_init(NULL, "users") == DB_ERROR);
    assert(ast_create_table_init(&statement, NULL) == DB_ERROR);

    assert(ast_create_table_init(&statement, "users") == DB_OK);

    assert(ast_create_table_add_column(NULL, "id", VALUE_INT) == DB_ERROR);
    assert(ast_create_table_add_column(&statement, NULL, VALUE_INT) == DB_ERROR);
}

static void test_ast_create_table_rejects_invalid_type(void) {
    CreateTableStatement statement;

    assert(ast_create_table_init(&statement, "users") == DB_OK);
    assert(ast_create_table_add_column(&statement, "bad", (ValueType)99) == DB_TYPE_ERROR);
}

static void test_ast_create_table_rejects_when_full(void) {
    CreateTableStatement statement;

    assert(ast_create_table_init(&statement, "users") == DB_OK);

    for (uint16_t i = 0; i < MAX_COLUMNS; i++) {
        char name[32];

        snprintf(name, sizeof(name), "col_%u", i);

        assert(ast_create_table_add_column(&statement, name, VALUE_INT) == DB_OK);
    }

    assert(ast_create_table_add_column(&statement, "extra", VALUE_INT) == DB_FULL);
}

static void test_ast_insert_statement(void) {
    InsertStatement statement;
    Value id = value_int(1);
    Value name;

    assert(value_text(&name, "Finn") == DB_OK);

    assert(ast_insert_init(&statement, "users") == DB_OK);
    assert(ast_insert_add_value(&statement, &id) == DB_OK);
    assert(ast_insert_add_value(&statement, &name) == DB_OK);

    assert(strcmp(statement.table_name, "users") == 0);
    assert(statement.value_count == 2);
    assert(statement.values[0].type == VALUE_INT);
    assert(statement.values[0].int_value == 1);
    assert(statement.values[1].type == VALUE_TEXT);
    assert(strcmp(statement.values[1].text_value, "Finn") == 0);
    assert(statement.values[1].text_value != name.text_value);

    value_free(&name);

    Statement wrapper;

    assert(ast_statement_init(&wrapper, STATEMENT_INSERT) == DB_OK);
    wrapper.insert = statement;
    ast_statement_free(&wrapper);
}

static void test_ast_insert_rejects_null_inputs(void) {
    InsertStatement statement;
    Value value = value_int(1);

    assert(ast_insert_init(NULL, "users") == DB_ERROR);
    assert(ast_insert_init(&statement, NULL) == DB_ERROR);

    assert(ast_insert_init(&statement, "users") == DB_OK);

    assert(ast_insert_add_value(NULL, &value) == DB_ERROR);
    assert(ast_insert_add_value(&statement, NULL) == DB_ERROR);
}

static void test_ast_insert_rejects_when_full(void) {
    InsertStatement statement;
    Value value = value_int(1);

    assert(ast_insert_init(&statement, "users") == DB_OK);

    for (uint16_t i = 0; i < MAX_COLUMNS; i++) {
        assert(ast_insert_add_value(&statement, &value) == DB_OK);
    }

    assert(ast_insert_add_value(&statement, &value) == DB_FULL);

    Statement wrapper;

    assert(ast_statement_init(&wrapper, STATEMENT_INSERT) == DB_OK);
    wrapper.insert = statement;
    ast_statement_free(&wrapper);
}

static void test_ast_where_condition(void) {
    WhereCondition condition;
    Value value = value_int(18);

    assert(ast_where_init(&condition, "age", SQL_OPERATOR_GREATER_EQUAL, &value) == DB_OK);

    assert(strcmp(condition.column_name, "age") == 0);
    assert(condition.operator_type == SQL_OPERATOR_GREATER_EQUAL);
    assert(condition.value.type == VALUE_INT);
    assert(condition.value.int_value == 18);

    ast_where_free(&condition);
}

static void test_ast_where_condition_copies_text_value(void) {
    WhereCondition condition;
    Value value;

    assert(value_text(&value, "Finn") == DB_OK);

    assert(ast_where_init(&condition, "name", SQL_OPERATOR_EQUAL, &value) == DB_OK);

    assert(condition.value.type == VALUE_TEXT);
    assert(strcmp(condition.value.text_value, "Finn") == 0);
    assert(condition.value.text_value != value.text_value);

    value_free(&value);
    ast_where_free(&condition);
}

static void test_ast_where_rejects_null_inputs(void) {
    WhereCondition condition;
    Value value = value_int(1);

    assert(ast_where_init(NULL, "id", SQL_OPERATOR_EQUAL, &value) == DB_ERROR);
    assert(ast_where_init(&condition, NULL, SQL_OPERATOR_EQUAL, &value) == DB_ERROR);
    assert(ast_where_init(&condition, "id", SQL_OPERATOR_EQUAL, NULL) == DB_ERROR);
}

static void test_ast_select_statement(void) {
    SelectStatement statement;

    assert(ast_select_init(&statement, "users") == DB_OK);
    assert(ast_select_add_column(&statement, "id") == DB_OK);
    assert(ast_select_add_column(&statement, "name") == DB_OK);

    assert(strcmp(statement.table_name, "users") == 0);
    assert(statement.selected_column_count == 2);
    assert(strcmp(statement.selected_columns[0], "id") == 0);
    assert(strcmp(statement.selected_columns[1], "name") == 0);
    assert(statement.has_where == false);
}

static void test_ast_select_star(void) {
    SelectStatement statement;

    assert(ast_select_init(&statement, "users") == DB_OK);

    assert(statement.selected_column_count == 0);
}

static void test_ast_select_with_where(void) {
    SelectStatement statement;
    WhereCondition condition;
    Value value = value_int(18);

    assert(ast_select_init(&statement, "users") == DB_OK);
    assert(ast_where_init(&condition, "age", SQL_OPERATOR_GREATER, &value) == DB_OK);
    assert(ast_select_set_where(&statement, &condition) == DB_OK);

    assert(statement.has_where == true);
    assert(strcmp(statement.where.column_name, "age") == 0);
    assert(statement.where.operator_type == SQL_OPERATOR_GREATER);
    assert(statement.where.value.int_value == 18);

    ast_where_free(&condition);

    Statement wrapper;

    assert(ast_statement_init(&wrapper, STATEMENT_SELECT) == DB_OK);
    wrapper.select = statement;
    ast_statement_free(&wrapper);
}

static void test_ast_select_rejects_null_inputs(void) {
    SelectStatement statement;
    WhereCondition condition;
    Value value = value_int(1);

    assert(ast_select_init(NULL, "users") == DB_ERROR);
    assert(ast_select_init(&statement, NULL) == DB_ERROR);

    assert(ast_select_init(&statement, "users") == DB_OK);
    assert(ast_where_init(&condition, "id", SQL_OPERATOR_EQUAL, &value) == DB_OK);

    assert(ast_select_add_column(NULL, "id") == DB_ERROR);
    assert(ast_select_add_column(&statement, NULL) == DB_ERROR);
    assert(ast_select_set_where(NULL, &condition) == DB_ERROR);
    assert(ast_select_set_where(&statement, NULL) == DB_ERROR);

    ast_where_free(&condition);
}

static void test_ast_delete_statement(void) {
    DeleteStatement statement;

    assert(ast_delete_init(&statement, "users") == DB_OK);

    assert(strcmp(statement.table_name, "users") == 0);
    assert(statement.has_where == false);
}

static void test_ast_delete_with_where(void) {
    DeleteStatement statement;
    WhereCondition condition;
    Value value = value_int(1);

    assert(ast_delete_init(&statement, "users") == DB_OK);
    assert(ast_where_init(&condition, "id", SQL_OPERATOR_EQUAL, &value) == DB_OK);
    assert(ast_delete_set_where(&statement, &condition) == DB_OK);

    assert(statement.has_where == true);
    assert(strcmp(statement.where.column_name, "id") == 0);
    assert(statement.where.operator_type == SQL_OPERATOR_EQUAL);
    assert(statement.where.value.int_value == 1);

    ast_where_free(&condition);

    Statement wrapper;

    assert(ast_statement_init(&wrapper, STATEMENT_DELETE) == DB_OK);
    wrapper.delete_statement = statement;
    ast_statement_free(&wrapper);
}

static void test_ast_delete_rejects_null_inputs(void) {
    DeleteStatement statement;
    WhereCondition condition;
    Value value = value_int(1);

    assert(ast_delete_init(NULL, "users") == DB_ERROR);
    assert(ast_delete_init(&statement, NULL) == DB_ERROR);

    assert(ast_delete_init(&statement, "users") == DB_OK);
    assert(ast_where_init(&condition, "id", SQL_OPERATOR_EQUAL, &value) == DB_OK);

    assert(ast_delete_set_where(NULL, &condition) == DB_ERROR);
    assert(ast_delete_set_where(&statement, NULL) == DB_ERROR);

    ast_where_free(&condition);
}

static void test_ast_meta_command_statement(void) {
    MetaCommandStatement statement;

    assert(ast_meta_command_init(&statement, ".help") == DB_OK);

    assert(strcmp(statement.command, ".help") == 0);
}

static void test_ast_meta_command_rejects_null_inputs(void) {
    MetaCommandStatement statement;

    assert(ast_meta_command_init(NULL, ".help") == DB_ERROR);
    assert(ast_meta_command_init(&statement, NULL) == DB_ERROR);
}

int main(void) {
    test_ast_statement_init();
    test_ast_statement_init_rejects_null();
    test_ast_create_table_statement();
    test_ast_create_table_rejects_null_inputs();
    test_ast_create_table_rejects_invalid_type();
    test_ast_create_table_rejects_when_full();
    test_ast_insert_statement();
    test_ast_insert_rejects_null_inputs();
    test_ast_insert_rejects_when_full();
    test_ast_where_condition();
    test_ast_where_condition_copies_text_value();
    test_ast_where_rejects_null_inputs();
    test_ast_select_statement();
    test_ast_select_star();
    test_ast_select_with_where();
    test_ast_select_rejects_null_inputs();
    test_ast_delete_statement();
    test_ast_delete_with_where();
    test_ast_delete_rejects_null_inputs();
    test_ast_meta_command_statement();
    test_ast_meta_command_rejects_null_inputs();

    printf("All AST tests passed.\n");

    return 0;
}
