#include <stdlib.h>

#include "index/index.h"

#define INDEX_INITIAL_CAPACITY 8

/*
 * Step 27 supports only primary-key indexes over INT values.
 * Keep that restriction centralized so later index types can relax it in one
 * place instead of spreading type checks across every operation.
 */
static DBStatus index_validate_int_key(const Index *index, const Value *key) {
    if (index == NULL || key == NULL) {
        return DB_ERROR;
    }

    if (index->type != INDEX_TYPE_PRIMARY_INT) {
        return DB_ERROR;
    }

    if (key->type != VALUE_INT) {
        return DB_TYPE_ERROR;
    }

    return DB_OK;
}

static DBStatus index_find_position(
    const Index *index,
    const Value *key,
    size_t *out_position
) {
    DBStatus status = index_validate_int_key(index, key);

    if (status != DB_OK) {
        return status;
    }

    if (out_position == NULL) {
        return DB_ERROR;
    }

    /*
     * The first index implementation is intentionally unsorted and in-memory.
     * Step 28 will replace this linear scan with binary search over sorted
     * entries while preserving the public index_find API.
     */
    for (size_t i = 0; i < index->count; i++) {
        if (index->entries[i].key.int_value == key->int_value) {
            *out_position = i;
            return DB_OK;
        }
    }

    return DB_NOT_FOUND;
}

static DBStatus index_grow(Index *index) {
    // Grow lazily so an empty index does not allocate until the first insert.
    size_t next_capacity = index->capacity == 0
        ? INDEX_INITIAL_CAPACITY
        : index->capacity * 2;

    IndexEntry *next_entries = realloc(
        index->entries,
        next_capacity * sizeof(IndexEntry)
    );

    if (next_entries == NULL) {
        return DB_ERROR;
    }

    index->entries = next_entries;
    index->capacity = next_capacity;

    return DB_OK;
}

DBStatus index_init(Index *index, IndexType type) {
    if (index == NULL) {
        return DB_ERROR;
    }

    // Other concrete index types will be added behind this abstraction later.
    if (type != INDEX_TYPE_PRIMARY_INT) {
        return DB_ERROR;
    }

    index->type = type;
    index->entries = NULL;
    index->count = 0;
    index->capacity = 0;

    return DB_OK;
}

void index_free(Index *index) {
    if (index == NULL) {
        return;
    }

    // Index entries own their keys, even though INT keys do not allocate today.
    for (size_t i = 0; i < index->count; i++) {
        value_free(&index->entries[i].key);
    }

    free(index->entries);

    index->entries = NULL;
    index->count = 0;
    index->capacity = 0;
}

DBStatus index_insert(Index *index, const Value *key, RID rid) {
    DBStatus status = index_validate_int_key(index, key);

    if (status != DB_OK) {
        return status;
    }

    size_t existing_position = 0;
    status = index_find_position(index, key, &existing_position);

    // Primary-key indexes enforce uniqueness by rejecting existing keys.
    if (status == DB_OK) {
        return DB_ERROR;
    }

    if (status != DB_NOT_FOUND) {
        return status;
    }

    if (index->count == index->capacity) {
        status = index_grow(index);

        if (status != DB_OK) {
            return status;
        }
    }

    // Copy the key into the index so callers keep ownership of their Value.
    index->entries[index->count].key = value_int(key->int_value);
    index->entries[index->count].rid = rid;
    index->count++;

    return DB_OK;
}

DBStatus index_delete(Index *index, const Value *key) {
    size_t position = 0;
    DBStatus status = index_find_position(index, key, &position);

    if (status != DB_OK) {
        return status;
    }

    value_free(&index->entries[position].key);

    // Keep the array dense after deletion by shifting later entries left.
    for (size_t i = position + 1; i < index->count; i++) {
        index->entries[i - 1] = index->entries[i];
    }

    index->count--;

    return DB_OK;
}

DBStatus index_find(const Index *index, const Value *key, RID *out_rid) {
    size_t position = 0;

    if (out_rid == NULL) {
        return DB_ERROR;
    }

    DBStatus status = index_find_position(index, key, &position);

    if (status != DB_OK) {
        return status;
    }

    *out_rid = index->entries[position].rid;

    return DB_OK;
}
