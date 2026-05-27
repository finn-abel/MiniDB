#include <stdio.h>

#include "value.h"

int main(void) {
    Value id = value_int(1);

    Value name;
    if (value_text(&name, "Finn") != DB_OK) {
        fprintf(stderr, "Failed to create text value.\n");
        return 1;
    }

    printf("id = ");
    value_print(&id, stdout);
    printf("\n");

    printf("name = ");
    value_print(&name, stdout);
    printf("\n");

    int cmp_result;
    if (value_compare(&id, &id, &cmp_result) != DB_OK) {
        fprintf(stderr, "Failed to compare values.\n");
        value_free(&name);
        return 1;
    }

    printf("id compared with id = %d\n", cmp_result);

    value_free(&id);
    value_free(&name);

    return 0;
}
