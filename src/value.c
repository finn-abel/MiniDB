#include <stdlib.h>
#include <string.h>

#include "value.h"

Value value_int(int32_t value) {
    Value result;

    // Store the integer value directly inside the Value struct.
    result.type = VALUE_INT;
    result.int_value = value;

    return result;
}

DBStatus value_text(Value *out, const char *text) {
    if (out == NULL || text == NULL) {
        return DB_ERROR;
    }

    size_t len = strlen(text);

    // Copy the text so the Value owns its own string.
    char *copy = malloc(len + 1);
    if (copy == NULL) {
        return DB_ERROR;
    }

    memcpy(copy, text, len + 1);

    out->type = VALUE_TEXT;
    out->text_value = copy;

    return DB_OK;
}

void value_free(Value *value) {
    if (value == NULL) {
        return;
    }

    // Only text values own heap memory.
    if (value->type == VALUE_TEXT) {
        free(value->text_value);
        value->text_value = NULL;
    }
}

void value_print(const Value *value, FILE *out) {
    if (value == NULL || out == NULL) {
        return;
    }

    // Print based on the active value type.
    switch (value->type) {
        case VALUE_INT:
            fprintf(out, "%d", value->int_value);
            break;

        case VALUE_TEXT:
            fprintf(out, "\"%s\"", value->text_value);
            break;
    }
}

DBStatus value_compare(const Value *left, const Value *right, int *out_result) {
    if (left == NULL || right == NULL || out_result == NULL) {
        return DB_ERROR;
    }

    // For now, MiniDB only compares values of the same type.
    if (left->type != right->type) {
        return DB_TYPE_ERROR;
    }

    switch (left->type) {
        case VALUE_INT:
            if (left->int_value < right->int_value) {
                *out_result = -1;
            } else if (left->int_value > right->int_value) {
                *out_result = 1;
            } else {
                *out_result = 0;
            }

            return DB_OK;

        case VALUE_TEXT: {
            int cmp = strcmp(left->text_value, right->text_value);

            if (cmp < 0) {
                *out_result = -1;
            } else if (cmp > 0) {
                *out_result = 1;
            } else {
                *out_result = 0;
            }

            return DB_OK;
        }
    }

    return DB_ERROR;
}
