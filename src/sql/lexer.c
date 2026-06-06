#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "sql/lexer.h"
#include "sql/token.h"

/*
 * Reads the current character without consuming it.
 */
static char lexer_peek(const Lexer *lexer) {
    return lexer->input[lexer->position];
}

/*
 * Consumes one character and moves the lexer forward.
 */
static char lexer_advance(Lexer *lexer) {
    char current = lexer_peek(lexer);

    if (current != '\0') {
        lexer->position++;
    }

    return current;
}

/*
 * SQL treats spaces, tabs, and newlines as separators between tokens.
 */
static void lexer_skip_whitespace(Lexer *lexer) {
    while (isspace((unsigned char)lexer_peek(lexer))) {
        lexer_advance(lexer);
    }
}

static bool lexer_is_identifier_start(char character) {
    return isalpha((unsigned char)character) || character == '_';
}

static bool lexer_is_identifier_part(char character) {
    return isalnum((unsigned char)character) || character == '_';
}

/*
 * Resets token fields before a specific token fills in its lexeme/value.
 */
static void token_init(Token *token, TokenType type) {
    token->type = type;
    token->lexeme[0] = '\0';
    token->int_value = 0;
}

static bool lexer_text_equals_keyword(
    const char *text,
    uint32_t length,
    const char *keyword
) {
    uint32_t i = 0;

    /*
     * Keywords are matched case-insensitively, but the original lexeme text
     * is still preserved in the token.
     */
    while (i < length && keyword[i] != '\0') {
        char left = (char)toupper((unsigned char)text[i]);
        char right = keyword[i];

        if (left != right) {
            return false;
        }

        i++;
    }

    return i == length && keyword[i] == '\0';
}

/*
 * Converts identifier-looking text into a keyword token when it matches one
 * of MiniDB's supported SQL keywords.
 */
static TokenType lexer_keyword_type(const char *text, uint32_t length) {
    if (lexer_text_equals_keyword(text, length, "CREATE")) {
        return TOKEN_CREATE;
    }

    if (lexer_text_equals_keyword(text, length, "TABLE")) {
        return TOKEN_TABLE;
    }

    if (lexer_text_equals_keyword(text, length, "INSERT")) {
        return TOKEN_INSERT;
    }

    if (lexer_text_equals_keyword(text, length, "INTO")) {
        return TOKEN_INTO;
    }

    if (lexer_text_equals_keyword(text, length, "VALUES")) {
        return TOKEN_VALUES;
    }

    if (lexer_text_equals_keyword(text, length, "SELECT")) {
        return TOKEN_SELECT;
    }

    if (lexer_text_equals_keyword(text, length, "FROM")) {
        return TOKEN_FROM;
    }

    if (lexer_text_equals_keyword(text, length, "WHERE")) {
        return TOKEN_WHERE;
    }

    if (lexer_text_equals_keyword(text, length, "DELETE")) {
        return TOKEN_DELETE;
    }

    if (lexer_text_equals_keyword(text, length, "UPDATE")) {
        return TOKEN_UPDATE;
    }

    if (lexer_text_equals_keyword(text, length, "SET")) {
        return TOKEN_SET;
    }

    return TOKEN_IDENTIFIER;
}

static DBStatus lexer_copy_text(
    Token *token,
    const char *start,
    uint32_t length
) {
    if (length >= SQL_TOKEN_TEXT_MAX) {
        /*
         * Token text is stored inline for now, so overlong lexemes are
         * rejected instead of being truncated.
         */
        return DB_ERROR;
    }

    memcpy(token->lexeme, start, length);
    token->lexeme[length] = '\0';

    return DB_OK;
}

/*
 * Reads keywords, table names, column names, and future type names.
 */
static DBStatus lexer_read_identifier(Lexer *lexer, Token *out_token) {
    uint32_t start = lexer->position;

    while (lexer_is_identifier_part(lexer_peek(lexer))) {
        lexer_advance(lexer);
    }

    uint32_t length = lexer->position - start;
    TokenType type = lexer_keyword_type(&lexer->input[start], length);

    token_init(out_token, type);

    return lexer_copy_text(out_token, &lexer->input[start], length);
}

/*
 * Reads a base-10 integer literal.
 */
static DBStatus lexer_read_integer(Lexer *lexer, Token *out_token) {
    uint32_t start = lexer->position;

    while (isdigit((unsigned char)lexer_peek(lexer))) {
        lexer_advance(lexer);
    }

    uint32_t length = lexer->position - start;

    token_init(out_token, TOKEN_INT_LITERAL);

    DBStatus status = lexer_copy_text(out_token, &lexer->input[start], length);

    if (status != DB_OK) {
        return status;
    }

    out_token->int_value = (int32_t)strtol(out_token->lexeme, NULL, 10);

    return DB_OK;
}

/*
 * Reads a double-quoted string literal.
 *
 * The stored lexeme excludes the surrounding quote characters. Escapes are
 * not supported yet; the lexer stops at the next double quote.
 */
static DBStatus lexer_read_string(Lexer *lexer, Token *out_token) {
    uint32_t start;
    uint32_t length;

    lexer_advance(lexer);
    start = lexer->position;

    while (lexer_peek(lexer) != '"' && lexer_peek(lexer) != '\0') {
        lexer_advance(lexer);
    }

    if (lexer_peek(lexer) == '\0') {
        return DB_PARSE_ERROR;
    }

    length = lexer->position - start;

    token_init(out_token, TOKEN_STRING_LITERAL);

    DBStatus status = lexer_copy_text(out_token, &lexer->input[start], length);

    if (status != DB_OK) {
        return status;
    }

    lexer_advance(lexer);

    return DB_OK;
}

DBStatus lexer_init(Lexer *lexer, const char *input) {
    if (lexer == NULL || input == NULL) {
        return DB_ERROR;
    }

    lexer->input = input;
    lexer->position = 0;

    return DB_OK;
}

DBStatus lexer_next(Lexer *lexer, Token *out_token) {
    if (lexer == NULL || lexer->input == NULL || out_token == NULL) {
        return DB_ERROR;
    }

    lexer_skip_whitespace(lexer);

    char current = lexer_peek(lexer);

    if (current == '\0') {
        token_init(out_token, TOKEN_EOF);
        return DB_OK;
    }

    /*
     * Identifiers, integer literals, and string literals have variable length,
     * so they each get a dedicated reader.
     */
    if (lexer_is_identifier_start(current)) {
        return lexer_read_identifier(lexer, out_token);
    }

    if (isdigit((unsigned char)current)) {
        return lexer_read_integer(lexer, out_token);
    }

    if (current == '"') {
        return lexer_read_string(lexer, out_token);
    }

    lexer_advance(lexer);

    /*
     * Single-character symbols are returned directly. Two-character
     * comparisons consume the second character only after it is confirmed.
     */
    switch (current) {
        case '(':
            token_init(out_token, TOKEN_LEFT_PAREN);
            return lexer_copy_text(out_token, "(", 1);
        case ')':
            token_init(out_token, TOKEN_RIGHT_PAREN);
            return lexer_copy_text(out_token, ")", 1);
        case ',':
            token_init(out_token, TOKEN_COMMA);
            return lexer_copy_text(out_token, ",", 1);
        case ';':
            token_init(out_token, TOKEN_SEMICOLON);
            return lexer_copy_text(out_token, ";", 1);
        case '*':
            token_init(out_token, TOKEN_STAR);
            return lexer_copy_text(out_token, "*", 1);
        case '=':
            token_init(out_token, TOKEN_EQUAL);
            return lexer_copy_text(out_token, "=", 1);
        case '>':
            if (lexer_peek(lexer) == '=') {
                lexer_advance(lexer);
                token_init(out_token, TOKEN_GREATER_EQUAL);
                return lexer_copy_text(out_token, ">=", 2);
            }

            token_init(out_token, TOKEN_GREATER);
            return lexer_copy_text(out_token, ">", 1);
        case '<':
            if (lexer_peek(lexer) == '=') {
                lexer_advance(lexer);
                token_init(out_token, TOKEN_LESS_EQUAL);
                return lexer_copy_text(out_token, "<=", 2);
            }

            token_init(out_token, TOKEN_LESS);
            return lexer_copy_text(out_token, "<", 1);
        case '!':
            if (lexer_peek(lexer) == '=') {
                lexer_advance(lexer);
                token_init(out_token, TOKEN_NOT_EQUAL);
                return lexer_copy_text(out_token, "!=", 2);
            }

            return DB_PARSE_ERROR;
        default:
            return DB_PARSE_ERROR;
    }
}
