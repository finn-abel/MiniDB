#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "catalog.h"
#include "common.h"
#include "db.h"
#include "schema.h"
#include "sql/ast.h"
#include "sql/binder.h"
#include "sql/parser.h"
#include "value.h"

static void cleanup_db_dir(const char *path) {
    char catalog_path[MAX_DB_PATH];
    char users_path[MAX_DB_PATH];
    char tables_dir[MAX_DB_PATH];

    snprintf(catalog_path, sizeof(catalog_path), "%s/catalog.db", path);
    snprintf(users_path, sizeof(users_path), "%s/tables/users.tbl", path);
    snprintf(tables_dir, sizeof(tables_dir), "%s/tables", path);

    remove(users_path);
    remove(catalog_path);
    rmdir(tables_dir);
    rmdir(path);
}

static void setup_db(DB *db, const char *path) {
    cleanup_db_dir(path);

    assert(db_open(db, path) == DB_OK);
}

static void add_users_table(DB *db) {
    Schema schema;

    assert(schema_init(&schema, "users") == DB_OK);
    assert(schema_add_column(&schema, "id", VALUE_INT, true, true) == DB_OK);
    assert(schema_add_column(&schema, "name", VALUE_TEXT, true, false) == DB_OK);
    assert(schema_add_column(&schema, "age", VALUE_INT, false, false) == DB_OK);
    assert(catalog_create_table(db, &schema) == DB_OK);
}

static void parse_statement(const char *sql, Statement *statement) {
    assert(parser_parse(sql, statement) == DB_OK);
}

static void test_binder_bind_create_table(void) {
    const char *path = "test_binder_create";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    parse_statement("CREATE TABLE users (id INT, name TEXT);", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_OK);

    assert(bound.statement.type == STATEMENT_CREATE_TABLE);
    assert(bound.has_table_schema == true);
    assert(strcmp(bound.table_schema.table_name, "users") == 0);
    assert(bound.table_schema.column_count == 2);
    assert(strcmp(bound.table_schema.columns[0].name, "id") == 0);
    assert(bound.table_schema.columns[0].type == VALUE_INT);
    assert(strcmp(bound.table_schema.columns[1].name, "name") == 0);
    assert(bound.table_schema.columns[1].type == VALUE_TEXT);

    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_rejects_create_existing_table(void) {
    const char *path = "test_binder_create_existing";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("CREATE TABLE users (id INT);", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_ERROR);
    assert(bound.has_table_schema == false);

    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_rejects_create_duplicate_columns(void) {
    const char *path = "test_binder_create_duplicate_columns";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    parse_statement("CREATE TABLE users (id INT, id TEXT);", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_ERROR);

    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_bind_insert(void) {
    const char *path = "test_binder_insert";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("INSERT INTO users VALUES (1, \"Finn\", 20);", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_OK);

    assert(bound.statement.type == STATEMENT_INSERT);
    assert(bound.has_table_schema == true);
    assert(strcmp(bound.table_schema.table_name, "users") == 0);
    assert(bound.table_schema.column_count == 3);
    assert(bound.statement.insert.values[1].type == VALUE_TEXT);
    assert(strcmp(bound.statement.insert.values[1].text_value, "Finn") == 0);
    assert(bound.statement.insert.values[1].text_value != statement.insert.values[1].text_value);

    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_rejects_insert_missing_table(void) {
    const char *path = "test_binder_insert_missing";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    parse_statement("INSERT INTO users VALUES (1, \"Finn\", 20);", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_NOT_FOUND);

    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_rejects_insert_wrong_value_count(void) {
    const char *path = "test_binder_insert_wrong_count";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("INSERT INTO users VALUES (1, \"Finn\");", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_ERROR);

    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_rejects_insert_wrong_type(void) {
    const char *path = "test_binder_insert_wrong_type";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("INSERT INTO users VALUES (1, 99, 20);", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_TYPE_ERROR);

    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_bind_select_star(void) {
    const char *path = "test_binder_select_star";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("SELECT * FROM users;", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_OK);

    assert(bound.statement.type == STATEMENT_SELECT);
    assert(bound.has_table_schema == true);
    assert(strcmp(bound.table_schema.table_name, "users") == 0);
    assert(bound.statement.select.selected_column_count == 0);

    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_bind_select_columns_with_where(void) {
    const char *path = "test_binder_select_where";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("SELECT name, age FROM users WHERE id = 1;", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_OK);

    assert(bound.statement.type == STATEMENT_SELECT);
    assert(bound.statement.select.selected_column_count == 2);
    assert(bound.statement.select.has_where == true);
    assert(strcmp(bound.statement.select.where.column_name, "id") == 0);
    assert(bound.statement.select.where.value.type == VALUE_INT);

    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_rejects_select_missing_column(void) {
    const char *path = "test_binder_select_missing_column";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("SELECT email FROM users;", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_NOT_FOUND);

    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_rejects_select_bad_where_column(void) {
    const char *path = "test_binder_select_bad_where_column";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("SELECT * FROM users WHERE email = \"x\";", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_NOT_FOUND);

    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_rejects_select_bad_where_type(void) {
    const char *path = "test_binder_select_bad_where_type";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("SELECT * FROM users WHERE id = \"one\";", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_TYPE_ERROR);

    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_bind_delete_with_where(void) {
    const char *path = "test_binder_delete";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("DELETE FROM users WHERE id = 1;", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_OK);

    assert(bound.statement.type == STATEMENT_DELETE);
    assert(bound.has_table_schema == true);
    assert(bound.statement.delete_statement.has_where == true);

    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_rejects_delete_bad_where_column(void) {
    const char *path = "test_binder_delete_bad_where_column";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("DELETE FROM users WHERE email = \"x\";", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_NOT_FOUND);

    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_rejects_delete_bad_where_type(void) {
    const char *path = "test_binder_delete_bad_where_type";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    add_users_table(&db);
    parse_statement("DELETE FROM users WHERE id = \"one\";", &statement);

    assert(binder_bind(&db, &statement, &bound) == DB_TYPE_ERROR);

    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_binder_bind_meta_command_without_db(void) {
    Statement statement;
    BoundStatement bound;

    parse_statement(".tables", &statement);

    assert(binder_bind(NULL, &statement, &bound) == DB_OK);
    assert(bound.statement.type == STATEMENT_META_COMMAND);
    assert(strcmp(bound.statement.meta_command.command, ".tables") == 0);
    assert(bound.has_table_schema == false);

    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
}

static void test_binder_rejects_null_inputs(void) {
    const char *path = "test_binder_null";

    DB db;
    Statement statement;
    BoundStatement bound;

    setup_db(&db, path);
    parse_statement("SELECT * FROM users;", &statement);

    assert(binder_bind(&db, NULL, &bound) == DB_ERROR);
    assert(binder_bind(&db, &statement, NULL) == DB_ERROR);
    assert(binder_bind(NULL, &statement, &bound) == DB_ERROR);

    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

int main(void) {
    test_binder_bind_create_table();
    test_binder_rejects_create_existing_table();
    test_binder_rejects_create_duplicate_columns();
    test_binder_bind_insert();
    test_binder_rejects_insert_missing_table();
    test_binder_rejects_insert_wrong_value_count();
    test_binder_rejects_insert_wrong_type();
    test_binder_bind_select_star();
    test_binder_bind_select_columns_with_where();
    test_binder_rejects_select_missing_column();
    test_binder_rejects_select_bad_where_column();
    test_binder_rejects_select_bad_where_type();
    test_binder_bind_delete_with_where();
    test_binder_rejects_delete_bad_where_column();
    test_binder_rejects_delete_bad_where_type();
    test_binder_bind_meta_command_without_db();
    test_binder_rejects_null_inputs();

    printf("All binder tests passed.\n");

    return 0;
}
