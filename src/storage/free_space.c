#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "page.h"
#include "pager.h"
#include "storage/free_space.h"

#define FREE_SPACE_MAX_TABLES 64

typedef struct {
    uint32_t page_id;
    uint32_t free_bytes;
} FreeSpaceEntry;

/*
 * One in-memory FSM map per table file.
 *
 * entries is heap-backed because a table can have more pages than fit nicely
 * in a fixed inline array. The registry itself stays fixed-size for now.
 */
typedef struct {
    bool is_valid;
    char table_file[MAX_DB_PATH];
    uint32_t entry_count;
    uint32_t entry_capacity;
    FreeSpaceEntry *entries;
} FreeSpaceMap;

static FreeSpaceMap free_space_maps[FREE_SPACE_MAX_TABLES];

static FreeSpaceMap *free_space_find_map(const char *table_file) {
    /*
     * Free-space maps are keyed by table file path. Page IDs alone are only
     * meaningful inside one table.
     */
    for (uint32_t i = 0; i < FREE_SPACE_MAX_TABLES; i++) {
        if (
            free_space_maps[i].is_valid &&
            strcmp(free_space_maps[i].table_file, table_file) == 0
        ) {
            return &free_space_maps[i];
        }
    }

    return NULL;
}

static DBStatus free_space_init_map(
    FreeSpaceMap *map,
    const char *table_file
) {
    if (map == NULL || table_file == NULL || strlen(table_file) >= MAX_DB_PATH) {
        return DB_ERROR;
    }

    /*
     * Do not allocate entry storage here. Empty tables are common, and entries
     * will be allocated only when rebuild/update needs them.
     */
    memset(map, 0, sizeof(FreeSpaceMap));
    strncpy(map->table_file, table_file, MAX_DB_PATH - 1);
    map->table_file[MAX_DB_PATH - 1] = '\0';
    map->is_valid = true;

    return DB_OK;
}

static DBStatus free_space_get_or_create_map(
    const char *table_file,
    FreeSpaceMap **out_map
) {
    if (table_file == NULL || out_map == NULL) {
        return DB_ERROR;
    }

    FreeSpaceMap *map = free_space_find_map(table_file);

    if (map != NULL) {
        *out_map = map;
        return DB_OK;
    }

    /*
     * Reuse an unused registry slot for a table's in-memory map.
     */
    for (uint32_t i = 0; i < FREE_SPACE_MAX_TABLES; i++) {
        if (!free_space_maps[i].is_valid) {
            DBStatus status = free_space_init_map(&free_space_maps[i], table_file);

            if (status != DB_OK) {
                return status;
            }

            *out_map = &free_space_maps[i];
            return DB_OK;
        }
    }

    return DB_FULL;
}

static DBStatus free_space_reserve_entries(
    FreeSpaceMap *map,
    uint32_t required_capacity
) {
    if (map == NULL) {
        return DB_ERROR;
    }

    if (required_capacity <= map->entry_capacity) {
        return DB_OK;
    }

    /*
     * Grow geometrically so repeated updates do not realloc on every page.
     */
    uint32_t new_capacity = map->entry_capacity == 0 ? 16 : map->entry_capacity;

    while (new_capacity < required_capacity) {
        new_capacity *= 2;
    }

    FreeSpaceEntry *entries = realloc(
        map->entries,
        new_capacity * sizeof(FreeSpaceEntry)
    );

    if (entries == NULL) {
        return DB_ERROR;
    }

    map->entries = entries;
    map->entry_capacity = new_capacity;

    return DB_OK;
}

static FreeSpaceEntry *free_space_find_entry(
    FreeSpaceMap *map,
    uint32_t page_id
) {
    if (map == NULL) {
        return NULL;
    }

    for (uint32_t i = 0; i < map->entry_count; i++) {
        if (map->entries[i].page_id == page_id) {
            return &map->entries[i];
        }
    }

    return NULL;
}

DBStatus free_space_find_page(
    const char *table_file,
    uint32_t required_bytes,
    uint32_t *out_page_id
) {
    if (table_file == NULL || out_page_id == NULL) {
        return DB_ERROR;
    }

    FreeSpaceMap *map = free_space_find_map(table_file);

    if (map == NULL) {
        DBStatus status = free_space_rebuild(table_file);

        if (status != DB_OK) {
            return status;
        }

        map = free_space_find_map(table_file);
    }

    if (map == NULL) {
        return DB_ERROR;
    }

    /*
     * First-fit keeps this version simple. The map avoids reading every page
     * just to discover that most pages are too full.
     */
    for (uint32_t i = 0; i < map->entry_count; i++) {
        if (map->entries[i].free_bytes >= required_bytes) {
            *out_page_id = map->entries[i].page_id;
            return DB_OK;
        }
    }

    return DB_NOT_FOUND;
}

DBStatus free_space_update(
    const char *table_file,
    uint32_t page_id,
    uint32_t free_bytes
) {
    if (table_file == NULL) {
        return DB_ERROR;
    }

    FreeSpaceMap *map = NULL;
    DBStatus status = free_space_get_or_create_map(table_file, &map);

    if (status != DB_OK) {
        return status;
    }

    FreeSpaceEntry *entry = free_space_find_entry(map, page_id);

    if (entry != NULL) {
        /*
         * Existing pages keep their position in the first-fit ordering.
         */
        entry->free_bytes = free_bytes;
        return DB_OK;
    }

    status = free_space_reserve_entries(map, map->entry_count + 1);

    if (status != DB_OK) {
        return status;
    }

    map->entries[map->entry_count].page_id = page_id;
    map->entries[map->entry_count].free_bytes = free_bytes;
    map->entry_count++;

    return DB_OK;
}

DBStatus free_space_rebuild(const char *table_file) {
    if (table_file == NULL) {
        return DB_ERROR;
    }

    FreeSpaceMap *map = NULL;
    DBStatus status = free_space_get_or_create_map(table_file, &map);

    if (status != DB_OK) {
        return status;
    }

    /*
     * Rebuild means the current page file is authoritative. Keep allocated
     * memory, but replace the map contents.
     */
    map->entry_count = 0;

    Pager pager;

    status = pager_open(&pager, table_file);

    if (status != DB_OK) {
        return status;
    }

    uint32_t page_count = pager_num_pages(&pager);

    status = free_space_reserve_entries(map, page_count);

    if (status != DB_OK) {
        pager_close(&pager);
        return status;
    }

    uint8_t page_buffer[PAGE_SIZE];

    for (uint32_t page_id = 0; page_id < page_count; page_id++) {
        status = pager_read_page(&pager, page_id, page_buffer);

        if (status != DB_OK) {
            pager_close(&pager);
            return status;
        }

        status = page_validate(page_buffer);

        if (status != DB_OK) {
            pager_close(&pager);
            return status;
        }

        /*
         * page_insertable_space reads only page metadata, so rebuild does not
         * need to deserialize rows. Tombstone slots count as reusable slot
         * space, but deleted row bytes are still not reclaimed.
         */
        map->entries[map->entry_count].page_id = page_id;
        map->entries[map->entry_count].free_bytes = page_insertable_space(page_buffer);
        map->entry_count++;
    }

    DBStatus close_status = pager_close(&pager);

    if (close_status != DB_OK) {
        return close_status;
    }

    return DB_OK;
}
