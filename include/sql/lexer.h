#ifndef SQL_LEXER_H
#define SQL_LEXER_H

#include <stdint.h>

#include "common.h"
#include "sql/token.h"

/*
 * Lexer walks through one SQL input string and returns tokens one at a time.
 *
 * The lexer does not own input. The caller must keep the input string alive
 * while tokens are being read.
 */
typedef struct {
    const char *input;
    uint32_t position;
} Lexer;

/*
 * Initializes a lexer at the start of input.
 */
DBStatus lexer_init(Lexer *lexer, const char *input);

/*
 * Reads the next token from input.
 *
 * Whitespace is skipped. TOKEN_EOF is returned after the end of input.
 * Invalid characters or unterminated strings return DB_PARSE_ERROR.
 */
DBStatus lexer_next(Lexer *lexer, Token *out_token);

#endif
