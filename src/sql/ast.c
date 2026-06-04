#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common.h"
#include "schema.h"
#include "sql/ast.h"
#include "value.h"

/*
 * Copies a table, column, or command name into a fixed-size AST field.
 * Empty and overlong names are rejected so AST nodes never store
 * partially-truncated identifiers.
 */
static DBStatus ast_copy_name(char *dest, uint32_t dest_size, const char *source) {
    if (dest == NULL || source == NULL || dest_size == 0) {
        return DB_ERROR;
    }

    if (strlen(source) == 0 || strlen(source) >= dest_size) {
        return DB_ERROR;
    }

    strncpy(dest, source, dest_size - 1);
    dest[dest_size - 1] = '\0';

    return DB_OK;
}

/*
 * Values can own heap memory, so AST nodes must deep-copy text values instead
 * of sharing pointers with parser temporaries.
 */
static DBStatus ast_copy_value(Value *dest, const Value *source) {
    if (dest == NULL || source == NULL) {
        return DB_ERROR;
    }

    if (source->type == VALUE_INT) {
        *dest = value_int(source->int_value);
        return DB_OK;
    }

    if (source->type == VALUE_TEXT) {
        return value_text(dest, source->text_value);
    }

    return DB_TYPE_ERROR;
}

/*
 * Releases value memory owned by an insert statement.
 */
static void ast_insert_free(InsertStatement *statement) {
    if (statement == NULL) {
        return;
    }

    for (uint16_t i = 0; i < statement->value_count; i++) {
        value_free(&statement->values[i]);
    }
}

DBStatus ast_statement_init(Statement *statement, StatementType type) {
    if (statement == NULL) {
        return DB_ERROR;
    }

    /*
     * Start from a clean payload so counts, flags, and union fields do not
     * carry data from an older parse result.
     */
    memset(statement, 0, sizeof(Statement));
    statement->type = type;

    return DB_OK;
}

void ast_statement_free(Statement *statement) {
    if (statement == NULL) {
        return;
    }

    /*
     * Only some statement variants own heap-backed Value data.
     */
    if (statement->type == STATEMENT_INSERT) {
        ast_insert_free(&statement->insert);
    }

    if (statement->type == STATEMENT_SELECT && statement->select.has_where) {
        ast_where_free(&statement->select.where);
    }

    if (statement->type == STATEMENT_DELETE && statement->delete_statement.has_where) {
        ast_where_free(&statement->delete_statement.where);
    }

    memset(statement, 0, sizeof(Statement));
}

DBStatus ast_create_table_init(
    CreateTableStatement *statement,
    const char *table_name
) {
    if (statement == NULL) {
        return DB_ERROR;
    }

    memset(statement, 0, sizeof(CreateTableStatement));

    return ast_copy_name(statement->table_name, sizeof(statement->table_name), table_name);
}

DBStatus ast_create_table_add_column(
    CreateTableStatement *statement,
    const char *column_name,
    ValueType type
) {
    if (statement == NULL || column_name == NULL) {
        return DB_ERROR;
    }

    /*
     * CREATE TABLE currently supports only the same primitive types as Value.
     */
    if (type != VALUE_INT && type != VALUE_TEXT) {
        return DB_TYPE_ERROR;
    }

    if (statement->column_count >= MAX_COLUMNS) {
        return DB_FULL;
    }

    Column *column = &statement->columns[statement->column_count];

    DBStatus status = ast_copy_name(column->name, sizeof(column->name), column_name);

    if (status != DB_OK) {
        return status;
    }

    column->type = type;
    /*
     * Constraint parsing is not implemented yet, so parsed columns start with
     * no constraints.
     */
    column->not_null = false;
    column->primary_key = false;
    statement->column_count++;

    return DB_OK;
}

DBStatus ast_insert_init(InsertStatement *statement, const char *table_name) {
    if (statement == NULL) {
        return DB_ERROR;
    }

    memset(statement, 0, sizeof(InsertStatement));

    return ast_copy_name(statement->table_name, sizeof(statement->table_name), table_name);
}

DBStatus ast_insert_add_value(InsertStatement *statement, const Value *value) {
    if (statement == NULL || value == NULL) {
        return DB_ERROR;
    }

    if (statement->value_count >= MAX_COLUMNS) {
        return DB_FULL;
    }

    DBStatus status = ast_copy_value(&statement->values[statement->value_count], value);

    if (status != DB_OK) {
        return status;
    }

    statement->value_count++;

    return DB_OK;
}

DBStatus ast_select_init(SelectStatement *statement, const char *table_name) {
    if (statement == NULL) {
        return DB_ERROR;
    }

    memset(statement, 0, sizeof(SelectStatement));

    return ast_copy_name(statement->table_name, sizeof(statement->table_name), table_name);
}

DBStatus ast_select_add_column(SelectStatement *statement, const char *column_name) {
    if (statement == NULL || column_name == NULL) {
        return DB_ERROR;
    }

    if (statement->selected_column_count >= MAX_COLUMNS) {
        return DB_FULL;
    }

    DBStatus status = ast_copy_name(
        statement->selected_columns[statement->selected_column_count],
        MAX_COLUMN_NAME,
        column_name
    );

    if (status != DB_OK) {
        return status;
    }

    statement->selected_column_count++;

    return DB_OK;
}

DBStatus ast_select_set_where(
    SelectStatement *statement,
    const WhereCondition *condition
) {
    if (statement == NULL || condition == NULL) {
        return DB_ERROR;
    }

    if (statement->has_where) {
        /*
         * Replacing a WHERE clause must release the old condition's Value
         * before copying the new one.
         */
        ast_where_free(&statement->where);
    }

    DBStatus status = ast_where_init(
        &statement->where,
        condition->column_name,
        condition->operator_type,
        &condition->value
    );

    if (status != DB_OK) {
        statement->has_where = false;
        return status;
    }

    statement->has_where = true;

    return DB_OK;
}

DBStatus ast_delete_init(DeleteStatement *statement, const char *table_name) {
    if (statement == NULL) {
        return DB_ERROR;
    }

    memset(statement, 0, sizeof(DeleteStatement));

    return ast_copy_name(statement->table_name, sizeof(statement->table_name), table_name);
}

DBStatus ast_delete_set_where(
    DeleteStatement *statement,
    const WhereCondition *condition
) {
    if (statement == NULL || condition == NULL) {
        return DB_ERROR;
    }

    if (statement->has_where) {
        /*
         * Replacing a WHERE clause must release the old condition's Value
         * before copying the new one.
         */
        ast_where_free(&statement->where);
    }

    DBStatus status = ast_where_init(
        &statement->where,
        condition->column_name,
        condition->operator_type,
        &condition->value
    );

    if (status != DB_OK) {
        statement->has_where = false;
        return status;
    }

    statement->has_where = true;

    return DB_OK;
}

DBStatus ast_where_init(
    WhereCondition *condition,
    const char *column_name,
    SqlOperator operator_type,
    const Value *value
) {
    if (condition == NULL || value == NULL) {
        return DB_ERROR;
    }

    if (operator_type > SQL_OPERATOR_LESS_EQUAL) {
        return DB_ERROR;
    }

    /*
     * Clear first so ast_where_free is safe after a partially successful init.
     */
    memset(condition, 0, sizeof(WhereCondition));

    DBStatus status = ast_copy_name(
        condition->column_name,
        sizeof(condition->column_name),
        column_name
    );

    if (status != DB_OK) {
        return status;
    }

    condition->operator_type = operator_type;

    return ast_copy_value(&condition->value, value);
}

void ast_where_free(WhereCondition *condition) {
    if (condition == NULL) {
        return;
    }

    value_free(&condition->value);
    memset(condition, 0, sizeof(WhereCondition));
}

DBStatus ast_meta_command_init(
    MetaCommandStatement *statement,
    const char *command
) {
    if (statement == NULL) {
        return DB_ERROR;
    }

    memset(statement, 0, sizeof(MetaCommandStatement));

    return ast_copy_name(statement->command, sizeof(statement->command), command);
}
