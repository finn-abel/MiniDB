#ifndef PAGER_H
#define PAGER_H

#include <stdint.h>
#include <stdio.h>

#include "common.h"

/*
 * Pager manages one disk file made of fixed-size pages.
 * It knows how many pages the file currently has.
 * It does not understand rows, slots, schemas, or SQL.
 */
typedef struct {
    FILE *file;
    char path[MAX_DB_PATH];
    uint32_t num_pages;
} Pager;

/*
 * Opens an existing page file or creates it if it does not exist.
 * The file size must be a multiple of PAGE_SIZE.
 * The pager calculates num_pages from the file size.
 */
DBStatus pager_open(Pager *pager, const char *path);

/*
 * Flushes and closes the pager file.
 * After closing, the Pager is reset to an empty state.
 * This should be called when the database/table is done using the file.
 */
DBStatus pager_close(Pager *pager);

/*
 * Reads one fixed-size page from disk into page_buffer.
 * page_buffer must point to at least PAGE_SIZE bytes.
 * Returns DB_NOT_FOUND if page_id does not exist.
 */
DBStatus pager_read_page(Pager *pager, uint32_t page_id, uint8_t *page_buffer);

/*
 * Writes one fixed-size page buffer to disk.
 * page_buffer must contain exactly PAGE_SIZE bytes of page data.
 * page_id must already exist inside the file.
 */
DBStatus pager_write_page(Pager *pager, uint32_t page_id, const uint8_t *page_buffer);

/*
 * Appends a new fixed-size page to the end of the file.
 * The new page is initialized using page_buffer.
 * out_page_id stores the ID of the newly allocated page.
 */
DBStatus pager_allocate_page(
    Pager *pager,
    const uint8_t *page_buffer,
    uint32_t *out_page_id
);

/*
 * Returns the number of pages currently in the file.
 * If pager is NULL, this returns 0.
 * This is based on the pager's tracked metadata.
 */
uint32_t pager_num_pages(const Pager *pager);

#endif
