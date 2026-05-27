#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "row.h"
#include "value.h"

DBStatus row_create(Row *row, uint16_t value_count) {
    if (row == NULL) {
        return DB_ERROR;
    }

    row->value_count = value_count;

    /*
     * Empty rows are valid.
     * They have zero values, so they do not need a values array.
     */
    if (value_count == 0) {
        row->values = NULL;
        return DB_OK;
    }

    /*
     * Allocate one Value slot for each value in the row.
     * calloc zeroes the memory, which gives each Value a safe starting state.
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

static uint32_t row_serialized_size(const Row *row) {
    /*
     * The serialized row always starts with the number of values.
     */
    uint32_t size = sizeof(uint16_t);

    for (uint16_t i = 0; i < row->value_count; i++) {
        const Value *value = &row->values[i];

        /*
         * Every value stores a 1-byte type tag first.
         * This tells deserialization whether the value is INT or TEXT.
         */
        size += sizeof(uint8_t);

        if (value->type == VALUE_INT) {
            /*
             * INT values store one int32_t after the type tag.
             */
            size += sizeof(int32_t);
        } else if (value->type == VALUE_TEXT) {
            /*
             * TEXT values store their length first, then the raw text bytes.
             * We do not store the null terminator in the serialized format.
             */
            size += sizeof(uint32_t);
            size += (uint32_t)strlen(value->text_value);
        }
    }

    return size;
}

DBStatus row_serialize(Row *row, uint8_t **out_bytes, uint32_t *out_len) {
    if (row == NULL || out_bytes == NULL || out_len == NULL) {
        return DB_ERROR;
    }

    if (row->values == NULL && row->value_count > 0) {
        return DB_ERROR;
    }

    /*
     * First calculate exactly how many bytes the row needs.
     * This lets us allocate one correctly sized buffer.
     */
    uint32_t total_size = row_serialized_size(row);
    uint8_t *bytes = malloc(total_size);

    if (bytes == NULL) {
        return DB_ERROR;
    }

    /*
     * offset tracks where the next field should be written.
     */
    uint32_t offset = 0;

    /*
     * Write the number of values first.
     * Deserialization needs this to know how many values to read.
     */
    memcpy(bytes + offset, &row->value_count, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    for (uint16_t i = 0; i < row->value_count; i++) {
        Value *value = &row->values[i];

        /*
         * Store the type tag before the actual value.
         * This keeps the format self-describing.
         */
        uint8_t type_tag = (uint8_t)value->type;

        memcpy(bytes + offset, &type_tag, sizeof(uint8_t));
        offset += sizeof(uint8_t);

        if (value->type == VALUE_INT) {
            /*
             * INT format:
             * [type tag: uint8_t][value: int32_t]
             */
            memcpy(bytes + offset, &value->int_value, sizeof(int32_t));
            offset += sizeof(int32_t);
        } else if (value->type == VALUE_TEXT) {
            if (value->text_value == NULL) {
                free(bytes);
                return DB_ERROR;
            }

            /*
             * TEXT format:
             * [type tag: uint8_t][length: uint32_t][text bytes]
             */
            uint32_t text_len = (uint32_t)strlen(value->text_value);

            memcpy(bytes + offset, &text_len, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            /*
             * Only copy the text characters.
             * The null terminator is not stored on disk/in bytes.
             */
            memcpy(bytes + offset, value->text_value, text_len);
            offset += text_len;
        } else {
            free(bytes);
            return DB_TYPE_ERROR;
        }
    }

    /*
     * Give the finished byte buffer back to the caller.
     * The caller is responsible for calling free on out_bytes.
     */
    *out_bytes = bytes;
    *out_len = total_size;

    return DB_OK;
}

DBStatus row_deserialize(uint8_t *bytes, uint32_t len, Row *out_row) {
    if (bytes == NULL || out_row == NULL) {
        return DB_ERROR;
    }

    uint32_t offset = 0;

    /*
     * A valid serialized row must at least contain the value count.
     */
    if (len < sizeof(uint16_t)) {
        return DB_ERROR;
    }

    uint16_t value_count = 0;

    /*
     * Read the number of values.
     * This tells us how many Value slots to allocate.
     */
    memcpy(&value_count, bytes + offset, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    if (row_create(out_row, value_count) != DB_OK) {
        return DB_ERROR;
    }

    for (uint16_t i = 0; i < value_count; i++) {
        /*
         * Before reading anything, make sure the type tag exists.
         */
        if (offset + sizeof(uint8_t) > len) {
            row_free(out_row);
            return DB_ERROR;
        }

        uint8_t type_tag = 0;

        /*
         * The type tag decides what kind of value comes next.
         */
        memcpy(&type_tag, bytes + offset, sizeof(uint8_t));
        offset += sizeof(uint8_t);

        if (type_tag == VALUE_INT) {
            /*
             * INT values need exactly sizeof(int32_t) more bytes.
             */
            if (offset + sizeof(int32_t) > len) {
                row_free(out_row);
                return DB_ERROR;
            }

            int32_t int_value = 0;

            memcpy(&int_value, bytes + offset, sizeof(int32_t));
            offset += sizeof(int32_t);

            out_row->values[i] = value_int(int_value);
        } else if (type_tag == VALUE_TEXT) {
            /*
             * TEXT values first store a uint32_t length.
             */
            if (offset + sizeof(uint32_t) > len) {
                row_free(out_row);
                return DB_ERROR;
            }

            uint32_t text_len = 0;

            memcpy(&text_len, bytes + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            /*
             * Make sure the claimed text length actually fits in the buffer.
             */
            if (offset + text_len > len) {
                row_free(out_row);
                return DB_ERROR;
            }

            /*
             * Allocate a temporary C string.
             * The serialized format does not include the null terminator,
             * so we add one here.
             */
            char *text = malloc(text_len + 1);

            if (text == NULL) {
                row_free(out_row);
                return DB_ERROR;
            }

            memcpy(text, bytes + offset, text_len);
            text[text_len] = '\0';
            offset += text_len;

            /*
             * value_text makes its own copy of the string.
             * That means this temporary buffer can be freed afterward.
             */
            DBStatus status = value_text(&out_row->values[i], text);

            free(text);

            if (status != DB_OK) {
                row_free(out_row);
                return status;
            }
        } else {
            /*
             * Unknown type tags are invalid.
             */
            row_free(out_row);
            return DB_TYPE_ERROR;
        }
    }

    /*
     * If bytes remain unread, the buffer does not match our expected format.
     */
    if (offset != len) {
        row_free(out_row);
        return DB_ERROR;
    }

    return DB_OK;
}
