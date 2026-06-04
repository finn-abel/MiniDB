#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "sql/ast.h"
#include "sql/parser.h"
#include "value.h"

static void test_parser_create_table(void) {
    Statement statement;

    assert(parser_parse("CREATE TABLE users (id INT, name TEXT, age INT);", &statement) == DB_OK);

    assert(statement.type == STATEMENT_CREATE_TABLE);
    assert(strcmp(statement.create_table.table_name, "users") == 0);
    assert(statement.create_table.column_count == 3);
    assert(strcmp(statement.create_table.columns[0].name, "id") == 0);
    assert(statement.create_table.columns[0].type == VALUE_INT);
    assert(strcmp(statement.create_table.columns[1].name, "name") == 0);
    assert(statement.create_table.columns[1].type == VALUE_TEXT);
    assert(strcmp(statement.create_table.columns[2].name, "age") == 0);
    assert(statement.create_table.columns[2].type == VALUE_INT);

    ast_statement_free(&statement);
}

static void test_parser_insert(void) {
    Statement statement;

    assert(parser_parse("INSERT INTO users VALUES (1, \"Finn\", 20);", &statement) == DB_OK);

    assert(statement.type == STATEMENT_INSERT);
    assert(strcmp(statement.insert.table_name, "users") == 0);
    assert(statement.insert.value_count == 3);
    assert(statement.insert.values[0].type == VALUE_INT);
    assert(statement.insert.values[0].int_value == 1);
    assert(statement.insert.values[1].type == VALUE_TEXT);
    assert(strcmp(statement.insert.values[1].text_value, "Finn") == 0);
    assert(statement.insert.values[2].type == VALUE_INT);
    assert(statement.insert.values[2].int_value == 20);

    ast_statement_free(&statement);
}

static void test_parser_select_star(void) {
    Statement statement;

    assert(parser_parse("SELECT * FROM users;", &statement) == DB_OK);

    assert(statement.type == STATEMENT_SELECT);
    assert(strcmp(statement.select.table_name, "users") == 0);
    assert(statement.select.selected_column_count == 0);
    assert(statement.select.has_where == false);

    ast_statement_free(&statement);
}

static void test_parser_select_columns_with_where(void) {
    Statement statement;

    assert(parser_parse("SELECT name, age FROM users WHERE id = 1;", &statement) == DB_OK);

    assert(statement.type == STATEMENT_SELECT);
    assert(strcmp(statement.select.table_name, "users") == 0);
    assert(statement.select.selected_column_count == 2);
    assert(strcmp(statement.select.selected_columns[0], "name") == 0);
    assert(strcmp(statement.select.selected_columns[1], "age") == 0);
    assert(statement.select.has_where == true);
    assert(strcmp(statement.select.where.column_name, "id") == 0);
    assert(statement.select.where.operator_type == SQL_OPERATOR_EQUAL);
    assert(statement.select.where.value.type == VALUE_INT);
    assert(statement.select.where.value.int_value == 1);

    ast_statement_free(&statement);
}

static void test_parser_select_where_string(void) {
    Statement statement;

    assert(parser_parse("SELECT * FROM missing WHERE name != \"Finn\";", &statement) == DB_OK);

    assert(statement.type == STATEMENT_SELECT);
    assert(strcmp(statement.select.table_name, "missing") == 0);
    assert(statement.select.has_where == true);
    assert(strcmp(statement.select.where.column_name, "name") == 0);
    assert(statement.select.where.operator_type == SQL_OPERATOR_NOT_EQUAL);
    assert(statement.select.where.value.type == VALUE_TEXT);
    assert(strcmp(statement.select.where.value.text_value, "Finn") == 0);

    ast_statement_free(&statement);
}

static void test_parser_delete_with_where(void) {
    Statement statement;

    assert(parser_parse("DELETE FROM users WHERE id = 1;", &statement) == DB_OK);

    assert(statement.type == STATEMENT_DELETE);
    assert(strcmp(statement.delete_statement.table_name, "users") == 0);
    assert(statement.delete_statement.has_where == true);
    assert(strcmp(statement.delete_statement.where.column_name, "id") == 0);
    assert(statement.delete_statement.where.operator_type == SQL_OPERATOR_EQUAL);
    assert(statement.delete_statement.where.value.type == VALUE_INT);
    assert(statement.delete_statement.where.value.int_value == 1);

    ast_statement_free(&statement);
}

static void test_parser_delete_without_where(void) {
    Statement statement;

    assert(parser_parse("DELETE FROM users;", &statement) == DB_OK);

    assert(statement.type == STATEMENT_DELETE);
    assert(strcmp(statement.delete_statement.table_name, "users") == 0);
    assert(statement.delete_statement.has_where == false);

    ast_statement_free(&statement);
}

static void test_parser_meta_commands(void) {
    Statement statement;

    assert(parser_parse(".exit", &statement) == DB_OK);
    assert(statement.type == STATEMENT_META_COMMAND);
    assert(strcmp(statement.meta_command.command, ".exit") == 0);
    ast_statement_free(&statement);

    assert(parser_parse(".tables", &statement) == DB_OK);
    assert(statement.type == STATEMENT_META_COMMAND);
    assert(strcmp(statement.meta_command.command, ".tables") == 0);
    ast_statement_free(&statement);

    assert(parser_parse(".schema users", &statement) == DB_OK);
    assert(statement.type == STATEMENT_META_COMMAND);
    assert(strcmp(statement.meta_command.command, ".schema users") == 0);
    ast_statement_free(&statement);

    assert(parser_parse(".help", &statement) == DB_OK);
    assert(statement.type == STATEMENT_META_COMMAND);
    assert(strcmp(statement.meta_command.command, ".help") == 0);
    ast_statement_free(&statement);
}

static void test_parser_rejects_null_inputs(void) {
    Statement statement;

    assert(parser_parse(NULL, &statement) == DB_ERROR);
    assert(parser_parse("SELECT * FROM users;", NULL) == DB_ERROR);
}

static void test_parser_rejects_unknown_meta_command(void) {
    Statement statement;

    assert(parser_parse(".unknown", &statement) == DB_PARSE_ERROR);
}

static void test_parser_rejects_missing_semicolon(void) {
    Statement statement;

    assert(parser_parse("SELECT * FROM users", &statement) == DB_PARSE_ERROR);
}

static void test_parser_rejects_extra_tokens(void) {
    Statement statement;

    assert(parser_parse("SELECT * FROM users; SELECT * FROM users;", &statement) == DB_PARSE_ERROR);
}

static void test_parser_rejects_bad_create_type(void) {
    Statement statement;

    assert(parser_parse("CREATE TABLE users (id FLOAT);", &statement) == DB_PARSE_ERROR);
}

static void test_parser_rejects_empty_create_columns(void) {
    Statement statement;

    assert(parser_parse("CREATE TABLE users ();", &statement) == DB_PARSE_ERROR);
}

static void test_parser_rejects_empty_insert_values(void) {
    Statement statement;

    assert(parser_parse("INSERT INTO users VALUES ();", &statement) == DB_PARSE_ERROR);
}

static void test_parser_rejects_select_missing_from(void) {
    Statement statement;

    assert(parser_parse("SELECT name users;", &statement) == DB_PARSE_ERROR);
}

static void test_parser_rejects_bad_where(void) {
    Statement statement;

    assert(parser_parse("SELECT * FROM users WHERE id;", &statement) == DB_PARSE_ERROR);
}

int main(void) {
    test_parser_create_table();
    test_parser_insert();
    test_parser_select_star();
    test_parser_select_columns_with_where();
    test_parser_select_where_string();
    test_parser_delete_with_where();
    test_parser_delete_without_where();
    test_parser_meta_commands();
    test_parser_rejects_null_inputs();
    test_parser_rejects_unknown_meta_command();
    test_parser_rejects_missing_semicolon();
    test_parser_rejects_extra_tokens();
    test_parser_rejects_bad_create_type();
    test_parser_rejects_empty_create_columns();
    test_parser_rejects_empty_insert_values();
    test_parser_rejects_select_missing_from();
    test_parser_rejects_bad_where();

    printf("All parser tests passed.\n");

    return 0;
}
