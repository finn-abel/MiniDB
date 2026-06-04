#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "catalog.h"
#include "common.h"
#include "db.h"
#include "execution/executor.h"
#include "execution/plan.h"
#include "record.h"
#include "rid.h"
#include "row.h"
#include "schema.h"
#include "table.h"
#include "value.h"

/*
 * SELECT scan callbacks need access to both the opened table schema and the
 * output stream where matching rows should be printed.
 */
typedef struct {
    Table *table;
    const SelectPlan *plan;
    FILE *out;
} SelectContext;

/*
 * DELETE scan callbacks need the opened table so matching RIDs can be deleted
 * from the same table file being scanned.
 */
typedef struct {
    Table *table;
    const DeletePlan *plan;
} DeleteContext;

/*
 * Evaluates a simple WHERE condition against one row.
 *
 * The binder already checked that the condition column exists and that the
 * literal type is comparable. The executor repeats the column lookup to find
 * the row value by position.
 */
static DBStatus executor_condition_matches(
    const Schema *schema,
    const Row *row,
    const WhereCondition *condition,
    bool *out_matches
) {
    uint16_t column_index = 0;
    int compare_result = 0;

    if (schema == NULL || row == NULL || condition == NULL || out_matches == NULL) {
        return DB_ERROR;
    }

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

    status = value_compare(row_value, &condition->value, &compare_result);

    if (status != DB_OK) {
        return status;
    }

    /*
     * value_compare gives a normalized comparison result. The SQL operator
     * decides how that result maps to a boolean match.
     */
    switch (condition->operator_type) {
        case SQL_OPERATOR_EQUAL:
            *out_matches = compare_result == 0;
            return DB_OK;
        case SQL_OPERATOR_NOT_EQUAL:
            *out_matches = compare_result != 0;
            return DB_OK;
        case SQL_OPERATOR_GREATER:
            *out_matches = compare_result > 0;
            return DB_OK;
        case SQL_OPERATOR_LESS:
            *out_matches = compare_result < 0;
            return DB_OK;
        case SQL_OPERATOR_GREATER_EQUAL:
            *out_matches = compare_result >= 0;
            return DB_OK;
        case SQL_OPERATOR_LESS_EQUAL:
            *out_matches = compare_result <= 0;
            return DB_OK;
        default:
            return DB_ERROR;
    }
}

/*
 * Prints only the columns requested by a ProjectPlan.
 *
 * row_print expects a Row, so the executor builds a temporary projected row
 * that owns deep copies of any text values.
 */
static DBStatus executor_print_projected_row(
    const Schema *schema,
    const Row *row,
    const ProjectPlan *project,
    FILE *out
) {
    Row projected;

    if (schema == NULL || row == NULL || project == NULL || out == NULL) {
        return DB_ERROR;
    }

    DBStatus status = row_create(&projected, project->column_count);

    if (status != DB_OK) {
        return status;
    }

    for (uint16_t i = 0; i < project->column_count; i++) {
        uint16_t column_index = 0;

        status = schema_get_column_index(schema, project->columns[i], &column_index);

        if (status != DB_OK) {
            row_free(&projected);
            return status;
        }

        const Value *source_value = row_get_value_const(row, column_index);

        if (source_value == NULL) {
            row_free(&projected);
            return DB_ERROR;
        }

        if (source_value->type == VALUE_INT) {
            projected.values[i] = value_int(source_value->int_value);
        } else if (source_value->type == VALUE_TEXT) {
            status = value_text(&projected.values[i], source_value->text_value);

            if (status != DB_OK) {
                row_free(&projected);
                return status;
            }
        } else {
            row_free(&projected);
            return DB_TYPE_ERROR;
        }
    }

    row_print(&projected, out);
    fprintf(out, "\n");

    row_free(&projected);

    return DB_OK;
}

/*
 * Callback used by table_scan for SELECT.
 * The row is owned by table_scan and is only valid during this callback.
 */
static DBStatus executor_select_callback(const Row *row, RID rid, void *context) {
    (void)rid;

    SelectContext *select_context = context;
    bool matches = true;

    if (select_context == NULL || row == NULL) {
        return DB_ERROR;
    }

    if (select_context->plan->has_filter) {
        DBStatus status = executor_condition_matches(
            &select_context->table->schema,
            row,
            &select_context->plan->filter.condition,
            &matches
        );

        if (status != DB_OK) {
            return status;
        }
    }

    if (!matches) {
        /*
         * Filtered-out rows are skipped without printing anything.
         */
        return DB_OK;
    }

    if (select_context->plan->has_project) {
        return executor_print_projected_row(
            &select_context->table->schema,
            row,
            &select_context->plan->project,
            select_context->out
        );
    }

    row_print(row, select_context->out);
    fprintf(select_context->out, "\n");

    return DB_OK;
}

/*
 * Callback used by table_scan for DELETE.
 * Matching rows are deleted by RID from the table file.
 */
static DBStatus executor_delete_callback(const Row *row, RID rid, void *context) {
    DeleteContext *delete_context = context;
    bool matches = true;

    if (delete_context == NULL || row == NULL) {
        return DB_ERROR;
    }

    if (delete_context->plan->has_condition) {
        DBStatus status = executor_condition_matches(
            &delete_context->table->schema,
            row,
            &delete_context->plan->condition,
            &matches
        );

        if (status != DB_OK) {
            return status;
        }
    }

    if (!matches) {
        return DB_OK;
    }

    /*
     * Use the record manager here because the plan has already identified the
     * row location through the scan callback's RID.
     */
    return record_delete(delete_context->table->file_path, rid);
}

static DBStatus executor_execute_create_table(DB *db, const CreateTablePlan *plan) {
    /*
     * catalog_create_table persists metadata and creates the table file.
     */
    return catalog_create_table(db, &plan->schema);
}

static DBStatus executor_execute_insert(DB *db, const InsertPlan *plan) {
    Table table;
    RID rid;

    DBStatus status = table_open(&table, db, plan->table_name);

    if (status != DB_OK) {
        return status;
    }

    /*
     * table_open gives us the schema and table file path. The actual write is
     * delegated to the record manager after one final row validation.
     */
    status = schema_validate_row(&table.schema, &plan->row);

    if (status == DB_OK) {
        status = record_insert(table.file_path, (Row *)&plan->row, &rid);
    }

    DBStatus close_status = table_close(&table);

    if (status != DB_OK) {
        return status;
    }

    return close_status;
}

static DBStatus executor_execute_select(
    DB *db,
    const SelectPlan *plan,
    FILE *out
) {
    Table table;
    SelectContext context;

    if (out == NULL) {
        return DB_ERROR;
    }

    DBStatus status = table_open(&table, db, plan->scan.table_name);

    if (status != DB_OK) {
        return status;
    }

    context.table = &table;
    context.plan = plan;
    context.out = out;

    /*
     * table_scan owns temporary rows and calls executor_select_callback for
     * each active row.
     */
    status = table_scan(&table, executor_select_callback, &context);

    DBStatus close_status = table_close(&table);

    if (status != DB_OK) {
        return status;
    }

    return close_status;
}

static DBStatus executor_execute_delete(DB *db, const DeletePlan *plan) {
    Table table;
    DeleteContext context;

    DBStatus status = table_open(&table, db, plan->table_name);

    if (status != DB_OK) {
        return status;
    }

    context.table = &table;
    context.plan = plan;

    /*
     * DELETE is implemented as table scan plus conditional record_delete.
     */
    status = table_scan(&table, executor_delete_callback, &context);

    DBStatus close_status = table_close(&table);

    if (status != DB_OK) {
        return status;
    }

    return close_status;
}

static DBStatus executor_execute_meta_command(
    DB *db,
    const MetaCommandPlan *plan,
    FILE *out
) {
    if (out == NULL) {
        return DB_ERROR;
    }

    /*
     * Meta commands are handled by the executor because they may need catalog
     * access or output, but they do not mutate table rows.
     */
    if (strcmp(plan->command, ".tables") == 0) {
        catalog_list_tables(db, out);
        return DB_OK;
    }

    if (strcmp(plan->command, ".help") == 0) {
        fprintf(out, "Supported commands:\n");
        fprintf(out, "  .help\n");
        fprintf(out, "  .exit\n");
        fprintf(out, "  .tables\n");
        fprintf(out, "  .schema <table>\n");
        return DB_OK;
    }

    if (strcmp(plan->command, ".exit") == 0) {
        return DB_OK;
    }

    if (strncmp(plan->command, ".schema ", 8) == 0) {
        Schema schema;

        DBStatus status = catalog_get_schema(db, plan->command + 8, &schema);

        if (status != DB_OK) {
            return status;
        }

        schema_print(&schema, out);
        fprintf(out, "\n");
        return DB_OK;
    }

    return DB_ERROR;
}

DBStatus executor_execute(DB *db, const Plan *plan, FILE *out) {
    if (db == NULL || plan == NULL) {
        return DB_ERROR;
    }

    /*
     * Dispatch on the already-built plan type. Parser, binder, and planner
     * work should all be complete before reaching this point.
     */
    switch (plan->type) {
        case PLAN_CREATE_TABLE:
            return executor_execute_create_table(db, &plan->create_table);
        case PLAN_INSERT:
            return executor_execute_insert(db, &plan->insert);
        case PLAN_SELECT:
            return executor_execute_select(db, &plan->select, out);
        case PLAN_DELETE:
            return executor_execute_delete(db, &plan->delete_plan);
        case PLAN_META_COMMAND:
            return executor_execute_meta_command(db, &plan->meta_command, out);
        default:
            return DB_ERROR;
    }
}
