#include <string.h>

#include "internal/basl_internal.h"
#include "basl/token.h"

static int basl_token_list_validate_mutable(
    const basl_token_list_t *list,
    basl_error_t *error
) {
    basl_error_clear(error);

    if (list == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "token list must not be null"
        );
        return 0;
    }

    if (list->runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "token list runtime must not be null"
        );
        return 0;
    }

    return 1;
}

static basl_status_t basl_token_list_grow(
    basl_token_list_t *list,
    size_t minimum_capacity,
    basl_error_t *error
) {
    basl_status_t status;
    size_t new_capacity;
    void *memory;

    if (list->capacity >= minimum_capacity) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    new_capacity = list->capacity == 0U ? 16U : list->capacity * 2U;
    while (new_capacity < minimum_capacity) {
        if (new_capacity > SIZE_MAX / 2U) {
            basl_error_set_literal(
                error,
                BASL_STATUS_INVALID_ARGUMENT,
                "token list capacity would overflow"
            );
            return BASL_STATUS_INVALID_ARGUMENT;
        }

        new_capacity *= 2U;
    }

    if (new_capacity > SIZE_MAX / sizeof(*list->items)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "token list capacity would overflow"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    memory = list->items;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            list->runtime,
            new_capacity * sizeof(*list->items),
            &memory,
            error
        );
    } else {
        status = basl_runtime_realloc(
            list->runtime,
            &memory,
            new_capacity * sizeof(*list->items),
            error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_token_t *)memory + list->capacity,
                0,
                (new_capacity - list->capacity) * sizeof(*list->items)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    list->items = (basl_token_t *)memory;
    list->capacity = new_capacity;
    return BASL_STATUS_OK;
}

void basl_token_list_init(
    basl_token_list_t *list,
    basl_runtime_t *runtime
) {
    if (list == NULL) {
        return;
    }

    memset(list, 0, sizeof(*list));
    list->runtime = runtime;
}

void basl_token_list_clear(basl_token_list_t *list) {
    if (list == NULL) {
        return;
    }

    list->count = 0U;
}

void basl_token_list_free(basl_token_list_t *list) {
    void *memory;

    if (list == NULL) {
        return;
    }

    memory = list->items;
    if (list->runtime != NULL) {
        basl_runtime_free(list->runtime, &memory);
    }
    memset(list, 0, sizeof(*list));
}

size_t basl_token_list_count(const basl_token_list_t *list) {
    if (list == NULL) {
        return 0U;
    }

    return list->count;
}

const basl_token_t *basl_token_list_get(
    const basl_token_list_t *list,
    size_t index
) {
    if (list == NULL || index >= list->count) {
        return NULL;
    }

    return &list->items[index];
}

basl_status_t basl_token_list_append(
    basl_token_list_t *list,
    basl_token_kind_t kind,
    basl_source_span_t span,
    basl_error_t *error
) {
    basl_status_t status;

    if (!basl_token_list_validate_mutable(list, error)) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (list->count == SIZE_MAX) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "token list is full"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_token_list_grow(list, list->count + 1U, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    list->items[list->count].kind = kind;
    list->items[list->count].span = span;
    list->count += 1U;
    basl_error_clear(error);
    return BASL_STATUS_OK;
}

const char *basl_token_kind_name(basl_token_kind_t kind) {
    switch (kind) {
        case BASL_TOKEN_EOF: return "eof";
        case BASL_TOKEN_IDENTIFIER: return "identifier";
        case BASL_TOKEN_INT_LITERAL: return "int_literal";
        case BASL_TOKEN_FLOAT_LITERAL: return "float_literal";
        case BASL_TOKEN_STRING_LITERAL: return "string_literal";
        case BASL_TOKEN_RAW_STRING_LITERAL: return "raw_string_literal";
        case BASL_TOKEN_FSTRING_LITERAL: return "fstring_literal";
        case BASL_TOKEN_CHAR_LITERAL: return "char_literal";
        case BASL_TOKEN_IMPORT: return "import";
        case BASL_TOKEN_AS: return "as";
        case BASL_TOKEN_PUB: return "pub";
        case BASL_TOKEN_FN: return "fn";
        case BASL_TOKEN_CLASS: return "class";
        case BASL_TOKEN_INTERFACE: return "interface";
        case BASL_TOKEN_ENUM: return "enum";
        case BASL_TOKEN_CONST: return "const";
        case BASL_TOKEN_RETURN: return "return";
        case BASL_TOKEN_IF: return "if";
        case BASL_TOKEN_ELSE: return "else";
        case BASL_TOKEN_FOR: return "for";
        case BASL_TOKEN_WHILE: return "while";
        case BASL_TOKEN_BREAK: return "break";
        case BASL_TOKEN_CONTINUE: return "continue";
        case BASL_TOKEN_TRUE: return "true";
        case BASL_TOKEN_FALSE: return "false";
        case BASL_TOKEN_LPAREN: return "left_paren";
        case BASL_TOKEN_RPAREN: return "right_paren";
        case BASL_TOKEN_LBRACE: return "left_brace";
        case BASL_TOKEN_RBRACE: return "right_brace";
        case BASL_TOKEN_LBRACKET: return "left_bracket";
        case BASL_TOKEN_RBRACKET: return "right_bracket";
        case BASL_TOKEN_COMMA: return "comma";
        case BASL_TOKEN_DOT: return "dot";
        case BASL_TOKEN_SEMICOLON: return "semicolon";
        case BASL_TOKEN_COLON: return "colon";
        case BASL_TOKEN_QUESTION: return "question";
        case BASL_TOKEN_ARROW: return "arrow";
        case BASL_TOKEN_ASSIGN: return "assign";
        case BASL_TOKEN_PLUS: return "plus";
        case BASL_TOKEN_MINUS: return "minus";
        case BASL_TOKEN_STAR: return "star";
        case BASL_TOKEN_SLASH: return "slash";
        case BASL_TOKEN_PERCENT: return "percent";
        case BASL_TOKEN_PLUS_PLUS: return "plus_plus";
        case BASL_TOKEN_MINUS_MINUS: return "minus_minus";
        case BASL_TOKEN_PLUS_ASSIGN: return "plus_assign";
        case BASL_TOKEN_MINUS_ASSIGN: return "minus_assign";
        case BASL_TOKEN_STAR_ASSIGN: return "star_assign";
        case BASL_TOKEN_SLASH_ASSIGN: return "slash_assign";
        case BASL_TOKEN_PERCENT_ASSIGN: return "percent_assign";
        case BASL_TOKEN_EQUAL_EQUAL: return "equal_equal";
        case BASL_TOKEN_BANG: return "bang";
        case BASL_TOKEN_BANG_EQUAL: return "bang_equal";
        case BASL_TOKEN_LESS: return "less";
        case BASL_TOKEN_LESS_EQUAL: return "less_equal";
        case BASL_TOKEN_GREATER: return "greater";
        case BASL_TOKEN_GREATER_EQUAL: return "greater_equal";
        case BASL_TOKEN_AMPERSAND: return "ampersand";
        case BASL_TOKEN_AMPERSAND_AMPERSAND: return "ampersand_ampersand";
        case BASL_TOKEN_PIPE: return "pipe";
        case BASL_TOKEN_PIPE_PIPE: return "pipe_pipe";
        case BASL_TOKEN_CARET: return "caret";
        case BASL_TOKEN_TILDE: return "tilde";
        case BASL_TOKEN_SHIFT_LEFT: return "shift_left";
        case BASL_TOKEN_SHIFT_RIGHT: return "shift_right";
        default: return "unknown";
    }
}
