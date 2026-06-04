#ifndef MINIDB_UTIL_ERROR_H
#define MINIDB_UTIL_ERROR_H

#include <stdio.h>

#include "common.h"

/*
 * Maximum size of a user-facing error message, including the null terminator.
 *
 * Error text is intentionally bounded so callers can keep DBError structs on
 * the stack without dynamic allocation.
 */
#define DB_ERROR_MESSAGE_MAX 256

/*
 * DBError pairs the existing status code with readable text.
 *
 * Lower layers can keep returning DBStatus, while shell-facing code can attach
 * a message when it has enough context to explain the failure.
 */
typedef struct {
    DBStatus status;
    char message[DB_ERROR_MESSAGE_MAX];
} DBError;

/*
 * Resets an error object to the no-error state.
 */
void db_error_clear(DBError *error);

/*
 * Stores a formatted error message and returns status.
 *
 * Returning status lets callers write:
 *   return db_error_set(error, DB_NOT_FOUND, "table '%s' does not exist.", name);
 */
DBStatus db_error_set(
    DBError *error,
    DBStatus status,
    const char *format,
    ...
);

/*
 * Stores the default message for a status code.
 */
DBStatus db_error_set_status(DBError *error, DBStatus status);

/*
 * Returns a generic readable message for a DBStatus.
 */
const char *db_status_default_message(DBStatus status);

/*
 * Prints a DBError as:
 *   Error: message
 */
void db_error_print(FILE *out, const DBError *error);

#endif
