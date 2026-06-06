#ifndef MINIDB_STORAGE_FREE_SPACE_H
#define MINIDB_STORAGE_FREE_SPACE_H

#include <stdint.h>

#include "common.h"

/*
 * Finds a page in table_file with at least required_bytes of free space.
 */
DBStatus free_space_find_page(
    const char *table_file,
    uint32_t required_bytes,
    uint32_t *out_page_id
);

/*
 * Updates the remembered free-space value for one page.
 */
DBStatus free_space_update(
    const char *table_file,
    uint32_t page_id,
    uint32_t free_bytes
);

/*
 * Rebuilds the in-memory free-space map by scanning every page in table_file.
 *
 * This is the first simple version of an FSM. Later, this can be persisted
 * under mydb/fsm/ instead of being rebuilt from table pages.
 */
DBStatus free_space_rebuild(const char *table_file);

#endif
