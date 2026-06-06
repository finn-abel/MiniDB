#include <assert.h>
#include <stdio.h>

#include "index/index.h"

static void test_index_init_free(void) {
    Index index;

    assert(index_init(&index, INDEX_TYPE_PRIMARY_INT) == DB_OK);
    assert(index.type == INDEX_TYPE_PRIMARY_INT);
    assert(index.entries == NULL);
    assert(index.count == 0);
    assert(index.capacity == 0);

    index_free(&index);

    assert(index.entries == NULL);
    assert(index.count == 0);
    assert(index.capacity == 0);
}

static void test_index_init_rejects_null(void) {
    assert(index_init(NULL, INDEX_TYPE_PRIMARY_INT) == DB_ERROR);
}

static void test_index_insert_and_find_int_key(void) {
    Index index;
    Value key = value_int(10);
    RID rid = {2, 4};
    RID found = {0, 0};

    assert(index_init(&index, INDEX_TYPE_PRIMARY_INT) == DB_OK);

    assert(index_insert(&index, &key, rid) == DB_OK);
    assert(index.count == 1);

    assert(index_find(&index, &key, &found) == DB_OK);
    assert(rid_equal(&found, &rid) == true);

    index_free(&index);
}

static void test_index_rejects_duplicate_primary_key(void) {
    Index index;
    Value key = value_int(10);
    RID first = {2, 4};
    RID second = {3, 5};
    RID found = {0, 0};

    assert(index_init(&index, INDEX_TYPE_PRIMARY_INT) == DB_OK);

    assert(index_insert(&index, &key, first) == DB_OK);
    assert(index_insert(&index, &key, second) == DB_ERROR);

    assert(index_find(&index, &key, &found) == DB_OK);
    assert(rid_equal(&found, &first) == true);

    index_free(&index);
}

static void test_index_find_missing_key(void) {
    Index index;
    Value key = value_int(10);
    RID found = {0, 0};

    assert(index_init(&index, INDEX_TYPE_PRIMARY_INT) == DB_OK);

    assert(index_find(&index, &key, &found) == DB_NOT_FOUND);

    index_free(&index);
}

static void test_index_delete_key(void) {
    Index index;
    Value key = value_int(10);
    RID rid = {2, 4};
    RID found = {0, 0};

    assert(index_init(&index, INDEX_TYPE_PRIMARY_INT) == DB_OK);

    assert(index_insert(&index, &key, rid) == DB_OK);
    assert(index_delete(&index, &key) == DB_OK);
    assert(index.count == 0);
    assert(index_find(&index, &key, &found) == DB_NOT_FOUND);

    index_free(&index);
}

static void test_index_delete_missing_key(void) {
    Index index;
    Value key = value_int(10);

    assert(index_init(&index, INDEX_TYPE_PRIMARY_INT) == DB_OK);

    assert(index_delete(&index, &key) == DB_NOT_FOUND);

    index_free(&index);
}

static void test_index_rejects_text_key(void) {
    Index index;
    Value key;
    RID rid = {2, 4};

    assert(index_init(&index, INDEX_TYPE_PRIMARY_INT) == DB_OK);
    assert(value_text(&key, "Finn") == DB_OK);

    assert(index_insert(&index, &key, rid) == DB_TYPE_ERROR);
    assert(index_delete(&index, &key) == DB_TYPE_ERROR);
    assert(index_find(&index, &key, &rid) == DB_TYPE_ERROR);

    value_free(&key);
    index_free(&index);
}

static void test_index_rejects_null_inputs(void) {
    Index index;
    Value key = value_int(10);
    RID rid = {2, 4};

    assert(index_init(&index, INDEX_TYPE_PRIMARY_INT) == DB_OK);

    assert(index_insert(NULL, &key, rid) == DB_ERROR);
    assert(index_insert(&index, NULL, rid) == DB_ERROR);
    assert(index_delete(NULL, &key) == DB_ERROR);
    assert(index_delete(&index, NULL) == DB_ERROR);
    assert(index_find(NULL, &key, &rid) == DB_ERROR);
    assert(index_find(&index, NULL, &rid) == DB_ERROR);
    assert(index_find(&index, &key, NULL) == DB_ERROR);

    index_free(&index);
}

int main(void) {
    test_index_init_free();
    test_index_init_rejects_null();
    test_index_insert_and_find_int_key();
    test_index_rejects_duplicate_primary_key();
    test_index_find_missing_key();
    test_index_delete_key();
    test_index_delete_missing_key();
    test_index_rejects_text_key();
    test_index_rejects_null_inputs();

    printf("All index tests passed.\n");

    return 0;
}
