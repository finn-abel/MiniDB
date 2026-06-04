#ifndef SCHEMA_H
#define SCHEMA_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "common.h"
#include "row.h"
#include "value.h"

/*
 * A Column describes one field in a table.
 * It stores the column name, expected value type,
 * and basic constraint flags for later validation.
 */
typedef struct {
    char name[MAX_COLUMN_NAME];
    ValueType type;
    bool not_null;
    bool primary_key;
} Column;

/*
 * A Schema describes the structure of a table.
 * Rows only store values by position.
 * The schema explains what each position means.
 */
typedef struct {
    char table_name[MAX_TABLE_NAME];
    uint16_t column_count;
    Column columns[MAX_COLUMNS];
} Schema;

/*
 * Initializes an empty schema for a table.
 * The table name is copied into the schema.
 * Columns can be added afterward using schema_add_column.
 */
DBStatus schema_init(Schema *schema, const char *table_name);

/*
 * Adds one column to a schema.
 * The column name and type are stored in the next available column slot.
 * not_null and primary_key are saved for validation and future use.
 */
DBStatus schema_add_column(
    Schema *schema,
    const char *column_name,
    ValueType type,
    bool not_null,
    bool primary_key
);

/*
 * Finds the position of a column by name.
 * Returns DB_OK if the column exists.
 * Stores the column index in out_index.
 */
DBStatus schema_get_column_index(
    const Schema *schema,
    const char *column_name,
    uint16_t *out_index
);

/*
 * Finds the type of a column by name.
 * Returns DB_OK if the column exists.
 * Stores the column type in out_type.
 */
DBStatus schema_get_column_type(
    const Schema *schema,
    const char *column_name,
    ValueType *out_type
);

/*
 * Checks whether a row matches the schema.
 * The row must have the same number of values as the schema has columns.
 * Each row value must match the expected column type.
 */
DBStatus schema_validate_row(const Schema *schema, const Row *row);

/*
 * Checks whether a row satisfies one WHERE condition.
 *
 * The schema maps the condition's column name to the row value index, then the
 * value layer performs the actual operator comparison.
 */
DBStatus row_matches_condition(
    const Row *row,
    const Schema *schema,
    const WhereCondition *condition,
    bool *out_matches
);

/*
 * Prints the schema in a readable format.
 * Example output: users (id INT, name TEXT, age INT)
 * This is mainly useful for debugging and later .schema output.
 */
void schema_print(const Schema *schema, FILE *out);

#endif
