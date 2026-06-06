#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "buffer/buffer_pool.h"
#include "catalog.h"
#include "common.h"
#include "db.h"
#include "execution/executor.h"
#include "execution/plan.h"
#include "index/btree.h"
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
    /*
     * DELETE scans rows, so the callback has the old row value needed to remove
     * its primary-key entry before the row slot is tombstoned.
     */
    BTree *primary_key_index;
    uint16_t primary_key_column;
    bool has_primary_key;
    BTree secondary_indexes[MAX_CATALOG_INDEXES];
    uint16_t secondary_index_columns[MAX_CATALOG_INDEXES];
    uint16_t secondary_index_count;
} DeleteContext;

/*
 * UPDATE scan callbacks need the opened table so they can validate and write
 * the replacement row at the RID provided by the scan.
 */
typedef struct {
    Table *table;
    const UpdatePlan *plan;
    Transaction *transaction;
    /*
     * UPDATE may change either the primary-key value or the row's RID. Both
     * cases require replacing the key -> RID entry after the row write.
     */
    BTree *primary_key_index;
    uint16_t primary_key_column;
    bool has_primary_key;
    BTree secondary_indexes[MAX_CATALOG_INDEXES];
    uint16_t secondary_index_columns[MAX_CATALOG_INDEXES];
    uint16_t secondary_index_count;
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

static DBStatus executor_primary_key_index_path(
    const DB *db,
    const char *table_name,
    char *out_path,
    size_t out_size
) {
    if (db == NULL || table_name == NULL || out_path == NULL || out_size == 0) {
        return DB_ERROR;
    }

    /*
     * Matches the path convention used by catalog creation and db_open rebuild.
     */
    int written = snprintf(
        out_path,
        out_size,
        "%s/indexes/%s_pk.btree",
        db->path,
        table_name
    );

    if (written < 0 || written >= (int)out_size) {
        return DB_ERROR;
    }

    return DB_OK;
}

static DBStatus executor_secondary_index_path(
    const DB *db,
    const char *index_name,
    char *out_path,
    size_t out_size
) {
    if (db == NULL || index_name == NULL || out_path == NULL || out_size == 0) {
        return DB_ERROR;
    }

    int written = snprintf(
        out_path,
        out_size,
        "%s/indexes/%s.btree",
        db->path,
        index_name
    );

    if (written < 0 || written >= (int)out_size) {
        return DB_ERROR;
    }

    return DB_OK;
}

static DBStatus executor_close_secondary_indexes(
    BTree indexes[MAX_CATALOG_INDEXES],
    uint16_t index_count
) {
    DBStatus status = DB_OK;

    for (uint16_t i = 0; i < index_count; i++) {
        DBStatus close_status = btree_close(&indexes[i]);

        if (status == DB_OK && close_status != DB_OK) {
            status = close_status;
        }
    }

    return status;
}

static DBStatus executor_open_secondary_indexes(
    const DB *db,
    const Schema *schema,
    BTree indexes[MAX_CATALOG_INDEXES],
    uint16_t columns[MAX_CATALOG_INDEXES],
    uint16_t *out_index_count
) {
    if (
        db == NULL ||
        schema == NULL ||
        indexes == NULL ||
        columns == NULL ||
        out_index_count == NULL
    ) {
        return DB_ERROR;
    }

    *out_index_count = 0;

    for (uint16_t i = 0; i < db->catalog.index_count; i++) {
        const CatalogIndex *index = &db->catalog.indexes[i];

        if (strcmp(index->table_name, schema->table_name) != 0) {
            continue;
        }

        if (*out_index_count >= MAX_CATALOG_INDEXES) {
            executor_close_secondary_indexes(indexes, *out_index_count);
            return DB_FULL;
        }

        uint16_t column_index = 0;
        DBStatus status = schema_get_column_index(
            schema,
            index->column_name,
            &column_index
        );

        if (status != DB_OK) {
            executor_close_secondary_indexes(indexes, *out_index_count);
            return status;
        }

        char index_path[MAX_DB_PATH];

        status = executor_secondary_index_path(
            db,
            index->index_name,
            index_path,
            sizeof(index_path)
        );

        if (status != DB_OK) {
            executor_close_secondary_indexes(indexes, *out_index_count);
            return status;
        }

        status = btree_open(&indexes[*out_index_count], index_path);

        if (status != DB_OK) {
            executor_close_secondary_indexes(indexes, *out_index_count);
            return status;
        }

        columns[*out_index_count] = column_index;
        (*out_index_count)++;
    }

    return DB_OK;
}

static DBStatus executor_open_primary_key_index(
    const DB *db,
    const Schema *schema,
    BTree *tree,
    uint16_t *out_primary_key_column,
    bool *out_has_primary_key
) {
    if (
        db == NULL ||
        schema == NULL ||
        tree == NULL ||
        out_primary_key_column == NULL ||
        out_has_primary_key == NULL
    ) {
        return DB_ERROR;
    }

    *out_has_primary_key = false;

    /*
     * Tables without a primary key are still valid. In that case callers keep
     * using the existing scan/record path and no B+ tree is opened.
     */
    uint16_t primary_key_column = 0;
    DBStatus status = schema_get_primary_key_index(schema, &primary_key_column);

    if (status == DB_NOT_FOUND) {
        return DB_OK;
    }

    if (status != DB_OK) {
        return status;
    }

    char index_path[MAX_DB_PATH];

    status = executor_primary_key_index_path(
        db,
        schema->table_name,
        index_path,
        sizeof(index_path)
    );

    if (status != DB_OK) {
        return status;
    }

    status = btree_open(tree, index_path);

    if (status != DB_OK) {
        return status;
    }

    *out_primary_key_column = primary_key_column;
    *out_has_primary_key = true;

    return DB_OK;
}

static DBStatus executor_get_primary_key_value(
    const Row *row,
    uint16_t primary_key_column,
    int32_t *out_key
) {
    if (row == NULL || out_key == NULL) {
        return DB_ERROR;
    }

    const Value *key = row_get_value_const(row, primary_key_column);

    if (key == NULL || key->type != VALUE_INT) {
        return DB_ERROR;
    }

    *out_key = key->int_value;

    return DB_OK;
}

static DBStatus executor_get_int_column_value(
    const Row *row,
    uint16_t column_index,
    int32_t *out_key
) {
    if (row == NULL || out_key == NULL) {
        return DB_ERROR;
    }

    const Value *key = row_get_value_const(row, column_index);

    if (key == NULL || key->type != VALUE_INT) {
        return DB_ERROR;
    }

    *out_key = key->int_value;

    return DB_OK;
}

static bool executor_condition_is_primary_key_equality(
    const Schema *schema,
    uint16_t primary_key_column,
    const WhereCondition *condition,
    int32_t *out_key
) {
    if (schema == NULL || condition == NULL || out_key == NULL) {
        return false;
    }

    /*
     * The B+ tree supports point lookups only. Other predicates continue
     * through table_scan so WHERE behavior stays complete.
     */
    if (
        condition->operator_type != SQL_OPERATOR_EQUAL ||
        condition->value.type != VALUE_INT ||
        strcmp(schema->columns[primary_key_column].name, condition->column_name) != 0
    ) {
        return false;
    }

    *out_key = condition->value.int_value;

    return true;
}

static bool executor_condition_is_indexable_equality(
    const WhereCondition *condition,
    int32_t *out_key
) {
    if (condition == NULL || out_key == NULL) {
        return false;
    }

    if (
        condition->operator_type != SQL_OPERATOR_EQUAL ||
        condition->value.type != VALUE_INT
    ) {
        return false;
    }

    *out_key = condition->value.int_value;

    return true;
}

static DBStatus executor_check_unique_secondary_indexes(
    const Row *row,
    BTree indexes[MAX_CATALOG_INDEXES],
    const uint16_t columns[MAX_CATALOG_INDEXES],
    uint16_t index_count
) {
    for (uint16_t i = 0; i < index_count; i++) {
        int32_t key = 0;
        RID existing_rid;

        DBStatus status = executor_get_int_column_value(row, columns[i], &key);

        if (status != DB_OK) {
            return status;
        }

        status = btree_search(&indexes[i], key, &existing_rid);

        if (status == DB_OK) {
            return DB_ERROR;
        }

        if (status != DB_NOT_FOUND) {
            return status;
        }
    }

    return DB_OK;
}

static DBStatus executor_insert_secondary_index_entries(
    const Row *row,
    RID rid,
    BTree indexes[MAX_CATALOG_INDEXES],
    const uint16_t columns[MAX_CATALOG_INDEXES],
    uint16_t index_count
) {
    for (uint16_t i = 0; i < index_count; i++) {
        int32_t key = 0;

        DBStatus status = executor_get_int_column_value(row, columns[i], &key);

        if (status != DB_OK) {
            return status;
        }

        status = btree_insert(&indexes[i], key, rid);

        if (status != DB_OK) {
            return status;
        }
    }

    return DB_OK;
}

static DBStatus executor_delete_secondary_index_entries(
    const Row *row,
    BTree indexes[MAX_CATALOG_INDEXES],
    const uint16_t columns[MAX_CATALOG_INDEXES],
    uint16_t index_count
) {
    for (uint16_t i = 0; i < index_count; i++) {
        int32_t key = 0;

        DBStatus status = executor_get_int_column_value(row, columns[i], &key);

        if (status != DB_OK) {
            return status;
        }

        status = btree_delete(&indexes[i], key);

        if (status != DB_OK) {
            return status;
        }
    }

    return DB_OK;
}

static bool executor_find_open_secondary_index_for_column(
    const Schema *schema,
    const char *column_name,
    const uint16_t columns[MAX_CATALOG_INDEXES],
    uint16_t index_count,
    uint16_t *out_index_position
) {
    uint16_t condition_column = 0;

    if (
        schema == NULL ||
        column_name == NULL ||
        columns == NULL ||
        out_index_position == NULL
    ) {
        return false;
    }

    if (schema_get_column_index(schema, column_name, &condition_column) != DB_OK) {
        return false;
    }

    for (uint16_t i = 0; i < index_count; i++) {
        if (columns[i] == condition_column) {
            *out_index_position = i;
            return true;
        }
    }

    return false;
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

    if (delete_context->has_primary_key) {
        int32_t key = 0;
        DBStatus status = executor_get_primary_key_value(
            row,
            delete_context->primary_key_column,
            &key
        );

        if (status != DB_OK) {
            return status;
        }

        status = btree_delete(delete_context->primary_key_index, key);

        if (status != DB_OK) {
            return status;
        }
    }

    DBStatus status = executor_delete_secondary_index_entries(
        row,
        delete_context->secondary_indexes,
        delete_context->secondary_index_columns,
        delete_context->secondary_index_count
    );

    if (status != DB_OK) {
        return status;
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
        int32_t old_primary_key = 0;
        int32_t new_primary_key = 0;
        bool primary_key_changed = false;
        int32_t old_secondary_keys[MAX_CATALOG_INDEXES];
        int32_t new_secondary_keys[MAX_CATALOG_INDEXES];
        bool secondary_index_changed[MAX_CATALOG_INDEXES];

        if (update_context->has_primary_key) {
            status = executor_get_primary_key_value(
                row,
                update_context->primary_key_column,
                &old_primary_key
            );
        }

        if (status == DB_OK && update_context->has_primary_key) {
            status = executor_get_primary_key_value(
                &updated_row,
                update_context->primary_key_column,
                &new_primary_key
            );
        }

        if (status == DB_OK && update_context->has_primary_key) {
            RID existing_rid;

            primary_key_changed = old_primary_key != new_primary_key;

            if (
                primary_key_changed &&
                btree_search(update_context->primary_key_index, new_primary_key, &existing_rid) == DB_OK
            ) {
                status = DB_ERROR;
            }
        }

        /*
         * Secondary indexes currently have unique integer keys. If an UPDATE
         * changes one of those keys, check the B+ tree before rewriting the
         * table row so duplicates do not leave orphaned row versions behind.
         */
        for (
            uint16_t i = 0;
            status == DB_OK && i < update_context->secondary_index_count;
            i++
        ) {
            status = executor_get_int_column_value(
                row,
                update_context->secondary_index_columns[i],
                &old_secondary_keys[i]
            );

            if (status != DB_OK) {
                break;
            }

            status = executor_get_int_column_value(
                &updated_row,
                update_context->secondary_index_columns[i],
                &new_secondary_keys[i]
            );

            if (status != DB_OK) {
                break;
            }

            secondary_index_changed[i] = old_secondary_keys[i] != new_secondary_keys[i];

            if (secondary_index_changed[i]) {
                RID existing_rid;
                DBStatus search_status = btree_search(
                    &update_context->secondary_indexes[i],
                    new_secondary_keys[i],
                    &existing_rid
                );

                if (search_status == DB_OK) {
                    status = DB_ERROR;
                } else if (search_status != DB_NOT_FOUND) {
                    status = search_status;
                }
            }
        }

        /*
         * UPDATE may be in-place or delete-plus-insert if the row grows. The
         * logged record layer handles both shapes while preserving the SQL
         * executor's simple "replace this row" request.
         */
        if (status == DB_OK) {
            status = record_update_logged(
                update_context->table->file_path,
                rid,
                &updated_row,
                &updated_rid,
                update_context->transaction
            );
        }

        if (
            status == DB_OK &&
            update_context->has_primary_key &&
            (primary_key_changed || !rid_equal(&rid, &updated_rid))
        ) {
            /*
             * record_update_logged can move a row when it grows. Replacing the
             * index entry covers both key changes and RID-only movement.
             */
            status = btree_delete(update_context->primary_key_index, old_primary_key);

            if (status == DB_OK) {
                status = btree_insert(
                    update_context->primary_key_index,
                    new_primary_key,
                    updated_rid
                );
            }
        }

        if (status == DB_OK) {
            for (uint16_t i = 0; i < update_context->secondary_index_count; i++) {
                if (!secondary_index_changed[i] && rid_equal(&rid, &updated_rid)) {
                    continue;
                }

                /*
                 * A row may move even when the indexed value is unchanged.
                 * Replacing the entry keeps key -> RID pointers current.
                 */
                status = btree_delete(
                    &update_context->secondary_indexes[i],
                    old_secondary_keys[i]
                );

                if (status != DB_OK) {
                    break;
                }

                status = btree_insert(
                    &update_context->secondary_indexes[i],
                    new_secondary_keys[i],
                    updated_rid
                );

                if (status != DB_OK) {
                    break;
                }
            }
        }
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

typedef struct {
    BTree *tree;
    uint16_t column_index;
} CreateIndexBuildContext;

static DBStatus executor_create_index_build_callback(
    const Row *row,
    RID rid,
    void *context
) {
    CreateIndexBuildContext *build = context;
    int32_t key = 0;

    if (row == NULL || build == NULL) {
        return DB_ERROR;
    }

    DBStatus status = executor_get_int_column_value(row, build->column_index, &key);

    if (status != DB_OK) {
        return status;
    }

    /*
     * btree_insert rejects duplicate keys. That makes CREATE INDEX fail on
     * existing duplicate values instead of silently creating an incomplete
     * access path.
     */
    return btree_insert(build->tree, key, rid);
}

static DBStatus executor_execute_create_index(DB *db, const CreateIndexPlan *plan) {
    Table table;
    uint16_t column_index = 0;
    char index_path[MAX_DB_PATH];
    BTree tree;

    index_path[0] = '\0';
    memset(&tree, 0, sizeof(BTree));

    DBStatus status = table_open(&table, db, plan->index.table_name);

    if (status != DB_OK) {
        return status;
    }

    status = schema_get_column_index(
        &table.schema,
        plan->index.column_name,
        &column_index
    );

    if (status == DB_OK) {
        status = executor_secondary_index_path(
            db,
            plan->index.index_name,
            index_path,
            sizeof(index_path)
        );
    }

    if (status == DB_OK) {
        status = buffer_pool_discard_file(index_path);
    }

    if (status == DB_OK) {
        FILE *file = fopen(index_path, "wb");

        if (file == NULL) {
            status = DB_IO_ERROR;
        } else if (fclose(file) != 0) {
            status = DB_IO_ERROR;
        }
    }

    if (status == DB_OK) {
        status = btree_open(&tree, index_path);
    }

    if (status == DB_OK) {
        CreateIndexBuildContext context;

        context.tree = &tree;
        context.column_index = column_index;

        status = table_scan(&table, executor_create_index_build_callback, &context);
    }

    DBStatus index_close_status = DB_OK;

    if (tree.is_open) {
        index_close_status = btree_close(&tree);
    }

    if (status != DB_OK && index_path[0] != '\0') {
        /*
         * A failed build should not leave a stray B+ tree behind with no
         * catalog entry. That can happen when existing rows contain duplicate
         * values for this unique secondary index.
         */
        buffer_pool_discard_file(index_path);
        remove(index_path);
    }

    /*
     * Commit catalog metadata only after the index file was fully built.
     * Otherwise the next db_open would believe a failed access path exists.
     */
    if (status == DB_OK && index_close_status == DB_OK) {
        status = catalog_create_index(db, &plan->index);
    }

    DBStatus close_status = table_close(&table);

    if (status != DB_OK) {
        return status;
    }

    if (index_close_status != DB_OK) {
        return index_close_status;
    }

    return close_status;
}

static DBStatus executor_execute_insert(DB *db, const InsertPlan *plan) {
    Table table;
    BTree primary_key_index;
    BTree secondary_indexes[MAX_CATALOG_INDEXES];
    uint16_t secondary_index_columns[MAX_CATALOG_INDEXES];
    uint16_t secondary_index_count = 0;
    bool has_primary_key = false;
    uint16_t primary_key_column = 0;
    RID rid;

    memset(&primary_key_index, 0, sizeof(BTree));
    memset(secondary_indexes, 0, sizeof(secondary_indexes));

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
        status = executor_open_primary_key_index(
            db,
            &table.schema,
            &primary_key_index,
            &primary_key_column,
            &has_primary_key
        );
    }

    if (status == DB_OK) {
        status = executor_open_secondary_indexes(
            db,
            &table.schema,
            secondary_indexes,
            secondary_index_columns,
            &secondary_index_count
        );
    }

    if (status == DB_OK && has_primary_key) {
        int32_t key = 0;
        RID existing_rid;

        status = executor_get_primary_key_value(&plan->row, primary_key_column, &key);

        /*
         * Enforce uniqueness before writing the table row. That keeps duplicate
         * primary-key INSERT failures from creating orphan rows.
         */
        if (
            status == DB_OK &&
            btree_search(&primary_key_index, key, &existing_rid) == DB_OK
        ) {
            status = DB_ERROR;
        }
    }

    if (status == DB_OK) {
        status = executor_check_unique_secondary_indexes(
            &plan->row,
            secondary_indexes,
            secondary_index_columns,
            secondary_index_count
        );
    }

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

    if (status == DB_OK && has_primary_key) {
        int32_t key = 0;

        status = executor_get_primary_key_value(&plan->row, primary_key_column, &key);

        /*
         * The row is durable by this point, so the index can now point at the
         * final RID returned by the record layer.
         */
        if (status == DB_OK) {
            status = btree_insert(&primary_key_index, key, rid);
        }
    }

    if (status == DB_OK) {
        status = executor_insert_secondary_index_entries(
            &plan->row,
            rid,
            secondary_indexes,
            secondary_index_columns,
            secondary_index_count
        );
    }

    DBStatus index_close_status = DB_OK;

    if (has_primary_key) {
        index_close_status = btree_close(&primary_key_index);
    }

    DBStatus secondary_close_status = executor_close_secondary_indexes(
        secondary_indexes,
        secondary_index_count
    );

    DBStatus close_status = table_close(&table);

    if (status != DB_OK) {
        return status;
    }

    if (index_close_status != DB_OK) {
        return index_close_status;
    }

    if (secondary_close_status != DB_OK) {
        return secondary_close_status;
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
    BTree primary_key_index;
    bool has_primary_key = false;
    uint16_t primary_key_column = 0;

    if (out == NULL) {
        return DB_ERROR;
    }

    memset(&primary_key_index, 0, sizeof(BTree));

    DBStatus status = table_open(&table, db, plan->scan.table_name);

    if (status != DB_OK) {
        return status;
    }

    context.table = &table;
    context.plan = plan;
    context.out = out;

    status = executor_open_primary_key_index(
        db,
        &table.schema,
        &primary_key_index,
        &primary_key_column,
        &has_primary_key
    );

    if (status != DB_OK) {
        table_close(&table);
        return status;
    }

    if (has_primary_key && plan->has_filter) {
        int32_t key = 0;

        if (
            executor_condition_is_primary_key_equality(
                &table.schema,
                primary_key_column,
                &plan->filter.condition,
                &key
            )
        ) {
            RID rid;
            status = btree_search(&primary_key_index, key, &rid);

            /*
             * Indexed SELECT is an optimization for equality on the primary key.
             * The normal select callback is still used so projection/filter
             * printing remains exactly the same as the scan path.
             */
            if (status == DB_NOT_FOUND) {
                status = DB_OK;
            } else if (status == DB_OK) {
                Row row;

                status = record_get(table.file_path, rid, &row);

                if (status == DB_OK) {
                    status = executor_select_callback(&row, rid, &context);
                    row_free(&row);
                }
            }

            DBStatus index_close_status = btree_close(&primary_key_index);
            DBStatus close_status = table_close(&table);

            if (status != DB_OK) {
                return status;
            }

            if (index_close_status != DB_OK) {
                return index_close_status;
            }

            return close_status;
        }
    }

    if (plan->has_filter) {
        int32_t key = 0;

        if (
            executor_condition_is_indexable_equality(&plan->filter.condition, &key)
        ) {
            CatalogIndex index;
            DBStatus index_status = catalog_find_index_for_column(
                db,
                table.schema.table_name,
                plan->filter.condition.column_name,
                &index
            );

            if (index_status == DB_OK) {
                char index_path[MAX_DB_PATH];
                BTree secondary_index;

                memset(&secondary_index, 0, sizeof(BTree));

                status = executor_secondary_index_path(
                    db,
                    index.index_name,
                    index_path,
                    sizeof(index_path)
                );

                if (status == DB_OK) {
                    status = btree_open(&secondary_index, index_path);
                }

                if (status == DB_OK) {
                    RID rid;

                    status = btree_search(&secondary_index, key, &rid);

                    if (status == DB_NOT_FOUND) {
                        status = DB_OK;
                    } else if (status == DB_OK) {
                        Row row;

                        status = record_get(table.file_path, rid, &row);

                        if (status == DB_OK) {
                            status = executor_select_callback(&row, rid, &context);
                            row_free(&row);
                        }
                    }
                }

                DBStatus secondary_close_status = DB_OK;

                if (secondary_index.is_open) {
                    secondary_close_status = btree_close(&secondary_index);
                }

                DBStatus primary_close_status = DB_OK;

                if (has_primary_key) {
                    primary_close_status = btree_close(&primary_key_index);
                }

                DBStatus close_status = table_close(&table);

                if (status != DB_OK) {
                    return status;
                }

                if (secondary_close_status != DB_OK) {
                    return secondary_close_status;
                }

                if (primary_close_status != DB_OK) {
                    return primary_close_status;
                }

                return close_status;
            }

            if (index_status != DB_NOT_FOUND) {
                if (has_primary_key) {
                    btree_close(&primary_key_index);
                }

                table_close(&table);
                return index_status;
            }
        }
    }

    /*
     * table_scan owns temporary rows and calls executor_select_callback for
     * each active row.
     */
    status = table_scan(&table, executor_select_callback, &context);

    DBStatus index_close_status = DB_OK;

    if (has_primary_key) {
        index_close_status = btree_close(&primary_key_index);
    }

    DBStatus close_status = table_close(&table);

    if (status != DB_OK) {
        return status;
    }

    if (index_close_status != DB_OK) {
        return index_close_status;
    }

    return close_status;
}

static DBStatus executor_execute_delete(DB *db, const DeletePlan *plan) {
    Table table;
    DeleteContext context;
    BTree primary_key_index;
    bool has_primary_key = false;
    uint16_t primary_key_column = 0;

    memset(&primary_key_index, 0, sizeof(BTree));

    DBStatus status = table_open(&table, db, plan->table_name);

    if (status != DB_OK) {
        return status;
    }

    context.table = &table;
    context.plan = plan;
    context.transaction = &db->transaction;
    context.primary_key_index = &primary_key_index;
    context.primary_key_column = primary_key_column;
    context.has_primary_key = false;
    memset(context.secondary_indexes, 0, sizeof(context.secondary_indexes));
    memset(context.secondary_index_columns, 0, sizeof(context.secondary_index_columns));
    context.secondary_index_count = 0;

    status = executor_open_primary_key_index(
        db,
        &table.schema,
        &primary_key_index,
        &primary_key_column,
        &has_primary_key
    );

    if (status != DB_OK) {
        table_close(&table);
        return status;
    }

    context.primary_key_column = primary_key_column;
    context.has_primary_key = has_primary_key;

    status = executor_open_secondary_indexes(
        db,
        &table.schema,
        context.secondary_indexes,
        context.secondary_index_columns,
        &context.secondary_index_count
    );

    if (status != DB_OK) {
        if (has_primary_key) {
            btree_close(&primary_key_index);
        }

        table_close(&table);
        return status;
    }

    if (plan->has_condition) {
        int32_t key = 0;
        bool used_index = false;

        if (
            has_primary_key &&
            executor_condition_is_primary_key_equality(
                &table.schema,
                primary_key_column,
                &plan->condition,
                &key
            )
        ) {
            RID rid;

            status = btree_search(&primary_key_index, key, &rid);
            used_index = true;

            if (status == DB_NOT_FOUND) {
                status = DB_OK;
            } else if (status == DB_OK) {
                Row row;

                status = record_get(table.file_path, rid, &row);

                if (status == DB_OK) {
                    status = executor_delete_callback(&row, rid, &context);
                    row_free(&row);
                }
            }
        } else if (executor_condition_is_indexable_equality(&plan->condition, &key)) {
            uint16_t secondary_index_position = 0;

            if (
                executor_find_open_secondary_index_for_column(
                    &table.schema,
                    plan->condition.column_name,
                    context.secondary_index_columns,
                    context.secondary_index_count,
                    &secondary_index_position
                )
            ) {
                RID rid;

                status = btree_search(
                    &context.secondary_indexes[secondary_index_position],
                    key,
                    &rid
                );
                used_index = true;

                if (status == DB_NOT_FOUND) {
                    status = DB_OK;
                } else if (status == DB_OK) {
                    Row row;

                    status = record_get(table.file_path, rid, &row);

                    if (status == DB_OK) {
                        status = executor_delete_callback(&row, rid, &context);
                        row_free(&row);
                    }
                }
            }
        }

        if (used_index) {
            DBStatus index_close_status = DB_OK;

            if (has_primary_key) {
                index_close_status = btree_close(&primary_key_index);
            }

            DBStatus secondary_close_status = executor_close_secondary_indexes(
                context.secondary_indexes,
                context.secondary_index_count
            );
            DBStatus close_status = table_close(&table);

            if (status != DB_OK) {
                return status;
            }

            if (index_close_status != DB_OK) {
                return index_close_status;
            }

            if (secondary_close_status != DB_OK) {
                return secondary_close_status;
            }

            return close_status;
        }
    }

    /*
     * DELETE is implemented as table scan plus conditional record_delete.
     */
    status = table_scan(&table, executor_delete_callback, &context);

    DBStatus index_close_status = DB_OK;

    if (has_primary_key) {
        index_close_status = btree_close(&primary_key_index);
    }

    DBStatus secondary_close_status = executor_close_secondary_indexes(
        context.secondary_indexes,
        context.secondary_index_count
    );

    DBStatus close_status = table_close(&table);

    if (status != DB_OK) {
        return status;
    }

    if (index_close_status != DB_OK) {
        return index_close_status;
    }

    if (secondary_close_status != DB_OK) {
        return secondary_close_status;
    }

    return close_status;
}

static DBStatus executor_execute_update(DB *db, const UpdatePlan *plan) {
    Table table;
    UpdateContext context;
    BTree primary_key_index;
    bool has_primary_key = false;
    uint16_t primary_key_column = 0;

    memset(&primary_key_index, 0, sizeof(BTree));

    DBStatus status = table_open(&table, db, plan->table_name);

    if (status != DB_OK) {
        return status;
    }

    context.table = &table;
    context.plan = plan;
    context.transaction = &db->transaction;
    context.primary_key_index = &primary_key_index;
    context.primary_key_column = primary_key_column;
    context.has_primary_key = false;
    memset(context.secondary_indexes, 0, sizeof(context.secondary_indexes));
    memset(context.secondary_index_columns, 0, sizeof(context.secondary_index_columns));
    context.secondary_index_count = 0;

    status = executor_open_primary_key_index(
        db,
        &table.schema,
        &primary_key_index,
        &primary_key_column,
        &has_primary_key
    );

    if (status != DB_OK) {
        table_close(&table);
        return status;
    }

    context.primary_key_column = primary_key_column;
    context.has_primary_key = has_primary_key;

    status = executor_open_secondary_indexes(
        db,
        &table.schema,
        context.secondary_indexes,
        context.secondary_index_columns,
        &context.secondary_index_count
    );

    if (status != DB_OK) {
        if (has_primary_key) {
            btree_close(&primary_key_index);
        }

        table_close(&table);
        return status;
    }

    if (plan->has_condition) {
        int32_t key = 0;
        bool used_index = false;

        if (
            has_primary_key &&
            executor_condition_is_primary_key_equality(
                &table.schema,
                primary_key_column,
                &plan->condition,
                &key
            )
        ) {
            RID rid;

            status = btree_search(&primary_key_index, key, &rid);
            used_index = true;

            if (status == DB_NOT_FOUND) {
                status = DB_OK;
            } else if (status == DB_OK) {
                Row row;

                status = record_get(table.file_path, rid, &row);

                if (status == DB_OK) {
                    status = executor_update_callback(&row, rid, &context);
                    row_free(&row);
                }
            }
        } else if (executor_condition_is_indexable_equality(&plan->condition, &key)) {
            uint16_t secondary_index_position = 0;

            if (
                executor_find_open_secondary_index_for_column(
                    &table.schema,
                    plan->condition.column_name,
                    context.secondary_index_columns,
                    context.secondary_index_count,
                    &secondary_index_position
                )
            ) {
                RID rid;

                status = btree_search(
                    &context.secondary_indexes[secondary_index_position],
                    key,
                    &rid
                );
                used_index = true;

                if (status == DB_NOT_FOUND) {
                    status = DB_OK;
                } else if (status == DB_OK) {
                    Row row;

                    status = record_get(table.file_path, rid, &row);

                    if (status == DB_OK) {
                        status = executor_update_callback(&row, rid, &context);
                        row_free(&row);
                    }
                }
            }
        }

        if (used_index) {
            DBStatus index_close_status = DB_OK;

            if (has_primary_key) {
                index_close_status = btree_close(&primary_key_index);
            }

            DBStatus secondary_close_status = executor_close_secondary_indexes(
                context.secondary_indexes,
                context.secondary_index_count
            );
            DBStatus close_status = table_close(&table);

            if (status != DB_OK) {
                return status;
            }

            if (index_close_status != DB_OK) {
                return index_close_status;
            }

            if (secondary_close_status != DB_OK) {
                return secondary_close_status;
            }

            return close_status;
        }
    }

    /*
     * UPDATE is implemented as table scan plus conditional record_update.
     */
    status = table_scan(&table, executor_update_callback, &context);

    DBStatus index_close_status = DB_OK;

    if (has_primary_key) {
        index_close_status = btree_close(&primary_key_index);
    }

    DBStatus secondary_close_status = executor_close_secondary_indexes(
        context.secondary_indexes,
        context.secondary_index_count
    );

    DBStatus close_status = table_close(&table);

    if (status != DB_OK) {
        return status;
    }

    if (index_close_status != DB_OK) {
        return index_close_status;
    }

    if (secondary_close_status != DB_OK) {
        return secondary_close_status;
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
        case PLAN_CREATE_INDEX:
            return executor_execute_create_index(db, &plan->create_index);
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
