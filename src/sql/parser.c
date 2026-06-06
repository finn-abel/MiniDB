#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common.h"
#include "sql/ast.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "sql/token.h"
#include "value.h"

typedef struct {
    Lexer lexer;
    Token current;
} Parser;

/*
 * Parser is a small recursive-descent parser.
 *
 * It keeps one current token as lookahead. Each parse function consumes the
 * tokens for one grammar rule and leaves current pointing at the next token
 * that has not been handled yet.
 */

/*
 * Moves the parser to the next token.
 *
 * Lexer errors are parse errors from the parser's point of view, so callers
 * just propagate the returned status.
 */
static DBStatus parser_advance(Parser *parser) {
    return lexer_next(&parser->lexer, &parser->current);
}

/*
 * Consumes the current token only if it has the expected type.
 *
 * This is used for required punctuation and keywords where there is only one
 * valid token in the grammar.
 */
static DBStatus parser_expect(Parser *parser, TokenType type) {
    if (parser->current.type != type) {
        return DB_PARSE_ERROR;
    }

    return parser_advance(parser);
}

/*
 * Compares parser text case-insensitively.
 *
 * The lexer leaves INT and TEXT as identifiers because they are only type
 * names inside CREATE TABLE, not global SQL keywords.
 */
static bool parser_text_equals(const char *left, const char *right) {
    uint32_t i = 0;

    while (left[i] != '\0' && right[i] != '\0') {
        if (toupper((unsigned char)left[i]) != toupper((unsigned char)right[i])) {
            return false;
        }

        i++;
    }

    return left[i] == '\0' && right[i] == '\0';
}

static bool parser_is_identifier_token(TokenType type) {
    return type == TOKEN_IDENTIFIER;
}

/*
 * Reads an identifier into a fixed-size caller buffer.
 *
 * This is used for table names, column names, and CREATE TABLE type words.
 * The parser only checks the token shape and name length, not whether the
 * identifier exists anywhere.
 */
static DBStatus parser_expect_identifier(Parser *parser, char *out, uint32_t out_size) {
    if (!parser_is_identifier_token(parser->current.type)) {
        return DB_PARSE_ERROR;
    }

    if (strlen(parser->current.lexeme) == 0 || strlen(parser->current.lexeme) >= out_size) {
        return DB_ERROR;
    }

    strncpy(out, parser->current.lexeme, out_size - 1);
    out[out_size - 1] = '\0';

    return parser_advance(parser);
}

/*
 * Parses the type names currently allowed in CREATE TABLE:
 *   INT
 *   TEXT
 */
static DBStatus parser_parse_type(Parser *parser, ValueType *out_type) {
    if (parser->current.type != TOKEN_IDENTIFIER || out_type == NULL) {
        return DB_PARSE_ERROR;
    }

    if (parser_text_equals(parser->current.lexeme, "INT")) {
        *out_type = VALUE_INT;
        return parser_advance(parser);
    }

    if (parser_text_equals(parser->current.lexeme, "TEXT")) {
        *out_type = VALUE_TEXT;
        return parser_advance(parser);
    }

    return DB_PARSE_ERROR;
}

static DBStatus parser_parse_column_constraints(
    Parser *parser,
    bool *out_not_null,
    bool *out_primary_key
) {
    if (parser == NULL || out_not_null == NULL || out_primary_key == NULL) {
        return DB_ERROR;
    }

    *out_not_null = false;
    *out_primary_key = false;

    while (parser->current.type == TOKEN_IDENTIFIER) {
        if (parser_text_equals(parser->current.lexeme, "PRIMARY")) {
            if (*out_primary_key) {
                return DB_PARSE_ERROR;
            }

            DBStatus status = parser_advance(parser);

            if (status != DB_OK) {
                return status;
            }

            if (
                parser->current.type != TOKEN_IDENTIFIER ||
                !parser_text_equals(parser->current.lexeme, "KEY")
            ) {
                return DB_PARSE_ERROR;
            }

            *out_primary_key = true;
            status = parser_advance(parser);

            if (status != DB_OK) {
                return status;
            }

            continue;
        }

        if (parser_text_equals(parser->current.lexeme, "NOT")) {
            if (*out_not_null) {
                return DB_PARSE_ERROR;
            }

            DBStatus status = parser_advance(parser);

            if (status != DB_OK) {
                return status;
            }

            if (
                parser->current.type != TOKEN_IDENTIFIER ||
                !parser_text_equals(parser->current.lexeme, "NULL")
            ) {
                return DB_PARSE_ERROR;
            }

            *out_not_null = true;
            status = parser_advance(parser);

            if (status != DB_OK) {
                return status;
            }

            continue;
        }

        break;
    }

    return DB_OK;
}

/*
 * Parses literal values that can appear in INSERT VALUES or WHERE clauses.
 *
 * Text literals allocate memory through value_text, so callers must either
 * transfer/copy the Value into an AST node or free it on error.
 */
static DBStatus parser_parse_literal(Parser *parser, Value *out_value) {
    DBStatus status;

    if (out_value == NULL) {
        return DB_ERROR;
    }

    if (parser->current.type == TOKEN_INT_LITERAL) {
        *out_value = value_int(parser->current.int_value);
        return parser_advance(parser);
    }

    if (parser->current.type == TOKEN_STRING_LITERAL) {
        status = value_text(out_value, parser->current.lexeme);

        if (status != DB_OK) {
            return status;
        }

        status = parser_advance(parser);

        if (status != DB_OK) {
            value_free(out_value);
        }

        return status;
    }

    return DB_PARSE_ERROR;
}

/*
 * Maps comparison tokens to the AST operator enum.
 */
static DBStatus parser_parse_operator(Parser *parser, SqlOperator *out_operator) {
    if (out_operator == NULL) {
        return DB_ERROR;
    }

    switch (parser->current.type) {
        case TOKEN_EQUAL:
            *out_operator = SQL_OPERATOR_EQUAL;
            break;
        case TOKEN_NOT_EQUAL:
            *out_operator = SQL_OPERATOR_NOT_EQUAL;
            break;
        case TOKEN_GREATER:
            *out_operator = SQL_OPERATOR_GREATER;
            break;
        case TOKEN_LESS:
            *out_operator = SQL_OPERATOR_LESS;
            break;
        case TOKEN_GREATER_EQUAL:
            *out_operator = SQL_OPERATOR_GREATER_EQUAL;
            break;
        case TOKEN_LESS_EQUAL:
            *out_operator = SQL_OPERATOR_LESS_EQUAL;
            break;
        default:
            return DB_PARSE_ERROR;
    }

    return parser_advance(parser);
}

/*
 * Parses the only WHERE shape supported right now:
 *   column_name operator literal
 *
 * The returned condition owns its Value. Call ast_where_free after copying it
 * into a statement.
 */
static DBStatus parser_parse_where_condition(Parser *parser, WhereCondition *out_condition) {
    char column_name[MAX_COLUMN_NAME];
    SqlOperator operator_type;
    Value value;

    DBStatus status = parser_expect_identifier(parser, column_name, sizeof(column_name));

    if (status != DB_OK) {
        return status;
    }

    status = parser_parse_operator(parser, &operator_type);

    if (status != DB_OK) {
        return status;
    }

    status = parser_parse_literal(parser, &value);

    if (status != DB_OK) {
        return status;
    }

    status = ast_where_init(out_condition, column_name, operator_type, &value);
    value_free(&value);

    return status;
}

/*
 * Finishes a complete SQL statement.
 *
 * MiniDB requires a semicolon for SQL statements and rejects any extra tokens
 * after it. Meta commands are handled separately and do not use semicolons.
 */
static DBStatus parser_finish_statement(Parser *parser) {
    DBStatus status = parser_expect(parser, TOKEN_SEMICOLON);

    if (status != DB_OK) {
        return status;
    }

    if (parser->current.type != TOKEN_EOF) {
        return DB_PARSE_ERROR;
    }

    return DB_OK;
}

/*
 * CREATE TABLE grammar after CREATE has already been consumed:
 *   TABLE identifier (identifier type [, identifier type]*);
 */
static DBStatus parser_parse_create_table(Parser *parser, Statement *out_statement) {
    char table_name[MAX_TABLE_NAME];
    char column_name[MAX_COLUMN_NAME];
    ValueType column_type;
    bool not_null;
    bool primary_key;
    DBStatus status;

    status = ast_statement_init(out_statement, STATEMENT_CREATE_TABLE);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_TABLE);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect_identifier(parser, table_name, sizeof(table_name));

    if (status != DB_OK) {
        return status;
    }

    status = ast_create_table_init(&out_statement->create_table, table_name);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_LEFT_PAREN);

    if (status != DB_OK) {
        return status;
    }

    while (true) {
        /*
         * At least one column is required. If the next token is ')', the first
         * identifier read fails and the statement is rejected.
         */
        status = parser_expect_identifier(parser, column_name, sizeof(column_name));

        if (status != DB_OK) {
            return status;
        }

        status = parser_parse_type(parser, &column_type);

        if (status != DB_OK) {
            return status;
        }

        status = parser_parse_column_constraints(
            parser,
            &not_null,
            &primary_key
        );

        if (status != DB_OK) {
            return status;
        }

        status = ast_create_table_add_column_with_constraints(
            &out_statement->create_table,
            column_name,
            column_type,
            not_null,
            primary_key
        );

        if (status != DB_OK) {
            return status;
        }

        if (parser->current.type != TOKEN_COMMA) {
            break;
        }

        status = parser_advance(parser);

        if (status != DB_OK) {
            return status;
        }
    }

    status = parser_expect(parser, TOKEN_RIGHT_PAREN);

    if (status != DB_OK) {
        return status;
    }

    return parser_finish_statement(parser);
}

/*
 * CREATE INDEX grammar after CREATE has already been consumed:
 *   INDEX identifier ON identifier (identifier [, identifier]*);
 */
static DBStatus parser_parse_create_index(Parser *parser, Statement *out_statement) {
    char index_name[MAX_INDEX_NAME];
    char table_name[MAX_TABLE_NAME];
    char column_name[MAX_COLUMN_NAME];
    DBStatus status;

    status = ast_statement_init(out_statement, STATEMENT_CREATE_INDEX);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_INDEX);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect_identifier(parser, index_name, sizeof(index_name));

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_ON);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect_identifier(parser, table_name, sizeof(table_name));

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_LEFT_PAREN);

    if (status != DB_OK) {
        return status;
    }

    status = ast_create_index_init(
        &out_statement->create_index,
        index_name,
        table_name
    );

    if (status != DB_OK) {
        return status;
    }

    while (true) {
        status = parser_expect_identifier(parser, column_name, sizeof(column_name));

        if (status != DB_OK) {
            return status;
        }

        status = ast_create_index_add_column(&out_statement->create_index, column_name);

        if (status != DB_OK) {
            return status;
        }

        if (parser->current.type != TOKEN_COMMA) {
            break;
        }

        status = parser_advance(parser);

        if (status != DB_OK) {
            return status;
        }
    }

    status = parser_expect(parser, TOKEN_RIGHT_PAREN);

    if (status != DB_OK) {
        return status;
    }

    return parser_finish_statement(parser);
}

/*
 * CREATE dispatch:
 *   CREATE TABLE ...
 *   CREATE INDEX ...
 */
static DBStatus parser_parse_create(Parser *parser, Statement *out_statement) {
    DBStatus status = parser_expect(parser, TOKEN_CREATE);

    if (status != DB_OK) {
        return status;
    }

    if (parser->current.type == TOKEN_TABLE) {
        return parser_parse_create_table(parser, out_statement);
    }

    if (parser->current.type == TOKEN_INDEX) {
        return parser_parse_create_index(parser, out_statement);
    }

    return DB_PARSE_ERROR;
}

/*
 * INSERT grammar:
 *   INSERT INTO identifier VALUES (literal [, literal]*);
 */
static DBStatus parser_parse_insert(Parser *parser, Statement *out_statement) {
    char table_name[MAX_TABLE_NAME];
    Value value;
    DBStatus status;

    status = ast_statement_init(out_statement, STATEMENT_INSERT);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_INSERT);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_INTO);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect_identifier(parser, table_name, sizeof(table_name));

    if (status != DB_OK) {
        return status;
    }

    status = ast_insert_init(&out_statement->insert, table_name);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_VALUES);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_LEFT_PAREN);

    if (status != DB_OK) {
        return status;
    }

    while (true) {
        /*
         * At least one literal is required. Empty VALUES () is not valid.
         */
        status = parser_parse_literal(parser, &value);

        if (status != DB_OK) {
            return status;
        }

        status = ast_insert_add_value(&out_statement->insert, &value);
        value_free(&value);

        if (status != DB_OK) {
            return status;
        }

        if (parser->current.type != TOKEN_COMMA) {
            break;
        }

        status = parser_advance(parser);

        if (status != DB_OK) {
            return status;
        }
    }

    status = parser_expect(parser, TOKEN_RIGHT_PAREN);

    if (status != DB_OK) {
        return status;
    }

    return parser_finish_statement(parser);
}

/*
 * SELECT grammar:
 *   SELECT * FROM identifier [WHERE condition];
 *   SELECT identifier [, identifier]* FROM identifier [WHERE condition];
 */
static DBStatus parser_parse_select(Parser *parser, Statement *out_statement) {
    char selected_columns[MAX_COLUMNS][MAX_COLUMN_NAME];
    uint16_t selected_column_count = 0;
    char table_name[MAX_TABLE_NAME];
    WhereCondition condition;
    bool condition_initialized = false;
    DBStatus status;

    status = ast_statement_init(out_statement, STATEMENT_SELECT);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_SELECT);

    if (status != DB_OK) {
        return status;
    }

    if (parser->current.type == TOKEN_STAR) {
        /*
         * The AST represents SELECT * with selected_column_count == 0.
         */
        status = parser_advance(parser);

        if (status != DB_OK) {
            return status;
        }
    } else {
        while (true) {
            if (selected_column_count >= MAX_COLUMNS) {
                return DB_FULL;
            }

            status = parser_expect_identifier(
                parser,
                selected_columns[selected_column_count],
                MAX_COLUMN_NAME
            );

            if (status != DB_OK) {
                return status;
            }

            selected_column_count++;

            if (parser->current.type != TOKEN_COMMA) {
                break;
            }

            status = parser_advance(parser);

            if (status != DB_OK) {
                return status;
            }
        }
    }

    status = parser_expect(parser, TOKEN_FROM);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect_identifier(parser, table_name, sizeof(table_name));

    if (status != DB_OK) {
        return status;
    }

    status = ast_select_init(&out_statement->select, table_name);

    if (status != DB_OK) {
        return status;
    }

    for (uint16_t i = 0; i < selected_column_count; i++) {
        /*
         * Projection columns are stored after the table name is known because
         * ast_select_init clears the statement payload.
         */
        status = ast_select_add_column(&out_statement->select, selected_columns[i]);

        if (status != DB_OK) {
            return status;
        }
    }

    if (parser->current.type == TOKEN_WHERE) {
        status = parser_advance(parser);

        if (status != DB_OK) {
            return status;
        }

        status = parser_parse_where_condition(parser, &condition);

        if (status != DB_OK) {
            return status;
        }

        condition_initialized = true;
        /*
         * ast_select_set_where deep-copies the condition, so the temporary
         * parsed condition can be released immediately afterward.
         */
        status = ast_select_set_where(&out_statement->select, &condition);
        ast_where_free(&condition);
        condition_initialized = false;

        if (status != DB_OK) {
            return status;
        }
    }

    if (condition_initialized) {
        ast_where_free(&condition);
    }

    return parser_finish_statement(parser);
}

/*
 * DELETE grammar:
 *   DELETE FROM identifier [WHERE condition];
 */
static DBStatus parser_parse_delete(Parser *parser, Statement *out_statement) {
    char table_name[MAX_TABLE_NAME];
    WhereCondition condition;
    bool condition_initialized = false;
    DBStatus status;

    status = ast_statement_init(out_statement, STATEMENT_DELETE);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_DELETE);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_FROM);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect_identifier(parser, table_name, sizeof(table_name));

    if (status != DB_OK) {
        return status;
    }

    status = ast_delete_init(&out_statement->delete_statement, table_name);

    if (status != DB_OK) {
        return status;
    }

    if (parser->current.type == TOKEN_WHERE) {
        status = parser_advance(parser);

        if (status != DB_OK) {
            return status;
        }

        status = parser_parse_where_condition(parser, &condition);

        if (status != DB_OK) {
            return status;
        }

        condition_initialized = true;
        /*
         * ast_delete_set_where deep-copies the condition, so the temporary
         * parsed condition can be released immediately afterward.
         */
        status = ast_delete_set_where(&out_statement->delete_statement, &condition);
        ast_where_free(&condition);
        condition_initialized = false;

        if (status != DB_OK) {
            return status;
        }
    }

    if (condition_initialized) {
        ast_where_free(&condition);
    }

    return parser_finish_statement(parser);
}

/*
 * UPDATE grammar:
 *   UPDATE identifier SET identifier = literal [WHERE condition];
 */
static DBStatus parser_parse_update(Parser *parser, Statement *out_statement) {
    char table_name[MAX_TABLE_NAME];
    char column_name[MAX_COLUMN_NAME];
    Value value;
    WhereCondition condition;
    bool condition_initialized = false;
    DBStatus status;

    status = ast_statement_init(out_statement, STATEMENT_UPDATE);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_UPDATE);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect_identifier(parser, table_name, sizeof(table_name));

    if (status != DB_OK) {
        return status;
    }

    status = ast_update_init(&out_statement->update, table_name);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_SET);

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect_identifier(parser, column_name, sizeof(column_name));

    if (status != DB_OK) {
        return status;
    }

    status = parser_expect(parser, TOKEN_EQUAL);

    if (status != DB_OK) {
        return status;
    }

    status = parser_parse_literal(parser, &value);

    if (status != DB_OK) {
        return status;
    }

    status = ast_update_set_assignment(&out_statement->update, column_name, &value);
    value_free(&value);

    if (status != DB_OK) {
        return status;
    }

    if (parser->current.type == TOKEN_WHERE) {
        status = parser_advance(parser);

        if (status != DB_OK) {
            return status;
        }

        status = parser_parse_where_condition(parser, &condition);

        if (status != DB_OK) {
            return status;
        }

        condition_initialized = true;
        /*
         * ast_update_set_where owns its own copy, so the temporary condition
         * can be freed immediately after the assignment.
         */
        status = ast_update_set_where(&out_statement->update, &condition);
        ast_where_free(&condition);
        condition_initialized = false;

        if (status != DB_OK) {
            return status;
        }
    }

    if (condition_initialized) {
        ast_where_free(&condition);
    }

    return parser_finish_statement(parser);
}

/*
 * Meta commands are shell commands, not SQL.
 *
 * They skip the lexer so commands like ".schema users" can be preserved as one
 * command string for the shell layer.
 */
static DBStatus parser_parse_meta_command(const char *input, Statement *out_statement) {
    DBStatus status = ast_statement_init(out_statement, STATEMENT_META_COMMAND);

    if (status != DB_OK) {
        return status;
    }

    if (strcmp(input, ".exit") == 0 ||
        strcmp(input, ".tables") == 0 ||
        strcmp(input, ".help") == 0) {
        return ast_meta_command_init(&out_statement->meta_command, input);
    }

    if (strncmp(input, ".schema ", 8) == 0 && input[8] != '\0') {
        return ast_meta_command_init(&out_statement->meta_command, input);
    }

    return DB_PARSE_ERROR;
}

DBStatus parser_parse(const char *input, Statement *out_statement) {
    Parser parser;
    DBStatus status;

    if (input == NULL || out_statement == NULL) {
        return DB_ERROR;
    }

    if (input[0] == '.') {
        /*
         * Dot-prefixed commands are handled before SQL lexing.
         */
        status = parser_parse_meta_command(input, out_statement);

        if (status != DB_OK) {
            ast_statement_free(out_statement);
        }

        return status;
    }

    status = lexer_init(&parser.lexer, input);

    if (status != DB_OK) {
        return status;
    }

    status = parser_advance(&parser);

    if (status != DB_OK) {
        return status;
    }

    switch (parser.current.type) {
        /*
         * Dispatch from the first SQL token. Each statement parser validates
         * the rest of its grammar.
         */
        case TOKEN_CREATE:
            status = parser_parse_create(&parser, out_statement);
            break;
        case TOKEN_INSERT:
            status = parser_parse_insert(&parser, out_statement);
            break;
        case TOKEN_SELECT:
            status = parser_parse_select(&parser, out_statement);
            break;
        case TOKEN_DELETE:
            status = parser_parse_delete(&parser, out_statement);
            break;
        case TOKEN_UPDATE:
            status = parser_parse_update(&parser, out_statement);
            break;
        default:
            status = DB_PARSE_ERROR;
            break;
    }

    if (status != DB_OK) {
        ast_statement_free(out_statement);
    }

    return status;
}
