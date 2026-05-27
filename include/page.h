#ifndef PAGE_H
#define PAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "common.h"

/*
 * Page types let us distinguish different kinds of pages later.
 * For now, MiniDB only has normal data pages.
 */
#define PAGE_TYPE_DATA 1

/*
 * Slot flags describe the state of a slot.
 * ACTIVE means the slot currently points to valid row bytes.
 * DELETED means the slot no longer contains a live row.
 */
#define PAGE_SLOT_ACTIVE 1
#define PAGE_SLOT_DELETED 2

/*
 * The page header lives at the beginning of every page.
 * It tracks the page identity, slot count, free-space boundaries,
 * and the type of page being stored.
 */
typedef struct {
    uint32_t page_id;
    uint16_t slot_count;
    uint16_t free_start;
    uint16_t free_end;
    uint8_t page_type;
} PageHeader;

/*
 * A slot points to one row stored inside the page.
 * offset is where the row bytes begin.
 * length is the number of row bytes.
 * flags tells us whether the slot is active or deleted.
 */
typedef struct {
    uint16_t offset;
    uint16_t length;
    uint8_t flags;
} PageSlot;

/*
 * Initializes a blank page in memory.
 * This clears all PAGE_SIZE bytes and writes a fresh PageHeader.
 * The page starts with zero slots and all remaining space free.
 */
DBStatus page_init(uint8_t *page_bytes, uint32_t page_id);

/*
 * Inserts raw row bytes into the page.
 * The row data is copied into the page from the end backward.
 * out_slot_id stores the slot number assigned to the inserted row.
 */
DBStatus page_insert(
    uint8_t *page_bytes,
    const uint8_t *row_bytes,
    uint32_t row_len,
    uint16_t *out_slot_id
);

/*
 * Gets a pointer to raw row bytes stored in a slot.
 * This does not copy the row data; out_ptr points inside page_bytes.
 * The slot must exist and must be active.
 */
DBStatus page_get(
    uint8_t *page_bytes,
    uint16_t slot_id,
    uint8_t **out_ptr,
    uint32_t *out_len
);

/*
 * Marks a slot as deleted.
 * This does not compact the page or reclaim the row bytes yet.
 * Later, a compaction step can be added if needed.
 */
DBStatus page_delete(uint8_t *page_bytes, uint16_t slot_id);

/*
 * Returns the amount of contiguous free space in the page.
 * This is the space between the slot directory and row data area.
 * Deleted row data is not counted as free space yet.
 */
uint32_t page_free_space(const uint8_t *page_bytes);

/*
 * Returns the number of slots currently in the slot directory.
 * Some of these slots may be deleted.
 * Use page_slot_is_active to check whether a slot is live.
 */
uint16_t page_slot_count(const uint8_t *page_bytes);

/*
 * Checks whether a slot exists and is active.
 * Returns false for invalid pages, invalid slot IDs, and deleted slots.
 * This is useful before reading or deleting a row.
 */
bool page_slot_is_active(const uint8_t *page_bytes, uint16_t slot_id);

#endif
