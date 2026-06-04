#ifndef SQL_BINDER_H
#define SQL_BINDER_H

#include <stdbool.h>

#include "catalog.h"
#include "common.h"
#include "schema.h"
#include "sql/ast.h"

/*
 * BoundStatement is the semantic form of a parsed SQL statement.
 *
 * The parser checks syntax only. The binder checks catalog and schema facts,
 * then stores the original statement plus the schema needed by later planning
 * and execution code.
 */
typedef struct {
    Statement statement;
    Schema table_schema;
    bool has_table_schema;
} BoundStatement;

/*
 * Checks a parsed statement against the database catalog.
 *
 * Meta commands do not need catalog checks. SQL statements that reference a
 * table must be checked here before planning/execution.
 */
DBStatus binder_bind(
    const DB *db,
    const Statement *statement,
    BoundStatement *out_bound
);

/*
 * Releases heap memory owned by the bound statement's copied AST.
 */
void binder_bound_statement_free(BoundStatement *bound);

#endif
