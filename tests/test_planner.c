#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "catalog.h"
#include "common.h"
#include "db.h"
#include "execution/plan.h"
#include "execution/planner.h"
#include "schema.h"
#include "sql/ast.h"
#include "sql/binder.h"
#include "sql/parser.h"
#include "value.h"

static void cleanup_db_dir(const char *path) {
    char catalog_path[MAX_DB_PATH];
    char users_path[MAX_DB_PATH];
    char users_index_path[MAX_DB_PATH];
    char users_age_index_path[MAX_DB_PATH];
    char tables_dir[MAX_DB_PATH];
    char indexes_dir[MAX_DB_PATH];
    char wal_path[MAX_DB_PATH];

    snprintf(catalog_path, sizeof(catalog_path), "%s/catalog.db", path);
    snprintf(users_path, sizeof(users_path), "%s/tables/users.tbl", path);
    snprintf(users_index_path, sizeof(users_index_path), "%s/indexes/users_pk.btree", path);
    snprintf(users_age_index_path, sizeof(users_age_index_path), "%s/indexes/users_age_idx.sidx", path);
    snprintf(tables_dir, sizeof(tables_dir), "%s/tables", path);
    snprintf(indexes_dir, sizeof(indexes_dir), "%s/indexes", path);
    snprintf(wal_path, sizeof(wal_path), "%s/minidb.wal", path);

    remove(users_path);
    remove(users_index_path);
    remove(users_age_index_path);
    remove(wal_path);
    remove(catalog_path);
    rmdir(tables_dir);
    rmdir(indexes_dir);
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

static void add_users_age_index(DB *db, const char *path) {
    CatalogIndex index;
    char index_path[MAX_DB_PATH];
    FILE *file;

    snprintf(index_path, sizeof(index_path), "%s/indexes/users_age_idx.sidx", path);

    file = fopen(index_path, "wb");
    assert(file != NULL);
    assert(fclose(file) == 0);

    memset(&index, 0, sizeof(index));
    strcpy(index.index_name, "users_age_idx");
    strcpy(index.table_name, "users");
    index.column_count = 1;
    strcpy(index.column_names[0], "age");
    index.unique = false;

    assert(catalog_create_index(db, &index) == DB_OK);
}

static void bind_sql(DB *db, const char *sql, Statement *statement, BoundStatement *bound) {
    assert(parser_parse(sql, statement) == DB_OK);
    assert(binder_bind(db, statement, bound) == DB_OK);
}

static void test_planner_create_table_plan(void) {
    const char *path = "test_planner_create";

    DB db;
    Statement statement;
    BoundStatement bound;
    Plan plan;

    setup_db(&db, path);
    bind_sql(&db, "CREATE TABLE users (id INT, name TEXT);", &statement, &bound);

    assert(planner_create_plan(&bound, &plan) == DB_OK);

    assert(plan.type == PLAN_CREATE_TABLE);
    assert(strcmp(plan.create_table.schema.table_name, "users") == 0);
    assert(plan.create_table.schema.column_count == 2);
    assert(strcmp(plan.create_table.schema.columns[0].name, "id") == 0);
    assert(plan.create_table.schema.columns[0].type == VALUE_INT);
    assert(strcmp(plan.create_table.schema.columns[1].name, "name") == 0);
    assert(plan.create_table.schema.columns[1].type == VALUE_TEXT);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_planner_create_index_plan(void) {
    const char *path = "test_planner_create_index";

    DB db;
    Statement statement;
    BoundStatement bound;
    Plan plan;

    setup_db(&db, path);
    add_users_table(&db);
    bind_sql(&db, "CREATE INDEX users_age_idx ON users (age);", &statement, &bound);

    assert(planner_create_plan(&bound, &plan) == DB_OK);

    assert(plan.type == PLAN_CREATE_INDEX);
    assert(strcmp(plan.create_index.index.index_name, "users_age_idx") == 0);
    assert(strcmp(plan.create_index.index.table_name, "users") == 0);
    assert(plan.create_index.index.column_count == 1);
    assert(strcmp(plan.create_index.index.column_names[0], "age") == 0);
    assert(plan.create_index.index.unique == false);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_planner_drop_index_plan(void) {
    const char *path = "test_planner_drop_index";

    DB db;
    Statement statement;
    BoundStatement bound;
    Plan plan;

    setup_db(&db, path);
    add_users_table(&db);
    add_users_age_index(&db, path);
    bind_sql(&db, "DROP INDEX users_age_idx;", &statement, &bound);

    assert(planner_create_plan(&bound, &plan) == DB_OK);

    assert(plan.type == PLAN_DROP_INDEX);
    assert(strcmp(plan.drop_index.index_name, "users_age_idx") == 0);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_planner_insert_plan(void) {
    const char *path = "test_planner_insert";

    DB db;
    Statement statement;
    BoundStatement bound;
    Plan plan;

    setup_db(&db, path);
    add_users_table(&db);
    bind_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", &statement, &bound);

    assert(planner_create_plan(&bound, &plan) == DB_OK);

    assert(plan.type == PLAN_INSERT);
    assert(strcmp(plan.insert.table_name, "users") == 0);
    assert(plan.insert.row.value_count == 3);
    assert(plan.insert.row.values[0].type == VALUE_INT);
    assert(plan.insert.row.values[0].int_value == 1);
    assert(plan.insert.row.values[1].type == VALUE_TEXT);
    assert(strcmp(plan.insert.row.values[1].text_value, "Finn") == 0);
    assert(plan.insert.row.values[1].text_value != bound.statement.insert.values[1].text_value);
    assert(plan.insert.row.values[2].type == VALUE_INT);
    assert(plan.insert.row.values[2].int_value == 20);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_planner_select_star_plan(void) {
    const char *path = "test_planner_select_star";

    DB db;
    Statement statement;
    BoundStatement bound;
    Plan plan;

    setup_db(&db, path);
    add_users_table(&db);
    bind_sql(&db, "SELECT * FROM users;", &statement, &bound);

    assert(planner_create_plan(&bound, &plan) == DB_OK);

    assert(plan.type == PLAN_SELECT);
    assert(strcmp(plan.select.scan.table_name, "users") == 0);
    assert(plan.select.has_filter == false);
    assert(plan.select.has_project == false);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_planner_select_filter_project_plan(void) {
    const char *path = "test_planner_select_filter_project";

    DB db;
    Statement statement;
    BoundStatement bound;
    Plan plan;

    setup_db(&db, path);
    add_users_table(&db);
    bind_sql(&db, "SELECT name FROM users WHERE age > 18;", &statement, &bound);

    assert(planner_create_plan(&bound, &plan) == DB_OK);

    assert(plan.type == PLAN_SELECT);
    assert(strcmp(plan.select.scan.table_name, "users") == 0);
    assert(plan.select.has_filter == true);
    assert(strcmp(plan.select.filter.condition.column_name, "age") == 0);
    assert(plan.select.filter.condition.operator_type == SQL_OPERATOR_GREATER);
    assert(plan.select.filter.condition.value.type == VALUE_INT);
    assert(plan.select.filter.condition.value.int_value == 18);
    assert(plan.select.has_project == true);
    assert(plan.select.project.column_count == 1);
    assert(strcmp(plan.select.project.columns[0], "name") == 0);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_planner_delete_plan(void) {
    const char *path = "test_planner_delete";

    DB db;
    Statement statement;
    BoundStatement bound;
    Plan plan;

    setup_db(&db, path);
    add_users_table(&db);
    bind_sql(&db, "DELETE FROM users WHERE id = 1;", &statement, &bound);

    assert(planner_create_plan(&bound, &plan) == DB_OK);

    assert(plan.type == PLAN_DELETE);
    assert(strcmp(plan.delete_plan.table_name, "users") == 0);
    assert(plan.delete_plan.has_condition == true);
    assert(strcmp(plan.delete_plan.condition.column_name, "id") == 0);
    assert(plan.delete_plan.condition.operator_type == SQL_OPERATOR_EQUAL);
    assert(plan.delete_plan.condition.value.type == VALUE_INT);
    assert(plan.delete_plan.condition.value.int_value == 1);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_planner_delete_without_condition_plan(void) {
    const char *path = "test_planner_delete_all";

    DB db;
    Statement statement;
    BoundStatement bound;
    Plan plan;

    setup_db(&db, path);
    add_users_table(&db);
    bind_sql(&db, "DELETE FROM users;", &statement, &bound);

    assert(planner_create_plan(&bound, &plan) == DB_OK);

    assert(plan.type == PLAN_DELETE);
    assert(strcmp(plan.delete_plan.table_name, "users") == 0);
    assert(plan.delete_plan.has_condition == false);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_planner_update_plan(void) {
    const char *path = "test_planner_update";

    DB db;
    Statement statement;
    BoundStatement bound;
    Plan plan;

    setup_db(&db, path);
    add_users_table(&db);
    bind_sql(&db, "UPDATE users SET age = 21 WHERE id = 1;", &statement, &bound);

    assert(planner_create_plan(&bound, &plan) == DB_OK);

    assert(plan.type == PLAN_UPDATE);
    assert(strcmp(plan.update.table_name, "users") == 0);
    assert(strcmp(plan.update.set_column, "age") == 0);
    assert(plan.update.set_value.type == VALUE_INT);
    assert(plan.update.set_value.int_value == 21);
    assert(plan.update.has_condition == true);
    assert(strcmp(plan.update.condition.column_name, "id") == 0);
    assert(plan.update.condition.operator_type == SQL_OPERATOR_EQUAL);
    assert(plan.update.condition.value.type == VALUE_INT);
    assert(plan.update.condition.value.int_value == 1);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_planner_meta_command_plan(void) {
    Statement statement;
    BoundStatement bound;
    Plan plan;

    assert(parser_parse(".tables", &statement) == DB_OK);
    assert(binder_bind(NULL, &statement, &bound) == DB_OK);

    assert(planner_create_plan(&bound, &plan) == DB_OK);

    assert(plan.type == PLAN_META_COMMAND);
    assert(strcmp(plan.meta_command.command, ".tables") == 0);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
}

static void test_planner_rejects_null_inputs(void) {
    BoundStatement bound;
    Plan plan;

    assert(planner_create_plan(NULL, &plan) == DB_ERROR);
    assert(planner_create_plan(&bound, NULL) == DB_ERROR);
}

int main(void) {
    test_planner_create_table_plan();
    test_planner_create_index_plan();
    test_planner_drop_index_plan();
    test_planner_insert_plan();
    test_planner_select_star_plan();
    test_planner_select_filter_project_plan();
    test_planner_delete_plan();
    test_planner_delete_without_condition_plan();
    test_planner_update_plan();
    test_planner_meta_command_plan();
    test_planner_rejects_null_inputs();

    printf("All planner tests passed.\n");

    return 0;
}
