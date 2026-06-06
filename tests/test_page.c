#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "page.h"

static void test_page_init(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 3) == DB_OK);

    assert(page_slot_count(page) == 0);
    assert(page_free_space(page) == PAGE_SIZE - sizeof(PageHeader));
}

static void test_page_init_rejects_null(void) {
    assert(page_init(NULL, 3) == DB_ERROR);
}

static void test_page_insert_single_row(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint8_t row_bytes[] = {1, 2, 3, 4};
    uint16_t slot_id = 0;

    assert(page_insert(page, row_bytes, sizeof(row_bytes), &slot_id) == DB_OK);

    assert(slot_id == 0);
    assert(page_slot_count(page) == 1);
    assert(page_slot_is_active(page, slot_id) == true);
}

static void test_page_insert_multiple_rows(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint8_t row_one[] = {1, 2, 3};
    uint8_t row_two[] = {4, 5, 6};

    uint16_t slot_one = 0;
    uint16_t slot_two = 0;

    assert(page_insert(page, row_one, sizeof(row_one), &slot_one) == DB_OK);
    assert(page_insert(page, row_two, sizeof(row_two), &slot_two) == DB_OK);

    assert(slot_one == 0);
    assert(slot_two == 1);

    assert(page_slot_count(page) == 2);
    assert(page_slot_is_active(page, slot_one) == true);
    assert(page_slot_is_active(page, slot_two) == true);
}

static void test_page_insert_rejects_null_inputs(void) {
    uint8_t page[PAGE_SIZE];
    uint8_t row_bytes[] = {1, 2, 3};
    uint16_t slot_id = 0;

    assert(page_init(page, 1) == DB_OK);

    assert(page_insert(NULL, row_bytes, sizeof(row_bytes), &slot_id) == DB_ERROR);
    assert(page_insert(page, NULL, sizeof(row_bytes), &slot_id) == DB_ERROR);
    assert(page_insert(page, row_bytes, sizeof(row_bytes), NULL) == DB_ERROR);
}

static void test_page_insert_rejects_zero_length_row(void) {
    uint8_t page[PAGE_SIZE];
    uint8_t row_bytes[] = {1};
    uint16_t slot_id = 0;

    assert(page_init(page, 1) == DB_OK);

    assert(page_insert(page, row_bytes, 0, &slot_id) == DB_ERROR);
}

static void test_page_get_existing_row(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint8_t row_bytes[] = {10, 20, 30, 40};
    uint16_t slot_id = 0;

    assert(page_insert(page, row_bytes, sizeof(row_bytes), &slot_id) == DB_OK);

    uint8_t *out_ptr = NULL;
    uint32_t out_len = 0;

    assert(page_get(page, slot_id, &out_ptr, &out_len) == DB_OK);

    assert(out_ptr != NULL);
    assert(out_len == sizeof(row_bytes));
    assert(memcmp(out_ptr, row_bytes, sizeof(row_bytes)) == 0);
}

static void test_page_get_rejects_null_inputs(void) {
    uint8_t page[PAGE_SIZE];
    uint8_t *out_ptr = NULL;
    uint32_t out_len = 0;

    assert(page_init(page, 1) == DB_OK);

    assert(page_get(NULL, 0, &out_ptr, &out_len) == DB_ERROR);
    assert(page_get(page, 0, NULL, &out_len) == DB_ERROR);
    assert(page_get(page, 0, &out_ptr, NULL) == DB_ERROR);
}

static void test_page_get_missing_slot(void) {
    uint8_t page[PAGE_SIZE];
    uint8_t *out_ptr = NULL;
    uint32_t out_len = 0;

    assert(page_init(page, 1) == DB_OK);

    assert(page_get(page, 0, &out_ptr, &out_len) == DB_NOT_FOUND);
}

static void test_page_delete_existing_row(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint8_t row_bytes[] = {1, 2, 3};
    uint16_t slot_id = 0;

    assert(page_insert(page, row_bytes, sizeof(row_bytes), &slot_id) == DB_OK);
    assert(page_slot_is_active(page, slot_id) == true);

    assert(page_delete(page, slot_id) == DB_OK);
    assert(page_slot_is_active(page, slot_id) == false);
}

static void test_page_delete_rejects_null(void) {
    assert(page_delete(NULL, 0) == DB_ERROR);
}

static void test_page_delete_missing_slot(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    assert(page_delete(page, 0) == DB_NOT_FOUND);
}

static void test_page_delete_already_deleted_slot(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint8_t row_bytes[] = {1, 2, 3};
    uint16_t slot_id = 0;

    assert(page_insert(page, row_bytes, sizeof(row_bytes), &slot_id) == DB_OK);

    assert(page_delete(page, slot_id) == DB_OK);
    assert(page_delete(page, slot_id) == DB_NOT_FOUND);
}

static void test_page_update_existing_row(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint8_t row_bytes[] = {1, 2, 3, 4};
    uint8_t updated_bytes[] = {9, 8, 7};
    uint16_t slot_id = 0;

    assert(page_insert(page, row_bytes, sizeof(row_bytes), &slot_id) == DB_OK);
    assert(page_update(page, slot_id, updated_bytes, sizeof(updated_bytes)) == DB_OK);

    uint8_t *out_ptr = NULL;
    uint32_t out_len = 0;

    assert(page_get(page, slot_id, &out_ptr, &out_len) == DB_OK);
    assert(out_len == sizeof(updated_bytes));
    assert(memcmp(out_ptr, updated_bytes, sizeof(updated_bytes)) == 0);
}

static void test_page_update_rejects_growth(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint8_t row_bytes[] = {1, 2, 3};
    uint8_t updated_bytes[] = {1, 2, 3, 4};
    uint16_t slot_id = 0;

    assert(page_insert(page, row_bytes, sizeof(row_bytes), &slot_id) == DB_OK);
    assert(page_update(page, slot_id, updated_bytes, sizeof(updated_bytes)) == DB_FULL);
}

static void test_page_update_rejects_deleted_slot(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint8_t row_bytes[] = {1, 2, 3};
    uint8_t updated_bytes[] = {7, 8, 9};
    uint16_t slot_id = 0;

    assert(page_insert(page, row_bytes, sizeof(row_bytes), &slot_id) == DB_OK);
    assert(page_delete(page, slot_id) == DB_OK);
    assert(page_update(page, slot_id, updated_bytes, sizeof(updated_bytes)) == DB_NOT_FOUND);
}

static void test_page_reuses_deleted_slot(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint8_t row_one[] = {1, 2, 3};
    uint8_t row_two[] = {4, 5, 6};

    uint16_t first_slot = 0;
    uint16_t second_slot = 0;

    assert(page_insert(page, row_one, sizeof(row_one), &first_slot) == DB_OK);
    assert(page_delete(page, first_slot) == DB_OK);

    assert(page_insert(page, row_two, sizeof(row_two), &second_slot) == DB_OK);

    assert(first_slot == 0);
    assert(second_slot == 0);
    assert(page_slot_count(page) == 1);
    assert(page_slot_is_active(page, second_slot) == true);
}

static void test_page_free_space_decreases_after_insert(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint32_t before = page_free_space(page);

    uint8_t row_bytes[] = {1, 2, 3, 4};
    uint16_t slot_id = 0;

    assert(page_insert(page, row_bytes, sizeof(row_bytes), &slot_id) == DB_OK);

    uint32_t after = page_free_space(page);

    assert(after < before);
}

static void test_page_free_space_null(void) {
    assert(page_free_space(NULL) == 0);
}

static void test_page_insertable_space_counts_deleted_slot(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint8_t row_bytes[] = {1, 2, 3, 4};
    uint16_t slot_id = 0;

    assert(page_insert(page, row_bytes, sizeof(row_bytes), &slot_id) == DB_OK);

    uint32_t free_before_delete = page_free_space(page);

    assert(page_delete(page, slot_id) == DB_OK);
    assert(page_free_space(page) == free_before_delete);
    assert(page_insertable_space(page) == free_before_delete + sizeof(PageSlot));
}

static void test_page_insertable_space_null(void) {
    assert(page_insertable_space(NULL) == 0);
}

static void test_page_slot_count_null(void) {
    assert(page_slot_count(NULL) == 0);
}

static void test_page_slot_is_active_invalid_inputs(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    assert(page_slot_is_active(NULL, 0) == false);
    assert(page_slot_is_active(page, 0) == false);
}

static void test_page_insert_until_full(void) {
    uint8_t page[PAGE_SIZE];

    assert(page_init(page, 1) == DB_OK);

    uint8_t row_bytes[512];
    memset(row_bytes, 7, sizeof(row_bytes));

    DBStatus status = DB_OK;
    uint16_t slot_id = 0;

    while (status == DB_OK) {
        status = page_insert(page, row_bytes, sizeof(row_bytes), &slot_id);
    }

    assert(status == DB_FULL);
}

int main(void) {
    test_page_init();
    test_page_init_rejects_null();
    test_page_insert_single_row();
    test_page_insert_multiple_rows();
    test_page_insert_rejects_null_inputs();
    test_page_insert_rejects_zero_length_row();
    test_page_get_existing_row();
    test_page_get_rejects_null_inputs();
    test_page_get_missing_slot();
    test_page_delete_existing_row();
    test_page_delete_rejects_null();
    test_page_delete_missing_slot();
    test_page_delete_already_deleted_slot();
    test_page_update_existing_row();
    test_page_update_rejects_growth();
    test_page_update_rejects_deleted_slot();
    test_page_reuses_deleted_slot();
    test_page_free_space_decreases_after_insert();
    test_page_free_space_null();
    test_page_insertable_space_counts_deleted_slot();
    test_page_insertable_space_null();
    test_page_slot_count_null();
    test_page_slot_is_active_invalid_inputs();
    test_page_insert_until_full();

    printf("All page tests passed.\n");

    return 0;
}
