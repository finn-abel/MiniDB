#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "buffer/buffer_pool.h"
#include "common.h"
#include "pager.h"

static void cleanup_file(const char *path) {
    remove(path);
}

static void test_buffer_pool_new_page_and_page_count(void) {
    const char *path = "test_buffer_pool_new.db";
    uint32_t page_id = 99;
    uint32_t page_count = 0;
    uint8_t *page = NULL;

    cleanup_file(path);

    assert(buffer_pool_new_page(path, &page_id, &page) == DB_OK);
    assert(page_id == 0);
    assert(page != NULL);

    page[0] = 42;

    assert(buffer_pool_unpin_page(path, page_id, true) == DB_OK);
    assert(buffer_pool_flush_page(path, page_id) == DB_OK);

    assert(buffer_pool_page_count(path, &page_count) == DB_OK);
    assert(page_count == 1);

    cleanup_file(path);
}

static void test_buffer_pool_fetch_returns_cached_frame(void) {
    const char *path = "test_buffer_pool_fetch_cached.db";
    uint32_t page_id = 0;
    uint8_t *first = NULL;
    uint8_t *second = NULL;

    cleanup_file(path);

    assert(buffer_pool_new_page(path, &page_id, &first) == DB_OK);
    first[0] = 11;
    assert(buffer_pool_unpin_page(path, page_id, true) == DB_OK);

    assert(buffer_pool_fetch_page(path, page_id, &first) == DB_OK);
    assert(buffer_pool_fetch_page(path, page_id, &second) == DB_OK);

    assert(first == second);
    assert(second[0] == 11);

    assert(buffer_pool_unpin_page(path, page_id, false) == DB_OK);
    assert(buffer_pool_unpin_page(path, page_id, false) == DB_OK);
    assert(buffer_pool_flush_page(path, page_id) == DB_OK);

    cleanup_file(path);
}

static void test_buffer_pool_flush_all_writes_dirty_pages(void) {
    const char *path = "test_buffer_pool_flush_all.db";
    uint32_t page_id = 0;
    uint8_t *page = NULL;
    uint8_t disk_page[PAGE_SIZE];
    Pager pager;

    cleanup_file(path);

    assert(buffer_pool_new_page(path, &page_id, &page) == DB_OK);
    page[0] = 99;

    assert(buffer_pool_unpin_page(path, page_id, true) == DB_OK);
    assert(buffer_pool_flush_all() == DB_OK);

    assert(pager_open(&pager, path) == DB_OK);
    assert(pager_read_page(&pager, page_id, disk_page) == DB_OK);
    assert(pager_close(&pager) == DB_OK);

    assert(disk_page[0] == 99);

    cleanup_file(path);
}

static void test_buffer_pool_rejects_eviction_when_all_frames_pinned(void) {
    const char *path = "test_buffer_pool_pinned.db";
    uint32_t page_ids[BUFFER_POOL_SIZE];
    uint8_t *pages[BUFFER_POOL_SIZE];
    uint32_t extra_page_id = 0;
    uint8_t *extra_page = NULL;

    cleanup_file(path);

    for (uint32_t i = 0; i < BUFFER_POOL_SIZE; i++) {
        assert(buffer_pool_new_page(path, &page_ids[i], &pages[i]) == DB_OK);
        assert(page_ids[i] == i);
    }

    assert(buffer_pool_new_page(path, &extra_page_id, &extra_page) == DB_FULL);

    for (uint32_t i = 0; i < BUFFER_POOL_SIZE; i++) {
        assert(buffer_pool_unpin_page(path, page_ids[i], false) == DB_OK);
    }

    cleanup_file(path);
}

static void test_buffer_pool_rejects_null_inputs(void) {
    const char *path = "test_buffer_pool_null.db";
    uint32_t page_id = 0;
    uint8_t *page = NULL;

    cleanup_file(path);

    assert(buffer_pool_fetch_page(NULL, 0, &page) == DB_ERROR);
    assert(buffer_pool_fetch_page(path, 0, NULL) == DB_ERROR);
    assert(buffer_pool_new_page(NULL, &page_id, &page) == DB_ERROR);
    assert(buffer_pool_new_page(path, NULL, &page) == DB_ERROR);
    assert(buffer_pool_new_page(path, &page_id, NULL) == DB_ERROR);
    assert(buffer_pool_unpin_page(NULL, 0, false) == DB_ERROR);
    assert(buffer_pool_flush_page(NULL, 0) == DB_ERROR);
    assert(buffer_pool_page_count(NULL, &page_id) == DB_ERROR);
    assert(buffer_pool_page_count(path, NULL) == DB_ERROR);

    cleanup_file(path);
}

int main(void) {
    test_buffer_pool_new_page_and_page_count();
    test_buffer_pool_fetch_returns_cached_frame();
    test_buffer_pool_flush_all_writes_dirty_pages();
    test_buffer_pool_rejects_eviction_when_all_frames_pinned();
    test_buffer_pool_rejects_null_inputs();

    printf("All buffer pool tests passed.\n");

    return 0;
}
