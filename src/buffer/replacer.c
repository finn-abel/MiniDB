#include <stdbool.h>
#include <stdint.h>

#include "buffer/buffer_pool.h"
#include "buffer/replacer.h"
#include "common.h"

DBStatus replacer_choose_victim(
    BufferFrame *frames,
    uint32_t frame_count,
    uint32_t *out_frame_index
) {
    if (frames == NULL || out_frame_index == NULL || frame_count == 0) {
        return DB_ERROR;
    }

    /*
     * Empty frames do not require eviction, so use them before touching valid
     * cached pages.
     */
    for (uint32_t i = 0; i < frame_count; i++) {
        if (!frames[i].is_valid) {
            *out_frame_index = i;
            return DB_OK;
        }
    }

    bool found = false;
    uint32_t victim_index = 0;
    uint64_t oldest_usage = 0;

    /*
     * LRU among unpinned frames. Pinned frames are actively in use and cannot
     * be evicted safely.
     */
    for (uint32_t i = 0; i < frame_count; i++) {
        if (frames[i].pin_count != 0) {
            continue;
        }

        if (!found || frames[i].usage_counter < oldest_usage) {
            found = true;
            victim_index = i;
            oldest_usage = frames[i].usage_counter;
        }
    }

    if (!found) {
        return DB_FULL;
    }

    *out_frame_index = victim_index;

    return DB_OK;
}
