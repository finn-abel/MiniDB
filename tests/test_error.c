#define _GNU_SOURCE

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "util/error.h"

static void test_error_clear(void) {
    DBError error;

    assert(db_error_set(&error, DB_ERROR, "operation failed.") == DB_ERROR);

    db_error_clear(&error);

    assert(error.status == DB_OK);
    assert(strcmp(error.message, "") == 0);
}

static void test_error_set_formatted_message(void) {
    DBError error;

    assert(
        db_error_set(
            &error,
            DB_NOT_FOUND,
            "table '%s' does not exist.",
            "orders"
        ) == DB_NOT_FOUND
    );

    assert(error.status == DB_NOT_FOUND);
    assert(strcmp(error.message, "table 'orders' does not exist.") == 0);
}

static void test_error_set_status_uses_default_message(void) {
    DBError error;

    assert(db_error_set_status(&error, DB_IO_ERROR) == DB_IO_ERROR);

    assert(error.status == DB_IO_ERROR);
    assert(strcmp(error.message, "database file could not be opened.") == 0);
}

static void test_status_default_message(void) {
    assert(strcmp(db_status_default_message(DB_OK), "") == 0);
    assert(strcmp(db_status_default_message(DB_PARSE_ERROR), "syntax error.") == 0);
    assert(strcmp(db_status_default_message(DB_FULL), "database object is full.") == 0);
}

static void test_error_print(void) {
    DBError error;
    char buffer[128] = "";
    FILE *out = fmemopen(buffer, sizeof(buffer), "w");

    assert(out != NULL);

    assert(db_error_set(&error, DB_ERROR, "row too large for page.") == DB_ERROR);

    db_error_print(out, &error);
    fclose(out);

    assert(strcmp(buffer, "Error: row too large for page.\n") == 0);
}

static void test_error_print_ignores_ok(void) {
    DBError error;
    char buffer[128] = "";
    FILE *out = fmemopen(buffer, sizeof(buffer), "w");

    assert(out != NULL);

    db_error_clear(&error);
    db_error_print(out, &error);
    fclose(out);

    assert(strcmp(buffer, "") == 0);
}

static void test_error_rejects_null_inputs(void) {
    db_error_clear(NULL);
    assert(db_error_set(NULL, DB_ERROR, "ignored") == DB_ERROR);
    assert(db_error_set_status(NULL, DB_ERROR) == DB_ERROR);
    db_error_print(NULL, NULL);
}

int main(void) {
    test_error_clear();
    test_error_set_formatted_message();
    test_error_set_status_uses_default_message();
    test_status_default_message();
    test_error_print();
    test_error_print_ignores_ok();
    test_error_rejects_null_inputs();

    printf("All error tests passed.\n");

    return 0;
}
