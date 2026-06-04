#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "catalog.h"
#include "common.h"
#include "schema.h"
#include "sql/ast.h"
#include "sql/binder.h"
#include "value.h"

/*
 * Bound statements own their copied AST.
 * Text values are deep-copied through AST helper functions.
 *
 * This keeps the bound statement independent from the parsed statement, so the
 * caller can free parser output immediately after binding.
 */
static DBStatus binder_copy_statement(
    Statement *dest,
    const Statement *source
) {
    DBStatus status = ast_statement_init(dest, source->type);

    if (status != DB_OK) {
        return status;
    }

    switch (source->type) {
        case STATEMENT_CREATE_TABLE:
            /*
             * CREATE TABLE has no heap-backed values, but copying through AST
             * helpers keeps name and column validation consistent.
             */
            status = ast_create_table_init(
                &dest->create_table,
                source->create_table.table_name
            );

            if (status != DB_OK) {
                return status;
            }

            for (uint16_t i = 0; i < source->create_table.column_count; i++) {
                const Column *column = &source->create_table.columns[i];

                status = ast_create_table_add_column(
                    &dest->create_table,
                    column->name,
                    column->type
                );

                if (status != DB_OK) {
                    return status;
                }
            }

            return DB_OK;
        case STATEMENT_INSERT:
            /*
             * INSERT values may contain TEXT, so ast_insert_add_value performs
             * the deep copy for each literal.
             */
            status = ast_insert_init(&dest->insert, source->insert.table_name);

            if (status != DB_OK) {
                return status;
            }

            for (uint16_t i = 0; i < source->insert.value_count; i++) {
                status = ast_insert_add_value(&dest->insert, &source->insert.values[i]);

                if (status != DB_OK) {
                    return status;
                }
            }

            return DB_OK;
        case STATEMENT_SELECT:
            /*
             * SELECT can own a WHERE value. Projection column names are copied
             * first, then the optional condition is copied if present.
             */
            status = ast_select_init(&dest->select, source->select.table_name);

            if (status != DB_OK) {
                return status;
            }

            for (uint16_t i = 0; i < source->select.selected_column_count; i++) {
                status = ast_select_add_column(
                    &dest->select,
                    source->select.selected_columns[i]
                );

                if (status != DB_OK) {
                    return status;
                }
            }

            if (source->select.has_where) {
                status = ast_select_set_where(&dest->select, &source->select.where);
            }

            return status;
        case STATEMENT_DELETE:
            /*
             * DELETE has only a table name plus an optional WHERE condition.
             */
            status = ast_delete_init(
                &dest->delete_statement,
                source->delete_statement.table_name
            );

            if (status != DB_OK) {
                return status;
            }

            if (source->delete_statement.has_where) {
                status = ast_delete_set_where(
                    &dest->delete_statement,
                    &source->delete_statement.where
                );
            }

            return status;
        case STATEMENT_META_COMMAND:
            return ast_meta_command_init(
                &dest->meta_command,
                source->meta_command.command
            );
        default:
            return DB_ERROR;
    }
}

static DBStatus binder_build_create_schema(
    const CreateTableStatement *statement,
    Schema *out_schema
) {
    /*
     * Convert CREATE TABLE AST metadata into a real Schema.
     * This validates duplicate column names before anything reaches catalog.
     */
    DBStatus status = schema_init(out_schema, statement->table_name);

    if (status != DB_OK) {
        return status;
    }

    for (uint16_t i = 0; i < statement->column_count; i++) {
        const Column *column = &statement->columns[i];

        if (column->type != VALUE_INT && column->type != VALUE_TEXT) {
            return DB_TYPE_ERROR;
        }

        /*
         * schema_add_column enforces unique column names, so the binder can
         * reuse the same validation the catalog will eventually rely on.
         */
        status = schema_add_column(
            out_schema,
            column->name,
            column->type,
            column->not_null,
            column->primary_key
        );

        if (status != DB_OK) {
            return status;
        }
    }

    return DB_OK;
}

static DBStatus binder_check_where(
    const Schema *schema,
    const WhereCondition *condition
) {
    ValueType column_type;

    /*
     * A WHERE condition can only refer to an existing column.
     */
    DBStatus status = schema_get_column_type(
        schema,
        condition->column_name,
        &column_type
    );

    if (status != DB_OK) {
        return status;
    }

    /*
     * At this stage, a WHERE comparison is valid when both sides have the
     * same primitive type. Operator-specific semantics can come later.
     */
    if (condition->value.type != column_type) {
        return DB_TYPE_ERROR;
    }

    return DB_OK;
}

static DBStatus binder_bind_create_table(
    const DB *db,
    const Statement *statement,
    BoundStatement *out_bound
) {
    /*
     * CREATE TABLE is valid only when the target table name is not already in
     * the catalog.
     */
    if (catalog_table_exists(db, statement->create_table.table_name)) {
        return DB_ERROR;
    }

    DBStatus status = binder_build_create_schema(
        &statement->create_table,
        &out_bound->table_schema
    );

    if (status != DB_OK) {
        return status;
    }

    out_bound->has_table_schema = true;

    return DB_OK;
}

static DBStatus binder_bind_insert(
    const DB *db,
    const Statement *statement,
    BoundStatement *out_bound
) {
    /*
     * INSERT binds against the target table schema so values can be checked
     * by position.
     */
    DBStatus status = catalog_get_schema(
        db,
        statement->insert.table_name,
        &out_bound->table_schema
    );

    if (status != DB_OK) {
        return status;
    }

    if (statement->insert.value_count != out_bound->table_schema.column_count) {
        return DB_ERROR;
    }

    /*
     * MiniDB rows store values by position, so each INSERT value must match
     * the column at the same index.
     */
    for (uint16_t i = 0; i < statement->insert.value_count; i++) {
        if (statement->insert.values[i].type != out_bound->table_schema.columns[i].type) {
            return DB_TYPE_ERROR;
        }
    }

    out_bound->has_table_schema = true;

    return DB_OK;
}

static DBStatus binder_bind_select(
    const DB *db,
    const Statement *statement,
    BoundStatement *out_bound
) {
    /*
     * SELECT * is represented by zero selected columns, so only explicit
     * projection columns need individual lookup.
     */
    DBStatus status = catalog_get_schema(
        db,
        statement->select.table_name,
        &out_bound->table_schema
    );

    if (status != DB_OK) {
        return status;
    }

    for (uint16_t i = 0; i < statement->select.selected_column_count; i++) {
        uint16_t column_index = 0;

        status = schema_get_column_index(
            &out_bound->table_schema,
            statement->select.selected_columns[i],
            &column_index
        );

        if (status != DB_OK) {
            return status;
        }
    }

    if (statement->select.has_where) {
        status = binder_check_where(
            &out_bound->table_schema,
            &statement->select.where
        );

        if (status != DB_OK) {
            return status;
        }
    }

    out_bound->has_table_schema = true;

    return DB_OK;
}

static DBStatus binder_bind_delete(
    const DB *db,
    const Statement *statement,
    BoundStatement *out_bound
) {
    /*
     * DELETE needs table existence, and WHERE validation only when a WHERE
     * clause was parsed.
     */
    DBStatus status = catalog_get_schema(
        db,
        statement->delete_statement.table_name,
        &out_bound->table_schema
    );

    if (status != DB_OK) {
        return status;
    }

    if (statement->delete_statement.has_where) {
        status = binder_check_where(
            &out_bound->table_schema,
            &statement->delete_statement.where
        );

        if (status != DB_OK) {
            return status;
        }
    }

    out_bound->has_table_schema = true;

    return DB_OK;
}

DBStatus binder_bind(
    const DB *db,
    const Statement *statement,
    BoundStatement *out_bound
) {
    DBStatus status;

    if (statement == NULL || out_bound == NULL) {
        return DB_ERROR;
    }

    /*
     * Start clean so binder_bound_statement_free is safe after any failure.
     */
    memset(out_bound, 0, sizeof(BoundStatement));

    status = binder_copy_statement(&out_bound->statement, statement);

    if (status != DB_OK) {
        binder_bound_statement_free(out_bound);
        return status;
    }

    if (statement->type == STATEMENT_META_COMMAND) {
        /*
         * Meta commands are shell instructions. They bind successfully without
         * catalog access because they are not table operations.
         */
        return DB_OK;
    }

    if (db == NULL) {
        binder_bound_statement_free(out_bound);
        return DB_ERROR;
    }

    switch (statement->type) {
        /*
         * Each statement-specific binder checks only semantic facts. It does
         * not change catalog or table files.
         */
        case STATEMENT_CREATE_TABLE:
            status = binder_bind_create_table(db, statement, out_bound);
            break;
        case STATEMENT_INSERT:
            status = binder_bind_insert(db, statement, out_bound);
            break;
        case STATEMENT_SELECT:
            status = binder_bind_select(db, statement, out_bound);
            break;
        case STATEMENT_DELETE:
            status = binder_bind_delete(db, statement, out_bound);
            break;
        default:
            status = DB_ERROR;
            break;
    }

    if (status != DB_OK) {
        binder_bound_statement_free(out_bound);
    }

    return status;
}

void binder_bound_statement_free(BoundStatement *bound) {
    if (bound == NULL) {
        return;
    }

    /*
     * ast_statement_free releases text values inside the copied AST; the
     * schema itself owns no heap memory.
     */
    ast_statement_free(&bound->statement);
    memset(bound, 0, sizeof(BoundStatement));
}
