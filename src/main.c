#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"

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
 * Handles shell-level commands.
 * These are not SQL commands, so they should not go to the parser later.
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
    char input[MAX_INPUT_SIZE];
    bool should_exit = false;

    while (!should_exit) {
        print_prompt();

        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }

        trim_newline(input);

        if (strlen(input) == 0) {
            continue;
        }

        /*
         * Meta commands belong to the shell.
         * SQL-like input will be handled by the parser in a later step.
         */
        if (input[0] == '.') {
            DBStatus status = handle_meta_command(input, &should_exit);

            if (status != DB_OK) {
                printf("Unknown command: %s\n", input);
            }

            continue;
        }

        printf("SQL execution is not implemented yet: %s\n", input);
    }

    return 0;
}
