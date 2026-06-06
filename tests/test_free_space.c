#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "page.h"
#include "pager.h"
#include "storage/free_space.h"

static void cleanup_file(const char *path) {
    remove(path);
}

static void append_page_with_row(const char *path, uint32_t row_len) {
    Pager pager;
    uint8_t page[PAGE_SIZE];
    uint8_t row_bytes[PAGE_SIZE];
    uint16_t slot_id = 0;
    uint32_t page_id = 0;

    assert(pager_open(&pager, path) == DB_OK);
    page_id = pager_num_pages(&pager);

    assert(page_init(page, page_id) == DB_OK);

    if (row_len > 0) {
        for (uint32_t i = 0; i < row_len; i++) {
            row_bytes[i] = (uint8_t)(i % 255);
        }

        assert(page_insert(page, row_bytes, row_len, &slot_id) == DB_OK);
    }

    assert(pager_allocate_page(&pager, page, &page_id) == DB_OK);
    assert(pager_close(&pager) == DB_OK);
}

static uint32_t append_page_with_deleted_row(const char *path, uint32_t row_len) {
    Pager pager;
    uint8_t page[PAGE_SIZE];
    uint8_t row_bytes[PAGE_SIZE];
    uint16_t slot_id = 0;
    uint32_t page_id = 0;
    uint32_t insertable_space = 0;

    assert(pager_open(&pager, path) == DB_OK);
    page_id = pager_num_pages(&pager);

    assert(page_init(page, page_id) == DB_OK);

    for (uint32_t i = 0; i < row_len; i++) {
        row_bytes[i] = (uint8_t)(i % 255);
    }

    assert(page_insert(page, row_bytes, row_len, &slot_id) == DB_OK);
    assert(page_delete(page, slot_id) == DB_OK);

    insertable_space = page_insertable_space(page);

    assert(pager_allocate_page(&pager, page, &page_id) == DB_OK);
    assert(pager_close(&pager) == DB_OK);

    return insertable_space;
}

static void test_free_space_rebuild_and_find_page(void) {
    const char *path = "test_free_space_rebuild.db";
    uint32_t page_id = 99;

    cleanup_file(path);
    append_page_with_row(path, 3000);
    append_page_with_row(path, 100);

    assert(free_space_rebuild(path) == DB_OK);
    assert(free_space_find_page(path, 2000, &page_id) == DB_OK);
    assert(page_id == 1);

    cleanup_file(path);
}

static void test_free_space_rebuild_counts_deleted_slot_space(void) {
    const char *path = "test_free_space_deleted_slot.db";
    uint32_t page_id = 99;
    uint32_t insertable_space = 0;

    cleanup_file(path);

    insertable_space = append_page_with_deleted_row(path, 100);

    assert(free_space_rebuild(path) == DB_OK);
    assert(free_space_find_page(path, insertable_space, &page_id) == DB_OK);
    assert(page_id == 0);
    assert(free_space_find_page(path, insertable_space + 1, &page_id) == DB_NOT_FOUND);

    cleanup_file(path);
}

static void test_free_space_find_lazily_rebuilds_map(void) {
    const char *path = "test_free_space_lazy.db";
    uint32_t page_id = 99;

    cleanup_file(path);
    append_page_with_row(path, 3000);
    append_page_with_row(path, 100);

    assert(free_space_find_page(path, 2000, &page_id) == DB_OK);
    assert(page_id == 1);

    cleanup_file(path);
}

static void test_free_space_lazy_rebuild_counts_deleted_slot_space(void) {
    const char *path = "test_free_space_lazy_deleted_slot.db";
    uint32_t page_id = 99;
    uint32_t insertable_space = 0;

    cleanup_file(path);

    insertable_space = append_page_with_deleted_row(path, 100);

    assert(free_space_find_page(path, insertable_space, &page_id) == DB_OK);
    assert(page_id == 0);

    cleanup_file(path);
}

static void test_free_space_update_changes_choice(void) {
    const char *path = "test_free_space_update.db";
    uint32_t page_id = 99;

    cleanup_file(path);
    append_page_with_row(path, 0);
    append_page_with_row(path, 0);

    assert(free_space_rebuild(path) == DB_OK);
    assert(free_space_update(path, 0, 50) == DB_OK);
    assert(free_space_update(path, 1, 500) == DB_OK);

    assert(free_space_find_page(path, 100, &page_id) == DB_OK);
    assert(page_id == 1);

    cleanup_file(path);
}

static void test_free_space_update_can_create_map(void) {
    const char *path = "test_free_space_update_only.db";
    uint32_t page_id = 99;

    cleanup_file(path);

    assert(free_space_update(path, 3, 256) == DB_OK);
    assert(free_space_find_page(path, 256, &page_id) == DB_OK);
    assert(page_id == 3);
    assert(free_space_find_page(path, 257, &page_id) == DB_NOT_FOUND);

    cleanup_file(path);
}

static void test_free_space_update_exact_match(void) {
    const char *path = "test_free_space_exact.db";
    uint32_t page_id = 99;

    cleanup_file(path);

    assert(free_space_update(path, 7, 128) == DB_OK);
    assert(free_space_find_page(path, 128, &page_id) == DB_OK);
    assert(page_id == 7);

    cleanup_file(path);
}

static void test_free_space_rebuild_replaces_stale_entries(void) {
    const char *path = "test_free_space_rebuild_replaces.db";
    uint32_t page_id = 99;

    cleanup_file(path);
    append_page_with_row(path, 0);

    assert(free_space_rebuild(path) == DB_OK);
    assert(free_space_update(path, 0, 1) == DB_OK);
    assert(free_space_find_page(path, 100, &page_id) == DB_NOT_FOUND);

    assert(free_space_rebuild(path) == DB_OK);
    assert(free_space_find_page(path, 100, &page_id) == DB_OK);
    assert(page_id == 0);

    cleanup_file(path);
}

static void test_free_space_keeps_tables_separate(void) {
    const char *users_path = "test_free_space_users.db";
    const char *orders_path = "test_free_space_orders.db";
    uint32_t page_id = 99;

    cleanup_file(users_path);
    cleanup_file(orders_path);

    assert(free_space_update(users_path, 0, 50) == DB_OK);
    assert(free_space_update(orders_path, 0, 500) == DB_OK);

    assert(free_space_find_page(users_path, 100, &page_id) == DB_NOT_FOUND);
    assert(free_space_find_page(orders_path, 100, &page_id) == DB_OK);
    assert(page_id == 0);

    cleanup_file(users_path);
    cleanup_file(orders_path);
}

static void test_free_space_rebuild_grows_entry_storage(void) {
    const char *path = "test_free_space_grows.db";
    uint32_t page_id = 99;

    cleanup_file(path);

    for (uint32_t i = 0; i < 20; i++) {
        append_page_with_row(path, 0);
    }

    assert(free_space_rebuild(path) == DB_OK);

    for (uint32_t i = 0; i < 19; i++) {
        assert(free_space_update(path, i, 1) == DB_OK);
    }

    assert(free_space_find_page(path, 100, &page_id) == DB_OK);
    assert(page_id == 19);

    cleanup_file(path);
}

static void test_free_space_rebuild_empty_file(void) {
    const char *path = "test_free_space_empty.db";
    uint32_t page_id = 99;

    cleanup_file(path);

    assert(free_space_rebuild(path) == DB_OK);
    assert(free_space_find_page(path, 1, &page_id) == DB_NOT_FOUND);

    cleanup_file(path);
}

static void test_free_space_find_missing_space(void) {
    const char *path = "test_free_space_missing.db";
    uint32_t page_id = 99;

    cleanup_file(path);
    append_page_with_row(path, 0);

    assert(free_space_rebuild(path) == DB_OK);
    assert(free_space_update(path, 0, 10) == DB_OK);
    assert(free_space_find_page(path, 100, &page_id) == DB_NOT_FOUND);

    cleanup_file(path);
}

static void test_free_space_rejects_null_inputs(void) {
    uint32_t page_id = 0;

    assert(free_space_rebuild(NULL) == DB_ERROR);
    assert(free_space_find_page(NULL, 1, &page_id) == DB_ERROR);
    assert(free_space_find_page("missing.db", 1, NULL) == DB_ERROR);
    assert(free_space_update(NULL, 0, 1) == DB_ERROR);
}

int main(void) {
    test_free_space_rebuild_and_find_page();
    test_free_space_rebuild_counts_deleted_slot_space();
    test_free_space_find_lazily_rebuilds_map();
    test_free_space_lazy_rebuild_counts_deleted_slot_space();
    test_free_space_update_changes_choice();
    test_free_space_update_can_create_map();
    test_free_space_update_exact_match();
    test_free_space_rebuild_replaces_stale_entries();
    test_free_space_keeps_tables_separate();
    test_free_space_rebuild_grows_entry_storage();
    test_free_space_rebuild_empty_file();
    test_free_space_find_missing_space();
    test_free_space_rejects_null_inputs();

    printf("All free-space tests passed.\n");

    return 0;
}
