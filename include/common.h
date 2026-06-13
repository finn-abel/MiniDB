#ifndef SIMPLEDB_COMMON_H
#define SIMPLEDB_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Core database constants.
 *
 * PAGE_SIZE:
 *   The size of one disk page in bytes.
 *   Later, table files will be split into fixed-size 4096-byte pages.
 *
 * MAX_TABLE_NAME:
 *   Maximum length for a table name, not including the null terminator.
 *
 * MAX_COLUMN_NAME:
 *   Maximum length for a column name, not including the null terminator.
 *
 * MAX_COLUMNS:
 *   Maximum number of columns allowed in a table.
 *
 * MAX_INDEX_NAME:
 *   Maximum length for an index name, not including the null terminator.
 */
#define PAGE_SIZE 4096
/*
 * Limits used throughout the database.
 */
#define MAX_TABLE_NAME 64
#define MAX_COLUMN_NAME 64
#define MAX_INDEX_NAME 64
#define MAX_COLUMNS 32

/*
 * Max database path length.
 *
 * For now, MiniDB "opens" a DB by opening or creating a folder.
 */
#define MAX_DB_PATH 256

/*
 * Maximum length of one input line in the shell.
 *
 * This is only for the CLI right now.
 * Later, the parser will consume this input.
 */
#define MAX_INPUT_SIZE 1024

/*
 * Object names become file names, so accept only the SQL identifier shape:
 * [A-Za-z_][A-Za-z0-9_]*.
 */
static inline bool db_identifier_is_valid(const char *name, size_t buffer_size) {
    if (name == NULL || buffer_size < 2 || name[0] == '\0') {
        return false;
    }

    char first = name[0];

    if (!(
        (first >= 'A' && first <= 'Z') ||
        (first >= 'a' && first <= 'z') ||
        first == '_'
    )) {
        return false;
    }

    size_t i = 1;

    for (; name[i] != '\0'; i++) {
        char current = name[i];

        if (i >= buffer_size - 1) {
            return false;
        }

        if (!(
            (current >= 'A' && current <= 'Z') ||
            (current >= 'a' && current <= 'z') ||
            (current >= '0' && current <= '9') ||
            current == '_'
        )) {
            return false;
        }
    }

    return i < buffer_size;
}

/*
 * DBStatus:
 *   A shared status code enum used across the database.
 *
 * Instead of every function returning random ints like -1, 0, or 1,
 * database functions can return a meaningful status.
 */
typedef enum {
    DB_OK = 0,
    DB_ERROR,
    DB_NOT_FOUND,
    DB_FULL,
    DB_TYPE_ERROR,
    DB_IO_ERROR,
    DB_PARSE_ERROR
} DBStatus;

#endif
