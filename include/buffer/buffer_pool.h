#ifndef MINIDB_BUFFER_POOL_H
#define MINIDB_BUFFER_POOL_H

#include <stdbool.h>
#include <stdint.h>

#include "common.h"

/*
 * Keep the first buffer pool intentionally small.
 * This is enough to exercise caching and eviction without hiding bugs.
 */
#define BUFFER_POOL_SIZE 16

/*
 * BufferFrame stores one cached page.
 *
 * table_file and page_id identify the disk page.
 * page_bytes is the in-memory copy.
 * is_dirty means the memory copy must be written back before eviction.
 * pin_count prevents active pages from being evicted.
 * usage_counter is updated on access and used by the simple LRU replacer.
 */
typedef struct {
    bool is_valid;
    char table_file[MAX_DB_PATH];
    uint32_t page_id;
    uint8_t page_bytes[PAGE_SIZE];
    bool is_dirty;
    uint32_t pin_count;
    uint64_t usage_counter;
} BufferFrame;

/*
 * Fetches an existing page into the buffer pool and pins it.
 *
 * out_page points directly at the cached page bytes. The caller must unpin the
 * page when done, marking it dirty if the bytes were changed.
 */
DBStatus buffer_pool_fetch_page(
    const char *table_file,
    uint32_t page_id,
    uint8_t **out_page
);

/*
 * Allocates a new page at the end of a table file, caches it, and pins it.
 *
 * The returned page starts zeroed. Callers normally initialize it with
 * page_init before unpinning it as dirty.
 */
DBStatus buffer_pool_new_page(
    const char *table_file,
    uint32_t *out_page_id,
    uint8_t **out_page
);

/*
 * Releases one pin on a cached page.
 * If is_dirty is true, the page will be written before eviction or flush-all.
 */
DBStatus buffer_pool_unpin_page(
    const char *table_file,
    uint32_t page_id,
    bool is_dirty
);

/*
 * Writes one dirty cached page to disk.
 */
DBStatus buffer_pool_flush_page(const char *table_file, uint32_t page_id);

/*
 * Writes every dirty cached page to disk.
 * This is called during db_close so table changes survive shutdown.
 */
DBStatus buffer_pool_flush_all(void);

/*
 * Drops cached pages for one file.
 *
 * This is used when a file is intentionally rebuilt from scratch. Pinned pages
 * cannot be discarded.
 */
DBStatus buffer_pool_discard_file(const char *table_file);

/*
 * Returns the number of pages currently in a table file.
 */
DBStatus buffer_pool_page_count(const char *table_file, uint32_t *out_page_count);

#endif
