#include <stdio.h>
#include <string.h>

#include "common.h"
#include "db.h"

static void print_prompt(void) {
    printf("minidb > ");
    fflush(stdout);
}

static void print_help(void) {
    printf("Supported commands:\n");
    printf("  .help   Show this help message\n");
    printf("  .exit   Exit the MiniDB shell\n");
}

static void trim_newline(char *input) {
    size_t len = strlen(input);

    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }
}

/*
 * Handles MiniDB shell commands.
 *
 * These are called "meta-commands" because they control the shell itself.
 * They are not SQL statements and should not be sent to the SQL parser later.
 */
static DBStatus handle_meta_command(const char *input, bool *should_exit) {
    if (strcmp(input, ".exit") == 0) {
        *should_exit = true;
        return DB_OK;
    }

    if (strcmp(input, ".help") == 0) {
        print_help();
        return DB_OK;
    }

    return DB_ERROR;
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

        /*
         * Commands beginning with '.' are meta-commands.
         */
        if (input[0] == '.') {
            DBStatus status = handle_meta_command(input, &should_exit);

            if (status != DB_OK) {
                printf("Unrecognized command: %s\n", input);
            }

            continue;
        }

        /*
         * SQL support comes later.
         */
        printf("SQL support has not been implemented yet.\n");
    }

    DBStatus close_status = db_close(&db);

    if (close_status != DB_OK) {
        fprintf(stderr, "Warning: database did not close cleanly.\n");
        return 1;
    }

    return 0;
}
