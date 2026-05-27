#include <stdlib.h>

#include "row.h"

DBStatus row_create(Row *row, uint16_t value_count) {
    if (row == NULL || value_count == 0) {
        return DB_ERROR;
    }

    row->value_count = value_count;

    /*
     * calloc gives us zero-initialized memory.
     *
     * This makes the values array predictable before we assign actual values.
     */
    row->values = calloc(value_count, sizeof(Value));
    if (row->values == NULL) {
        row->value_count = 0;
        return DB_ERROR;
    }

    return DB_OK;
}

void row_free(Row *row) {
    if (row == NULL) {
        return;
    }

    /*
     * Each Value may own memory.
     *
     * Currently, only VALUE_TEXT owns heap memory, but row_free should not
     * need to know those details. It delegates cleanup to value_free.
     */
    for (uint16_t i = 0; i < row->value_count; i++) {
        value_free(&row->values[i]);
    }

    free(row->values);

    row->values = NULL;
    row->value_count = 0;
}

Value *row_get_value(Row *row, uint16_t index) {
    if (row == NULL || index >= row->value_count) {
        return NULL;
    }

    return &row->values[index];
}

const Value *row_get_value_const(const Row *row, uint16_t index) {
    if (row == NULL || index >= row->value_count) {
        return NULL;
    }

    return &row->values[index];
}

void row_print(const Row *row, FILE *out) {
    if (row == NULL || out == NULL) {
        return;
    }

    fprintf(out, "[");

    for (uint16_t i = 0; i < row->value_count; i++) {
        value_print(&row->values[i], out);

        if (i + 1 < row->value_count) {
            fprintf(out, ", ");
        }
    }

    fprintf(out, "]");
}
