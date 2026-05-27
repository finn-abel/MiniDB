#ifndef MINIDB_ROW_H
#define MINIDB_ROW_H

#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "value.h"

/*
 * Row represents one in-memory database row.
 *
 * A row is just an ordered list of values.
 *
 * Example:
 *   [1, "Finn", 20]
 */
typedef struct {
    uint16_t value_count;
    Value *values;
} Row;

/*
 * Creates a row with space for value_count values.
 *
 * The row owns the values array and must later be freed with row_free.
 */
DBStatus row_create(Row *row, uint16_t value_count);

/*
 * Frees all values inside the row, then frees the values array.
 */
void row_free(Row *row);

/*
 * Returns a pointer to the value at index.
 *
 * Returns NULL if the row is NULL or the index is out of bounds.
 */
Value *row_get_value(Row *row, uint16_t index);

/*
 * Const version of row_get_value.
 *
 * Useful when you only want to read the value, not modify it.
 */
const Value *row_get_value_const(const Row *row, uint16_t index);

/*
 * Prints a row in a readable format.
 *
 * Example:
 *   [1, "Finn", 20]
 */
void row_print(const Row *row, FILE *out);

#endif
