#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "util/error.h"

void db_error_clear(DBError *error) {
    if (error == NULL) {
        return;
    }

    /*
     * An empty message plus DB_OK means "nothing to print".
     */
    error->status = DB_OK;
    error->message[0] = '\0';
}

const char *db_status_default_message(DBStatus status) {
    /*
     * These messages are intentionally generic. Callers with more context
     * should use db_error_set to provide names like table/column identifiers.
     */
    switch (status) {
        case DB_OK:
            return "";
        case DB_NOT_FOUND:
            return "object does not exist.";
        case DB_FULL:
            return "database object is full.";
        case DB_TYPE_ERROR:
            return "type mismatch.";
        case DB_IO_ERROR:
            return "database file could not be opened.";
        case DB_PARSE_ERROR:
            return "syntax error.";
        case DB_ERROR:
        default:
            return "operation failed.";
    }
}

DBStatus db_error_set(
    DBError *error,
    DBStatus status,
    const char *format,
    ...
) {
    if (error == NULL) {
        return status;
    }

    error->status = status;

    if (format == NULL) {
        const char *message = db_status_default_message(status);

        /*
         * Copy through snprintf so the message is always null-terminated.
         */
        snprintf(error->message, sizeof(error->message), "%s", message);
        return status;
    }

    va_list args;

    /*
     * Format into the fixed-size message buffer. vsnprintf truncates safely
     * and always leaves room for a null terminator.
     */
    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);

    return status;
}

DBStatus db_error_set_status(DBError *error, DBStatus status) {
    return db_error_set(error, status, NULL);
}

void db_error_print(FILE *out, const DBError *error) {
    if (out == NULL || error == NULL || error->status == DB_OK) {
        return;
    }

    /*
     * If a caller only filled status, still print a useful default message.
     */
    if (error->message[0] == '\0') {
        fprintf(out, "Error: %s\n", db_status_default_message(error->status));
        return;
    }

    fprintf(out, "Error: %s\n", error->message);
}
