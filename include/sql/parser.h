#ifndef SQL_PARSER_H
#define SQL_PARSER_H

#include "common.h"
#include "sql/ast.h"

/*
 * Parses one SQL or meta-command input string into a Statement AST.
 *
 * The parser only checks syntax. It does not check whether referenced tables
 * or columns exist; that belongs to the binder/execution layers later.
 */
DBStatus parser_parse(const char *input, Statement *out_statement);

#endif
