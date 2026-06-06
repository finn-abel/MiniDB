#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "sql/lexer.h"
#include "sql/token.h"

static void assert_next_token(
    Lexer *lexer,
    TokenType type,
    const char *lexeme
) {
    Token token;

    assert(lexer_next(lexer, &token) == DB_OK);
    assert(token.type == type);
    assert(strcmp(token.lexeme, lexeme) == 0);
}

static void test_lexer_insert_statement(void) {
    Lexer lexer;
    Token token;

    assert(lexer_init(&lexer, "INSERT INTO users VALUES (1, \"Finn\", 20);") == DB_OK);

    assert_next_token(&lexer, TOKEN_INSERT, "INSERT");
    assert_next_token(&lexer, TOKEN_INTO, "INTO");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "users");
    assert_next_token(&lexer, TOKEN_VALUES, "VALUES");
    assert_next_token(&lexer, TOKEN_LEFT_PAREN, "(");

    assert(lexer_next(&lexer, &token) == DB_OK);
    assert(token.type == TOKEN_INT_LITERAL);
    assert(strcmp(token.lexeme, "1") == 0);
    assert(token.int_value == 1);

    assert_next_token(&lexer, TOKEN_COMMA, ",");
    assert_next_token(&lexer, TOKEN_STRING_LITERAL, "Finn");
    assert_next_token(&lexer, TOKEN_COMMA, ",");

    assert(lexer_next(&lexer, &token) == DB_OK);
    assert(token.type == TOKEN_INT_LITERAL);
    assert(strcmp(token.lexeme, "20") == 0);
    assert(token.int_value == 20);

    assert_next_token(&lexer, TOKEN_RIGHT_PAREN, ")");
    assert_next_token(&lexer, TOKEN_SEMICOLON, ";");
    assert_next_token(&lexer, TOKEN_EOF, "");
}

static void test_lexer_create_statement(void) {
    Lexer lexer;

    assert(lexer_init(&lexer, "CREATE TABLE users (id INT, name TEXT);") == DB_OK);

    assert_next_token(&lexer, TOKEN_CREATE, "CREATE");
    assert_next_token(&lexer, TOKEN_TABLE, "TABLE");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "users");
    assert_next_token(&lexer, TOKEN_LEFT_PAREN, "(");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "id");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "INT");
    assert_next_token(&lexer, TOKEN_COMMA, ",");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "name");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "TEXT");
    assert_next_token(&lexer, TOKEN_RIGHT_PAREN, ")");
    assert_next_token(&lexer, TOKEN_SEMICOLON, ";");
    assert_next_token(&lexer, TOKEN_EOF, "");
}

static void test_lexer_select_statement(void) {
    Lexer lexer;

    assert(lexer_init(&lexer, "select * from users where age >= 18;") == DB_OK);

    assert_next_token(&lexer, TOKEN_SELECT, "select");
    assert_next_token(&lexer, TOKEN_STAR, "*");
    assert_next_token(&lexer, TOKEN_FROM, "from");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "users");
    assert_next_token(&lexer, TOKEN_WHERE, "where");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "age");
    assert_next_token(&lexer, TOKEN_GREATER_EQUAL, ">=");
    assert_next_token(&lexer, TOKEN_INT_LITERAL, "18");
    assert_next_token(&lexer, TOKEN_SEMICOLON, ";");
    assert_next_token(&lexer, TOKEN_EOF, "");
}

static void test_lexer_delete_statement(void) {
    Lexer lexer;

    assert(lexer_init(&lexer, "DELETE FROM users WHERE id != 1;") == DB_OK);

    assert_next_token(&lexer, TOKEN_DELETE, "DELETE");
    assert_next_token(&lexer, TOKEN_FROM, "FROM");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "users");
    assert_next_token(&lexer, TOKEN_WHERE, "WHERE");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "id");
    assert_next_token(&lexer, TOKEN_NOT_EQUAL, "!=");
    assert_next_token(&lexer, TOKEN_INT_LITERAL, "1");
    assert_next_token(&lexer, TOKEN_SEMICOLON, ";");
    assert_next_token(&lexer, TOKEN_EOF, "");
}

static void test_lexer_update_statement(void) {
    Lexer lexer;

    assert(lexer_init(&lexer, "UPDATE users SET age = 21 WHERE id = 1;") == DB_OK);

    assert_next_token(&lexer, TOKEN_UPDATE, "UPDATE");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "users");
    assert_next_token(&lexer, TOKEN_SET, "SET");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "age");
    assert_next_token(&lexer, TOKEN_EQUAL, "=");
    assert_next_token(&lexer, TOKEN_INT_LITERAL, "21");
    assert_next_token(&lexer, TOKEN_WHERE, "WHERE");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "id");
    assert_next_token(&lexer, TOKEN_EQUAL, "=");
    assert_next_token(&lexer, TOKEN_INT_LITERAL, "1");
    assert_next_token(&lexer, TOKEN_SEMICOLON, ";");
    assert_next_token(&lexer, TOKEN_EOF, "");
}

static void test_lexer_comparison_symbols(void) {
    Lexer lexer;

    assert(lexer_init(&lexer, "= > < >= <= !=") == DB_OK);

    assert_next_token(&lexer, TOKEN_EQUAL, "=");
    assert_next_token(&lexer, TOKEN_GREATER, ">");
    assert_next_token(&lexer, TOKEN_LESS, "<");
    assert_next_token(&lexer, TOKEN_GREATER_EQUAL, ">=");
    assert_next_token(&lexer, TOKEN_LESS_EQUAL, "<=");
    assert_next_token(&lexer, TOKEN_NOT_EQUAL, "!=");
    assert_next_token(&lexer, TOKEN_EOF, "");
}

static void test_lexer_identifier_with_underscore(void) {
    Lexer lexer;

    assert(lexer_init(&lexer, "user_profiles age_2 _internal") == DB_OK);

    assert_next_token(&lexer, TOKEN_IDENTIFIER, "user_profiles");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "age_2");
    assert_next_token(&lexer, TOKEN_IDENTIFIER, "_internal");
    assert_next_token(&lexer, TOKEN_EOF, "");
}

static void test_lexer_empty_string_literal(void) {
    Lexer lexer;

    assert(lexer_init(&lexer, "\"\"") == DB_OK);

    assert_next_token(&lexer, TOKEN_STRING_LITERAL, "");
    assert_next_token(&lexer, TOKEN_EOF, "");
}

static void test_lexer_rejects_null_inputs(void) {
    Lexer lexer;
    Token token;

    assert(lexer_init(NULL, "SELECT") == DB_ERROR);
    assert(lexer_init(&lexer, NULL) == DB_ERROR);

    assert(lexer_init(&lexer, "SELECT") == DB_OK);

    assert(lexer_next(NULL, &token) == DB_ERROR);
    assert(lexer_next(&lexer, NULL) == DB_ERROR);
}

static void test_lexer_rejects_unterminated_string(void) {
    Lexer lexer;
    Token token;

    assert(lexer_init(&lexer, "\"Finn") == DB_OK);
    assert(lexer_next(&lexer, &token) == DB_PARSE_ERROR);
}

static void test_lexer_rejects_unknown_character(void) {
    Lexer lexer;
    Token token;

    assert(lexer_init(&lexer, "@") == DB_OK);
    assert(lexer_next(&lexer, &token) == DB_PARSE_ERROR);
}

static void test_lexer_rejects_bang_without_equal(void) {
    Lexer lexer;
    Token token;

    assert(lexer_init(&lexer, "!") == DB_OK);
    assert(lexer_next(&lexer, &token) == DB_PARSE_ERROR);
}

int main(void) {
    test_lexer_insert_statement();
    test_lexer_create_statement();
    test_lexer_select_statement();
    test_lexer_delete_statement();
    test_lexer_update_statement();
    test_lexer_comparison_symbols();
    test_lexer_identifier_with_underscore();
    test_lexer_empty_string_literal();
    test_lexer_rejects_null_inputs();
    test_lexer_rejects_unterminated_string();
    test_lexer_rejects_unknown_character();
    test_lexer_rejects_bang_without_equal();

    printf("All lexer tests passed.\n");

    return 0;
}
