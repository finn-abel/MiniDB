#ifndef EXECUTION_PLAN_H
#define EXECUTION_PLAN_H

#include <stdbool.h>
#include <stdint.h>

#include "common.h"
#include "catalog.h"
#include "row.h"
#include "schema.h"
#include "sql/ast.h"

/*
 * PlanType identifies the physical-ish operation MiniDB should execute.
 *
 * This is not a cost-based optimizer output. It is a simple representation
 * chosen directly from the already-bound statement.
 */
typedef enum {
    PLAN_CREATE_TABLE,
    PLAN_CREATE_INDEX,
    PLAN_DROP_INDEX,
    PLAN_INSERT,
    PLAN_SELECT,
    PLAN_DELETE,
    PLAN_UPDATE,
    PLAN_META_COMMAND
} PlanType;

/*
 * TableScanPlan means "read every active row from this table".
 * Later execution code can combine it with filter/project settings.
 */
typedef struct {
    char table_name[MAX_TABLE_NAME];
} TableScanPlan;

/*
 * FilterPlan applies one WHERE condition to rows from a scan.
 */
typedef struct {
    WhereCondition condition;
} FilterPlan;

/*
 * ProjectPlan keeps only the named columns.
 * SELECT * is represented by the absence of a project plan.
 */
typedef struct {
    uint16_t column_count;
    char columns[MAX_COLUMNS][MAX_COLUMN_NAME];
} ProjectPlan;

/*
 * SelectPlan is always built as:
 *   TableScan
 *     optional Filter
 *     optional Project
 */
typedef struct {
    TableScanPlan scan;
    bool has_filter;
    FilterPlan filter;
    bool has_project;
    ProjectPlan project;
} SelectPlan;

/*
 * InsertPlan owns the Row it will insert.
 */
typedef struct {
    char table_name[MAX_TABLE_NAME];
    Row row;
} InsertPlan;

/*
 * DeletePlan scans a table and optionally applies one condition before
 * deleting matching rows.
 */
typedef struct {
    char table_name[MAX_TABLE_NAME];
    bool has_condition;
    WhereCondition condition;
} DeletePlan;

/*
 * UpdatePlan scans a table, applies one optional condition, and writes one
 * column assignment to every matching row.
 */
typedef struct {
    char table_name[MAX_TABLE_NAME];
    char set_column[MAX_COLUMN_NAME];
    Value set_value;
    bool has_condition;
    WhereCondition condition;
} UpdatePlan;

/*
 * CreateTablePlan carries the already-validated schema from the binder.
 */
typedef struct {
    Schema schema;
} CreateTablePlan;

typedef struct {
    CatalogIndex index;
} CreateIndexPlan;

typedef struct {
    char index_name[MAX_INDEX_NAME];
} DropIndexPlan;

/*
 * MetaCommandPlan preserves shell commands for shell-level execution.
 */
typedef struct {
    char command[MAX_INPUT_SIZE];
} MetaCommandPlan;

/*
 * Plan is the root object handed to future execution code.
 * Only the union member matching type is valid.
 */
typedef struct {
    PlanType type;

    union {
        CreateTablePlan create_table;
        CreateIndexPlan create_index;
        DropIndexPlan drop_index;
        InsertPlan insert;
        SelectPlan select;
        DeletePlan delete_plan;
        UpdatePlan update;
        MetaCommandPlan meta_command;
    };
} Plan;

/*
 * Initializes an empty plan of the requested type.
 */
DBStatus plan_init(Plan *plan, PlanType type);

/*
 * Frees any heap memory owned by the active plan variant.
 */
void plan_free(Plan *plan);

#endif
