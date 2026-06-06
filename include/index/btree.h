#ifndef MINIDB_INDEX_BTREE_H
#define MINIDB_INDEX_BTREE_H

#include <stdbool.h>
#include <stdint.h>

#include "common.h"
#include "rid.h"

#define BTREE_ROOT_PAGE_ID 0
#define BTREE_INVALID_PAGE_ID UINT32_MAX

/*
 * These capacities are deliberately small for the first persistent B+ tree.
 * They fit comfortably inside one 4096-byte page and make split behavior easy
 * to exercise in tests before the implementation is tuned for page density.
 */
#define BTREE_LEAF_MAX_ENTRIES 32
#define BTREE_INTERNAL_MAX_KEYS 32

typedef struct {
    char file_path[MAX_DB_PATH];
    bool is_open;
} BTree;

/*
 * Opens an index file. If the file is empty, page 0 is created as a leaf root.
 */
DBStatus btree_open(BTree *tree, const char *file_path);

/*
 * Flushes dirty B+ tree pages.
 */
DBStatus btree_close(BTree *tree);

/*
 * Searches for an INT key and returns its RID.
 */
DBStatus btree_search(BTree *tree, int32_t key, RID *out_rid);

/*
 * Inserts an INT key -> RID mapping.
 *
 * Duplicate keys are rejected with DB_ERROR because this index is intended for
 * primary-key usage.
 */
DBStatus btree_insert(BTree *tree, int32_t key, RID rid);

/*
 * Deletes an INT key from the tree.
 *
 * This first delete implementation removes leaf entries without rebalancing.
 */
DBStatus btree_delete(BTree *tree, int32_t key);

/*
 * Splits a full leaf page. These are exposed for focused storage tests, while
 * normal callers should use btree_insert.
 */
DBStatus btree_split_leaf(
    BTree *tree,
    uint32_t leaf_page_id,
    int32_t *out_separator_key,
    uint32_t *out_right_page_id
);

/*
 * Splits a full internal page. Delete/rebalancing is intentionally left for a
 * later step.
 */
DBStatus btree_split_internal(
    BTree *tree,
    uint32_t internal_page_id,
    int32_t *out_separator_key,
    uint32_t *out_right_page_id
);

#endif
