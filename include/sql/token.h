#ifndef SQL_TOKEN_H
#define SQL_TOKEN_H

#include <stdint.h>

/*
 * Maximum stored token text length, including the null terminator.
 * Tokens keep a small copy of their source text so later parser code
 * can read identifiers and literal text without owning the full input.
 */
#define SQL_TOKEN_TEXT_MAX 256

/*
 * TokenType describes the SQL pieces the lexer currently understands.
 *
 * The lexer only classifies text. It does not know whether a table,
 * column, type name, or SQL statement is valid.
 */
typedef enum {
    TOKEN_EOF = 0,
    TOKEN_CREATE,
    TOKEN_TABLE,
    TOKEN_INSERT,
    TOKEN_INTO,
    TOKEN_VALUES,
    TOKEN_SELECT,
    TOKEN_FROM,
    TOKEN_WHERE,
    TOKEN_DELETE,
    TOKEN_IDENTIFIER,
    TOKEN_INT_LITERAL,
    TOKEN_STRING_LITERAL,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_STAR,
    TOKEN_EQUAL,
    TOKEN_GREATER,
    TOKEN_LESS,
    TOKEN_GREATER_EQUAL,
    TOKEN_LESS_EQUAL,
    TOKEN_NOT_EQUAL
} TokenType;

/*
 * Token stores one lexed SQL item.
 *
 * lexeme preserves the text that produced the token. For string literals,
 * lexeme stores the string contents without the surrounding quotes.
 * int_value is meaningful only when type is TOKEN_INT_LITERAL.
 */
typedef struct {
    TokenType type;
    char lexeme[SQL_TOKEN_TEXT_MAX];
    int32_t int_value;
} Token;

#endif
