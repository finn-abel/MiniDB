#ifndef SQL_AST_H
#define SQL_AST_H

#include <stdbool.h>
#include <stdint.h>

#include "common.h"
#include "schema.h"
#include "value.h"

/*
 * StatementType identifies the top-level command represented by an AST node.
 *
 * The parser will choose one of these after looking at the first token.
 * Execution code can later dispatch on this type without re-reading SQL text.
 */
typedef enum {
    STATEMENT_CREATE_TABLE,
    STATEMENT_INSERT,
    STATEMENT_SELECT,
    STATEMENT_DELETE,
    STATEMENT_UPDATE,
    STATEMENT_META_COMMAND
} StatementType;

/*
 * CREATE TABLE table_name (...columns...)
 *
 * Column reuses the schema layer's column metadata. The AST only stores what
 * was parsed; schema validation and catalog persistence happen later.
 */
typedef struct {
    char table_name[MAX_TABLE_NAME];
    uint16_t column_count;
    Column columns[MAX_COLUMNS];
} CreateTableStatement;

/*
 * INSERT INTO table_name VALUES (...values...)
 */
typedef struct {
    char table_name[MAX_TABLE_NAME];
    uint16_t value_count;
    Value values[MAX_COLUMNS];
} InsertStatement;

/*
 * SELECT columns FROM table_name [WHERE condition]
 *
 * selected_column_count == 0 represents SELECT *.
 */
typedef struct {
    char table_name[MAX_TABLE_NAME];
    uint16_t selected_column_count;
    char selected_columns[MAX_COLUMNS][MAX_COLUMN_NAME];
    bool has_where;
    WhereCondition where;
} SelectStatement;

/*
 * DELETE FROM table_name [WHERE condition]
 */
typedef struct {
    char table_name[MAX_TABLE_NAME];
    bool has_where;
    WhereCondition where;
} DeleteStatement;

/*
 * UPDATE table_name SET column = value [WHERE condition]
 *
 * The parser stores one assignment and one optional WHERE condition for now.
 */
typedef struct {
    char table_name[MAX_TABLE_NAME];
    char set_column[MAX_COLUMN_NAME];
    Value set_value;
    bool has_set;
    bool has_where;
    WhereCondition where;
} UpdateStatement;

/*
 * Meta commands are shell-level commands such as .help and .exit.
 * They are represented here so parser output can use one statement wrapper.
 */
typedef struct {
    char command[MAX_INPUT_SIZE];
} MetaCommandStatement;

/*
 * Statement is the root AST node for one parsed command.
 *
 * Only the union member matching type is valid.
 * Call ast_statement_free when a statement may own Value text memory.
 */
typedef struct {
    StatementType type;

    union {
        CreateTableStatement create_table;
        InsertStatement insert;
        SelectStatement select;
        DeleteStatement delete_statement;
        UpdateStatement update;
        MetaCommandStatement meta_command;
    };
} Statement;

/*
 * Initializes a statement wrapper with the chosen top-level type.
 * The specific statement payload can be filled afterward.
 */
DBStatus ast_statement_init(Statement *statement, StatementType type);

/*
 * Frees any heap memory owned by the active statement payload.
 * This mainly matters for text values inside INSERT and WHERE clauses.
 */
void ast_statement_free(Statement *statement);

/*
 * CREATE TABLE helpers build the parsed table name and column list.
 */
DBStatus ast_create_table_init(
    CreateTableStatement *statement,
    const char *table_name
);
DBStatus ast_create_table_add_column(
    CreateTableStatement *statement,
    const char *column_name,
    ValueType type
);

/*
 * INSERT helpers build the parsed destination table and literal values.
 */
DBStatus ast_insert_init(InsertStatement *statement, const char *table_name);
DBStatus ast_insert_add_value(InsertStatement *statement, const Value *value);

/*
 * SELECT helpers build the parsed projection list and optional WHERE clause.
 * A SELECT * statement has zero selected columns.
 */
DBStatus ast_select_init(SelectStatement *statement, const char *table_name);
DBStatus ast_select_add_column(SelectStatement *statement, const char *column_name);
DBStatus ast_select_set_where(
    SelectStatement *statement,
    const WhereCondition *condition
);

/*
 * DELETE helpers build the parsed table name and optional WHERE clause.
 */
DBStatus ast_delete_init(DeleteStatement *statement, const char *table_name);
DBStatus ast_delete_set_where(
    DeleteStatement *statement,
    const WhereCondition *condition
);

/*
 * UPDATE helpers build the target table, assignment, and optional WHERE.
 */
DBStatus ast_update_init(UpdateStatement *statement, const char *table_name);
DBStatus ast_update_set_assignment(
    UpdateStatement *statement,
    const char *column_name,
    const Value *value
);
DBStatus ast_update_set_where(
    UpdateStatement *statement,
    const WhereCondition *condition
);

/*
 * WHERE helpers build and release a single comparison condition.
 */
DBStatus ast_where_init(
    WhereCondition *condition,
    const char *column_name,
    SqlOperator operator_type,
    const Value *value
);
void ast_where_free(WhereCondition *condition);

/*
 * Stores a shell-level meta command such as .help or .exit.
 */
DBStatus ast_meta_command_init(
    MetaCommandStatement *statement,
    const char *command
);

#endif
