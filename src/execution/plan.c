#include <string.h>

#include "common.h"
#include "execution/plan.h"
#include "row.h"
#include "sql/ast.h"

DBStatus plan_init(Plan *plan, PlanType type) {
    if (plan == NULL) {
        return DB_ERROR;
    }

    /*
     * Clear the union so optional flags and owned fields start safe.
     */
    memset(plan, 0, sizeof(Plan));
    plan->type = type;

    return DB_OK;
}

void plan_free(Plan *plan) {
    if (plan == NULL) {
        return;
    }

    /*
     * INSERT plans own a Row, and row_free releases any text values inside it.
     */
    if (plan->type == PLAN_INSERT) {
        row_free(&plan->insert.row);
    }

    /*
     * Filter/condition plans own copied WHERE values.
     */
    if (plan->type == PLAN_SELECT && plan->select.has_filter) {
        ast_where_free(&plan->select.filter.condition);
    }

    if (plan->type == PLAN_DELETE && plan->delete_plan.has_condition) {
        ast_where_free(&plan->delete_plan.condition);
    }

    /*
     * Reset after cleanup so accidental reuse behaves like an empty plan.
     */
    memset(plan, 0, sizeof(Plan));
}
