#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "buffer/buffer_pool.h"
#include "index/btree.h"
#include "pager.h"
#include "page.h"

static void cleanup_file(const char *path) {
    remove(path);
}

static RID rid_for_key(int32_t key) {
    RID rid;

    rid.page_id = (uint32_t)(key / 10 + 1);
    rid.slot_id = (uint16_t)(key % 10);

    return rid;
}

static uint8_t root_page_type(const char *path) {
    uint8_t *page = NULL;
    uint8_t page_type = 0;

    assert(buffer_pool_fetch_page(path, BTREE_ROOT_PAGE_ID, &page) == DB_OK);
    page_type = page[0];
    assert(buffer_pool_unpin_page(path, BTREE_ROOT_PAGE_ID, false) == DB_OK);

    return page_type;
}

static void test_btree_open_creates_root_leaf(void) {
    const char *path = "test_btree_open_root.db";
    BTree tree;
    uint32_t page_count = 0;
    RID found = {0, 0};

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);
    assert(buffer_pool_page_count(path, &page_count) == DB_OK);
    assert(page_count == 1);
    assert(root_page_type(path) == PAGE_TYPE_BTREE_LEAF);
    assert(btree_search(&tree, 10, &found) == DB_NOT_FOUND);
    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_insert_and_search_single_key(void) {
    const char *path = "test_btree_insert_single.db";
    BTree tree;
    RID rid = {2, 4};
    RID found = {0, 0};

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);
    assert(btree_insert(&tree, 10, rid) == DB_OK);
    assert(btree_search(&tree, 10, &found) == DB_OK);
    assert(rid_equal(&found, &rid) == true);
    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_insert_and_search_negative_keys(void) {
    const char *path = "test_btree_negative.db";
    BTree tree;
    RID negative = {9, 1};
    RID zero = {9, 2};
    RID positive = {9, 3};
    RID found = {0, 0};

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);
    assert(btree_insert(&tree, -20, negative) == DB_OK);
    assert(btree_insert(&tree, 0, zero) == DB_OK);
    assert(btree_insert(&tree, 20, positive) == DB_OK);

    assert(btree_search(&tree, -20, &found) == DB_OK);
    assert(rid_equal(&found, &negative) == true);
    assert(btree_search(&tree, 0, &found) == DB_OK);
    assert(rid_equal(&found, &zero) == true);
    assert(btree_search(&tree, 20, &found) == DB_OK);
    assert(rid_equal(&found, &positive) == true);
    assert(btree_search(&tree, -21, &found) == DB_NOT_FOUND);

    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_rejects_duplicate_key(void) {
    const char *path = "test_btree_duplicate.db";
    BTree tree;
    RID first = {2, 4};
    RID second = {3, 5};
    RID found = {0, 0};

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);
    assert(btree_insert(&tree, 10, first) == DB_OK);
    assert(btree_insert(&tree, 10, second) == DB_ERROR);
    assert(btree_search(&tree, 10, &found) == DB_OK);
    assert(rid_equal(&found, &first) == true);
    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_delete_key(void) {
    const char *path = "test_btree_delete.db";
    BTree tree;
    RID first = rid_for_key(10);
    RID second = rid_for_key(20);
    RID found;

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);
    assert(btree_insert(&tree, 10, first) == DB_OK);
    assert(btree_insert(&tree, 20, second) == DB_OK);
    assert(btree_delete(&tree, 10) == DB_OK);
    assert(btree_search(&tree, 10, &found) == DB_NOT_FOUND);
    assert(btree_search(&tree, 20, &found) == DB_OK);
    assert(rid_equal(&found, &second) == true);
    assert(btree_delete(&tree, 10) == DB_NOT_FOUND);
    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_root_split_keeps_page_zero_as_internal_root(void) {
    const char *path = "test_btree_root_split.db";
    BTree tree;

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);

    for (int32_t key = 1; key <= BTREE_LEAF_MAX_ENTRIES + 1; key++) {
        assert(btree_insert(&tree, key, rid_for_key(key)) == DB_OK);
    }

    assert(root_page_type(path) == PAGE_TYPE_BTREE_INTERNAL);
    assert(btree_close(&tree) == DB_OK);
    assert(btree_open(&tree, path) == DB_OK);
    assert(root_page_type(path) == PAGE_TYPE_BTREE_INTERNAL);

    for (int32_t key = 1; key <= BTREE_LEAF_MAX_ENTRIES + 1; key++) {
        RID found = {0, 0};
        RID expected = rid_for_key(key);

        assert(btree_search(&tree, key, &found) == DB_OK);
        assert(rid_equal(&found, &expected) == true);
    }

    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_leaf_split_and_search_many_keys(void) {
    const char *path = "test_btree_leaf_split.db";
    BTree tree;
    uint32_t page_count = 0;

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);

    for (int32_t key = 64; key >= 1; key--) {
        assert(btree_insert(&tree, key, rid_for_key(key)) == DB_OK);
    }

    assert(buffer_pool_page_count(path, &page_count) == DB_OK);
    assert(page_count > 1);

    for (int32_t key = 1; key <= 64; key++) {
        RID found = {0, 0};
        RID expected = rid_for_key(key);

        assert(btree_search(&tree, key, &found) == DB_OK);
        assert(rid_equal(&found, &expected) == true);
    }

    assert(btree_search(&tree, 1000, &(RID){0, 0}) == DB_NOT_FOUND);
    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_searches_between_sparse_keys(void) {
    const char *path = "test_btree_sparse.db";
    BTree tree;
    RID found = {0, 0};

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);

    for (int32_t key = 10; key <= 100; key += 10) {
        assert(btree_insert(&tree, key, rid_for_key(key)) == DB_OK);
    }

    assert(btree_search(&tree, 10, &found) == DB_OK);
    assert(btree_search(&tree, 55, &found) == DB_NOT_FOUND);
    assert(btree_search(&tree, 100, &found) == DB_OK);
    assert(btree_search(&tree, 101, &found) == DB_NOT_FOUND);
    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_internal_split_and_search_many_keys(void) {
    const char *path = "test_btree_internal_split.db";
    BTree tree;
    uint32_t page_count = 0;

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);

    for (int32_t key = 1; key <= 700; key++) {
        assert(btree_insert(&tree, key, rid_for_key(key)) == DB_OK);
    }

    assert(buffer_pool_page_count(path, &page_count) == DB_OK);
    assert(page_count > BTREE_INTERNAL_MAX_KEYS);

    for (int32_t key = 1; key <= 700; key++) {
        RID found = {0, 0};
        RID expected = rid_for_key(key);

        assert(btree_search(&tree, key, &found) == DB_OK);
        assert(rid_equal(&found, &expected) == true);
    }

    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_reopen_after_internal_split(void) {
    const char *path = "test_btree_reopen_internal.db";
    BTree tree;

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);

    for (int32_t key = 700; key >= 1; key--) {
        assert(btree_insert(&tree, key, rid_for_key(key)) == DB_OK);
    }

    assert(btree_close(&tree) == DB_OK);
    assert(btree_open(&tree, path) == DB_OK);
    assert(root_page_type(path) == PAGE_TYPE_BTREE_INTERNAL);

    for (int32_t key = 1; key <= 700; key++) {
        RID found = {0, 0};
        RID expected = rid_for_key(key);

        assert(btree_search(&tree, key, &found) == DB_OK);
        assert(rid_equal(&found, &expected) == true);
    }

    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_reopen_keeps_inserted_keys(void) {
    const char *path = "test_btree_reopen.db";
    BTree tree;

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);

    for (int32_t key = 1; key <= 80; key++) {
        assert(btree_insert(&tree, key, rid_for_key(key)) == DB_OK);
    }

    assert(btree_close(&tree) == DB_OK);
    assert(btree_open(&tree, path) == DB_OK);

    for (int32_t key = 1; key <= 80; key++) {
        RID found = {0, 0};
        RID expected = rid_for_key(key);

        assert(btree_search(&tree, key, &found) == DB_OK);
        assert(rid_equal(&found, &expected) == true);
    }

    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_split_helpers_reject_invalid_pages(void) {
    const char *path = "test_btree_split_invalid.db";
    BTree tree;
    int32_t separator_key = 0;
    uint32_t right_page_id = 0;

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);
    assert(btree_split_leaf(
        &tree,
        BTREE_ROOT_PAGE_ID,
        &separator_key,
        &right_page_id
    ) == DB_ERROR);
    assert(btree_split_internal(
        &tree,
        BTREE_ROOT_PAGE_ID,
        &separator_key,
        &right_page_id
    ) == DB_ERROR);
    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_rejects_closed_tree_operations(void) {
    const char *path = "test_btree_closed.db";
    BTree tree;
    RID rid = {2, 4};
    int32_t separator_key = 0;
    uint32_t right_page_id = 0;

    cleanup_file(path);

    assert(btree_open(&tree, path) == DB_OK);
    assert(btree_close(&tree) == DB_OK);

    assert(btree_search(&tree, 10, &rid) == DB_ERROR);
    assert(btree_insert(&tree, 10, rid) == DB_ERROR);
    assert(btree_delete(&tree, 10) == DB_ERROR);
    assert(btree_split_leaf(&tree, 0, &separator_key, &right_page_id) == DB_ERROR);
    assert(btree_split_internal(&tree, 0, &separator_key, &right_page_id) == DB_ERROR);
    assert(btree_close(&tree) == DB_ERROR);

    cleanup_file(path);
}

static void test_btree_rejects_null_inputs(void) {
    const char *path = "test_btree_null.db";
    BTree tree;
    RID rid = {2, 4};

    cleanup_file(path);

    assert(btree_open(NULL, path) == DB_ERROR);
    assert(btree_open(&tree, NULL) == DB_ERROR);
    assert(btree_open(&tree, path) == DB_OK);
    assert(btree_search(NULL, 10, &rid) == DB_ERROR);
    assert(btree_search(&tree, 10, NULL) == DB_ERROR);
    assert(btree_insert(NULL, 10, rid) == DB_ERROR);
    assert(btree_delete(NULL, 10) == DB_ERROR);
    assert(btree_split_leaf(NULL, BTREE_ROOT_PAGE_ID, &(int32_t){0}, &(uint32_t){0}) == DB_ERROR);
    assert(btree_split_internal(NULL, BTREE_ROOT_PAGE_ID, &(int32_t){0}, &(uint32_t){0}) == DB_ERROR);
    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_open_rejects_corrupt_key_count(void) {
    const char *path = "test_btree_corrupt_count.db";
    BTree tree;
    Pager pager;
    uint8_t page[PAGE_SIZE];
    uint16_t bad_count = BTREE_LEAF_MAX_ENTRIES + 1;

    cleanup_file(path);
    assert(btree_open(&tree, path) == DB_OK);
    assert(btree_close(&tree) == DB_OK);
    assert(buffer_pool_discard_file(path) == DB_OK);

    assert(pager_open(&pager, path) == DB_OK);
    assert(pager_read_page(&pager, BTREE_ROOT_PAGE_ID, page) == DB_OK);
    memcpy(page + 2, &bad_count, sizeof(bad_count));
    assert(pager_write_page(&pager, BTREE_ROOT_PAGE_ID, page) == DB_OK);
    assert(pager_close(&pager) == DB_OK);

    assert(btree_open(&tree, path) == DB_ERROR);
    assert(buffer_pool_discard_file(path) == DB_OK);
    cleanup_file(path);
}

static void test_btree_operations_reject_corrupt_node_type(void) {
    const char *path = "test_btree_corrupt_type.db";
    BTree tree;
    RID rid = {1, 1};
    RID found;
    uint8_t *root = NULL;

    cleanup_file(path);
    assert(btree_open(&tree, path) == DB_OK);
    assert(buffer_pool_fetch_page(path, BTREE_ROOT_PAGE_ID, &root) == DB_OK);
    root[0] = 0xff;
    assert(buffer_pool_unpin_page(path, BTREE_ROOT_PAGE_ID, true) == DB_OK);

    assert(btree_search(&tree, 1, &found) == DB_ERROR);
    assert(btree_insert(&tree, 1, rid) == DB_ERROR);
    assert(btree_delete(&tree, 1) == DB_ERROR);
    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_operations_reject_cyclic_child_links(void) {
    const char *path = "test_btree_cycle.db";
    BTree tree;
    RID rid = {1, 1};
    RID found;
    uint8_t *root = NULL;

    cleanup_file(path);
    assert(btree_open(&tree, path) == DB_OK);

    for (int32_t key = 0; key <= BTREE_LEAF_MAX_ENTRIES; key++) {
        assert(btree_insert(&tree, key, rid_for_key(key)) == DB_OK);
    }

    assert(buffer_pool_fetch_page(path, BTREE_ROOT_PAGE_ID, &root) == DB_OK);

    for (uint16_t i = 0; i <= BTREE_INTERNAL_MAX_KEYS; i++) {
        uint32_t root_page_id = BTREE_ROOT_PAGE_ID;
        memcpy(root + 8 + i * sizeof(uint32_t), &root_page_id, sizeof(root_page_id));
    }

    assert(buffer_pool_unpin_page(path, BTREE_ROOT_PAGE_ID, true) == DB_OK);
    assert(btree_search(&tree, 1, &found) == DB_ERROR);
    assert(btree_insert(&tree, 1, rid) == DB_ERROR);
    assert(btree_delete(&tree, 1) == DB_ERROR);
    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

static void test_btree_operations_reject_empty_open_file(void) {
    const char *path = "test_btree_empty_open.db";
    BTree tree;
    RID rid = {1, 1};
    RID found;
    FILE *file;

    cleanup_file(path);
    assert(btree_open(&tree, path) == DB_OK);
    assert(buffer_pool_discard_file(path) == DB_OK);

    file = fopen(path, "wb");
    assert(file != NULL);
    assert(fclose(file) == 0);

    assert(btree_search(&tree, 1, &found) == DB_ERROR);
    assert(btree_insert(&tree, 1, rid) == DB_ERROR);
    assert(btree_delete(&tree, 1) == DB_ERROR);
    assert(btree_close(&tree) == DB_OK);

    cleanup_file(path);
}

int main(void) {
    test_btree_open_creates_root_leaf();
    test_btree_insert_and_search_single_key();
    test_btree_insert_and_search_negative_keys();
    test_btree_rejects_duplicate_key();
    test_btree_delete_key();
    test_btree_root_split_keeps_page_zero_as_internal_root();
    test_btree_leaf_split_and_search_many_keys();
    test_btree_searches_between_sparse_keys();
    test_btree_internal_split_and_search_many_keys();
    test_btree_reopen_after_internal_split();
    test_btree_reopen_keeps_inserted_keys();
    test_btree_split_helpers_reject_invalid_pages();
    test_btree_rejects_closed_tree_operations();
    test_btree_rejects_null_inputs();
    test_btree_open_rejects_corrupt_key_count();
    test_btree_operations_reject_corrupt_node_type();
    test_btree_operations_reject_cyclic_child_links();
    test_btree_operations_reject_empty_open_file();

    printf("All B+ tree tests passed.\n");

    return 0;
}
