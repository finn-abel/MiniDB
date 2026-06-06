#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common.h"
#include "execution/plan.h"
#include "execution/planner.h"
#include "row.h"
#include "schema.h"
#include "sql/ast.h"
#include "sql/binder.h"
#include "value.h"

/*
 * Copies names into fixed-size plan buffers.
 * The binder should already have validated names, but keeping this check here
 * protects plan construction from malformed direct callers.
 */
static DBStatus planner_copy_name(char *dest, uint32_t dest_size, const char *source) {
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
 * Plan objects own their copied values.
 */
static DBStatus planner_copy_value(Value *dest, const Value *source) {
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
 * WHERE conditions can contain text values, so copy them through the AST
 * helper that knows how to deep-copy Value.
 */
static DBStatus planner_copy_where(
    WhereCondition *dest,
    const WhereCondition *source
) {
    return ast_where_init(
        dest,
        source->column_name,
        source->operator_type,
        &source->value
    );
}

static DBStatus planner_build_create_table_plan(
    const BoundStatement *bound,
    Plan *out_plan
) {
    /*
     * CREATE TABLE planning is just carrying forward the bound schema.
     * No table file or catalog mutation happens in the planner.
     */
    DBStatus status = plan_init(out_plan, PLAN_CREATE_TABLE);

    if (status != DB_OK) {
        return status;
    }

    out_plan->create_table.schema = bound->table_schema;

    return DB_OK;
}

static DBStatus planner_build_insert_plan(
    const BoundStatement *bound,
    Plan *out_plan
) {
    const InsertStatement *insert = &bound->statement.insert;

    /*
     * INSERT plans materialize AST values into a Row so execution can pass the
     * row directly to table_insert later.
     */
    DBStatus status = plan_init(out_plan, PLAN_INSERT);

    if (status != DB_OK) {
        return status;
    }

    status = planner_copy_name(
        out_plan->insert.table_name,
        sizeof(out_plan->insert.table_name),
        insert->table_name
    );

    if (status != DB_OK) {
        return status;
    }

    status = row_create(&out_plan->insert.row, insert->value_count);

    if (status != DB_OK) {
        return status;
    }

    for (uint16_t i = 0; i < insert->value_count; i++) {
        status = planner_copy_value(&out_plan->insert.row.values[i], &insert->values[i]);

        if (status != DB_OK) {
            return status;
        }
    }

    return DB_OK;
}

static DBStatus planner_build_select_plan(
    const BoundStatement *bound,
    Plan *out_plan
) {
    const SelectStatement *select = &bound->statement.select;

    /*
     * SELECT plans always begin with a table scan.
     */
    DBStatus status = plan_init(out_plan, PLAN_SELECT);

    if (status != DB_OK) {
        return status;
    }

    status = planner_copy_name(
        out_plan->select.scan.table_name,
        sizeof(out_plan->select.scan.table_name),
        select->table_name
    );

    if (status != DB_OK) {
        return status;
    }

    if (select->has_where) {
        /*
         * A WHERE clause becomes a filter layered on top of the scan.
         */
        status = planner_copy_where(
            &out_plan->select.filter.condition,
            &select->where
        );

        if (status != DB_OK) {
            return status;
        }

        out_plan->select.has_filter = true;
    }

    if (select->selected_column_count > 0) {
        /*
         * Explicit selected columns become a project step.
         * SELECT * leaves has_project false.
         */
        out_plan->select.has_project = true;
        out_plan->select.project.column_count = select->selected_column_count;

        for (uint16_t i = 0; i < select->selected_column_count; i++) {
            status = planner_copy_name(
                out_plan->select.project.columns[i],
                MAX_COLUMN_NAME,
                select->selected_columns[i]
            );

            if (status != DB_OK) {
                return status;
            }
        }
    }

    return DB_OK;
}

static DBStatus planner_build_delete_plan(
    const BoundStatement *bound,
    Plan *out_plan
) {
    const DeleteStatement *delete_statement = &bound->statement.delete_statement;

    /*
     * DELETE plans identify the target table and optional condition.
     * Execution will decide which rows match.
     */
    DBStatus status = plan_init(out_plan, PLAN_DELETE);

    if (status != DB_OK) {
        return status;
    }

    status = planner_copy_name(
        out_plan->delete_plan.table_name,
        sizeof(out_plan->delete_plan.table_name),
        delete_statement->table_name
    );

    if (status != DB_OK) {
        return status;
    }

    if (delete_statement->has_where) {
        status = planner_copy_where(
            &out_plan->delete_plan.condition,
            &delete_statement->where
        );

        if (status != DB_OK) {
            return status;
        }

        out_plan->delete_plan.has_condition = true;
    }

    return DB_OK;
}

static DBStatus planner_build_update_plan(
    const BoundStatement *bound,
    Plan *out_plan
) {
    const UpdateStatement *update = &bound->statement.update;

    /*
     * UPDATE uses the same table-scan shape as DELETE, plus one assignment
     * copied from the bound AST.
     */
    DBStatus status = plan_init(out_plan, PLAN_UPDATE);

    if (status != DB_OK) {
        return status;
    }

    status = planner_copy_name(
        out_plan->update.table_name,
        sizeof(out_plan->update.table_name),
        update->table_name
    );

    if (status != DB_OK) {
        return status;
    }

    status = planner_copy_name(
        out_plan->update.set_column,
        sizeof(out_plan->update.set_column),
        update->set_column
    );

    if (status != DB_OK) {
        return status;
    }

    status = planner_copy_value(&out_plan->update.set_value, &update->set_value);

    if (status != DB_OK) {
        return status;
    }

    if (update->has_where) {
        status = planner_copy_where(
            &out_plan->update.condition,
            &update->where
        );

        if (status != DB_OK) {
            return status;
        }

        out_plan->update.has_condition = true;
    }

    return DB_OK;
}

static DBStatus planner_build_meta_command_plan(
    const BoundStatement *bound,
    Plan *out_plan
) {
    /*
     * Meta commands pass through to shell execution unchanged.
     */
    DBStatus status = plan_init(out_plan, PLAN_META_COMMAND);

    if (status != DB_OK) {
        return status;
    }

    return planner_copy_name(
        out_plan->meta_command.command,
        sizeof(out_plan->meta_command.command),
        bound->statement.meta_command.command
    );
}

DBStatus planner_create_plan(const BoundStatement *bound, Plan *out_plan) {
    DBStatus status;

    if (bound == NULL || out_plan == NULL) {
        return DB_ERROR;
    }

    /*
     * Dispatch directly from the bound statement type.
     * There is intentionally no cost model or access-path choice here.
     */
    switch (bound->statement.type) {
        case STATEMENT_CREATE_TABLE:
            status = planner_build_create_table_plan(bound, out_plan);
            break;
        case STATEMENT_INSERT:
            status = planner_build_insert_plan(bound, out_plan);
            break;
        case STATEMENT_SELECT:
            status = planner_build_select_plan(bound, out_plan);
            break;
        case STATEMENT_DELETE:
            status = planner_build_delete_plan(bound, out_plan);
            break;
        case STATEMENT_UPDATE:
            status = planner_build_update_plan(bound, out_plan);
            break;
        case STATEMENT_META_COMMAND:
            status = planner_build_meta_command_plan(bound, out_plan);
            break;
        default:
            status = DB_ERROR;
            break;
    }

    if (status != DB_OK) {
        /*
         * If a partial plan allocated any owned fields, release them before
         * returning the failure.
         */
        plan_free(out_plan);
    }

    return status;
}
