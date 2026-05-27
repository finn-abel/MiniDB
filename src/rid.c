#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "rid.h"

void rid_print(const RID *rid, FILE *out) {
    if (rid == NULL || out == NULL) {
        return;
    }

    /*
     * Keep the output simple and readable.
     * This will help when debugging pages, records, and indexes later.
     */
    fprintf(out, "RID(page=%u, slot=%u)", rid->page_id, rid->slot_id);
}

bool rid_equal(const RID *left, const RID *right) {
    if (left == NULL || right == NULL) {
        return false;
    }

    /*
     * Two RIDs are equal only if they point to the exact same page and slot.
     */
    return left->page_id == right->page_id &&
           left->slot_id == right->slot_id;
}
