#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "row.h"
#include "schema.h"
#include "value.h"

static const char *schema_type_name(ValueType type) {
    /*
     * Convert a ValueType into a readable string.
     * This keeps schema_print simple.
     */
    if (type == VALUE_INT) {
        return "INT";
    }

    if (type == VALUE_TEXT) {
        return "TEXT";
    }

    return "UNKNOWN";
}

static bool schema_valid_name(const char *name, uint32_t max_len) {
    if (name == NULL) {
        return false;
    }

    /*
     * Names must be non-empty and fit inside the fixed-size name buffer.
     * We reserve one byte for the null terminator.
     */
    size_t len = strlen(name);

    return len > 0 && len < max_len;
}

DBStatus schema_init(Schema *schema, const char *table_name) {
    if (schema == NULL || !schema_valid_name(table_name, MAX_TABLE_NAME)) {
        return DB_ERROR;
    }

    /*
     * Clear the full schema so unused columns start in a predictable state.
     */
    memset(schema, 0, sizeof(Schema));

    /*
     * Copy the table name into the fixed-size schema buffer.
     */
    strncpy(schema->table_name, table_name, MAX_TABLE_NAME - 1);
    schema->table_name[MAX_TABLE_NAME - 1] = '\0';

    schema->column_count = 0;

    return DB_OK;
}

DBStatus schema_add_column(
    Schema *schema,
    const char *column_name,
    ValueType type,
    bool not_null,
    bool primary_key
) {
    if (schema == NULL || !schema_valid_name(column_name, MAX_COLUMN_NAME)) {
        return DB_ERROR;
    }

    if (schema->column_count >= MAX_COLUMNS) {
        return DB_FULL;
    }

    if (type != VALUE_INT && type != VALUE_TEXT) {
        return DB_TYPE_ERROR;
    }

    /*
     * Prevent duplicate column names inside the same schema.
     */
    for (uint16_t i = 0; i < schema->column_count; i++) {
        if (strcmp(schema->columns[i].name, column_name) == 0) {
            return DB_ERROR;
        }
    }

    Column *column = &schema->columns[schema->column_count];

    /*
     * Store the column metadata in the next available column slot.
     */
    strncpy(column->name, column_name, MAX_COLUMN_NAME - 1);
    column->name[MAX_COLUMN_NAME - 1] = '\0';

    column->type = type;
    column->not_null = not_null;
    column->primary_key = primary_key;

    schema->column_count++;

    return DB_OK;
}

DBStatus schema_get_column_index(
    const Schema *schema,
    const char *column_name,
    uint16_t *out_index
) {
    if (schema == NULL || column_name == NULL || out_index == NULL) {
        return DB_ERROR;
    }

    /*
     * Column order matters because rows store values by position.
     */
    for (uint16_t i = 0; i < schema->column_count; i++) {
        if (strcmp(schema->columns[i].name, column_name) == 0) {
            *out_index = i;
            return DB_OK;
        }
    }

    return DB_NOT_FOUND;
}

DBStatus schema_get_column_type(
    const Schema *schema,
    const char *column_name,
    ValueType *out_type
) {
    if (schema == NULL || column_name == NULL || out_type == NULL) {
        return DB_ERROR;
    }

    uint16_t index = 0;

    /*
     * Reuse the column lookup logic so name searching stays in one place.
     */
    DBStatus status = schema_get_column_index(schema, column_name, &index);

    if (status != DB_OK) {
        return status;
    }

    *out_type = schema->columns[index].type;

    return DB_OK;
}

DBStatus schema_validate_row(const Schema *schema, const Row *row) {
    if (schema == NULL || row == NULL) {
        return DB_ERROR;
    }

    /*
     * A row must have exactly one value for each schema column.
     */
    if (row->value_count != schema->column_count) {
        return DB_ERROR;
    }

    /*
     * Check each row value against the matching column type.
     */
    for (uint16_t i = 0; i < schema->column_count; i++) {
        const Value *value = row_get_value_const(row, i);

        if (value == NULL) {
            return DB_ERROR;
        }

        if (value->type != schema->columns[i].type) {
            return DB_TYPE_ERROR;
        }

        /*
         * For now, TEXT values should not be NULL.
         * Later, we can add a real NULL value type.
         */
        if (
            schema->columns[i].not_null &&
            value->type == VALUE_TEXT &&
            value->text_value == NULL
        ) {
            return DB_ERROR;
        }
    }

    return DB_OK;
}

DBStatus row_matches_condition(
    const Row *row,
    const Schema *schema,
    const WhereCondition *condition,
    bool *out_matches
) {
    uint16_t column_index = 0;

    if (row == NULL || schema == NULL || condition == NULL || out_matches == NULL) {
        return DB_ERROR;
    }

    /*
     * Rows store values by position, while WHERE conditions name columns.
     * The schema is the bridge between those two representations.
     */
    DBStatus status = schema_get_column_index(
        schema,
        condition->column_name,
        &column_index
    );

    if (status != DB_OK) {
        return status;
    }

    const Value *row_value = row_get_value_const(row, column_index);

    if (row_value == NULL) {
        return DB_ERROR;
    }

    return value_compare(
        row_value,
        condition->operator_type,
        &condition->value,
        out_matches
    );
}

void schema_print(const Schema *schema, FILE *out) {
    if (schema == NULL || out == NULL) {
        return;
    }

    fprintf(out, "%s (", schema->table_name);

    for (uint16_t i = 0; i < schema->column_count; i++) {
        const Column *column = &schema->columns[i];

        fprintf(out, "%s %s", column->name, schema_type_name(column->type));

        if (column->primary_key) {
            fprintf(out, " PRIMARY KEY");
        }

        if (column->not_null) {
            fprintf(out, " NOT NULL");
        }

        if (i + 1 < schema->column_count) {
            fprintf(out, ", ");
        }
    }

    fprintf(out, ")");
}
