#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "row.h"
#include "value.h"

static void test_serialize_deserialize_mixed_row(void) {
    Row row;

    assert(row_create(&row, 3) == DB_OK);

    row.values[0] = value_int(1);
    assert(value_text(&row.values[1], "Finn") == DB_OK);
    row.values[2] = value_int(20);

    uint8_t *bytes = NULL;
    uint32_t len = 0;

    assert(row_serialize(&row, &bytes, &len) == DB_OK);
    assert(bytes != NULL);
    assert(len > 0);

    Row copy;

    assert(row_deserialize(bytes, len, &copy) == DB_OK);

    assert(copy.value_count == 3);

    assert(copy.values[0].type == VALUE_INT);
    assert(copy.values[0].int_value == 1);

    assert(copy.values[1].type == VALUE_TEXT);
    assert(strcmp(copy.values[1].text_value, "Finn") == 0);

    assert(copy.values[2].type == VALUE_INT);
    assert(copy.values[2].int_value == 20);

    row_free(&row);
    row_free(&copy);
    free(bytes);
}

static void test_serialize_deserialize_empty_row(void) {
    Row row;

    assert(row_create(&row, 0) == DB_OK);

    uint8_t *bytes = NULL;
    uint32_t len = 0;

    assert(row_serialize(&row, &bytes, &len) == DB_OK);
    assert(bytes != NULL);
    assert(len == sizeof(uint16_t));

    Row copy;

    assert(row_deserialize(bytes, len, &copy) == DB_OK);
    assert(copy.value_count == 0);

    row_free(&row);
    row_free(&copy);
    free(bytes);
}

static void test_deserialize_rejects_truncated_data(void) {
    uint8_t bad_bytes[] = {
        1, 0,
        VALUE_INT
    };

    Row row;

    assert(row_deserialize(bad_bytes, sizeof(bad_bytes), &row) == DB_ERROR);
}

static void test_deserialize_rejects_overflowing_text_length(void) {
    uint8_t bad_bytes[sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint32_t)];
    uint16_t value_count = 1;
    uint8_t type = VALUE_TEXT;
    uint32_t text_len = UINT32_MAX;
    size_t offset = 0;

    memcpy(bad_bytes + offset, &value_count, sizeof(value_count));
    offset += sizeof(value_count);
    memcpy(bad_bytes + offset, &type, sizeof(type));
    offset += sizeof(type);
    memcpy(bad_bytes + offset, &text_len, sizeof(text_len));

    Row row;
    assert(row_deserialize(bad_bytes, sizeof(bad_bytes), &row) == DB_ERROR);
}

static void test_serialize_rejects_null_text(void) {
    Row row;
    uint8_t *bytes = NULL;
    uint32_t len = 0;

    assert(row_create(&row, 1) == DB_OK);
    row.values[0].type = VALUE_TEXT;
    row.values[0].text_value = NULL;

    assert(row_serialize(&row, &bytes, &len) == DB_ERROR);
    assert(bytes == NULL);
    row_free(&row);
}

static void test_serialize_rejects_unknown_value_type(void) {
    Row row;
    uint8_t *bytes = NULL;
    uint32_t len = 0;

    assert(row_create(&row, 1) == DB_OK);
    row.values[0].type = (ValueType)99;

    assert(row_serialize(&row, &bytes, &len) == DB_TYPE_ERROR);
    assert(bytes == NULL);
    row_free(&row);
}

int main(void) {
    test_serialize_deserialize_mixed_row();
    test_serialize_deserialize_empty_row();
    test_deserialize_rejects_truncated_data();
    test_deserialize_rejects_overflowing_text_length();
    test_serialize_rejects_null_text();
    test_serialize_rejects_unknown_value_type();

    printf("All row serialization tests passed.\n");

    return 0;
}
