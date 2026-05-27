#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common.h"
#include "page.h"

static void page_read_header(const uint8_t *page_bytes, PageHeader *header) {
    /*
     * Copy the header out of the raw page bytes.
     * Using memcpy avoids alignment issues from casting uint8_t pointers.
     */
    memcpy(header, page_bytes, sizeof(PageHeader));
}

static void page_write_header(uint8_t *page_bytes, const PageHeader *header) {
    /*
     * Write the updated header back into the start of the page.
     */
    memcpy(page_bytes, header, sizeof(PageHeader));
}

static uint16_t page_slot_offset(uint16_t slot_id) {
    /*
     * Slots are stored directly after the PageHeader.
     * Slot 0 starts right after the header.
     */
    return (uint16_t)(sizeof(PageHeader) + slot_id * sizeof(PageSlot));
}

static void page_read_slot(
    const uint8_t *page_bytes,
    uint16_t slot_id,
    PageSlot *slot
) {
    /*
     * Read one slot from the slot directory.
     */
    uint16_t offset = page_slot_offset(slot_id);
    memcpy(slot, page_bytes + offset, sizeof(PageSlot));
}

static void page_write_slot(
    uint8_t *page_bytes,
    uint16_t slot_id,
    const PageSlot *slot
) {
    /*
     * Write one slot into the slot directory.
     */
    uint16_t offset = page_slot_offset(slot_id);
    memcpy(page_bytes + offset, slot, sizeof(PageSlot));
}

static bool page_find_deleted_slot(
    const uint8_t *page_bytes,
    const PageHeader *header,
    uint16_t *out_slot_id
) {
    /*
     * Deleted slots can be reused.
     * This avoids growing the slot directory when an old slot is available.
     */
    for (uint16_t i = 0; i < header->slot_count; i++) {
        PageSlot slot;
        page_read_slot(page_bytes, i, &slot);

        if (slot.flags == PAGE_SLOT_DELETED) {
            *out_slot_id = i;
            return true;
        }
    }

    return false;
}

DBStatus page_init(uint8_t *page_bytes, uint32_t page_id) {
    if (page_bytes == NULL) {
        return DB_ERROR;
    }

    /*
     * Start with a fully cleared page.
     * This makes unused bytes deterministic and easier to debug.
     */
    memset(page_bytes, 0, PAGE_SIZE);

    PageHeader header;

    header.page_id = page_id;
    header.slot_count = 0;
    header.free_start = sizeof(PageHeader);
    header.free_end = PAGE_SIZE;
    header.page_type = PAGE_TYPE_DATA;

    page_write_header(page_bytes, &header);

    return DB_OK;
}

DBStatus page_insert(
    uint8_t *page_bytes,
    const uint8_t *row_bytes,
    uint32_t row_len,
    uint16_t *out_slot_id
) {
    if (
        page_bytes == NULL ||
        row_bytes == NULL ||
        out_slot_id == NULL ||
        row_len == 0 ||
        row_len > UINT16_MAX
    ) {
        return DB_ERROR;
    }

    PageHeader header;
    page_read_header(page_bytes, &header);

    if (header.page_type != PAGE_TYPE_DATA) {
        return DB_ERROR;
    }

    uint16_t slot_id = 0;
    bool reused_slot = page_find_deleted_slot(page_bytes, &header, &slot_id);

    /*
     * If we are not reusing a deleted slot, inserting requires space for:
     * 1. the row bytes
     * 2. one new PageSlot in the slot directory
     */
    uint32_t needed_space = row_len;

    if (!reused_slot) {
        needed_space += sizeof(PageSlot);
    }

    if (page_free_space(page_bytes) < needed_space) {
        return DB_FULL;
    }

    /*
     * Row data grows backward from the end of the page.
     */
    uint16_t row_offset = (uint16_t)(header.free_end - row_len);

    memcpy(page_bytes + row_offset, row_bytes, row_len);

    PageSlot slot;

    slot.offset = row_offset;
    slot.length = (uint16_t)row_len;
    slot.flags = PAGE_SLOT_ACTIVE;

    if (!reused_slot) {
        slot_id = header.slot_count;
        header.slot_count++;
        header.free_start = (uint16_t)(header.free_start + sizeof(PageSlot));
    }

    /*
     * Update the row-data boundary after copying the row.
     */
    header.free_end = row_offset;

    page_write_slot(page_bytes, slot_id, &slot);
    page_write_header(page_bytes, &header);

    *out_slot_id = slot_id;

    return DB_OK;
}

DBStatus page_get(
    uint8_t *page_bytes,
    uint16_t slot_id,
    uint8_t **out_ptr,
    uint32_t *out_len
) {
    if (page_bytes == NULL || out_ptr == NULL || out_len == NULL) {
        return DB_ERROR;
    }

    PageHeader header;
    page_read_header(page_bytes, &header);

    if (header.page_type != PAGE_TYPE_DATA) {
        return DB_ERROR;
    }

    if (slot_id >= header.slot_count) {
        return DB_NOT_FOUND;
    }

    PageSlot slot;
    page_read_slot(page_bytes, slot_id, &slot);

    if (slot.flags != PAGE_SLOT_ACTIVE) {
        return DB_NOT_FOUND;
    }

    /*
     * Make sure the slot points to bytes inside the page.
     * This protects us from reading corrupted slot metadata.
     */
    if (
        slot.offset >= PAGE_SIZE ||
        slot.length == 0 ||
        slot.offset + slot.length > PAGE_SIZE
    ) {
        return DB_ERROR;
    }

    *out_ptr = page_bytes + slot.offset;
    *out_len = slot.length;

    return DB_OK;
}

DBStatus page_delete(uint8_t *page_bytes, uint16_t slot_id) {
    if (page_bytes == NULL) {
        return DB_ERROR;
    }

    PageHeader header;
    page_read_header(page_bytes, &header);

    if (header.page_type != PAGE_TYPE_DATA) {
        return DB_ERROR;
    }

    if (slot_id >= header.slot_count) {
        return DB_NOT_FOUND;
    }

    PageSlot slot;
    page_read_slot(page_bytes, slot_id, &slot);

    if (slot.flags != PAGE_SLOT_ACTIVE) {
        return DB_NOT_FOUND;
    }

    /*
     * Mark the slot deleted.
     * The old row bytes remain in the page for now.
     */
    slot.flags = PAGE_SLOT_DELETED;
    page_write_slot(page_bytes, slot_id, &slot);

    return DB_OK;
}

uint32_t page_free_space(const uint8_t *page_bytes) {
    if (page_bytes == NULL) {
        return 0;
    }

    PageHeader header;
    page_read_header(page_bytes, &header);

    if (header.free_end < header.free_start) {
        return 0;
    }

    /*
     * Free space is the gap between:
     * - the slot directory growing forward
     * - the row data growing backward
     */
    return (uint32_t)(header.free_end - header.free_start);
}

uint16_t page_slot_count(const uint8_t *page_bytes) {
    if (page_bytes == NULL) {
        return 0;
    }

    PageHeader header;
    page_read_header(page_bytes, &header);

    return header.slot_count;
}

bool page_slot_is_active(const uint8_t *page_bytes, uint16_t slot_id) {
    if (page_bytes == NULL) {
        return false;
    }

    PageHeader header;
    page_read_header(page_bytes, &header);

    if (header.page_type != PAGE_TYPE_DATA) {
        return false;
    }

    if (slot_id >= header.slot_count) {
        return false;
    }

    PageSlot slot;
    page_read_slot(page_bytes, slot_id, &slot);

    return slot.flags == PAGE_SLOT_ACTIVE;
}
