#include <string.h>

#include "buffer/buffer_pool.h"
#include "index/btree.h"
#include "page.h"

/*
 * B+ tree pages use a tiny custom layout inside normal PAGE_SIZE buffers:
 *
 *   byte 0      node type: leaf or internal
 *   bytes 2-3   number of keys
 *   bytes 4-7   next leaf page id, used only by leaves
 *
 * Leaf pages then store fixed-width key/RID entries. Internal pages store all
 * child page IDs first, followed by separator keys. Accessors below use memcpy
 * rather than pointer casts so the on-disk format does not depend on alignment.
 */
#define BTREE_NODE_HEADER_SIZE 8
#define BTREE_NODE_TYPE_OFFSET 0
#define BTREE_KEY_COUNT_OFFSET 2
#define BTREE_NEXT_LEAF_OFFSET 4

#define BTREE_LEAF_ENTRY_SIZE 12
#define BTREE_LEAF_KEY_OFFSET 0
#define BTREE_LEAF_RID_PAGE_OFFSET 4
#define BTREE_LEAF_RID_SLOT_OFFSET 8

#define BTREE_INTERNAL_CHILDREN_OFFSET BTREE_NODE_HEADER_SIZE
#define BTREE_INTERNAL_KEYS_OFFSET \
    (BTREE_INTERNAL_CHILDREN_OFFSET + \
     (BTREE_INTERNAL_MAX_KEYS + 1) * sizeof(uint32_t))

typedef struct {
    /*
     * Recursive insert bubbles this up one level when a child split creates a
     * new right sibling. The separator key is the first key that belongs to
     * that right subtree.
     */
    bool did_split;
    int32_t separator_key;
    uint32_t right_page_id;
} BTreeSplitResult;

static void btree_write_u8(uint8_t *page, uint16_t offset, uint8_t value) {
    memcpy(page + offset, &value, sizeof(value));
}

static uint8_t btree_read_u8(const uint8_t *page, uint16_t offset) {
    uint8_t value = 0;
    memcpy(&value, page + offset, sizeof(value));
    return value;
}

static void btree_write_u16(uint8_t *page, uint16_t offset, uint16_t value) {
    memcpy(page + offset, &value, sizeof(value));
}

static uint16_t btree_read_u16(const uint8_t *page, uint16_t offset) {
    uint16_t value = 0;
    memcpy(&value, page + offset, sizeof(value));
    return value;
}

static void btree_write_u32(uint8_t *page, uint16_t offset, uint32_t value) {
    memcpy(page + offset, &value, sizeof(value));
}

static uint32_t btree_read_u32(const uint8_t *page, uint16_t offset) {
    uint32_t value = 0;
    memcpy(&value, page + offset, sizeof(value));
    return value;
}

static void btree_write_i32(uint8_t *page, uint16_t offset, int32_t value) {
    memcpy(page + offset, &value, sizeof(value));
}

static int32_t btree_read_i32(const uint8_t *page, uint16_t offset) {
    int32_t value = 0;
    memcpy(&value, page + offset, sizeof(value));
    return value;
}

static bool btree_is_open(const BTree *tree) {
    return tree != NULL && tree->is_open;
}

static uint16_t btree_node_key_count(const uint8_t *page) {
    return btree_read_u16(page, BTREE_KEY_COUNT_OFFSET);
}

static void btree_set_node_key_count(uint8_t *page, uint16_t key_count) {
    btree_write_u16(page, BTREE_KEY_COUNT_OFFSET, key_count);
}

static void btree_init_leaf_page(uint8_t *page) {
    /*
     * Leaves form a right-linked list. Range scans do not use it yet, but
     * keeping the pointer now makes the page format ready for them.
     */
    memset(page, 0, PAGE_SIZE);
    btree_write_u8(page, BTREE_NODE_TYPE_OFFSET, PAGE_TYPE_BTREE_LEAF);
    btree_set_node_key_count(page, 0);
    btree_write_u32(page, BTREE_NEXT_LEAF_OFFSET, BTREE_INVALID_PAGE_ID);
}

static void btree_init_internal_page(uint8_t *page) {
    memset(page, 0, PAGE_SIZE);
    btree_write_u8(page, BTREE_NODE_TYPE_OFFSET, PAGE_TYPE_BTREE_INTERNAL);
    btree_set_node_key_count(page, 0);
}

static uint16_t btree_leaf_entry_offset(uint16_t entry_index) {
    return (uint16_t)(BTREE_NODE_HEADER_SIZE + entry_index * BTREE_LEAF_ENTRY_SIZE);
}

static int32_t btree_leaf_key(const uint8_t *page, uint16_t entry_index) {
    return btree_read_i32(
        page,
        (uint16_t)(btree_leaf_entry_offset(entry_index) + BTREE_LEAF_KEY_OFFSET)
    );
}

static RID btree_leaf_rid(const uint8_t *page, uint16_t entry_index) {
    uint16_t offset = btree_leaf_entry_offset(entry_index);
    RID rid;

    rid.page_id = btree_read_u32(
        page,
        (uint16_t)(offset + BTREE_LEAF_RID_PAGE_OFFSET)
    );
    rid.slot_id = btree_read_u16(
        page,
        (uint16_t)(offset + BTREE_LEAF_RID_SLOT_OFFSET)
    );

    return rid;
}

static void btree_write_leaf_entry(
    uint8_t *page,
    uint16_t entry_index,
    int32_t key,
    RID rid
) {
    uint16_t offset = btree_leaf_entry_offset(entry_index);

    btree_write_i32(page, (uint16_t)(offset + BTREE_LEAF_KEY_OFFSET), key);
    btree_write_u32(page, (uint16_t)(offset + BTREE_LEAF_RID_PAGE_OFFSET), rid.page_id);
    btree_write_u16(page, (uint16_t)(offset + BTREE_LEAF_RID_SLOT_OFFSET), rid.slot_id);
}

static int32_t btree_internal_key(const uint8_t *page, uint16_t key_index) {
    return btree_read_i32(
        page,
        (uint16_t)(BTREE_INTERNAL_KEYS_OFFSET + key_index * sizeof(int32_t))
    );
}

static void btree_write_internal_key(
    uint8_t *page,
    uint16_t key_index,
    int32_t key
) {
    btree_write_i32(
        page,
        (uint16_t)(BTREE_INTERNAL_KEYS_OFFSET + key_index * sizeof(int32_t)),
        key
    );
}

static uint32_t btree_internal_child(const uint8_t *page, uint16_t child_index) {
    return btree_read_u32(
        page,
        (uint16_t)(BTREE_INTERNAL_CHILDREN_OFFSET + child_index * sizeof(uint32_t))
    );
}

static void btree_write_internal_child(
    uint8_t *page,
    uint16_t child_index,
    uint32_t page_id
) {
    btree_write_u32(
        page,
        (uint16_t)(BTREE_INTERNAL_CHILDREN_OFFSET + child_index * sizeof(uint32_t)),
        page_id
    );
}

static uint16_t btree_leaf_lower_bound(const uint8_t *page, int32_t key) {
    /*
     * Leaf keys are sorted. Lower-bound gives either the matching key location
     * or the insertion slot where the key should be written.
     */
    uint16_t left = 0;
    uint16_t right = btree_node_key_count(page);

    while (left < right) {
        uint16_t mid = (uint16_t)(left + (right - left) / 2);

        if (btree_leaf_key(page, mid) < key) {
            left = (uint16_t)(mid + 1);
        } else {
            right = mid;
        }
    }

    return left;
}

static uint16_t btree_internal_child_index(const uint8_t *page, int32_t key) {
    /*
     * Internal keys are separators: keys greater than or equal to separator N
     * descend into child N + 1.
     */
    uint16_t left = 0;
    uint16_t right = btree_node_key_count(page);

    while (left < right) {
        uint16_t mid = (uint16_t)(left + (right - left) / 2);

        if (key >= btree_internal_key(page, mid)) {
            left = (uint16_t)(mid + 1);
        } else {
            right = mid;
        }
    }

    return left;
}

static void btree_write_leaf_entries(
    uint8_t *page,
    const int32_t *keys,
    const RID *rids,
    uint16_t count,
    uint32_t next_leaf
) {
    /*
     * Rewriting the compact fixed-width entries is simpler and less fragile
     * than shifting bytes in place, especially during split paths.
     */
    btree_init_leaf_page(page);
    btree_set_node_key_count(page, count);
    btree_write_u32(page, BTREE_NEXT_LEAF_OFFSET, next_leaf);

    for (uint16_t i = 0; i < count; i++) {
        btree_write_leaf_entry(page, i, keys[i], rids[i]);
    }
}

static void btree_write_internal_entries(
    uint8_t *page,
    const int32_t *keys,
    const uint32_t *children,
    uint16_t key_count
) {
    btree_init_internal_page(page);
    btree_set_node_key_count(page, key_count);

    for (uint16_t i = 0; i < key_count; i++) {
        btree_write_internal_key(page, i, keys[i]);
    }

    for (uint16_t i = 0; i <= key_count; i++) {
        btree_write_internal_child(page, i, children[i]);
    }
}

static DBStatus btree_leaf_insert(
    BTree *tree,
    uint32_t page_id,
    int32_t key,
    RID rid,
    BTreeSplitResult *split
) {
    uint8_t *page = NULL;
    DBStatus status = buffer_pool_fetch_page(tree->file_path, page_id, &page);

    if (status != DB_OK) {
        return status;
    }

    if (btree_read_u8(page, BTREE_NODE_TYPE_OFFSET) != PAGE_TYPE_BTREE_LEAF) {
        buffer_pool_unpin_page(tree->file_path, page_id, false);
        return DB_ERROR;
    }

    uint16_t count = btree_node_key_count(page);
    uint16_t position = btree_leaf_lower_bound(page, key);

    // This B+ tree is currently used as a primary-key index.
    if (position < count && btree_leaf_key(page, position) == key) {
        buffer_pool_unpin_page(tree->file_path, page_id, false);
        return DB_ERROR;
    }

    int32_t keys[BTREE_LEAF_MAX_ENTRIES + 1];
    RID rids[BTREE_LEAF_MAX_ENTRIES + 1];

    /*
     * Build a temporary sorted array containing the new entry. If it fits, we
     * write it back into the same leaf. If it overflows, the same array is
     * split across the old leaf and a newly allocated right leaf.
     */
    for (uint16_t i = 0; i < position; i++) {
        keys[i] = btree_leaf_key(page, i);
        rids[i] = btree_leaf_rid(page, i);
    }

    keys[position] = key;
    rids[position] = rid;

    for (uint16_t i = position; i < count; i++) {
        keys[i + 1] = btree_leaf_key(page, i);
        rids[i + 1] = btree_leaf_rid(page, i);
    }

    uint16_t total = (uint16_t)(count + 1);
    uint32_t next_leaf = btree_read_u32(page, BTREE_NEXT_LEAF_OFFSET);

    if (total <= BTREE_LEAF_MAX_ENTRIES) {
        btree_write_leaf_entries(
            page,
            keys,
            rids,
            total,
            next_leaf
        );
        status = buffer_pool_unpin_page(tree->file_path, page_id, true);
        split->did_split = false;
        return status;
    }

    uint32_t right_page_id = 0;
    uint8_t *right_page = NULL;

    status = buffer_pool_new_page(tree->file_path, &right_page_id, &right_page);

    if (status != DB_OK) {
        buffer_pool_unpin_page(tree->file_path, page_id, false);
        return status;
    }

    uint16_t left_count = (uint16_t)(total / 2);
    uint16_t right_count = (uint16_t)(total - left_count);

    /*
     * In a B+ tree, the promoted separator also remains in the right leaf.
     * That is why separator_key below is keys[left_count], not a removed key.
     */
    btree_write_leaf_entries(page, keys, rids, left_count, right_page_id);
    btree_write_leaf_entries(
        right_page,
        keys + left_count,
        rids + left_count,
        right_count,
        next_leaf
    );

    status = buffer_pool_unpin_page(tree->file_path, right_page_id, true);

    if (status != DB_OK) {
        buffer_pool_unpin_page(tree->file_path, page_id, true);
        return status;
    }

    status = buffer_pool_unpin_page(tree->file_path, page_id, true);

    if (status != DB_OK) {
        return status;
    }

    split->did_split = true;
    split->separator_key = keys[left_count];
    split->right_page_id = right_page_id;

    return DB_OK;
}

static DBStatus btree_internal_insert_split(
    BTree *tree,
    uint32_t page_id,
    const int32_t *keys,
    const uint32_t *children,
    uint16_t total_keys,
    BTreeSplitResult *split
) {
    uint32_t right_page_id = 0;
    uint8_t *right_page = NULL;
    DBStatus status = buffer_pool_new_page(tree->file_path, &right_page_id, &right_page);

    if (status != DB_OK) {
        return status;
    }

    uint16_t promote_index = (uint16_t)(total_keys / 2);
    uint16_t left_key_count = promote_index;
    uint16_t right_key_count = (uint16_t)(total_keys - promote_index - 1);

    /*
     * Internal-node splits differ from leaf splits: the promoted separator is
     * removed from both children and copied up into the parent.
     */
    uint8_t *left_page = NULL;
    status = buffer_pool_fetch_page(tree->file_path, page_id, &left_page);

    if (status != DB_OK) {
        buffer_pool_unpin_page(tree->file_path, right_page_id, false);
        return status;
    }

    btree_write_internal_entries(left_page, keys, children, left_key_count);
    btree_write_internal_entries(
        right_page,
        keys + promote_index + 1,
        children + promote_index + 1,
        right_key_count
    );

    status = buffer_pool_unpin_page(tree->file_path, right_page_id, true);

    if (status != DB_OK) {
        buffer_pool_unpin_page(tree->file_path, page_id, true);
        return status;
    }

    status = buffer_pool_unpin_page(tree->file_path, page_id, true);

    if (status != DB_OK) {
        return status;
    }

    split->did_split = true;
    split->separator_key = keys[promote_index];
    split->right_page_id = right_page_id;

    return DB_OK;
}

static DBStatus btree_insert_recursive(
    BTree *tree,
    uint32_t page_id,
    int32_t key,
    RID rid,
    BTreeSplitResult *split
) {
    uint8_t *page = NULL;
    DBStatus status = buffer_pool_fetch_page(tree->file_path, page_id, &page);

    if (status != DB_OK) {
        return status;
    }

    uint8_t node_type = btree_read_u8(page, BTREE_NODE_TYPE_OFFSET);

    if (node_type == PAGE_TYPE_BTREE_LEAF) {
        /*
         * btree_leaf_insert fetches the page mutably. Drop this read pin first
         * so buffer pool pin counts remain balanced.
         */
        status = buffer_pool_unpin_page(tree->file_path, page_id, false);

        if (status != DB_OK) {
            return status;
        }

        return btree_leaf_insert(tree, page_id, key, rid, split);
    }

    if (node_type != PAGE_TYPE_BTREE_INTERNAL) {
        buffer_pool_unpin_page(tree->file_path, page_id, false);
        return DB_ERROR;
    }

    uint16_t count = btree_node_key_count(page);
    uint16_t child_index = btree_internal_child_index(page, key);
    uint32_t child_page_id = btree_internal_child(page, child_index);

    status = buffer_pool_unpin_page(tree->file_path, page_id, false);

    if (status != DB_OK) {
        return status;
    }

    BTreeSplitResult child_split;
    child_split.did_split = false;
    child_split.separator_key = 0;
    child_split.right_page_id = BTREE_INVALID_PAGE_ID;

    status = btree_insert_recursive(tree, child_page_id, key, rid, &child_split);

    /*
     * If the child did not split, this internal page has nothing to absorb.
     */
    if (status != DB_OK || !child_split.did_split) {
        split->did_split = false;
        return status;
    }

    status = buffer_pool_fetch_page(tree->file_path, page_id, &page);

    if (status != DB_OK) {
        return status;
    }

    int32_t keys[BTREE_INTERNAL_MAX_KEYS + 1];
    uint32_t children[BTREE_INTERNAL_MAX_KEYS + 2];

    /*
     * Merge the child split into temporary internal arrays. The new separator
     * goes immediately after the original left child, followed by the newly
     * created right child page.
     */
    for (uint16_t i = 0; i < child_index; i++) {
        keys[i] = btree_internal_key(page, i);
        children[i] = btree_internal_child(page, i);
    }

    children[child_index] = btree_internal_child(page, child_index);
    keys[child_index] = child_split.separator_key;
    children[child_index + 1] = child_split.right_page_id;

    for (uint16_t i = child_index; i < count; i++) {
        keys[i + 1] = btree_internal_key(page, i);
        children[i + 2] = btree_internal_child(page, (uint16_t)(i + 1));
    }

    uint16_t total_keys = (uint16_t)(count + 1);

    if (total_keys <= BTREE_INTERNAL_MAX_KEYS) {
        btree_write_internal_entries(page, keys, children, total_keys);
        status = buffer_pool_unpin_page(tree->file_path, page_id, true);
        split->did_split = false;
        return status;
    }

    status = buffer_pool_unpin_page(tree->file_path, page_id, false);

    if (status != DB_OK) {
        return status;
    }

    return btree_internal_insert_split(
        tree,
        page_id,
        keys,
        children,
        total_keys,
        split
    );
}

static DBStatus btree_create_new_root(BTree *tree, const BTreeSplitResult *split) {
    uint8_t *root_page = NULL;
    DBStatus status = buffer_pool_fetch_page(
        tree->file_path,
        BTREE_ROOT_PAGE_ID,
        &root_page
    );

    if (status != DB_OK) {
        return status;
    }

    uint32_t left_page_id = 0;
    uint8_t *left_page = NULL;

    status = buffer_pool_new_page(tree->file_path, &left_page_id, &left_page);

    if (status != DB_OK) {
        buffer_pool_unpin_page(tree->file_path, BTREE_ROOT_PAGE_ID, false);
        return status;
    }

    /*
     * Page 0 is always the root. When the old root splits, copy its bytes into
     * a new left child page, then rewrite page 0 as a fresh internal root.
     * That avoids needing a separate metadata page to remember the root id.
     */
    memcpy(left_page, root_page, PAGE_SIZE);

    int32_t keys[1] = {split->separator_key};
    uint32_t children[2] = {left_page_id, split->right_page_id};

    btree_write_internal_entries(root_page, keys, children, 1);

    status = buffer_pool_unpin_page(tree->file_path, left_page_id, true);

    if (status != DB_OK) {
        buffer_pool_unpin_page(tree->file_path, BTREE_ROOT_PAGE_ID, true);
        return status;
    }

    return buffer_pool_unpin_page(tree->file_path, BTREE_ROOT_PAGE_ID, true);
}

DBStatus btree_open(BTree *tree, const char *file_path) {
    if (tree == NULL || file_path == NULL || strlen(file_path) >= MAX_DB_PATH) {
        return DB_ERROR;
    }

    memset(tree, 0, sizeof(BTree));
    strncpy(tree->file_path, file_path, MAX_DB_PATH - 1);
    tree->file_path[MAX_DB_PATH - 1] = '\0';

    uint32_t page_count = 0;
    DBStatus status = buffer_pool_page_count(tree->file_path, &page_count);

    if (status != DB_OK) {
        return status;
    }

    if (page_count == 0) {
        /*
         * Empty index files start with a single leaf root at page 0. Later root
         * splits preserve page 0 as the root by copying old root bytes away.
         */
        uint32_t root_page_id = BTREE_INVALID_PAGE_ID;
        uint8_t *root_page = NULL;

        status = buffer_pool_new_page(tree->file_path, &root_page_id, &root_page);

        if (status != DB_OK) {
            return status;
        }

        if (root_page_id != BTREE_ROOT_PAGE_ID) {
            buffer_pool_unpin_page(tree->file_path, root_page_id, false);
            return DB_ERROR;
        }

        btree_init_leaf_page(root_page);

        status = buffer_pool_unpin_page(tree->file_path, root_page_id, true);

        if (status != DB_OK) {
            return status;
        }
    }

    tree->is_open = true;

    return DB_OK;
}

DBStatus btree_close(BTree *tree) {
    if (!btree_is_open(tree)) {
        return DB_ERROR;
    }

    DBStatus status = buffer_pool_flush_all();

    tree->is_open = false;
    tree->file_path[0] = '\0';

    return status;
}

DBStatus btree_search(BTree *tree, int32_t key, RID *out_rid) {
    if (!btree_is_open(tree) || out_rid == NULL) {
        return DB_ERROR;
    }

    uint32_t page_id = BTREE_ROOT_PAGE_ID;

    /*
     * This is deliberately a simple primary-key maintenance delete: descend to
     * the leaf, remove the key, and leave internal separators/rebalancing alone.
     * Search remains correct because separators are routing hints to leaves.
     */
    while (true) {
        uint8_t *page = NULL;
        DBStatus status = buffer_pool_fetch_page(tree->file_path, page_id, &page);

        if (status != DB_OK) {
            return status;
        }

        uint8_t node_type = btree_read_u8(page, BTREE_NODE_TYPE_OFFSET);

        if (node_type == PAGE_TYPE_BTREE_LEAF) {
            // The traversal ends at a leaf; binary search verifies the key.
            uint16_t count = btree_node_key_count(page);
            uint16_t position = btree_leaf_lower_bound(page, key);

            if (position < count && btree_leaf_key(page, position) == key) {
                *out_rid = btree_leaf_rid(page, position);
                return buffer_pool_unpin_page(tree->file_path, page_id, false);
            }

            buffer_pool_unpin_page(tree->file_path, page_id, false);
            return DB_NOT_FOUND;
        }

        if (node_type != PAGE_TYPE_BTREE_INTERNAL) {
            buffer_pool_unpin_page(tree->file_path, page_id, false);
            return DB_ERROR;
        }

        uint16_t child_index = btree_internal_child_index(page, key);
        uint32_t child_page_id = btree_internal_child(page, child_index);

        // Release the parent before descending to keep pins short-lived.
        status = buffer_pool_unpin_page(tree->file_path, page_id, false);

        if (status != DB_OK) {
            return status;
        }

        page_id = child_page_id;
    }
}

DBStatus btree_insert(BTree *tree, int32_t key, RID rid) {
    if (!btree_is_open(tree)) {
        return DB_ERROR;
    }

    BTreeSplitResult split;
    split.did_split = false;
    split.separator_key = 0;
    split.right_page_id = BTREE_INVALID_PAGE_ID;

    DBStatus status = btree_insert_recursive(
        tree,
        BTREE_ROOT_PAGE_ID,
        key,
        rid,
        &split
    );

    if (status != DB_OK) {
        return status;
    }

    if (!split.did_split) {
        return DB_OK;
    }

    // A root split is the only split that cannot bubble to a parent.
    return btree_create_new_root(tree, &split);
}

DBStatus btree_delete(BTree *tree, int32_t key) {
    if (!btree_is_open(tree)) {
        return DB_ERROR;
    }

    uint32_t page_id = BTREE_ROOT_PAGE_ID;

    while (true) {
        uint8_t *page = NULL;
        DBStatus status = buffer_pool_fetch_page(tree->file_path, page_id, &page);

        if (status != DB_OK) {
            return status;
        }

        uint8_t node_type = btree_read_u8(page, BTREE_NODE_TYPE_OFFSET);

        if (node_type == PAGE_TYPE_BTREE_INTERNAL) {
            uint16_t child_index = btree_internal_child_index(page, key);
            uint32_t child_page_id = btree_internal_child(page, child_index);

            status = buffer_pool_unpin_page(tree->file_path, page_id, false);

            if (status != DB_OK) {
                return status;
            }

            page_id = child_page_id;
            continue;
        }

        if (node_type != PAGE_TYPE_BTREE_LEAF) {
            buffer_pool_unpin_page(tree->file_path, page_id, false);
            return DB_ERROR;
        }

        uint16_t count = btree_node_key_count(page);
        uint16_t position = btree_leaf_lower_bound(page, key);

        if (position >= count || btree_leaf_key(page, position) != key) {
            buffer_pool_unpin_page(tree->file_path, page_id, false);
            return DB_NOT_FOUND;
        }

        int32_t keys[BTREE_LEAF_MAX_ENTRIES];
        RID rids[BTREE_LEAF_MAX_ENTRIES];
        uint16_t next_count = 0;

        for (uint16_t i = 0; i < count; i++) {
            if (i == position) {
                continue;
            }

            keys[next_count] = btree_leaf_key(page, i);
            rids[next_count] = btree_leaf_rid(page, i);
            next_count++;
        }

        uint32_t next_leaf = btree_read_u32(page, BTREE_NEXT_LEAF_OFFSET);
        btree_write_leaf_entries(page, keys, rids, next_count, next_leaf);

        status = buffer_pool_unpin_page(tree->file_path, page_id, true);

        if (status != DB_OK) {
            return status;
        }

        return buffer_pool_flush_page(tree->file_path, page_id);
    }
}

DBStatus btree_split_leaf(
    BTree *tree,
    uint32_t leaf_page_id,
    int32_t *out_separator_key,
    uint32_t *out_right_page_id
) {
    if (!btree_is_open(tree) || out_separator_key == NULL || out_right_page_id == NULL) {
        return DB_ERROR;
    }

    uint8_t *page = NULL;
    DBStatus status = buffer_pool_fetch_page(tree->file_path, leaf_page_id, &page);

    if (status != DB_OK) {
        return status;
    }

    if (
        btree_read_u8(page, BTREE_NODE_TYPE_OFFSET) != PAGE_TYPE_BTREE_LEAF ||
        btree_node_key_count(page) != BTREE_LEAF_MAX_ENTRIES
    ) {
        // The helper intentionally only handles a full leaf page.
        buffer_pool_unpin_page(tree->file_path, leaf_page_id, false);
        return DB_ERROR;
    }

    int32_t keys[BTREE_LEAF_MAX_ENTRIES];
    RID rids[BTREE_LEAF_MAX_ENTRIES];

    for (uint16_t i = 0; i < BTREE_LEAF_MAX_ENTRIES; i++) {
        keys[i] = btree_leaf_key(page, i);
        rids[i] = btree_leaf_rid(page, i);
    }

    uint32_t old_next_leaf = btree_read_u32(page, BTREE_NEXT_LEAF_OFFSET);
    uint32_t right_page_id = 0;
    uint8_t *right_page = NULL;

    status = buffer_pool_new_page(tree->file_path, &right_page_id, &right_page);

    if (status != DB_OK) {
        buffer_pool_unpin_page(tree->file_path, leaf_page_id, false);
        return status;
    }

    uint16_t left_count = BTREE_LEAF_MAX_ENTRIES / 2;
    uint16_t right_count = (uint16_t)(BTREE_LEAF_MAX_ENTRIES - left_count);

    btree_write_leaf_entries(page, keys, rids, left_count, right_page_id);
    btree_write_leaf_entries(
        right_page,
        keys + left_count,
        rids + left_count,
        right_count,
        old_next_leaf
    );

    status = buffer_pool_unpin_page(tree->file_path, right_page_id, true);

    if (status != DB_OK) {
        buffer_pool_unpin_page(tree->file_path, leaf_page_id, true);
        return status;
    }

    status = buffer_pool_unpin_page(tree->file_path, leaf_page_id, true);

    if (status != DB_OK) {
        return status;
    }

    *out_separator_key = keys[left_count];
    *out_right_page_id = right_page_id;

    return DB_OK;
}

DBStatus btree_split_internal(
    BTree *tree,
    uint32_t internal_page_id,
    int32_t *out_separator_key,
    uint32_t *out_right_page_id
) {
    if (!btree_is_open(tree) || out_separator_key == NULL || out_right_page_id == NULL) {
        return DB_ERROR;
    }

    uint8_t *page = NULL;
    DBStatus status = buffer_pool_fetch_page(tree->file_path, internal_page_id, &page);

    if (status != DB_OK) {
        return status;
    }

    if (
        btree_read_u8(page, BTREE_NODE_TYPE_OFFSET) != PAGE_TYPE_BTREE_INTERNAL ||
        btree_node_key_count(page) != BTREE_INTERNAL_MAX_KEYS
    ) {
        buffer_pool_unpin_page(tree->file_path, internal_page_id, false);
        return DB_ERROR;
    }

    int32_t keys[BTREE_INTERNAL_MAX_KEYS];
    uint32_t children[BTREE_INTERNAL_MAX_KEYS + 1];

    for (uint16_t i = 0; i < BTREE_INTERNAL_MAX_KEYS; i++) {
        keys[i] = btree_internal_key(page, i);
    }

    for (uint16_t i = 0; i <= BTREE_INTERNAL_MAX_KEYS; i++) {
        children[i] = btree_internal_child(page, i);
    }

    BTreeSplitResult split;
    split.did_split = false;
    split.separator_key = 0;
    split.right_page_id = BTREE_INVALID_PAGE_ID;

    status = buffer_pool_unpin_page(tree->file_path, internal_page_id, false);

    if (status != DB_OK) {
        return status;
    }

    status = btree_internal_insert_split(
        tree,
        internal_page_id,
        keys,
        children,
        BTREE_INTERNAL_MAX_KEYS,
        &split
    );

    if (status != DB_OK) {
        return status;
    }

    *out_separator_key = split.separator_key;
    *out_right_page_id = split.right_page_id;

    return DB_OK;
}
