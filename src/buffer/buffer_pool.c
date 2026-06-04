#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "buffer/buffer_pool.h"
#include "buffer/replacer.h"
#include "common.h"
#include "pager.h"

/*
 * The first buffer pool is process-wide and fixed-size.
 * Later, DB can own this state explicitly when multiple open databases matter.
 */
typedef struct {
    BufferFrame frames[BUFFER_POOL_SIZE];
    uint64_t next_usage_counter;
} BufferPool;

static BufferPool global_buffer_pool;

static bool buffer_pool_same_page(
    const BufferFrame *frame,
    const char *table_file,
    uint32_t page_id
) {
    /*
     * Page IDs are only unique inside one table file, so the cache key must
     * include both the file path and the page id.
     */
    return (
        frame->is_valid &&
        frame->page_id == page_id &&
        strcmp(frame->table_file, table_file) == 0
    );
}

static BufferFrame *buffer_pool_find_frame(
    const char *table_file,
    uint32_t page_id
) {
    /*
     * Linear search is fine for a 16-frame first pass. A hash table can come
     * later without changing the public buffer pool API.
     */
    for (uint32_t i = 0; i < BUFFER_POOL_SIZE; i++) {
        BufferFrame *frame = &global_buffer_pool.frames[i];

        if (buffer_pool_same_page(frame, table_file, page_id)) {
            return frame;
        }
    }

    return NULL;
}

static uint64_t buffer_pool_next_usage(void) {
    /*
     * Usage starts at 1 so zero remains the natural value for unused frames.
     */
    global_buffer_pool.next_usage_counter++;

    if (global_buffer_pool.next_usage_counter == 0) {
        global_buffer_pool.next_usage_counter = 1;
    }

    return global_buffer_pool.next_usage_counter;
}

static DBStatus buffer_pool_write_frame(BufferFrame *frame) {
    if (frame == NULL || !frame->is_valid) {
        return DB_ERROR;
    }

    if (!frame->is_dirty) {
        return DB_OK;
    }

    /*
     * The buffer pool opens the pager only for the short disk operation. This
     * keeps ownership simple while record/table code is still path-based.
     */
    Pager pager;
    DBStatus status = pager_open(&pager, frame->table_file);

    if (status != DB_OK) {
        return status;
    }

    status = pager_write_page(&pager, frame->page_id, frame->page_bytes);

    DBStatus close_status = pager_close(&pager);

    if (status != DB_OK) {
        return status;
    }

    if (close_status != DB_OK) {
        return close_status;
    }

    frame->is_dirty = false;

    return DB_OK;
}

static DBStatus buffer_pool_prepare_frame(BufferFrame **out_frame) {
    uint32_t frame_index = 0;

    if (out_frame == NULL) {
        return DB_ERROR;
    }

    /*
     * The replacer handles policy. The pool handles consequences of the
     * choice: flushing dirty data, clearing old identity, and reusing storage.
     */
    DBStatus status = replacer_choose_victim(
        global_buffer_pool.frames,
        BUFFER_POOL_SIZE,
        &frame_index
    );

    if (status != DB_OK) {
        return status;
    }

    BufferFrame *frame = &global_buffer_pool.frames[frame_index];

    /*
     * Dirty victims must reach disk before their frame is reused for another
     * table/page.
     */
    if (frame->is_valid && frame->is_dirty) {
        status = buffer_pool_write_frame(frame);

        if (status != DB_OK) {
            return status;
        }
    }

    memset(frame, 0, sizeof(BufferFrame));
    *out_frame = frame;

    return DB_OK;
}

static DBStatus buffer_pool_set_frame_identity(
    BufferFrame *frame,
    const char *table_file,
    uint32_t page_id
) {
    if (frame == NULL || table_file == NULL || strlen(table_file) >= MAX_DB_PATH) {
        return DB_ERROR;
    }

    /*
     * A frame becomes valid only after its page bytes have been loaded or the
     * new page has been reserved. New/fetched pages start pinned by caller.
     */
    strncpy(frame->table_file, table_file, MAX_DB_PATH - 1);
    frame->table_file[MAX_DB_PATH - 1] = '\0';
    frame->page_id = page_id;
    frame->is_valid = true;
    frame->is_dirty = false;
    frame->pin_count = 1;
    frame->usage_counter = buffer_pool_next_usage();

    return DB_OK;
}

DBStatus buffer_pool_fetch_page(
    const char *table_file,
    uint32_t page_id,
    uint8_t **out_page
) {
    if (table_file == NULL || out_page == NULL) {
        return DB_ERROR;
    }

    BufferFrame *frame = buffer_pool_find_frame(table_file, page_id);

    if (frame != NULL) {
        /*
         * Cache hit: return the existing bytes and add one pin for this caller.
         * The caller must eventually unpin the page.
         */
        frame->pin_count++;
        frame->usage_counter = buffer_pool_next_usage();
        *out_page = frame->page_bytes;
        return DB_OK;
    }

    DBStatus status = buffer_pool_prepare_frame(&frame);

    if (status != DB_OK) {
        return status;
    }

    Pager pager;

    /*
     * Cache miss: read the page from disk into the prepared frame.
     */
    status = pager_open(&pager, table_file);

    if (status != DB_OK) {
        return status;
    }

    status = pager_read_page(&pager, page_id, frame->page_bytes);

    DBStatus close_status = pager_close(&pager);

    if (status != DB_OK) {
        return status;
    }

    if (close_status != DB_OK) {
        return close_status;
    }

    status = buffer_pool_set_frame_identity(frame, table_file, page_id);

    if (status != DB_OK) {
        memset(frame, 0, sizeof(BufferFrame));
        return status;
    }

    *out_page = frame->page_bytes;

    return DB_OK;
}

DBStatus buffer_pool_new_page(
    const char *table_file,
    uint32_t *out_page_id,
    uint8_t **out_page
) {
    if (table_file == NULL || out_page_id == NULL || out_page == NULL) {
        return DB_ERROR;
    }

    BufferFrame *frame = NULL;
    DBStatus status = buffer_pool_prepare_frame(&frame);

    if (status != DB_OK) {
        return status;
    }

    memset(frame->page_bytes, 0, PAGE_SIZE);

    Pager pager;

    status = pager_open(&pager, table_file);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Reserve the page ID on disk immediately. The cached page may be changed
     * before it is flushed later, but pager metadata now knows the page exists.
     */
    status = pager_allocate_page(&pager, frame->page_bytes, out_page_id);

    DBStatus close_status = pager_close(&pager);

    if (status != DB_OK) {
        return status;
    }

    if (close_status != DB_OK) {
        return close_status;
    }

    status = buffer_pool_set_frame_identity(frame, table_file, *out_page_id);

    if (status != DB_OK) {
        memset(frame, 0, sizeof(BufferFrame));
        return status;
    }

    *out_page = frame->page_bytes;

    return DB_OK;
}

DBStatus buffer_pool_unpin_page(
    const char *table_file,
    uint32_t page_id,
    bool is_dirty
) {
    if (table_file == NULL) {
        return DB_ERROR;
    }

    BufferFrame *frame = buffer_pool_find_frame(table_file, page_id);

    if (frame == NULL) {
        return DB_NOT_FOUND;
    }

    if (frame->pin_count == 0) {
        return DB_ERROR;
    }

    /*
     * Dirty state is sticky. A clean unpin must not erase an earlier dirty
     * mark from another caller.
     */
    if (is_dirty) {
        frame->is_dirty = true;
    }

    /*
     * Once pin_count reaches zero, the replacer may choose this frame.
     */
    frame->pin_count--;
    frame->usage_counter = buffer_pool_next_usage();

    return DB_OK;
}

DBStatus buffer_pool_flush_page(const char *table_file, uint32_t page_id) {
    if (table_file == NULL) {
        return DB_ERROR;
    }

    BufferFrame *frame = buffer_pool_find_frame(table_file, page_id);

    if (frame == NULL) {
        return DB_NOT_FOUND;
    }

    return buffer_pool_write_frame(frame);
}

DBStatus buffer_pool_flush_all(void) {
    DBStatus result = DB_OK;

    /*
     * Try every dirty frame even if one write fails. Return the first error so
     * callers still know shutdown/flush was not fully clean.
     */
    for (uint32_t i = 0; i < BUFFER_POOL_SIZE; i++) {
        BufferFrame *frame = &global_buffer_pool.frames[i];

        if (!frame->is_valid || !frame->is_dirty) {
            continue;
        }

        DBStatus status = buffer_pool_write_frame(frame);

        if (status != DB_OK && result == DB_OK) {
            result = status;
        }
    }

    return result;
}

DBStatus buffer_pool_page_count(const char *table_file, uint32_t *out_page_count) {
    if (table_file == NULL || out_page_count == NULL) {
        return DB_ERROR;
    }

    /*
     * Page count still comes from the pager/file metadata. The buffer pool may
     * have cached pages, but the pager owns the authoritative file size.
     */
    Pager pager;
    DBStatus status = pager_open(&pager, table_file);

    if (status != DB_OK) {
        return status;
    }

    *out_page_count = pager_num_pages(&pager);

    DBStatus close_status = pager_close(&pager);

    if (close_status != DB_OK) {
        return close_status;
    }

    return DB_OK;
}
