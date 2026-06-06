#include <stdio.h>
#include <string.h>

#include "catalog.h"
#include "common.h"
#include "db.h"
#include "execution/executor.h"
#include "execution/plan.h"
#include "execution/planner.h"
#include "sql/ast.h"
#include "sql/binder.h"
#include "sql/parser.h"
#include "util/error.h"

static void print_prompt(void) {
    /*
     * Keep the prompt short because query output may print multiple rows.
     */
    printf("db > ");
    fflush(stdout);
}

static void trim_newline(char *input) {
    size_t len = strlen(input);

    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }
}

static const char *cli_value_type_name(ValueType type) {
    /*
     * Keep shell error wording aligned with CREATE TABLE syntax.
     */
    if (type == VALUE_INT) {
        return "INT";
    }

    if (type == VALUE_TEXT) {
        return "TEXT";
    }

    return "UNKNOWN";
}

static const char *statement_table_name(const Statement *statement) {
    if (statement == NULL) {
        return NULL;
    }

    /*
     * Most SQL errors can be explained better if the shell can name the table
     * involved. Meta commands do not have one.
     */
    switch (statement->type) {
        case STATEMENT_CREATE_TABLE:
            return statement->create_table.table_name;
        case STATEMENT_INSERT:
            return statement->insert.table_name;
        case STATEMENT_SELECT:
            return statement->select.table_name;
        case STATEMENT_DELETE:
            return statement->delete_statement.table_name;
        case STATEMENT_UPDATE:
            return statement->update.table_name;
        case STATEMENT_META_COMMAND:
        default:
            return NULL;
    }
}

static bool set_insert_type_error(
    const DB *db,
    const InsertStatement *statement,
    DBError *error
) {
    Schema schema;

    /*
     * INSERT type errors are positional: the first value maps to the first
     * schema column, the second value maps to the second column, and so on.
     */
    if (catalog_get_schema(db, statement->table_name, &schema) != DB_OK) {
        return false;
    }

    for (
        uint16_t i = 0;
        i < statement->value_count && i < schema.column_count;
        i++
    ) {
        if (statement->values[i].type != schema.columns[i].type) {
            db_error_set(
                error,
                DB_TYPE_ERROR,
                "column '%s' expects %s, got %s.",
                schema.columns[i].name,
                cli_value_type_name(schema.columns[i].type),
                cli_value_type_name(statement->values[i].type)
            );
            return true;
        }
    }

    return false;
}

static bool set_where_type_error(
    const DB *db,
    const char *table_name,
    const WhereCondition *condition,
    DBError *error
) {
    Schema schema;
    ValueType column_type;

    /*
     * WHERE type errors are name-based, so look up the condition column before
     * formatting the expected/actual type message.
     */
    if (catalog_get_schema(db, table_name, &schema) != DB_OK) {
        return false;
    }

    if (schema_get_column_type(&schema, condition->column_name, &column_type) != DB_OK) {
        return false;
    }

    db_error_set(
        error,
        DB_TYPE_ERROR,
        "column '%s' expects %s, got %s.",
        condition->column_name,
        cli_value_type_name(column_type),
        cli_value_type_name(condition->value.type)
    );

    return true;
}

static bool set_update_type_error(
    const DB *db,
    const UpdateStatement *statement,
    DBError *error
) {
    Schema schema;
    ValueType column_type;

    /*
     * UPDATE type errors can come from either the SET assignment or WHERE.
     * Prefer naming the SET column when that value is the mismatch.
     */
    if (catalog_get_schema(db, statement->table_name, &schema) != DB_OK) {
        return false;
    }

    if (
        schema_get_column_type(&schema, statement->set_column, &column_type) == DB_OK &&
        statement->set_value.type != column_type
    ) {
        db_error_set(
            error,
            DB_TYPE_ERROR,
            "column '%s' expects %s, got %s.",
            statement->set_column,
            cli_value_type_name(column_type),
            cli_value_type_name(statement->set_value.type)
        );
        return true;
    }

    if (statement->has_where) {
        return set_where_type_error(
            db,
            statement->table_name,
            &statement->where,
            error
        );
    }

    return false;
}

/*
 * Adds context to status-only failures when the CLI has enough parsed data.
 * Engine layers still return DBStatus; the shell turns that into readable text.
 */
static void set_statement_error(
    const DB *db,
    const Statement *statement,
    DBStatus status,
    DBError *error
) {
    const char *table_name = statement_table_name(statement);

    /*
     * The binder reports duplicate CREATE TABLE as a generic DB_ERROR because
     * the lower layer should not print shell text. The CLI can inspect the
     * parsed statement and catalog to make that failure specific.
     */
    if (
        status == DB_ERROR &&
        statement != NULL &&
        statement->type == STATEMENT_CREATE_TABLE &&
        catalog_table_exists(db, statement->create_table.table_name)
    ) {
        db_error_set(
            error,
            status,
            "table '%s' already exists.",
            statement->create_table.table_name
        );
        return;
    }

    /*
     * DB_NOT_FOUND can mean a missing table or column. If the table itself is
     * absent, prefer that clearer message; otherwise fall back to the generic
     * status text for now.
     */
    if (
        status == DB_NOT_FOUND &&
        table_name != NULL &&
        !catalog_table_exists(db, table_name)
    ) {
        db_error_set(error, status, "table '%s' does not exist.", table_name);
        return;
    }

    /*
     * Type errors are most useful when they name the column and show the SQL
     * type expected by the schema.
     */
    if (
        status == DB_TYPE_ERROR &&
        statement != NULL &&
        statement->type == STATEMENT_INSERT &&
        set_insert_type_error(db, &statement->insert, error)
    ) {
        return;
    }

    if (
        status == DB_TYPE_ERROR &&
        statement != NULL &&
        statement->type == STATEMENT_UPDATE &&
        set_update_type_error(db, &statement->update, error)
    ) {
        return;
    }

    if (
        status == DB_TYPE_ERROR &&
        statement != NULL &&
        statement->type == STATEMENT_SELECT &&
        statement->select.has_where &&
        set_where_type_error(db, table_name, &statement->select.where, error)
    ) {
        return;
    }

    if (
        status == DB_TYPE_ERROR &&
        statement != NULL &&
        statement->type == STATEMENT_DELETE &&
        statement->delete_statement.has_where &&
        set_where_type_error(
            db,
            table_name,
            &statement->delete_statement.where,
            error
        )
    ) {
        return;
    }

    db_error_set_status(error, status);
}

/*
 * Prints simple confirmations for statements whose executor does not produce
 * row output.
 */
static void print_success_message(const Plan *plan) {
    /*
     * SELECT and meta commands may already print output during execution.
     * Only mutating SQL statements get an extra confirmation line here.
     */
    if (plan->type == PLAN_CREATE_TABLE) {
        printf("Table created.\n");
    } else if (plan->type == PLAN_INSERT) {
        printf("1 row inserted.\n");
    } else if (plan->type == PLAN_DELETE) {
        printf("Rows deleted.\n");
    } else if (plan->type == PLAN_UPDATE) {
        printf("Rows updated.\n");
    }
}

/*
 * Runs one input line through the SQL pipeline:
 * parser -> binder -> planner -> executor.
 */
static DBStatus execute_input(
    DB *db,
    const char *input,
    bool *should_exit,
    DBError *error
) {
    Statement statement;
    BoundStatement bound;
    Plan plan;
    DBStatus status;

    /*
     * These structs may own heap memory after their init/build functions run.
     * Starting them at zero makes cleanup safe on every exit path.
     */
    memset(&statement, 0, sizeof(Statement));
    memset(&bound, 0, sizeof(BoundStatement));
    memset(&plan, 0, sizeof(Plan));
    db_error_clear(error);

    /*
     * Parser: input text -> syntax tree.
     */
    status = parser_parse(input, &statement);

    if (status != DB_OK) {
        db_error_set_status(error, status);
        return status;
    }

    /*
     * Binder: syntax tree -> catalog-checked statement.
     */
    status = binder_bind(db, &statement, &bound);

    if (status != DB_OK) {
        set_statement_error(db, &statement, status, error);
        ast_statement_free(&statement);
        return status;
    }

    /*
     * Planner: bound statement -> simple execution plan.
     */
    status = planner_create_plan(&bound, &plan);

    if (status != DB_OK) {
        set_statement_error(db, &statement, status, error);
        binder_bound_statement_free(&bound);
        ast_statement_free(&statement);
        return status;
    }

    /*
     * Executor: plan -> catalog/table/record operations.
     * SELECT and meta commands write their output to stdout.
     */
    status = executor_execute(db, &plan, stdout);

    if (status == DB_OK) {
        print_success_message(&plan);

        /*
         * .exit still travels through the full meta-command pipeline, but the
         * shell loop owns the decision to stop reading input.
         */
        if (
            plan.type == PLAN_META_COMMAND &&
            strcmp(plan.meta_command.command, ".exit") == 0
        ) {
            *should_exit = true;
        }
    } else {
        set_statement_error(db, &statement, status, error);
    }

    /*
     * Free in reverse pipeline order because later objects may own copies of
     * earlier data.
     */
    plan_free(&plan);
    binder_bound_statement_free(&bound);
    ast_statement_free(&statement);

    return status;
}

int main(void) {
    DB db;
    DBError error;
    char input[MAX_INPUT_SIZE];
    bool should_exit = false;

    db_error_clear(&error);

    DBStatus open_status = db_open(&db, "mydb");

    if (open_status != DB_OK) {
        db_error_set_status(&error, open_status);
        db_error_print(stderr, &error);
        return 1;
    }

    printf("MiniDB shell\n");
    printf("Opened database at: %s\n", db.path);
    printf("Type .help for help.\n");

    while (!should_exit) {
        print_prompt();

        /*
         * EOF exits cleanly, which makes scripted input work:
         * printf 'SELECT * FROM users;\n' | ./MiniDB
         */
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }

        trim_newline(input);

        /*
         * Ignore empty input.
         */
        if (input[0] == '\0') {
            continue;
        }

        DBStatus status = execute_input(&db, input, &should_exit, &error);

        /*
         * The lower layers report status only. The CLI owns user-facing error
         * messages so engine code can stay UI-agnostic.
         */
        if (status != DB_OK) {
            db_error_print(stdout, &error);
        }
    }

    DBStatus close_status = db_close(&db);

    if (close_status != DB_OK) {
        fprintf(stderr, "Warning: database did not close cleanly.\n");
        return 1;
    }

    return 0;
}
