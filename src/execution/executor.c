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
    Transaction *transaction;
} DeleteContext;

/*
 * UPDATE scan callbacks need the opened table so they can validate and write
 * the replacement row at the RID provided by the scan.
 */
typedef struct {
    Table *table;
    const UpdatePlan *plan;
    Transaction *transaction;
} UpdateContext;

typedef struct {
    /*
     * transaction_execute_autocommit accepts a generic callback, so this wraps
     * the normal executor arguments into one pointer-sized context.
     */
    DB *db;
    const Plan *plan;
    FILE *out;
} ExecuteContext;

/*
 * Execution rows and plans own their values independently.
 */
static DBStatus executor_copy_value(Value *dest, const Value *source) {
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
 * Prints one value in shell result format.
 * Text values are printed without quotes for query output.
 */
static DBStatus executor_print_value_plain(const Value *value, FILE *out) {
    if (value == NULL || out == NULL) {
        return DB_ERROR;
    }

    if (value->type == VALUE_INT) {
        fprintf(out, "%d", value->int_value);
        return DB_OK;
    }

    if (value->type == VALUE_TEXT) {
        fprintf(out, "%s", value->text_value);
        return DB_OK;
    }

    return DB_TYPE_ERROR;
}

/*
 * Prints all row values in shell result format:
 *   1 | Finn | 20
 *
 * This is intentionally separate from row_print, which remains a debug-style
 * representation used by lower-level tests.
 */
static DBStatus executor_print_full_row(const Row *row, FILE *out) {
    if (row == NULL || out == NULL) {
        return DB_ERROR;
    }

    for (uint16_t i = 0; i < row->value_count; i++) {
        const Value *value = row_get_value_const(row, i);

        if (value == NULL) {
            return DB_ERROR;
        }

        if (i > 0) {
            fprintf(out, " | ");
        }

        DBStatus status = executor_print_value_plain(value, out);

        if (status != DB_OK) {
            return status;
        }
    }

    fprintf(out, "\n");

    return DB_OK;
}

/*
 * Prints only the columns requested by a ProjectPlan.
 */
static DBStatus executor_print_projected_row(
    const Schema *schema,
    const Row *row,
    const ProjectPlan *project,
    FILE *out
) {
    if (schema == NULL || row == NULL || project == NULL || out == NULL) {
        return DB_ERROR;
    }

    for (uint16_t i = 0; i < project->column_count; i++) {
        uint16_t column_index = 0;

        DBStatus status = schema_get_column_index(schema, project->columns[i], &column_index);

        if (status != DB_OK) {
            return status;
        }

        const Value *source_value = row_get_value_const(row, column_index);

        if (source_value == NULL) {
            return DB_ERROR;
        }

        if (i > 0) {
            fprintf(out, " | ");
        }

        status = executor_print_value_plain(source_value, out);

        if (status != DB_OK) {
            return status;
        }
    }

    fprintf(out, "\n");

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
        DBStatus status = row_matches_condition(
            row,
            &select_context->table->schema,
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

    return executor_print_full_row(row, select_context->out);
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
        DBStatus status = row_matches_condition(
            row,
            &delete_context->table->schema,
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
     * Use the logged record path here because the scan callback has both the
     * matching RID and the active statement transaction.
     */
    return record_delete_logged(
        delete_context->table->file_path,
        rid,
        delete_context->transaction
    );
}

/*
 * Builds a row with one column replaced by the UPDATE assignment value.
 */
static DBStatus executor_build_updated_row(
    const Row *source,
    uint16_t set_column_index,
    const Value *set_value,
    Row *out_row
) {
    if (source == NULL || set_value == NULL || out_row == NULL) {
        return DB_ERROR;
    }

    DBStatus status = row_create(out_row, source->value_count);

    if (status != DB_OK) {
        return status;
    }

    for (uint16_t i = 0; i < source->value_count; i++) {
        const Value *value = (i == set_column_index) ?
            set_value :
            row_get_value_const(source, i);

        if (value == NULL) {
            row_free(out_row);
            return DB_ERROR;
        }

        status = executor_copy_value(&out_row->values[i], value);

        if (status != DB_OK) {
            row_free(out_row);
            return status;
        }
    }

    return DB_OK;
}

/*
 * Callback used by table_scan for UPDATE.
 * Matching rows are rewritten through the WAL-aware record update path.
 */
static DBStatus executor_update_callback(const Row *row, RID rid, void *context) {
    UpdateContext *update_context = context;
    bool matches = true;

    if (update_context == NULL || row == NULL) {
        return DB_ERROR;
    }

    if (update_context->plan->has_condition) {
        DBStatus status = row_matches_condition(
            row,
            &update_context->table->schema,
            &update_context->plan->condition,
            &matches
        );

        if (status != DB_OK) {
            return status;
        }
    }

    if (!matches) {
        return DB_OK;
    }

    uint16_t set_column_index = 0;
    DBStatus status = schema_get_column_index(
        &update_context->table->schema,
        update_context->plan->set_column,
        &set_column_index
    );

    if (status != DB_OK) {
        return status;
    }

    Row updated_row;

    status = executor_build_updated_row(
        row,
        set_column_index,
        &update_context->plan->set_value,
        &updated_row
    );

    if (status != DB_OK) {
        return status;
    }

    status = schema_validate_row(&update_context->table->schema, &updated_row);

    if (status == DB_OK) {
        RID updated_rid;

        /*
         * UPDATE may be in-place or delete-plus-insert if the row grows. The
         * logged record layer handles both shapes while preserving the SQL
         * executor's simple "replace this row" request.
         */
        status = record_update_logged(
            update_context->table->file_path,
            rid,
            &updated_row,
            &updated_rid,
            update_context->transaction
        );
    }

    row_free(&updated_row);

    return status;
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
        /*
         * The executor is inside transaction_execute_autocommit by the time it
         * reaches this call, so db->transaction has a valid txn id for WAL.
         */
        status = record_insert_logged(
            table.file_path,
            (Row *)&plan->row,
            &rid,
            &db->transaction
        );
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
    context.transaction = &db->transaction;

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

static DBStatus executor_execute_update(DB *db, const UpdatePlan *plan) {
    Table table;
    UpdateContext context;

    DBStatus status = table_open(&table, db, plan->table_name);

    if (status != DB_OK) {
        return status;
    }

    context.table = &table;
    context.plan = plan;
    context.transaction = &db->transaction;

    /*
     * UPDATE is implemented as table scan plus conditional record_update.
     */
    status = table_scan(&table, executor_update_callback, &context);

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

static DBStatus executor_execute_plan(DB *db, const Plan *plan, FILE *out) {
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
        case PLAN_UPDATE:
            return executor_execute_update(db, &plan->update);
        case PLAN_META_COMMAND:
            return executor_execute_meta_command(db, &plan->meta_command, out);
        default:
            return DB_ERROR;
    }
}

static DBStatus executor_execute_autocommit_callback(void *context) {
    ExecuteContext *execute_context = context;

    if (execute_context == NULL) {
        return DB_ERROR;
    }

    /*
     * This is the actual statement body run between transaction_begin and
     * transaction_commit. Any error causes the transaction wrapper to rollback
     * state and return the original failure.
     */
    return executor_execute_plan(
        execute_context->db,
        execute_context->plan,
        execute_context->out
    );
}

DBStatus executor_execute(DB *db, const Plan *plan, FILE *out) {
    if (db == NULL || plan == NULL) {
        return DB_ERROR;
    }

    ExecuteContext context;

    context.db = db;
    context.plan = plan;
    context.out = out;

    /*
     * Public executor entry point: every statement is currently autocommit.
     * Future explicit BEGIN support can bypass this wrapper while reusing
     * executor_execute_plan for the actual work.
     */
    return transaction_execute_autocommit(
        &db->transaction,
        executor_execute_autocommit_callback,
        &context
    );
}
