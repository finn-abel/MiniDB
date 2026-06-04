#ifndef EXECUTION_EXECUTOR_H
#define EXECUTION_EXECUTOR_H

#include <stdio.h>

#include "common.h"
#include "db.h"
#include "execution/plan.h"

/*
 * Executes one planned statement.
 *
 * The executor is the first SQL layer that mutates or reads table files.
 * SELECT and meta-command output is written to out when output is needed.
 */
DBStatus executor_execute(DB *db, const Plan *plan, FILE *out);

#endif
