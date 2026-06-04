#ifndef EXECUTION_PLANNER_H
#define EXECUTION_PLANNER_H

#include "common.h"
#include "execution/plan.h"
#include "sql/binder.h"

/*
 * Converts a bound statement into a simple execution plan.
 *
 * The planner assumes semantic checks have already happened in the binder.
 * It does not choose between access paths; table scans are always used.
 */
DBStatus planner_create_plan(const BoundStatement *bound, Plan *out_plan);

#endif
