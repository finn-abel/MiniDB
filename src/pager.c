#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "pager.h"

static DBStatus pager_seek_to_page(Pager *pager, uint32_t page_id) {
    /*
     * Each page starts at page_id * PAGE_SIZE.
     * This converts a logical page number into a physical file offset.
     */
    long offset = (long)page_id * PAGE_SIZE;

    if (fseek(pager->file, offset, SEEK_SET) != 0) {
        return DB_IO_ERROR;
    }

    return DB_OK;
}

static DBStatus pager_file_size(FILE *file, long *out_size) {
    if (file == NULL || out_size == NULL) {
        return DB_ERROR;
    }

    /*
     * Move to the end of the file so ftell can report the file size.
     */
    if (fseek(file, 0, SEEK_END) != 0) {
        return DB_IO_ERROR;
    }

    long size = ftell(file);

    if (size < 0) {
        return DB_IO_ERROR;
    }

    *out_size = size;

    return DB_OK;
}

DBStatus pager_open(Pager *pager, const char *path) {
    if (pager == NULL || path == NULL) {
        return DB_ERROR;
    }

    /*
     * Clear the pager first so it starts in a predictable state.
     */
    pager->file = NULL;
    pager->path[0] = '\0';
    pager->num_pages = 0;

    /*
     * Try opening an existing file first.
     * If it does not exist, create a new empty file.
     */
    FILE *file = fopen(path, "r+b");

    if (file == NULL) {
        file = fopen(path, "w+b");

        if (file == NULL) {
            return DB_IO_ERROR;
        }
    }

    long file_size = 0;
    DBStatus status = pager_file_size(file, &file_size);

    if (status != DB_OK) {
        fclose(file);
        return status;
    }

    /*
     * The pager only works with full PAGE_SIZE chunks.
     * A partial page means the file is corrupted or not a MiniDB page file.
     */
    if (file_size % PAGE_SIZE != 0) {
        fclose(file);
        return DB_IO_ERROR;
    }

    pager->file = file;
    pager->num_pages = (uint32_t)(file_size / PAGE_SIZE);

    /*
     * Store the path for debugging and future error messages.
     */
    strncpy(pager->path, path, MAX_DB_PATH - 1);
    pager->path[MAX_DB_PATH - 1] = '\0';

    return DB_OK;
}

DBStatus pager_close(Pager *pager) {
    if (pager == NULL) {
        return DB_ERROR;
    }

    if (pager->file == NULL) {
        return DB_OK;
    }

    /*
     * Flush buffered writes before closing the file.
     */
    if (fflush(pager->file) != 0) {
        fclose(pager->file);

        pager->file = NULL;
        pager->path[0] = '\0';
        pager->num_pages = 0;

        return DB_IO_ERROR;
    }

    if (fclose(pager->file) != 0) {
        pager->file = NULL;
        pager->path[0] = '\0';
        pager->num_pages = 0;

        return DB_IO_ERROR;
    }

    /*
     * Reset after close so the Pager cannot accidentally reuse old state.
     */
    pager->file = NULL;
    pager->path[0] = '\0';
    pager->num_pages = 0;

    return DB_OK;
}

DBStatus pager_read_page(Pager *pager, uint32_t page_id, uint8_t *page_buffer) {
    if (pager == NULL || pager->file == NULL || page_buffer == NULL) {
        return DB_ERROR;
    }

    /*
     * You can only read pages that already exist.
     */
    if (page_id >= pager->num_pages) {
        return DB_NOT_FOUND;
    }

    DBStatus status = pager_seek_to_page(pager, page_id);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Read exactly one PAGE_SIZE block into memory.
     */
    size_t items_read = fread(page_buffer, PAGE_SIZE, 1, pager->file);

    if (items_read != 1) {
        return DB_IO_ERROR;
    }

    return DB_OK;
}

DBStatus pager_write_page(Pager *pager, uint32_t page_id, const uint8_t *page_buffer) {
    if (pager == NULL || pager->file == NULL || page_buffer == NULL) {
        return DB_ERROR;
    }

    /*
     * This function updates existing pages only.
     * New pages should be created with pager_allocate_page.
     */
    if (page_id >= pager->num_pages) {
        return DB_NOT_FOUND;
    }

    DBStatus status = pager_seek_to_page(pager, page_id);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Write exactly one PAGE_SIZE block to disk.
     */
    size_t items_written = fwrite(page_buffer, PAGE_SIZE, 1, pager->file);

    if (items_written != 1) {
        return DB_IO_ERROR;
    }

    /*
     * Flush now so tests can close/reopen and see the written bytes.
     */
    if (fflush(pager->file) != 0) {
        return DB_IO_ERROR;
    }

    return DB_OK;
}

DBStatus pager_allocate_page(
    Pager *pager,
    const uint8_t *page_buffer,
    uint32_t *out_page_id
) {
    if (pager == NULL || pager->file == NULL || page_buffer == NULL || out_page_id == NULL) {
        return DB_ERROR;
    }

    /*
     * The new page ID is the current number of pages.
     * Example: if the file has 3 pages, the new page is page 3.
     */
    uint32_t new_page_id = pager->num_pages;

    DBStatus status = pager_seek_to_page(pager, new_page_id);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Append one full page to the end of the file.
     */
    size_t items_written = fwrite(page_buffer, PAGE_SIZE, 1, pager->file);

    if (items_written != 1) {
        return DB_IO_ERROR;
    }

    if (fflush(pager->file) != 0) {
        return DB_IO_ERROR;
    }

    pager->num_pages++;
    *out_page_id = new_page_id;

    return DB_OK;
}

uint32_t pager_num_pages(const Pager *pager) {
    if (pager == NULL) {
        return 0;
    }

    return pager->num_pages;
}
