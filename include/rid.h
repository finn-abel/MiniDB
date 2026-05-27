#ifndef RID_H
#define RID_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * A RID identifies the physical location of a row.
 * page_id tells us which page contains the row.
 * slot_id tells us which slot inside that page contains the row.
 */
typedef struct {
    uint32_t page_id;
    uint16_t slot_id;
} RID;

/*
 * Prints a RID in a readable format.
 * Example output: RID(page=3, slot=7)
 * This is mainly useful for debugging and tests.
 */
void rid_print(const RID *rid, FILE *out);

/*
 * Compares two RIDs for equality.
 * Returns true only when both page_id and slot_id match.
 * Returns false if either pointer is NULL.
 */
bool rid_equal(const RID *left, const RID *right);

#endif
