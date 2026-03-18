#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/token.h"

static int vigil_token_list_validate_mutable(
    const vigil_token_list_t *list,
    vigil_error_t *error
) {
    vigil_error_clear(error);

    if (list == NULL) {
        vigil_error_set_literal(
            error,
            VIGIL_STATUS_INVALID_ARGUMENT,
            "token list must not be null"
        );
        return 0;
    }

    if (list->runtime == NULL) {
        vigil_error_set_literal(
            error,
            VIGIL_STATUS_INVALID_ARGUMENT,
            "token list runtime must not be null"
        );
        return 0;
    }

    return 1;
}

static vigil_status_t vigil_token_list_grow(
    vigil_token_list_t *list,
    size_t minimum_capacity,
    vigil_error_t *error
) {
    vigil_status_t status;
    size_t new_capacity;
    void *memory;

    if (list->capacity >= minimum_capacity) {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    new_capacity = list->capacity == 0U ? 16U : list->capacity;
    while (new_capacity < minimum_capacity) {
        if (new_capacity > SIZE_MAX / 2U) {
            vigil_error_set_literal(
                error,
                VIGIL_STATUS_INVALID_ARGUMENT,
                "token list capacity would overflow"
            );
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }

        new_capacity *= 2U;
    }

    if (new_capacity > SIZE_MAX / sizeof(*list->items)) {
        vigil_error_set_literal(
            error,
            VIGIL_STATUS_INVALID_ARGUMENT,
            "token list capacity would overflow"
        );
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    memory = list->items;
    if (memory == NULL) {
        status = vigil_runtime_alloc(
            list->runtime,
            new_capacity * sizeof(*list->items),
            &memory,
            error
        );
    } else {
        status = vigil_runtime_realloc(
            list->runtime,
            &memory,
            new_capacity * sizeof(*list->items),
            error
        );
        if (status == VIGIL_STATUS_OK) {
            memset(
                (vigil_token_t *)memory + list->capacity,
                0,
                (new_capacity - list->capacity) * sizeof(*list->items)
            );
        }
    }

    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    list->items = (vigil_token_t *)memory;
    list->capacity = new_capacity;
    return VIGIL_STATUS_OK;
}

void vigil_token_list_init(
    vigil_token_list_t *list,
    vigil_runtime_t *runtime
) {
    if (list == NULL) {
        return;
    }

    memset(list, 0, sizeof(*list));
    list->runtime = runtime;
}

void vigil_token_list_clear(vigil_token_list_t *list) {
    if (list == NULL) {
        return;
    }

    list->count = 0U;
}

void vigil_token_list_free(vigil_token_list_t *list) {
    void *memory;

    if (list == NULL) {
        return;
    }

    memory = list->items;
    if (list->runtime != NULL) {
        vigil_runtime_free(list->runtime, &memory);
    }
    memset(list, 0, sizeof(*list));
}

size_t vigil_token_list_count(const vigil_token_list_t *list) {
    if (list == NULL) {
        return 0U;
    }

    return list->count;
}

const vigil_token_t *vigil_token_list_get(
    const vigil_token_list_t *list,
    size_t index
) {
    if (list == NULL || index >= list->count) {
        return NULL;
    }

    return &list->items[index];
}

vigil_status_t vigil_token_list_append(
    vigil_token_list_t *list,
    vigil_token_kind_t kind,
    vigil_source_span_t span,
    vigil_error_t *error
) {
    vigil_status_t status;

    if (!vigil_token_list_validate_mutable(list, error)) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (list->count == SIZE_MAX) {
        vigil_error_set_literal(
            error,
            VIGIL_STATUS_INVALID_ARGUMENT,
            "token list is full"
        );
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_token_list_grow(list, list->count + 1U, error);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    list->items[list->count].kind = kind;
    list->items[list->count].span = span;
    list->count += 1U;
    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}

const char *vigil_token_kind_name(vigil_token_kind_t kind) {
    switch (kind) {
        case VIGIL_TOKEN_EOF: return "eof";
        case VIGIL_TOKEN_IDENTIFIER: return "identifier";
        case VIGIL_TOKEN_INT_LITERAL: return "int_literal";
        case VIGIL_TOKEN_FLOAT_LITERAL: return "float_literal";
        case VIGIL_TOKEN_STRING_LITERAL: return "string_literal";
        case VIGIL_TOKEN_RAW_STRING_LITERAL: return "raw_string_literal";
        case VIGIL_TOKEN_FSTRING_LITERAL: return "fstring_literal";
        case VIGIL_TOKEN_CHAR_LITERAL: return "char_literal";
        case VIGIL_TOKEN_IMPORT: return "import";
        case VIGIL_TOKEN_AS: return "as";
        case VIGIL_TOKEN_PUB: return "pub";
        case VIGIL_TOKEN_FN: return "fn";
        case VIGIL_TOKEN_CLASS: return "class";
        case VIGIL_TOKEN_INTERFACE: return "interface";
        case VIGIL_TOKEN_ENUM: return "enum";
        case VIGIL_TOKEN_CONST: return "const";
        case VIGIL_TOKEN_RETURN: return "return";
        case VIGIL_TOKEN_DEFER: return "defer";
        case VIGIL_TOKEN_IF: return "if";
        case VIGIL_TOKEN_ELSE: return "else";
        case VIGIL_TOKEN_FOR: return "for";
        case VIGIL_TOKEN_WHILE: return "while";
        case VIGIL_TOKEN_SWITCH: return "switch";
        case VIGIL_TOKEN_GUARD: return "guard";
        case VIGIL_TOKEN_CASE: return "case";
        case VIGIL_TOKEN_DEFAULT: return "default";
        case VIGIL_TOKEN_BREAK: return "break";
        case VIGIL_TOKEN_CONTINUE: return "continue";
        case VIGIL_TOKEN_IN: return "in";
        case VIGIL_TOKEN_NIL: return "nil";
        case VIGIL_TOKEN_TRUE: return "true";
        case VIGIL_TOKEN_FALSE: return "false";
        case VIGIL_TOKEN_LPAREN: return "left_paren";
        case VIGIL_TOKEN_RPAREN: return "right_paren";
        case VIGIL_TOKEN_LBRACE: return "left_brace";
        case VIGIL_TOKEN_RBRACE: return "right_brace";
        case VIGIL_TOKEN_LBRACKET: return "left_bracket";
        case VIGIL_TOKEN_RBRACKET: return "right_bracket";
        case VIGIL_TOKEN_COMMA: return "comma";
        case VIGIL_TOKEN_DOT: return "dot";
        case VIGIL_TOKEN_SEMICOLON: return "semicolon";
        case VIGIL_TOKEN_COLON: return "colon";
        case VIGIL_TOKEN_QUESTION: return "question";
        case VIGIL_TOKEN_ARROW: return "arrow";
        case VIGIL_TOKEN_ASSIGN: return "assign";
        case VIGIL_TOKEN_PLUS: return "plus";
        case VIGIL_TOKEN_MINUS: return "minus";
        case VIGIL_TOKEN_STAR: return "star";
        case VIGIL_TOKEN_SLASH: return "slash";
        case VIGIL_TOKEN_PERCENT: return "percent";
        case VIGIL_TOKEN_PLUS_PLUS: return "plus_plus";
        case VIGIL_TOKEN_MINUS_MINUS: return "minus_minus";
        case VIGIL_TOKEN_PLUS_ASSIGN: return "plus_assign";
        case VIGIL_TOKEN_MINUS_ASSIGN: return "minus_assign";
        case VIGIL_TOKEN_STAR_ASSIGN: return "star_assign";
        case VIGIL_TOKEN_SLASH_ASSIGN: return "slash_assign";
        case VIGIL_TOKEN_PERCENT_ASSIGN: return "percent_assign";
        case VIGIL_TOKEN_EQUAL_EQUAL: return "equal_equal";
        case VIGIL_TOKEN_BANG: return "bang";
        case VIGIL_TOKEN_BANG_EQUAL: return "bang_equal";
        case VIGIL_TOKEN_LESS: return "less";
        case VIGIL_TOKEN_LESS_EQUAL: return "less_equal";
        case VIGIL_TOKEN_GREATER: return "greater";
        case VIGIL_TOKEN_GREATER_EQUAL: return "greater_equal";
        case VIGIL_TOKEN_AMPERSAND: return "ampersand";
        case VIGIL_TOKEN_AMPERSAND_AMPERSAND: return "ampersand_ampersand";
        case VIGIL_TOKEN_PIPE: return "pipe";
        case VIGIL_TOKEN_PIPE_PIPE: return "pipe_pipe";
        case VIGIL_TOKEN_CARET: return "caret";
        case VIGIL_TOKEN_TILDE: return "tilde";
        case VIGIL_TOKEN_SHIFT_LEFT: return "shift_left";
        case VIGIL_TOKEN_SHIFT_RIGHT: return "shift_right";
        default: return "unknown";
    }
}
