#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "pager.h"

static void fill_page(uint8_t *page, uint8_t value) {
    memset(page, value, PAGE_SIZE);
}

static void cleanup_file(const char *path) {
    remove(path);
}

static void test_pager_open_new_file(void) {
    const char *path = "test_pager_open_new.db";
    cleanup_file(path);

    Pager pager;

    assert(pager_open(&pager, path) == DB_OK);

    assert(pager.file != NULL);
    assert(strcmp(pager.path, path) == 0);
    assert(pager_num_pages(&pager) == 0);

    assert(pager_close(&pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_open_rejects_null_inputs(void) {
    Pager pager;

    assert(pager_open(NULL, "test.db") == DB_ERROR);
    assert(pager_open(&pager, NULL) == DB_ERROR);
}

static void test_pager_close_null(void) {
    assert(pager_close(NULL) == DB_ERROR);
}

static void test_pager_close_unopened_pager(void) {
    Pager pager;

    pager.file = NULL;
    pager.path[0] = '\0';
    pager.num_pages = 0;

    assert(pager_close(&pager) == DB_OK);
}

static void test_pager_close_resets_pager(void) {
    const char *path = "test_pager_close_resets.db";
    cleanup_file(path);

    Pager pager;

    assert(pager_open(&pager, path) == DB_OK);
    assert(pager_close(&pager) == DB_OK);

    assert(pager.file == NULL);
    assert(pager.path[0] == '\0');
    assert(pager.num_pages == 0);

    cleanup_file(path);
}

static void test_pager_allocate_page(void) {
    const char *path = "test_pager_allocate.db";
    cleanup_file(path);

    Pager pager;
    uint8_t page[PAGE_SIZE];
    uint32_t page_id = 99;

    fill_page(page, 7);

    assert(pager_open(&pager, path) == DB_OK);

    assert(pager_allocate_page(&pager, page, &page_id) == DB_OK);

    assert(page_id == 0);
    assert(pager_num_pages(&pager) == 1);

    assert(pager_close(&pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_allocate_multiple_pages(void) {
    const char *path = "test_pager_allocate_multiple.db";
    cleanup_file(path);

    Pager pager;
    uint8_t page_one[PAGE_SIZE];
    uint8_t page_two[PAGE_SIZE];

    uint32_t page_id_one = 99;
    uint32_t page_id_two = 99;

    fill_page(page_one, 1);
    fill_page(page_two, 2);

    assert(pager_open(&pager, path) == DB_OK);

    assert(pager_allocate_page(&pager, page_one, &page_id_one) == DB_OK);
    assert(pager_allocate_page(&pager, page_two, &page_id_two) == DB_OK);

    assert(page_id_one == 0);
    assert(page_id_two == 1);
    assert(pager_num_pages(&pager) == 2);

    assert(pager_close(&pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_allocate_rejects_null_inputs(void) {
    const char *path = "test_pager_allocate_null.db";
    cleanup_file(path);

    Pager pager;
    uint8_t page[PAGE_SIZE];
    uint32_t page_id = 0;

    fill_page(page, 1);

    assert(pager_open(&pager, path) == DB_OK);

    assert(pager_allocate_page(NULL, page, &page_id) == DB_ERROR);
    assert(pager_allocate_page(&pager, NULL, &page_id) == DB_ERROR);
    assert(pager_allocate_page(&pager, page, NULL) == DB_ERROR);

    assert(pager_close(&pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_allocate_rejects_closed_pager(void) {
    Pager pager;
    uint8_t page[PAGE_SIZE];
    uint32_t page_id = 0;

    pager.file = NULL;
    pager.path[0] = '\0';
    pager.num_pages = 0;

    fill_page(page, 1);

    assert(pager_allocate_page(&pager, page, &page_id) == DB_ERROR);
}

static void test_pager_read_existing_page(void) {
    const char *path = "test_pager_read_existing.db";
    cleanup_file(path);

    Pager pager;
    uint8_t page[PAGE_SIZE];
    uint8_t read_buffer[PAGE_SIZE];
    uint32_t page_id = 0;

    fill_page(page, 8);
    memset(read_buffer, 0, PAGE_SIZE);

    assert(pager_open(&pager, path) == DB_OK);
    assert(pager_allocate_page(&pager, page, &page_id) == DB_OK);

    assert(pager_read_page(&pager, page_id, read_buffer) == DB_OK);

    assert(memcmp(page, read_buffer, PAGE_SIZE) == 0);

    assert(pager_close(&pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_read_second_page(void) {
    const char *path = "test_pager_read_second.db";
    cleanup_file(path);

    Pager pager;
    uint8_t page_one[PAGE_SIZE];
    uint8_t page_two[PAGE_SIZE];
    uint8_t read_buffer[PAGE_SIZE];

    uint32_t page_id_one = 0;
    uint32_t page_id_two = 0;

    fill_page(page_one, 1);
    fill_page(page_two, 2);
    memset(read_buffer, 0, PAGE_SIZE);

    assert(pager_open(&pager, path) == DB_OK);

    assert(pager_allocate_page(&pager, page_one, &page_id_one) == DB_OK);
    assert(pager_allocate_page(&pager, page_two, &page_id_two) == DB_OK);

    assert(pager_read_page(&pager, page_id_two, read_buffer) == DB_OK);

    assert(memcmp(page_two, read_buffer, PAGE_SIZE) == 0);

    assert(pager_close(&pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_read_rejects_null_inputs(void) {
    const char *path = "test_pager_read_null.db";
    cleanup_file(path);

    Pager pager;
    uint8_t page[PAGE_SIZE];

    assert(pager_open(&pager, path) == DB_OK);

    assert(pager_read_page(NULL, 0, page) == DB_ERROR);
    assert(pager_read_page(&pager, 0, NULL) == DB_ERROR);

    assert(pager_close(&pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_read_rejects_closed_pager(void) {
    Pager pager;
    uint8_t page[PAGE_SIZE];

    pager.file = NULL;
    pager.path[0] = '\0';
    pager.num_pages = 0;

    assert(pager_read_page(&pager, 0, page) == DB_ERROR);
}

static void test_pager_read_missing_page(void) {
    const char *path = "test_pager_read_missing.db";
    cleanup_file(path);

    Pager pager;
    uint8_t page[PAGE_SIZE];

    assert(pager_open(&pager, path) == DB_OK);

    assert(pager_read_page(&pager, 0, page) == DB_NOT_FOUND);

    assert(pager_close(&pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_write_existing_page(void) {
    const char *path = "test_pager_write_existing.db";
    cleanup_file(path);

    Pager pager;
    uint8_t original_page[PAGE_SIZE];
    uint8_t updated_page[PAGE_SIZE];
    uint8_t read_buffer[PAGE_SIZE];

    uint32_t page_id = 0;

    fill_page(original_page, 1);
    fill_page(updated_page, 9);
    memset(read_buffer, 0, PAGE_SIZE);

    assert(pager_open(&pager, path) == DB_OK);

    assert(pager_allocate_page(&pager, original_page, &page_id) == DB_OK);
    assert(pager_write_page(&pager, page_id, updated_page) == DB_OK);
    assert(pager_read_page(&pager, page_id, read_buffer) == DB_OK);

    assert(memcmp(updated_page, read_buffer, PAGE_SIZE) == 0);

    assert(pager_close(&pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_write_rejects_null_inputs(void) {
    const char *path = "test_pager_write_null.db";
    cleanup_file(path);

    Pager pager;
    uint8_t page[PAGE_SIZE];

    fill_page(page, 1);

    assert(pager_open(&pager, path) == DB_OK);

    assert(pager_write_page(NULL, 0, page) == DB_ERROR);
    assert(pager_write_page(&pager, 0, NULL) == DB_ERROR);

    assert(pager_close(&pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_write_rejects_closed_pager(void) {
    Pager pager;
    uint8_t page[PAGE_SIZE];

    pager.file = NULL;
    pager.path[0] = '\0';
    pager.num_pages = 0;

    fill_page(page, 1);

    assert(pager_write_page(&pager, 0, page) == DB_ERROR);
}

static void test_pager_write_missing_page(void) {
    const char *path = "test_pager_write_missing.db";
    cleanup_file(path);

    Pager pager;
    uint8_t page[PAGE_SIZE];

    fill_page(page, 1);

    assert(pager_open(&pager, path) == DB_OK);

    assert(pager_write_page(&pager, 0, page) == DB_NOT_FOUND);

    assert(pager_close(&pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_persists_page_after_reopen(void) {
    const char *path = "test_pager_persist.db";
    cleanup_file(path);

    uint8_t page[PAGE_SIZE];
    uint8_t read_buffer[PAGE_SIZE];
    uint32_t page_id = 0;

    fill_page(page, 5);
    memset(read_buffer, 0, PAGE_SIZE);

    Pager first_pager;

    assert(pager_open(&first_pager, path) == DB_OK);
    assert(pager_allocate_page(&first_pager, page, &page_id) == DB_OK);
    assert(pager_close(&first_pager) == DB_OK);

    Pager second_pager;

    assert(pager_open(&second_pager, path) == DB_OK);

    assert(pager_num_pages(&second_pager) == 1);
    assert(pager_read_page(&second_pager, 0, read_buffer) == DB_OK);
    assert(memcmp(page, read_buffer, PAGE_SIZE) == 0);

    assert(pager_close(&second_pager) == DB_OK);

    cleanup_file(path);
}

static void test_pager_open_rejects_partial_page_file(void) {
    const char *path = "test_pager_partial_page.db";
    cleanup_file(path);

    FILE *file = fopen(path, "w+b");
    assert(file != NULL);

    fputc('x', file);
    fclose(file);

    Pager pager;

    assert(pager_open(&pager, path) == DB_IO_ERROR);

    cleanup_file(path);
}

static void test_pager_num_pages_null(void) {
    assert(pager_num_pages(NULL) == 0);
}

int main(void) {
    test_pager_open_new_file();
    test_pager_open_rejects_null_inputs();
    test_pager_close_null();
    test_pager_close_unopened_pager();
    test_pager_close_resets_pager();
    test_pager_allocate_page();
    test_pager_allocate_multiple_pages();
    test_pager_allocate_rejects_null_inputs();
    test_pager_allocate_rejects_closed_pager();
    test_pager_read_existing_page();
    test_pager_read_second_page();
    test_pager_read_rejects_null_inputs();
    test_pager_read_rejects_closed_pager();
    test_pager_read_missing_page();
    test_pager_write_existing_page();
    test_pager_write_rejects_null_inputs();
    test_pager_write_rejects_closed_pager();
    test_pager_write_missing_page();
    test_pager_persists_page_after_reopen();
    test_pager_open_rejects_partial_page_file();
    test_pager_num_pages_null();

    printf("All pager tests passed.\n");

    return 0;
}
