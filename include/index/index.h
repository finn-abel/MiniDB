#ifndef MINIDB_INDEX_INDEX_H
#define MINIDB_INDEX_INDEX_H

#include <stddef.h>

#include "common.h"
#include "rid.h"
#include "value.h"

/*
 * IndexType identifies the concrete index implementation.
 *
 * Step 27 starts with a primary-key index over INT columns. Later steps can
 * add sorted arrays and B+ trees without changing the public index calls.
 */
typedef enum {
    INDEX_TYPE_PRIMARY_INT
} IndexType;

typedef struct {
    Value key;
    RID rid;
} IndexEntry;

/*
 * Index owns an in-memory list of key -> RID entries.
 *
 * The initial implementation is intentionally simple: it supports primary
 * keys on INT columns and performs linear searches. Step 28 will replace the
 * entry layout with a sorted array and binary search.
 */
typedef struct {
    IndexType type;
    IndexEntry *entries;
    size_t count;
    size_t capacity;
} Index;

/*
 * Initializes an index.
 */
DBStatus index_init(Index *index, IndexType type);

/*
 * Frees all memory owned by the index.
 */
void index_free(Index *index);

/*
 * Inserts a key -> RID mapping.
 *
 * Primary-key indexes reject duplicate keys with DB_ERROR.
 */
DBStatus index_insert(Index *index, const Value *key, RID rid);

/*
 * Deletes a key from the index.
 */
DBStatus index_delete(Index *index, const Value *key);

/*
 * Finds a key and writes the associated RID into out_rid.
 */
DBStatus index_find(const Index *index, const Value *key, RID *out_rid);

#endif
