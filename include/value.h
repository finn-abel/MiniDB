#ifndef MINIDB_VALUE_H
#define MINIDB_VALUE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "common.h"

/*
 * ValueType identifies what kind of data a Value currently stores.
 *
 * For now, MiniDB only supports:
 * - VALUE_INT
 * - VALUE_TEXT
 *
 * More types can be added later, such as:
 * - VALUE_BOOL
 * - VALUE_FLOAT
 * - VALUE_NULL
 * - VALUE_DATE
 */
typedef enum {
    VALUE_INT,
    VALUE_TEXT
} ValueType;

/*
 * Value represents one individual database value.
 *
 * Examples:
 *   1       -> VALUE_INT
 *   "Finn"  -> VALUE_TEXT
 *
 * The union means a Value stores either an int_value or a text_value,
 * but not both at the same time.
 */
typedef struct {
    ValueType type;

    union {
        int32_t int_value;
        char *text_value;
    };
} Value;

/*
 * Creates an integer value.
 */
Value value_int(int32_t value);

/*
 * Creates a text value.
 *
 * This function copies the input string into heap memory.
 * That means the caller can safely pass a temporary string or string literal.
 */
DBStatus value_text(Value *out, const char *text);

/*
 * Frees any memory owned by the value.
 *
 * Important:
 * - INT values own no heap memory.
 * - TEXT values own heap memory and must be freed.
 */
void value_free(Value *value);

/*
 * Prints a value to the given output stream.
 *
 * Example:
 *   VALUE_INT  20      prints as 20
 *   VALUE_TEXT "Finn"  prints as "Finn"
 */
void value_print(const Value *value, FILE *out);

/*
 * Compares two values.
 *
 * Returns:
 *   -1 if left < right
 *    0 if left == right
 *    1 if left > right
 *
 * If the two values have different types, returns DB_TYPE_ERROR.
 *
 * The comparison result is written into out_result.
 */
DBStatus value_compare(const Value *left, const Value *right, int *out_result);

#endif
