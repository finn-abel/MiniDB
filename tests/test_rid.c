#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rid.h"

static void test_rid_equal_matching(void) {
    RID left = {3, 7};
    RID right = {3, 7};

    assert(rid_equal(&left, &right) == true);
}

static void test_rid_equal_different_page(void) {
    RID left = {3, 7};
    RID right = {4, 7};

    assert(rid_equal(&left, &right) == false);
}

static void test_rid_equal_different_slot(void) {
    RID left = {3, 7};
    RID right = {3, 8};

    assert(rid_equal(&left, &right) == false);
}

static void test_rid_equal_null(void) {
    RID rid = {3, 7};

    assert(rid_equal(&rid, NULL) == false);
    assert(rid_equal(NULL, &rid) == false);
    assert(rid_equal(NULL, NULL) == false);
}

static void test_rid_print(void) {
    RID rid = {3, 7};

    char buffer[64];

    FILE *out = fmemopen(buffer, sizeof(buffer), "w");
    assert(out != NULL);

    rid_print(&rid, out);
    fclose(out);

    assert(strcmp(buffer, "RID(page=3, slot=7)") == 0);
}

int main(void) {
    test_rid_equal_matching();
    test_rid_equal_different_page();
    test_rid_equal_different_slot();
    test_rid_equal_null();
    test_rid_print();

    printf("All RID tests passed.\n");

    return 0;
}
