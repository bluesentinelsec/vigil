#ifndef BASL_TOKEN_H
#define BASL_TOKEN_H

#include <stddef.h>

#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/source.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum basl_token_kind {
    BASL_TOKEN_EOF = 0,
    BASL_TOKEN_IDENTIFIER = 1,
    BASL_TOKEN_INT_LITERAL = 2,
    BASL_TOKEN_FLOAT_LITERAL = 3,
    BASL_TOKEN_STRING_LITERAL = 4,
    BASL_TOKEN_RAW_STRING_LITERAL = 5,
    BASL_TOKEN_FSTRING_LITERAL = 6,
    BASL_TOKEN_CHAR_LITERAL = 7,
    BASL_TOKEN_IMPORT = 8,
    BASL_TOKEN_AS = 9,
    BASL_TOKEN_PUB = 10,
    BASL_TOKEN_FN = 11,
    BASL_TOKEN_CLASS = 12,
    BASL_TOKEN_INTERFACE = 13,
    BASL_TOKEN_ENUM = 14,
    BASL_TOKEN_CONST = 15,
    BASL_TOKEN_RETURN = 16,
    BASL_TOKEN_IF = 17,
    BASL_TOKEN_ELSE = 18,
    BASL_TOKEN_FOR = 19,
    BASL_TOKEN_WHILE = 20,
    BASL_TOKEN_BREAK = 21,
    BASL_TOKEN_CONTINUE = 22,
    BASL_TOKEN_NIL = 23,
    BASL_TOKEN_TRUE = 24,
    BASL_TOKEN_FALSE = 25,
    BASL_TOKEN_LPAREN = 26,
    BASL_TOKEN_RPAREN = 27,
    BASL_TOKEN_LBRACE = 28,
    BASL_TOKEN_RBRACE = 29,
    BASL_TOKEN_LBRACKET = 30,
    BASL_TOKEN_RBRACKET = 31,
    BASL_TOKEN_COMMA = 32,
    BASL_TOKEN_DOT = 33,
    BASL_TOKEN_SEMICOLON = 34,
    BASL_TOKEN_COLON = 35,
    BASL_TOKEN_QUESTION = 36,
    BASL_TOKEN_ARROW = 37,
    BASL_TOKEN_ASSIGN = 38,
    BASL_TOKEN_PLUS = 39,
    BASL_TOKEN_MINUS = 40,
    BASL_TOKEN_STAR = 41,
    BASL_TOKEN_SLASH = 42,
    BASL_TOKEN_PERCENT = 43,
    BASL_TOKEN_PLUS_PLUS = 44,
    BASL_TOKEN_MINUS_MINUS = 45,
    BASL_TOKEN_PLUS_ASSIGN = 46,
    BASL_TOKEN_MINUS_ASSIGN = 47,
    BASL_TOKEN_STAR_ASSIGN = 48,
    BASL_TOKEN_SLASH_ASSIGN = 49,
    BASL_TOKEN_PERCENT_ASSIGN = 50,
    BASL_TOKEN_EQUAL_EQUAL = 51,
    BASL_TOKEN_BANG = 52,
    BASL_TOKEN_BANG_EQUAL = 53,
    BASL_TOKEN_LESS = 54,
    BASL_TOKEN_LESS_EQUAL = 55,
    BASL_TOKEN_GREATER = 56,
    BASL_TOKEN_GREATER_EQUAL = 57,
    BASL_TOKEN_AMPERSAND = 58,
    BASL_TOKEN_AMPERSAND_AMPERSAND = 59,
    BASL_TOKEN_PIPE = 60,
    BASL_TOKEN_PIPE_PIPE = 61,
    BASL_TOKEN_CARET = 62,
    BASL_TOKEN_TILDE = 63,
    BASL_TOKEN_SHIFT_LEFT = 64,
    BASL_TOKEN_SHIFT_RIGHT = 65
} basl_token_kind_t;

typedef struct basl_token {
    basl_token_kind_t kind;
    basl_source_span_t span;
} basl_token_t;

typedef struct basl_token_list {
    basl_runtime_t *runtime;
    basl_token_t *items;
    size_t count;
    size_t capacity;
} basl_token_list_t;

BASL_API void basl_token_list_init(
    basl_token_list_t *list,
    basl_runtime_t *runtime
);
BASL_API void basl_token_list_clear(basl_token_list_t *list);
BASL_API void basl_token_list_free(basl_token_list_t *list);
BASL_API size_t basl_token_list_count(const basl_token_list_t *list);
/*
 * The returned pointer is invalidated by any later token-list mutation that
 * may reallocate the backing storage.
 */
BASL_API const basl_token_t *basl_token_list_get(
    const basl_token_list_t *list,
    size_t index
);
BASL_API const char *basl_token_kind_name(basl_token_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif
