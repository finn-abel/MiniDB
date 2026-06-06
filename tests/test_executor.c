#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "db.h"
#include "execution/executor.h"
#include "execution/plan.h"
#include "execution/planner.h"
#include "sql/ast.h"
#include "sql/binder.h"
#include "sql/parser.h"

static void cleanup_db_dir(const char *path) {
    char catalog_path[MAX_DB_PATH];
    char users_path[MAX_DB_PATH];
    char users_index_path[MAX_DB_PATH];
    char users_age_index_path[MAX_DB_PATH];
    char users_age_name_index_path[MAX_DB_PATH];
    char tables_dir[MAX_DB_PATH];
    char indexes_dir[MAX_DB_PATH];
    char wal_path[MAX_DB_PATH];

    snprintf(catalog_path, sizeof(catalog_path), "%s/catalog.db", path);
    snprintf(users_path, sizeof(users_path), "%s/tables/users.tbl", path);
    snprintf(users_index_path, sizeof(users_index_path), "%s/indexes/users_pk.btree", path);
    snprintf(users_age_index_path, sizeof(users_age_index_path), "%s/indexes/users_age_idx.sidx", path);
    snprintf(users_age_name_index_path, sizeof(users_age_name_index_path), "%s/indexes/users_age_name_idx.sidx", path);
    snprintf(tables_dir, sizeof(tables_dir), "%s/tables", path);
    snprintf(indexes_dir, sizeof(indexes_dir), "%s/indexes", path);
    snprintf(wal_path, sizeof(wal_path), "%s/minidb.wal", path);

    remove(users_path);
    remove(users_index_path);
    remove(users_age_index_path);
    remove(users_age_name_index_path);
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

static void execute_sql(DB *db, const char *sql, FILE *out) {
    Statement statement;
    BoundStatement bound;
    Plan plan;

    assert(parser_parse(sql, &statement) == DB_OK);
    assert(binder_bind(db, &statement, &bound) == DB_OK);
    assert(planner_create_plan(&bound, &plan) == DB_OK);
    assert(executor_execute(db, &plan, out) == DB_OK);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
}

static DBStatus execute_sql_status(DB *db, const char *sql, FILE *out) {
    Statement statement;
    BoundStatement bound;
    Plan plan;
    DBStatus status;

    status = parser_parse(sql, &statement);

    if (status != DB_OK) {
        return status;
    }

    status = binder_bind(db, &statement, &bound);

    if (status != DB_OK) {
        ast_statement_free(&statement);
        return status;
    }

    status = planner_create_plan(&bound, &plan);

    if (status == DB_OK) {
        status = executor_execute(db, &plan, out);
        plan_free(&plan);
    }

    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);

    return status;
}

static void assert_next_line(FILE *file, const char *expected) {
    char buffer[256];

    assert(fgets(buffer, sizeof(buffer), file) != NULL);
    assert(strcmp(buffer, expected) == 0);
}

static long file_size(const char *path) {
    struct stat info;

    assert(stat(path, &info) == 0);

    return info.st_size;
}

static void test_executor_create_table(void) {
    const char *path = "test_executor_create";

    DB db;
    FILE *table_file;

    setup_db(&db, path);

    execute_sql(&db, "CREATE TABLE users (id INT, name TEXT, age INT);", stdout);

    assert(db.catalog.table_count == 1);
    assert(strcmp(db.catalog.tables[0].table_name, "users") == 0);
    assert(db.catalog.tables[0].column_count == 3);

    table_file = fopen("test_executor_create/tables/users.tbl", "rb");
    assert(table_file != NULL);
    assert(fclose(table_file) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_insert_and_select_all(void) {
    const char *path = "test_executor_select_all";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT, name TEXT, age INT);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", stdout);
    assert(db.transaction.state == TRANSACTION_STATE_IDLE);
    assert(file_size("test_executor_select_all/minidb.wal") > 0);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT * FROM users;", out);

    rewind(out);
    assert_next_line(out, "1 | Finn | 20\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_primary_key_rejects_duplicate_insert(void) {
    const char *path = "test_executor_pk_duplicate";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", stdout);
    assert(execute_sql_status(&db, "INSERT INTO users VALUES (1, \"Alex\", 17);", stdout) == DB_ERROR);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT * FROM users WHERE id = 1;", out);

    rewind(out);
    assert_next_line(out, "1 | Finn | 20\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_primary_key_update_maintains_index(void) {
    const char *path = "test_executor_pk_update";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", stdout);
    execute_sql(&db, "UPDATE users SET id = 2 WHERE id = 1;", stdout);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT * FROM users WHERE id = 1;", out);
    rewind(out);
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT * FROM users WHERE id = 2;", out);
    rewind(out);
    assert_next_line(out, "2 | Finn | 20\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_primary_key_delete_maintains_index(void) {
    const char *path = "test_executor_pk_delete";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", stdout);
    execute_sql(&db, "DELETE FROM users WHERE id = 1;", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Alex\", 17);", stdout);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT * FROM users WHERE id = 1;", out);
    rewind(out);
    assert_next_line(out, "1 | Alex | 17\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_primary_key_schema_persists_after_reopen(void) {
    const char *path = "test_executor_pk_reopen";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", stdout);
    assert(db_close(&db) == DB_OK);

    assert(db_open(&db, path) == DB_OK);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, ".schema users", out);
    rewind(out);
    assert_next_line(out, "users (id INT PRIMARY KEY NOT NULL, name TEXT NOT NULL, age INT)\n");
    assert(fclose(out) == 0);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT * FROM users WHERE id = 1;", out);
    rewind(out);
    assert_next_line(out, "1 | Finn | 20\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_secondary_index_workflow(void) {
    const char *path = "test_executor_secondary_index";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (2, \"Alex\", 17);", stdout);
    execute_sql(&db, "CREATE INDEX users_age_idx ON users (age);", stdout);
    execute_sql(&db, "CREATE INDEX users_age_name_idx ON users (age, name);", stdout);

    assert(db.catalog.index_count == 2);
    assert(strcmp(db.catalog.indexes[0].index_name, "users_age_idx") == 0);
    assert(db.catalog.indexes[0].column_count == 1);
    assert(strcmp(db.catalog.indexes[0].column_names[0], "age") == 0);
    assert(db.catalog.indexes[0].unique == false);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT name FROM users WHERE age = 17;", out);
    rewind(out);
    assert_next_line(out, "Alex\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT name FROM users WHERE age >= 20;", out);
    rewind(out);
    assert_next_line(out, "Finn\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT id FROM users WHERE name = \"Finn\";", out);
    rewind(out);
    assert_next_line(out, "1\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    execute_sql(&db, "INSERT INTO users VALUES (3, \"Sam\", 17);", stdout);

    execute_sql(&db, "UPDATE users SET age = 18 WHERE age = 17;", stdout);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT name FROM users WHERE age = 17;", out);
    rewind(out);
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    assert(db_open(&db, path) == DB_OK);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT name FROM users WHERE age = 18;", out);
    rewind(out);
    assert_next_line(out, "Alex\n");
    assert_next_line(out, "Sam\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    execute_sql(&db, "DELETE FROM users WHERE age = 18;", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (3, \"Sam\", 18);", stdout);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT name FROM users WHERE age = 18;", out);
    rewind(out);
    assert_next_line(out, "Sam\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_select_with_filter_and_project(void) {
    const char *path = "test_executor_select_filter_project";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT, name TEXT, age INT);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (2, \"Alex\", 17);", stdout);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT name, age FROM users WHERE age > 18;", out);

    rewind(out);
    assert_next_line(out, "Finn | 20\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_delete_with_condition(void) {
    const char *path = "test_executor_delete";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT, name TEXT, age INT);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (2, \"Alex\", 17);", stdout);
    execute_sql(&db, "DELETE FROM users WHERE id = 1;", stdout);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT * FROM users;", out);

    rewind(out);
    assert_next_line(out, "2 | Alex | 17\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_delete_without_condition(void) {
    const char *path = "test_executor_delete_all";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT, name TEXT, age INT);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (2, \"Alex\", 17);", stdout);
    execute_sql(&db, "DELETE FROM users;", stdout);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT * FROM users;", out);

    rewind(out);
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_update_with_condition(void) {
    const char *path = "test_executor_update";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT, name TEXT, age INT);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (2, \"Alex\", 17);", stdout);
    execute_sql(&db, "UPDATE users SET age = 21 WHERE id = 1;", stdout);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT * FROM users;", out);

    rewind(out);
    assert_next_line(out, "1 | Finn | 21\n");
    assert_next_line(out, "2 | Alex | 17\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_update_without_condition(void) {
    const char *path = "test_executor_update_all";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT, name TEXT, age INT);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (1, \"Finn\", 20);", stdout);
    execute_sql(&db, "INSERT INTO users VALUES (2, \"Alex\", 17);", stdout);
    execute_sql(&db, "UPDATE users SET name = \"Updated\";", stdout);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, "SELECT * FROM users;", out);

    rewind(out);
    assert_next_line(out, "1 | Updated | 20\n");
    assert_next_line(out, "2 | Updated | 17\n");
    assert(fgetc(out) == EOF);
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_meta_commands(void) {
    const char *path = "test_executor_meta";

    DB db;
    FILE *out;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT, name TEXT, age INT);", stdout);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, ".tables", out);
    rewind(out);
    assert_next_line(out, "users\n");
    assert(fclose(out) == 0);

    out = tmpfile();
    assert(out != NULL);

    execute_sql(&db, ".schema users", out);
    rewind(out);
    assert_next_line(out, "users (id INT, name TEXT, age INT)\n");
    assert(fclose(out) == 0);

    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

static void test_executor_rejects_null_inputs(void) {
    const char *path = "test_executor_null";

    DB db;
    Statement statement;
    BoundStatement bound;
    Plan plan;

    setup_db(&db, path);
    execute_sql(&db, "CREATE TABLE users (id INT, name TEXT, age INT);", stdout);

    assert(parser_parse("SELECT * FROM users;", &statement) == DB_OK);
    assert(binder_bind(&db, &statement, &bound) == DB_OK);
    assert(planner_create_plan(&bound, &plan) == DB_OK);

    assert(executor_execute(NULL, &plan, stdout) == DB_ERROR);
    assert(executor_execute(&db, NULL, stdout) == DB_ERROR);
    assert(executor_execute(&db, &plan, NULL) == DB_ERROR);
    assert(db.transaction.state == TRANSACTION_STATE_IDLE);

    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);
    assert(db_close(&db) == DB_OK);
    cleanup_db_dir(path);
}

int main(void) {
    test_executor_create_table();
    test_executor_insert_and_select_all();
    test_executor_primary_key_rejects_duplicate_insert();
    test_executor_primary_key_update_maintains_index();
    test_executor_primary_key_delete_maintains_index();
    test_executor_primary_key_schema_persists_after_reopen();
    test_executor_secondary_index_workflow();
    test_executor_select_with_filter_and_project();
    test_executor_delete_with_condition();
    test_executor_delete_without_condition();
    test_executor_update_with_condition();
    test_executor_update_without_condition();
    test_executor_meta_commands();
    test_executor_rejects_null_inputs();

    printf("All executor tests passed.\n");

    return 0;
}
