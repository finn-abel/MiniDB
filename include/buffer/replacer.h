#ifndef MINIDB_BUFFER_REPLACER_H
#define MINIDB_BUFFER_REPLACER_H

#include <stdint.h>

#include "buffer/buffer_pool.h"
#include "common.h"

/*
 * Chooses a frame that can be used for a new page.
 *
 * Invalid frames are preferred immediately. If all frames are valid, the
 * replacer chooses the unpinned frame with the oldest usage counter.
 */
DBStatus replacer_choose_victim(
    BufferFrame *frames,
    uint32_t frame_count,
    uint32_t *out_frame_index
);

#endif
