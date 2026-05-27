#include <stdio.h>

#include "row.h"

int main(void) {
    Row row;

    if (row_create(&row, 3) != DB_OK) {
        fprintf(stderr, "Failed to create row.\n");
        return 1;
    }

    row.values[0] = value_int(1);

    if (value_text(&row.values[1], "Finn") != DB_OK) {
        fprintf(stderr, "Failed to create text value.\n");
        row_free(&row);
        return 1;
    }

    row.values[2] = value_int(20);

    printf("row = ");
    row_print(&row, stdout);
    printf("\n");

    const Value *name = row_get_value_const(&row, 1);
    if (name == NULL) {
        fprintf(stderr, "Failed to get value.\n");
        row_free(&row);
        return 1;
    }

    printf("name = ");
    value_print(name, stdout);
    printf("\n");

    row_free(&row);

    return 0;
}
