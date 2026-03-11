#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "basl/chunk.h"
#include "basl/compiler.h"
#include "basl/lexer.h"
#include "basl/token.h"
#include "internal/basl_internal.h"

typedef enum basl_parser_type {
    BASL_PARSER_TYPE_INVALID = 0,
    BASL_PARSER_TYPE_I32 = 1,
    BASL_PARSER_TYPE_BOOL = 2,
    BASL_PARSER_TYPE_NIL = 3
} basl_parser_type_t;

typedef struct basl_local {
    const char *name;
    size_t length;
    size_t depth;
    basl_parser_type_t type;
} basl_local_t;

typedef struct basl_parser_state {
    const basl_source_registry_t *registry;
    const basl_source_file_t *source;
    const basl_token_list_t *tokens;
    size_t current;
    basl_diagnostic_list_t *diagnostics;
    basl_error_t *error;
    basl_chunk_t chunk;
    basl_local_t *locals;
    size_t local_count;
    size_t local_capacity;
    size_t scope_depth;
} basl_parser_state_t;

static const basl_token_t *basl_parser_previous(const basl_parser_state_t *state);

static basl_source_span_t basl_parser_fallback_span(
    const basl_parser_state_t *state
) {
    basl_source_span_t span;
    const basl_token_t *token;

    basl_source_span_clear(&span);
    if (state == NULL || state->source == NULL) {
        return span;
    }

    span.source_id = state->source->id;
    token = basl_parser_previous(state);
    if (token != NULL) {
        return token->span;
    }

    return span;
}

static const basl_token_t *basl_parser_peek(const basl_parser_state_t *state) {
    return basl_token_list_get(state->tokens, state->current);
}

static const basl_token_t *basl_parser_peek_next(const basl_parser_state_t *state) {
    return basl_token_list_get(state->tokens, state->current + 1U);
}

static const basl_token_t *basl_parser_previous(const basl_parser_state_t *state) {
    if (state->current == 0U) {
        return NULL;
    }

    return basl_token_list_get(state->tokens, state->current - 1U);
}

static int basl_parser_check(
    const basl_parser_state_t *state,
    basl_token_kind_t kind
) {
    const basl_token_t *token;

    token = basl_parser_peek(state);
    return token != NULL && token->kind == kind;
}

static int basl_parser_is_at_end(const basl_parser_state_t *state) {
    const basl_token_t *token;

    token = basl_parser_peek(state);
    return token == NULL || token->kind == BASL_TOKEN_EOF;
}

static const basl_token_t *basl_parser_advance(basl_parser_state_t *state) {
    if (!basl_parser_is_at_end(state)) {
        state->current += 1U;
    }

    return basl_parser_previous(state);
}

static int basl_parser_match(
    basl_parser_state_t *state,
    basl_token_kind_t kind
) {
    if (!basl_parser_check(state, kind)) {
        return 0;
    }

    basl_parser_advance(state);
    return 1;
}

static basl_status_t basl_parser_report(
    basl_parser_state_t *state,
    basl_source_span_t span,
    const char *message
) {
    basl_status_t status;

    status = basl_diagnostic_list_append_cstr(
        state->diagnostics,
        BASL_DIAGNOSTIC_ERROR,
        span,
        message,
        state->error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_error_set_literal(state->error, BASL_STATUS_SYNTAX_ERROR, message);
    state->error->location.source_id = span.source_id;
    return BASL_STATUS_SYNTAX_ERROR;
}

static basl_status_t basl_parser_expect(
    basl_parser_state_t *state,
    basl_token_kind_t kind,
    const char *message,
    const basl_token_t **out_token
) {
    const basl_token_t *token;

    token = basl_parser_peek(state);
    if (token == NULL) {
        basl_source_span_t span;

        basl_source_span_clear(&span);
        span.source_id = state->source->id;
        span.start_offset = basl_string_length(&state->source->text);
        span.end_offset = span.start_offset;
        return basl_parser_report(state, span, message);
    }

    if (token->kind != kind) {
        return basl_parser_report(state, token->span, message);
    }

    token = basl_parser_advance(state);
    if (out_token != NULL) {
        *out_token = token;
    }
    return BASL_STATUS_OK;
}

static const char *basl_parser_token_text(
    const basl_parser_state_t *state,
    const basl_token_t *token,
    size_t *out_length
) {
    size_t length;

    if (token == NULL) {
        if (out_length != NULL) {
            *out_length = 0U;
        }
        return NULL;
    }

    length = token->span.end_offset - token->span.start_offset;
    if (out_length != NULL) {
        *out_length = length;
    }

    return basl_string_c_str(&state->source->text) + token->span.start_offset;
}

static basl_status_t basl_parser_parse_int_literal(
    basl_parser_state_t *state,
    const basl_token_t *token,
    basl_value_t *out_value
) {
    const char *text;
    size_t length;
    char buffer[128];
    char *end;
    long long parsed;

    text = basl_parser_token_text(state, token, &length);
    if (text == NULL || length == 0U) {
        return basl_parser_report(state, token->span, "invalid integer literal");
    }
    if (length >= sizeof(buffer)) {
        return basl_parser_report(state, token->span, "integer literal is too long");
    }

    memcpy(buffer, text, length);
    buffer[length] = '\0';
    errno = 0;
    parsed = strtoll(buffer, &end, 0);
    if (errno != 0 || end == buffer || *end != '\0') {
        return basl_parser_report(state, token->span, "invalid integer literal");
    }

    basl_value_init_int(out_value, (int64_t)parsed);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_emit_opcode(
    basl_parser_state_t *state,
    basl_opcode_t opcode,
    basl_source_span_t span
) {
    return basl_chunk_write_opcode(&state->chunk, opcode, span, state->error);
}

static basl_status_t basl_parser_emit_u32(
    basl_parser_state_t *state,
    uint32_t value,
    basl_source_span_t span
) {
    return basl_chunk_write_u32(&state->chunk, value, span, state->error);
}

static basl_status_t basl_parser_emit_jump(
    basl_parser_state_t *state,
    basl_opcode_t opcode,
    basl_source_span_t span,
    size_t *out_operand_offset
) {
    basl_status_t status;

    status = basl_parser_emit_opcode(state, opcode, span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (out_operand_offset != NULL) {
        *out_operand_offset = basl_chunk_code_size(&state->chunk);
    }

    return basl_parser_emit_u32(state, 0U, span);
}

static basl_status_t basl_parser_patch_u32(
    basl_parser_state_t *state,
    size_t operand_offset,
    uint32_t value
) {
    uint8_t *code;
    size_t code_size;

    code = state->chunk.code.data;
    code_size = basl_chunk_code_size(&state->chunk);
    if (code == NULL || operand_offset + 3U >= code_size) {
        basl_error_set_literal(
            state->error,
            BASL_STATUS_INTERNAL,
            "jump patch offset is out of range"
        );
        return BASL_STATUS_INTERNAL;
    }

    code[operand_offset] = (uint8_t)(value & 0xffU);
    code[operand_offset + 1U] = (uint8_t)((value >> 8U) & 0xffU);
    code[operand_offset + 2U] = (uint8_t)((value >> 16U) & 0xffU);
    code[operand_offset + 3U] = (uint8_t)((value >> 24U) & 0xffU);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_patch_jump(
    basl_parser_state_t *state,
    size_t operand_offset
) {
    size_t code_size;
    size_t jump_distance;

    code_size = basl_chunk_code_size(&state->chunk);
    if (code_size < operand_offset + 4U) {
        basl_error_set_literal(
            state->error,
            BASL_STATUS_INTERNAL,
            "jump patch target is invalid"
        );
        return BASL_STATUS_INTERNAL;
    }

    jump_distance = code_size - (operand_offset + 4U);
    if (jump_distance > UINT32_MAX) {
        basl_error_set_literal(
            state->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "jump distance overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    return basl_parser_patch_u32(state, operand_offset, (uint32_t)jump_distance);
}

static basl_status_t basl_parser_emit_loop(
    basl_parser_state_t *state,
    size_t loop_start,
    basl_source_span_t span
) {
    basl_status_t status;
    size_t loop_end;
    size_t distance;

    loop_end = basl_chunk_code_size(&state->chunk) + 5U;
    if (loop_end < loop_start) {
        basl_error_set_literal(
            state->error,
            BASL_STATUS_INTERNAL,
            "loop target is invalid"
        );
        return BASL_STATUS_INTERNAL;
    }

    distance = loop_end - loop_start;
    if (distance > UINT32_MAX) {
        basl_error_set_literal(
            state->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "loop distance overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    status = basl_parser_emit_opcode(state, BASL_OPCODE_LOOP, span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_parser_emit_u32(state, (uint32_t)distance, span);
}

static basl_status_t basl_parser_grow_locals(
    basl_parser_state_t *state,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= state->local_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = state->local_capacity;
    next_capacity = old_capacity == 0U ? 16U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*state->locals)) {
        basl_error_set_literal(
            state->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "local table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = state->locals;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            state->chunk.runtime,
            next_capacity * sizeof(*state->locals),
            &memory,
            state->error
        );
    } else {
        status = basl_runtime_realloc(
            state->chunk.runtime,
            &memory,
            next_capacity * sizeof(*state->locals),
            state->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_local_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*state->locals)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    state->locals = (basl_local_t *)memory;
    state->local_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_resolve_type_name(
    basl_parser_state_t *state,
    const basl_token_t *token,
    basl_parser_type_t *out_type
) {
    const char *text;
    size_t length;

    text = basl_parser_token_text(state, token, &length);
    if (length == 3U && memcmp(text, "i32", 3U) == 0) {
        *out_type = BASL_PARSER_TYPE_I32;
        return BASL_STATUS_OK;
    }

    if (length == 4U && memcmp(text, "bool", 4U) == 0) {
        *out_type = BASL_PARSER_TYPE_BOOL;
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, token->span, "only i32 and bool local types are supported");
}

static int basl_parser_tokens_equal(
    const basl_parser_state_t *state,
    const basl_token_t *left,
    const basl_local_t *right
) {
    size_t left_length;
    const char *left_text;

    left_text = basl_parser_token_text(state, left, &left_length);
    return right != NULL &&
           left_text != NULL &&
           left_length == right->length &&
           memcmp(left_text, right->name, left_length) == 0;
}

static basl_status_t basl_parser_declare_local(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_parser_type_t type,
    size_t *out_index
) {
    basl_status_t status;
    size_t i;
    basl_local_t *local;

    for (i = state->local_count; i > 0U; --i) {
        local = &state->locals[i - 1U];
        if (local->depth < state->scope_depth) {
            break;
        }

        if (basl_parser_tokens_equal(state, name_token, local)) {
            return basl_parser_report(
                state,
                name_token->span,
                "local variable is already declared in this scope"
            );
        }
    }

    status = basl_parser_grow_locals(state, state->local_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    local = &state->locals[state->local_count];
    local->name = basl_parser_token_text(state, name_token, &local->length);
    local->depth = state->scope_depth;
    local->type = type;
    if (out_index != NULL) {
        *out_index = state->local_count;
    }
    state->local_count += 1U;
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_resolve_local(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    size_t *out_index,
    basl_parser_type_t *out_type
) {
    size_t i;

    for (i = state->local_count; i > 0U; --i) {
        if (basl_parser_tokens_equal(state, name_token, &state->locals[i - 1U])) {
            if (out_index != NULL) {
                *out_index = i - 1U;
            }
            if (out_type != NULL) {
                *out_type = state->locals[i - 1U].type;
            }
            return BASL_STATUS_OK;
        }
    }

    return basl_parser_report(state, name_token->span, "unknown local variable");
}

static void basl_parser_begin_scope(basl_parser_state_t *state) {
    state->scope_depth += 1U;
}

static basl_status_t basl_parser_end_scope(basl_parser_state_t *state) {
    basl_status_t status;
    basl_source_span_t span;

    if (state->scope_depth == 0U) {
        return BASL_STATUS_OK;
    }

    state->scope_depth -= 1U;
    while (
        state->local_count > 0U &&
        state->locals[state->local_count - 1U].depth > state->scope_depth
    ) {
        span = basl_parser_fallback_span(state);
        status = basl_parser_emit_opcode(
            state,
            BASL_OPCODE_POP,
            span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        state->local_count -= 1U;
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_parser_type_t *out_type
);
static basl_status_t basl_parser_parse_statement(
    basl_parser_state_t *state
);

static basl_status_t basl_parser_parse_primary(
    basl_parser_state_t *state,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    const basl_token_t *token;
    basl_value_t value;
    size_t local_index;

    token = basl_parser_peek(state);
    if (token == NULL) {
        return basl_parser_report(
            state,
            basl_parser_fallback_span(state),
            "expected expression"
        );
    }

    switch (token->kind) {
        case BASL_TOKEN_INT_LITERAL:
            basl_parser_advance(state);
            status = basl_parser_parse_int_literal(state, token, &value);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_chunk_write_constant(
                &state->chunk,
                &value,
                token->span,
                NULL,
                state->error
            );
            basl_value_release(&value);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            *out_type = BASL_PARSER_TYPE_I32;
            return BASL_STATUS_OK;
        case BASL_TOKEN_TRUE:
            basl_parser_advance(state);
            *out_type = BASL_PARSER_TYPE_BOOL;
            return basl_parser_emit_opcode(state, BASL_OPCODE_TRUE, token->span);
        case BASL_TOKEN_FALSE:
            basl_parser_advance(state);
            *out_type = BASL_PARSER_TYPE_BOOL;
            return basl_parser_emit_opcode(state, BASL_OPCODE_FALSE, token->span);
        case BASL_TOKEN_NIL:
            basl_parser_advance(state);
            *out_type = BASL_PARSER_TYPE_NIL;
            return basl_parser_emit_opcode(state, BASL_OPCODE_NIL, token->span);
        case BASL_TOKEN_IDENTIFIER:
            basl_parser_advance(state);
            status = basl_parser_resolve_local(state, token, &local_index, out_type);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            return basl_parser_emit_u32(state, (uint32_t)local_index, token->span);
        case BASL_TOKEN_LPAREN:
            basl_parser_advance(state);
            status = basl_parser_parse_expression(state, out_type);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            return basl_parser_expect(
                state,
                BASL_TOKEN_RPAREN,
                "expected ')' after expression",
                NULL
            );
        case BASL_TOKEN_STRING_LITERAL:
        case BASL_TOKEN_RAW_STRING_LITERAL:
        case BASL_TOKEN_CHAR_LITERAL:
            return basl_parser_report(state, token->span, "string expressions are not yet supported");
        case BASL_TOKEN_FLOAT_LITERAL:
            return basl_parser_report(state, token->span, "float expressions are not yet supported");
        case BASL_TOKEN_FSTRING_LITERAL:
            return basl_parser_report(state, token->span, "f-strings are not yet supported");
        default:
            return basl_parser_report(state, token->span, "expected expression");
    }
}

static basl_status_t basl_parser_parse_unary(
    basl_parser_state_t *state,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    const basl_token_t *operator_token;
    basl_parser_type_t operand_type;

    operator_token = basl_parser_peek(state);
    if (operator_token != NULL) {
        if (operator_token->kind == BASL_TOKEN_MINUS || operator_token->kind == BASL_TOKEN_BANG) {
            basl_parser_advance(state);
            status = basl_parser_parse_unary(state, &operand_type);
            if (status != BASL_STATUS_OK) {
                return status;
            }

            if (operator_token->kind == BASL_TOKEN_MINUS) {
                if (operand_type != BASL_PARSER_TYPE_I32) {
                    return basl_parser_report(
                        state,
                        operator_token->span,
                        "unary '-' requires an i32 operand"
                    );
                }
                *out_type = BASL_PARSER_TYPE_I32;
                return basl_parser_emit_opcode(state, BASL_OPCODE_NEGATE, operator_token->span);
            }

            if (operand_type != BASL_PARSER_TYPE_BOOL) {
                return basl_parser_report(
                    state,
                    operator_token->span,
                    "logical '!' requires a bool operand"
                );
            }
            *out_type = BASL_PARSER_TYPE_BOOL;
            return basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_token->span);
        }
    }

    return basl_parser_parse_primary(state, out_type);
}

static basl_status_t basl_parser_parse_factor(
    basl_parser_state_t *state,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    basl_parser_type_t left_type;
    basl_parser_type_t right_type;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    status = basl_parser_parse_unary(state, &left_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (basl_parser_check(state, BASL_TOKEN_STAR)) {
            operator_kind = BASL_TOKEN_STAR;
        } else if (basl_parser_check(state, BASL_TOKEN_SLASH)) {
            operator_kind = BASL_TOKEN_SLASH;
        } else if (basl_parser_check(state, BASL_TOKEN_PERCENT)) {
            operator_kind = BASL_TOKEN_PERCENT;
        } else {
            break;
        }

        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_unary(state, &right_type);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (left_type != BASL_PARSER_TYPE_I32 || right_type != BASL_PARSER_TYPE_I32) {
            return basl_parser_report(
                state,
                operator_span,
                "arithmetic operators require i32 operands"
            );
        }

        if (operator_kind == BASL_TOKEN_STAR) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_MULTIPLY, operator_span);
        } else if (operator_kind == BASL_TOKEN_SLASH) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_DIVIDE, operator_span);
        } else {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_MODULO, operator_span);
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_type = BASL_PARSER_TYPE_I32;
    }

    *out_type = left_type;
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_term(
    basl_parser_state_t *state,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    basl_parser_type_t left_type;
    basl_parser_type_t right_type;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    status = basl_parser_parse_factor(state, &left_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (basl_parser_check(state, BASL_TOKEN_PLUS)) {
            operator_kind = BASL_TOKEN_PLUS;
        } else if (basl_parser_check(state, BASL_TOKEN_MINUS)) {
            operator_kind = BASL_TOKEN_MINUS;
        } else {
            break;
        }

        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_factor(state, &right_type);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (left_type != BASL_PARSER_TYPE_I32 || right_type != BASL_PARSER_TYPE_I32) {
            return basl_parser_report(
                state,
                operator_span,
                "arithmetic operators require i32 operands"
            );
        }

        status = basl_parser_emit_opcode(
            state,
            operator_kind == BASL_TOKEN_PLUS ? BASL_OPCODE_ADD : BASL_OPCODE_SUBTRACT,
            operator_span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_type = BASL_PARSER_TYPE_I32;
    }

    *out_type = left_type;
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_comparison(
    basl_parser_state_t *state,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    basl_parser_type_t left_type;
    basl_parser_type_t right_type;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    status = basl_parser_parse_term(state, &left_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (basl_parser_check(state, BASL_TOKEN_GREATER)) {
            operator_kind = BASL_TOKEN_GREATER;
        } else if (basl_parser_check(state, BASL_TOKEN_GREATER_EQUAL)) {
            operator_kind = BASL_TOKEN_GREATER_EQUAL;
        } else if (basl_parser_check(state, BASL_TOKEN_LESS)) {
            operator_kind = BASL_TOKEN_LESS;
        } else if (basl_parser_check(state, BASL_TOKEN_LESS_EQUAL)) {
            operator_kind = BASL_TOKEN_LESS_EQUAL;
        } else {
            break;
        }

        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_term(state, &right_type);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (left_type != BASL_PARSER_TYPE_I32 || right_type != BASL_PARSER_TYPE_I32) {
            return basl_parser_report(
                state,
                operator_span,
                "comparison operators require i32 operands"
            );
        }

        switch (operator_kind) {
            case BASL_TOKEN_GREATER:
                status = basl_parser_emit_opcode(state, BASL_OPCODE_GREATER, operator_span);
                break;
            case BASL_TOKEN_LESS:
                status = basl_parser_emit_opcode(state, BASL_OPCODE_LESS, operator_span);
                break;
            case BASL_TOKEN_GREATER_EQUAL:
                status = basl_parser_emit_opcode(state, BASL_OPCODE_LESS, operator_span);
                if (status == BASL_STATUS_OK) {
                    status = basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_span);
                }
                break;
            case BASL_TOKEN_LESS_EQUAL:
                status = basl_parser_emit_opcode(state, BASL_OPCODE_GREATER, operator_span);
                if (status == BASL_STATUS_OK) {
                    status = basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_span);
                }
                break;
            default:
                status = BASL_STATUS_INTERNAL;
                break;
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_type = BASL_PARSER_TYPE_BOOL;
    }

    *out_type = left_type;
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_equality(
    basl_parser_state_t *state,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    basl_parser_type_t left_type;
    basl_parser_type_t right_type;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    status = basl_parser_parse_comparison(state, &left_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (basl_parser_check(state, BASL_TOKEN_EQUAL_EQUAL)) {
            operator_kind = BASL_TOKEN_EQUAL_EQUAL;
        } else if (basl_parser_check(state, BASL_TOKEN_BANG_EQUAL)) {
            operator_kind = BASL_TOKEN_BANG_EQUAL;
        } else {
            break;
        }

        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_comparison(state, &right_type);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (left_type != right_type) {
            return basl_parser_report(
                state,
                operator_span,
                "equality operators require operands of the same type"
            );
        }

        status = basl_parser_emit_opcode(state, BASL_OPCODE_EQUAL, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (operator_kind == BASL_TOKEN_BANG_EQUAL) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
        }
        left_type = BASL_PARSER_TYPE_BOOL;
    }

    *out_type = left_type;
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_parser_type_t *out_type
) {
    return basl_parser_parse_equality(state, out_type);
}

static basl_status_t basl_parser_parse_block_contents(
    basl_parser_state_t *state
);

static basl_status_t basl_parser_parse_block_statement(
    basl_parser_state_t *state
) {
    basl_status_t status;

    status = basl_parser_expect(state, BASL_TOKEN_LBRACE, "expected '{'", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_parser_begin_scope(state);
    status = basl_parser_parse_block_contents(state);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_RBRACE, "expected '}' after block", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_parser_end_scope(state);
}

static basl_status_t basl_parser_parse_return_statement(
    basl_parser_state_t *state
) {
    basl_status_t status;
    const basl_token_t *return_token;
    basl_parser_type_t return_type;

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RETURN,
        "expected return statement",
        &return_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &return_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (return_type != BASL_PARSER_TYPE_I32) {
        return basl_parser_report(
            state,
            return_token->span,
            "main entrypoint must return an i32 expression"
        );
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_SEMICOLON,
        "expected ';' after return value",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_parser_emit_opcode(state, BASL_OPCODE_RETURN, return_token->span);
}

static basl_status_t basl_parser_parse_if_statement(
    basl_parser_state_t *state
) {
    basl_status_t status;
    const basl_token_t *if_token;
    basl_parser_type_t condition_type;
    size_t false_jump_offset;
    size_t end_jump_offset;

    status = basl_parser_expect(state, BASL_TOKEN_IF, "expected 'if'", &if_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after 'if'",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &condition_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (condition_type != BASL_PARSER_TYPE_BOOL) {
        return basl_parser_report(state, if_token->span, "if condition must be bool");
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after if condition",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP_IF_FALSE,
        if_token->span,
        &false_jump_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, if_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_statement(state);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (basl_parser_match(state, BASL_TOKEN_ELSE)) {
        status = basl_parser_emit_jump(
            state,
            BASL_OPCODE_JUMP,
            if_token->span,
            &end_jump_offset
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_patch_jump(state, false_jump_offset);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, if_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_parse_statement(state);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        return basl_parser_patch_jump(state, end_jump_offset);
    }

    status = basl_parser_patch_jump(state, false_jump_offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_parser_emit_opcode(state, BASL_OPCODE_POP, if_token->span);
}

static basl_status_t basl_parser_parse_while_statement(
    basl_parser_state_t *state
) {
    basl_status_t status;
    const basl_token_t *while_token;
    basl_parser_type_t condition_type;
    size_t loop_start;
    size_t exit_jump_offset;

    status = basl_parser_expect(state, BASL_TOKEN_WHILE, "expected 'while'", &while_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop_start = basl_chunk_code_size(&state->chunk);
    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after 'while'",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &condition_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (condition_type != BASL_PARSER_TYPE_BOOL) {
        return basl_parser_report(state, while_token->span, "while condition must be bool");
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after while condition",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP_IF_FALSE,
        while_token->span,
        &exit_jump_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, while_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_statement(state);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_loop(state, loop_start, while_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_patch_jump(state, exit_jump_offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    return basl_parser_emit_opcode(state, BASL_OPCODE_POP, while_token->span);
}

static basl_status_t basl_parser_parse_assignment_statement(
    basl_parser_state_t *state
) {
    basl_status_t status;
    const basl_token_t *name_token;
    size_t local_index;
    basl_parser_type_t local_type;
    basl_parser_type_t value_type;

    status = basl_parser_expect(
        state,
        BASL_TOKEN_IDENTIFIER,
        "expected local variable name",
        &name_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_resolve_local(state, name_token, &local_index, &local_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_ASSIGN,
        "expected '=' in assignment",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &value_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (value_type != local_type) {
        return basl_parser_report(
            state,
            name_token->span,
            "assigned expression type does not match local variable type"
        );
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_SEMICOLON,
        "expected ';' after assignment",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_opcode(state, BASL_OPCODE_SET_LOCAL, name_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)local_index, name_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    return basl_parser_emit_opcode(state, BASL_OPCODE_POP, name_token->span);
}

static basl_status_t basl_parser_parse_expression_statement(
    basl_parser_state_t *state
) {
    basl_status_t status;
    basl_parser_type_t expression_type;
    const basl_token_t *last_token;

    status = basl_parser_parse_expression(state, &expression_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    last_token = basl_parser_previous(state);
    status = basl_parser_expect(
        state,
        BASL_TOKEN_SEMICOLON,
        "expected ';' after expression",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_parser_emit_opcode(
        state,
        BASL_OPCODE_POP,
        last_token == NULL ? basl_parser_fallback_span(state) : last_token->span
    );
}

static basl_status_t basl_parser_parse_statement(
    basl_parser_state_t *state
) {
    if (basl_parser_check(state, BASL_TOKEN_RETURN)) {
        return basl_parser_parse_return_statement(state);
    }
    if (basl_parser_check(state, BASL_TOKEN_IF)) {
        return basl_parser_parse_if_statement(state);
    }
    if (basl_parser_check(state, BASL_TOKEN_WHILE)) {
        return basl_parser_parse_while_statement(state);
    }
    if (basl_parser_check(state, BASL_TOKEN_LBRACE)) {
        return basl_parser_parse_block_statement(state);
    }
    if (
        basl_parser_check(state, BASL_TOKEN_IDENTIFIER) &&
        basl_parser_peek_next(state) != NULL &&
        basl_parser_peek_next(state)->kind == BASL_TOKEN_ASSIGN
    ) {
        return basl_parser_parse_assignment_statement(state);
    }

    return basl_parser_parse_expression_statement(state);
}

static basl_status_t basl_parser_parse_variable_declaration(
    basl_parser_state_t *state
) {
    basl_status_t status;
    const basl_token_t *type_token;
    const basl_token_t *name_token;
    basl_parser_type_t declared_type;
    basl_parser_type_t initializer_type;

    status = basl_parser_expect(state, BASL_TOKEN_IDENTIFIER, "expected local type name", &type_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_expect(state, BASL_TOKEN_IDENTIFIER, "expected local variable name", &name_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_resolve_type_name(state, type_token, &declared_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (!basl_parser_match(state, BASL_TOKEN_ASSIGN)) {
        return basl_parser_report(
            state,
            name_token->span,
            "variables must be initialized at declaration"
        );
    }

    status = basl_parser_parse_expression(state, &initializer_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (initializer_type != declared_type) {
        return basl_parser_report(
            state,
            name_token->span,
            "initializer type does not match local variable type"
        );
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_SEMICOLON,
        "expected ';' after local declaration",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_parser_declare_local(state, name_token, declared_type, NULL);
}

static basl_status_t basl_parser_parse_declaration(
    basl_parser_state_t *state
) {
    const basl_token_t *token;
    const basl_token_t *next_token;

    token = basl_parser_peek(state);
    next_token = basl_parser_peek_next(state);
    if (
        token != NULL &&
        next_token != NULL &&
        token->kind == BASL_TOKEN_IDENTIFIER &&
        next_token->kind == BASL_TOKEN_IDENTIFIER
    ) {
        return basl_parser_parse_variable_declaration(state);
    }

    return basl_parser_parse_statement(state);
}

static basl_status_t basl_parser_parse_block_contents(
    basl_parser_state_t *state
) {
    basl_status_t status;

    while (!basl_parser_is_at_end(state) && !basl_parser_check(state, BASL_TOKEN_RBRACE)) {
        status = basl_parser_parse_declaration(state);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_script(
    basl_parser_state_t *state,
    basl_object_t **out_function
) {
    basl_status_t status;
    const basl_token_t *name_token;
    const basl_token_t *return_type_token;
    const char *name_text;
    size_t name_length;
    const char *return_type_text;
    size_t return_type_length;

    status = basl_parser_expect(state, BASL_TOKEN_FN, "expected top-level 'fn'", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_IDENTIFIER,
        "expected function name",
        &name_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    name_text = basl_parser_token_text(state, name_token, &name_length);
    if (name_length != 4U || memcmp(name_text, "main", 4U) != 0) {
        return basl_parser_report(state, name_token->span, "expected top-level function 'main'");
    }

    status = basl_parser_expect(state, BASL_TOKEN_LPAREN, "expected '(' after function name", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_RPAREN, "expected ')' after parameter list", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_ARROW, "expected '->' after function signature", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_IDENTIFIER,
        "expected return type name",
        &return_type_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return_type_text = basl_parser_token_text(state, return_type_token, &return_type_length);
    if (return_type_length != 3U || memcmp(return_type_text, "i32", 3U) != 0) {
        return basl_parser_report(
            state,
            return_type_token->span,
            "main entrypoint must declare return type i32"
        );
    }

    status = basl_parser_expect(state, BASL_TOKEN_LBRACE, "expected '{' before function body", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_parser_begin_scope(state);
    status = basl_parser_parse_block_contents(state);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_RBRACE, "expected '}' after function body", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_end_scope(state);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_EOF, "expected end of source after top-level function", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_function_object_new(
        state->chunk.runtime,
        name_text,
        name_length,
        &state->chunk,
        out_function,
        state->error
    );
}

basl_status_t basl_compile_source(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_object_t **out_function,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
) {
    basl_status_t status;
    basl_token_list_t tokens;
    basl_parser_state_t state;
    const basl_source_file_t *source;
    void *memory;

    basl_error_clear(error);
    if (out_function != NULL) {
        *out_function = NULL;
    }

    if (registry == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "source registry must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (diagnostics == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "diagnostic list must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (out_function == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "out_function must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    source = basl_source_registry_get(registry, source_id);
    if (source == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "source_id must reference a registered source file");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    basl_token_list_init(&tokens, registry->runtime);
    status = basl_lex_source(registry, source_id, &tokens, diagnostics, error);
    if (status != BASL_STATUS_OK) {
        basl_token_list_free(&tokens);
        return status;
    }

    memset(&state, 0, sizeof(state));
    state.registry = registry;
    state.source = source;
    state.tokens = &tokens;
    state.diagnostics = diagnostics;
    state.error = error;
    basl_chunk_init(&state.chunk, registry->runtime);

    status = basl_parser_parse_script(&state, out_function);
    if (status != BASL_STATUS_OK) {
        basl_chunk_free(&state.chunk);
    }

    basl_token_list_free(&tokens);
    memory = state.locals;
    if (state.chunk.runtime != NULL) {
        basl_runtime_free(state.chunk.runtime, &memory);
    }
    return status;
}
