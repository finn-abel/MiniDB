#include <stdio.h>
#include <string.h>

#include "common.h"
#include "db.h"
#include "execution/executor.h"
#include "execution/plan.h"
#include "execution/planner.h"
#include "sql/ast.h"
#include "sql/binder.h"
#include "sql/parser.h"

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

/*
 * Prints a short shell-facing error.
 * Internal layers return status codes; the CLI turns them into readable text.
 */
static void print_status_error(DBStatus status) {
    if (status == DB_PARSE_ERROR) {
        printf("Syntax error.\n");
    } else if (status == DB_NOT_FOUND) {
        printf("Not found.\n");
    } else if (status == DB_FULL) {
        printf("Database object is full.\n");
    } else if (status == DB_TYPE_ERROR) {
        printf("Type error.\n");
    } else if (status == DB_IO_ERROR) {
        printf("I/O error.\n");
    } else {
        printf("Error.\n");
    }
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
    }
}

/*
 * Runs one input line through the SQL pipeline:
 * parser -> binder -> planner -> executor.
 */
static DBStatus execute_input(DB *db, const char *input, bool *should_exit) {
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

    /*
     * Parser: input text -> syntax tree.
     */
    status = parser_parse(input, &statement);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Binder: syntax tree -> catalog-checked statement.
     */
    status = binder_bind(db, &statement, &bound);

    if (status != DB_OK) {
        ast_statement_free(&statement);
        return status;
    }

    /*
     * Planner: bound statement -> simple execution plan.
     */
    status = planner_create_plan(&bound, &plan);

    if (status != DB_OK) {
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
    char input[MAX_INPUT_SIZE];
    bool should_exit = false;

    DBStatus open_status = db_open(&db, "mydb");

    if (open_status != DB_OK) {
        fprintf(stderr, "Failed to open database.\n");
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

        DBStatus status = execute_input(&db, input, &should_exit);

        /*
         * The lower layers report status only. The CLI owns user-facing error
         * messages so engine code can stay UI-agnostic.
         */
        if (status != DB_OK) {
            print_status_error(status);
        }
    }

    DBStatus close_status = db_close(&db);

    if (close_status != DB_OK) {
        fprintf(stderr, "Warning: database did not close cleanly.\n");
        return 1;
    }

    return 0;
}
