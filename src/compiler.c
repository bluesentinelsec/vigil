#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "basl/chunk.h"
#include "basl/lexer.h"
#include "basl/token.h"
#include "basl/type.h"
#include "internal/basl_binding.h"
#include "internal/basl_compiler_internal.h"
#include "internal/basl_internal.h"

typedef basl_type_kind_t basl_parser_type_t;
typedef basl_binding_function_t basl_function_decl_t;
typedef basl_binding_function_param_t basl_function_param_t;

typedef struct basl_loop_jump {
    size_t operand_offset;
    basl_source_span_t span;
} basl_loop_jump_t;

typedef struct basl_loop_context {
    size_t loop_start;
    size_t scope_depth;
    basl_loop_jump_t *break_jumps;
    size_t break_count;
    size_t break_capacity;
} basl_loop_context_t;

typedef struct basl_program_state {
    const basl_source_registry_t *registry;
    const basl_source_file_t *source;
    const basl_token_list_t *tokens;
    basl_diagnostic_list_t *diagnostics;
    basl_error_t *error;
    basl_binding_function_table_t functions;
} basl_program_state_t;

typedef struct basl_parser_state {
    const basl_program_state_t *program;
    size_t current;
    size_t body_end;
    size_t function_index;
    basl_parser_type_t expected_return_type;
    basl_chunk_t chunk;
    basl_binding_scope_stack_t locals;
    basl_loop_context_t *loops;
    size_t loop_count;
    size_t loop_capacity;
} basl_parser_state_t;

typedef struct basl_expression_result {
    basl_parser_type_t type;
} basl_expression_result_t;

typedef struct basl_statement_result {
    int guaranteed_return;
} basl_statement_result_t;

static void basl_parser_state_free(
    basl_parser_state_t *state
) {
    size_t i;
    void *memory;

    if (state == NULL || state->program == NULL) {
        return;
    }

    for (i = 0U; i < state->loop_count; ++i) {
        memory = state->loops[i].break_jumps;
        basl_runtime_free(state->program->registry->runtime, &memory);
    }

    memory = state->loops;
    basl_runtime_free(state->program->registry->runtime, &memory);
    basl_binding_scope_stack_free(&state->locals);

    state->loops = NULL;
    state->loop_count = 0U;
    state->loop_capacity = 0U;
}

static void basl_expression_result_clear(
    basl_expression_result_t *result
) {
    if (result == NULL) {
        return;
    }

    result->type = BASL_TYPE_INVALID;
}

static void basl_expression_result_set_type(
    basl_expression_result_t *result,
    basl_parser_type_t type
) {
    if (result == NULL) {
        return;
    }

    result->type = type;
}

static void basl_statement_result_clear(
    basl_statement_result_t *result
) {
    if (result == NULL) {
        return;
    }

    result->guaranteed_return = 0;
}

static void basl_statement_result_set_guaranteed_return(
    basl_statement_result_t *result,
    int guaranteed_return
) {
    if (result == NULL) {
        return;
    }

    result->guaranteed_return = guaranteed_return;
}

static basl_status_t basl_compile_report(
    const basl_program_state_t *program,
    basl_source_span_t span,
    const char *message
) {
    basl_status_t status;
    basl_source_location_t location;

    status = basl_diagnostic_list_append_cstr(
        program->diagnostics,
        BASL_DIAGNOSTIC_ERROR,
        span,
        message,
        program->error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_error_set_literal(program->error, BASL_STATUS_SYNTAX_ERROR, message);
    if (program->error != NULL) {
        basl_source_location_clear(&location);
        location.source_id = span.source_id;
        location.offset = span.start_offset;
        if (basl_source_registry_resolve_location(program->registry, &location, NULL) == BASL_STATUS_OK) {
            program->error->location = location;
        } else {
            program->error->location.source_id = span.source_id;
            program->error->location.offset = span.start_offset;
        }
    }
    return BASL_STATUS_SYNTAX_ERROR;
}

static const basl_token_t *basl_program_token_at(
    const basl_program_state_t *program,
    size_t index
) {
    return basl_token_list_get(program->tokens, index);
}

static const char *basl_program_token_text(
    const basl_program_state_t *program,
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

    return basl_string_c_str(&program->source->text) + token->span.start_offset;
}

static basl_source_span_t basl_program_eof_span(const basl_program_state_t *program) {
    basl_source_span_t span;

    basl_source_span_clear(&span);
    if (program == NULL || program->source == NULL) {
        return span;
    }

    span.source_id = program->source->id;
    span.start_offset = basl_string_length(&program->source->text);
    span.end_offset = span.start_offset;
    return span;
}

static int basl_program_names_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
) {
    return left != NULL &&
           right != NULL &&
           left_length == right_length &&
           memcmp(left, right, left_length) == 0;
}

static basl_status_t basl_program_parse_type_name(
    const basl_program_state_t *program,
    const basl_token_t *token,
    const char *unsupported_message,
    basl_parser_type_t *out_type
) {
    const char *text;
    size_t length;
    basl_type_kind_t type_kind;

    text = basl_program_token_text(program, token, &length);
    type_kind = basl_type_kind_from_name(text, length);
    if (type_kind == BASL_TYPE_I32 || type_kind == BASL_TYPE_BOOL) {
        *out_type = type_kind;
        return BASL_STATUS_OK;
    }

    return basl_compile_report(program, token->span, unsupported_message);
}

static basl_status_t basl_program_validate_main_signature(
    basl_program_state_t *program,
    basl_function_decl_t *decl,
    const basl_token_t *type_token
) {
    if (decl->param_count != 0U) {
        return basl_compile_report(
            program,
            decl->name_span,
            "main entrypoint must not declare parameters"
        );
    }

    if (decl->return_type != BASL_TYPE_I32) {
        return basl_compile_report(
            program,
            type_token->span,
            "main entrypoint must declare return type i32"
        );
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_program_grow_functions(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    basl_status_t status;

    if (minimum_capacity <= program->functions.capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->functions.capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*program->functions.functions)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "function table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->functions.functions;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->functions.functions),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->functions.functions),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_function_decl_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->functions.functions)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->functions.functions = (basl_function_decl_t *)memory;
    program->functions.capacity = next_capacity;
    return BASL_STATUS_OK;
}

static void basl_function_decl_free(
    basl_program_state_t *program,
    basl_function_decl_t *decl
) {
    if (program == NULL || program->registry == NULL || decl == NULL) {
        return;
    }

    basl_binding_function_free(program->registry->runtime, decl);
}

static basl_status_t basl_program_fail_partial_decl(
    basl_program_state_t *program,
    basl_function_decl_t *decl,
    basl_status_t status
) {
    basl_function_decl_free(program, decl);
    return status;
}

static basl_status_t basl_program_add_param(
    basl_program_state_t *program,
    basl_function_decl_t *decl,
    basl_parser_type_t type,
    const basl_token_t *name_token
) {
    basl_status_t status;
    const char *name;
    size_t name_length;

    name = basl_program_token_text(program, name_token, &name_length);
    status = basl_binding_function_add_param(
        program->registry->runtime,
        decl,
        name,
        name_length,
        name_token->span,
        type,
        program->error
    );
    if (status == BASL_STATUS_INVALID_ARGUMENT) {
        return basl_compile_report(
            program,
            name_token->span,
            "function parameter is already declared"
        );
    }

    return status;
}

static basl_status_t basl_program_parse_declarations(
    basl_program_state_t *program
) {
    basl_status_t status;
    size_t cursor;
    const basl_token_t *token;
    const basl_token_t *name_token;
    const basl_token_t *type_token;
    const basl_token_t *param_name_token;
    basl_function_decl_t *decl;
    const char *name_text;
    size_t name_length;
    size_t body_depth;
    int found_main;

    cursor = 0U;
    found_main = 0;
    while (1) {
        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind == BASL_TOKEN_EOF) {
            break;
        }
        if (token->kind != BASL_TOKEN_FN) {
            return basl_compile_report(
                program,
                token->span,
                "expected top-level 'fn'"
            );
        }
        cursor += 1U;

        name_token = basl_program_token_at(program, cursor);
        if (name_token == NULL || name_token->kind != BASL_TOKEN_IDENTIFIER) {
            return basl_compile_report(
                program,
                token->span,
                "expected function name"
            );
        }

        name_text = basl_program_token_text(program, name_token, &name_length);
        if (
            basl_binding_function_table_find(
                &program->functions,
                name_text,
                name_length,
                NULL,
                NULL
            )
        ) {
            return basl_compile_report(
                program,
                name_token->span,
                "function is already declared"
            );
        }

        status = basl_program_grow_functions(program, program->functions.count + 1U);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        decl = &program->functions.functions[program->functions.count];
        memset(decl, 0, sizeof(*decl));
        decl->name = name_text;
        decl->name_length = name_length;
        decl->name_span = name_token->span;
        cursor += 1U;

        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_LPAREN) {
            return basl_program_fail_partial_decl(
                program,
                decl,
                basl_compile_report(
                    program,
                    name_token->span,
                    "expected '(' after function name"
                )
            );
        }
        cursor += 1U;

        token = basl_program_token_at(program, cursor);
        if (token != NULL && token->kind != BASL_TOKEN_RPAREN) {
            while (1) {
                type_token = basl_program_token_at(program, cursor);
                if (type_token == NULL || type_token->kind != BASL_TOKEN_IDENTIFIER) {
                    return basl_program_fail_partial_decl(
                        program,
                        decl,
                        basl_compile_report(
                            program,
                            decl->name_span,
                            "expected parameter type name"
                        )
                    );
                }
                status = basl_program_parse_type_name(
                    program,
                    type_token,
                    "only i32 and bool function parameter types are supported",
                    &decl->return_type
                );
                if (status != BASL_STATUS_OK) {
                    return basl_program_fail_partial_decl(program, decl, status);
                }
                cursor += 1U;

                param_name_token = basl_program_token_at(program, cursor);
                if (param_name_token == NULL ||
                    param_name_token->kind != BASL_TOKEN_IDENTIFIER) {
                    return basl_program_fail_partial_decl(
                        program,
                        decl,
                        basl_compile_report(
                            program,
                            type_token->span,
                            "expected parameter name"
                        )
                    );
                }
                status = basl_program_add_param(
                    program,
                    decl,
                    decl->return_type,
                    param_name_token
                );
                if (status != BASL_STATUS_OK) {
                    return basl_program_fail_partial_decl(program, decl, status);
                }
                cursor += 1U;

                token = basl_program_token_at(program, cursor);
                if (token != NULL && token->kind == BASL_TOKEN_COMMA) {
                    cursor += 1U;
                    continue;
                }
                break;
            }
        }

        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_RPAREN) {
            return basl_program_fail_partial_decl(
                program,
                decl,
                basl_compile_report(
                    program,
                    decl->name_span,
                    "expected ')' after parameter list"
                )
            );
        }
        cursor += 1U;

        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_ARROW) {
            return basl_program_fail_partial_decl(
                program,
                decl,
                basl_compile_report(
                    program,
                    decl->name_span,
                    "expected '->' after function signature"
                )
            );
        }
        cursor += 1U;

        type_token = basl_program_token_at(program, cursor);
        if (type_token == NULL || type_token->kind != BASL_TOKEN_IDENTIFIER) {
            return basl_program_fail_partial_decl(
                program,
                decl,
                basl_compile_report(
                    program,
                    decl->name_span,
                    "expected return type name"
                )
            );
        }
        if (basl_program_names_equal(name_text, name_length, "main", 4U)) {
            found_main = 1;
            program->functions.main_index = program->functions.count;
            program->functions.has_main = 1;
            status = basl_program_parse_type_name(
                program,
                type_token,
                "main entrypoint must declare return type i32",
                &decl->return_type
            );
            if (status != BASL_STATUS_OK) {
                return basl_program_fail_partial_decl(program, decl, status);
            }
            status = basl_program_validate_main_signature(program, decl, type_token);
            if (status != BASL_STATUS_OK) {
                return basl_program_fail_partial_decl(program, decl, status);
            }
        } else {
            status = basl_program_parse_type_name(
                program,
                type_token,
                "only i32 and bool function return types are supported",
                &decl->return_type
            );
            if (status != BASL_STATUS_OK) {
                return basl_program_fail_partial_decl(program, decl, status);
            }
        }
        cursor += 1U;

        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_LBRACE) {
            return basl_program_fail_partial_decl(
                program,
                decl,
                basl_compile_report(
                    program,
                    decl->name_span,
                    "expected '{' before function body"
                )
            );
        }
        cursor += 1U;
        decl->body_start = cursor;

        body_depth = 1U;
        while (body_depth > 0U) {
            token = basl_program_token_at(program, cursor);
            if (token == NULL || token->kind == BASL_TOKEN_EOF) {
                return basl_program_fail_partial_decl(
                    program,
                    decl,
                    basl_compile_report(
                        program,
                        basl_program_eof_span(program),
                        "expected '}' after function body"
                    )
                );
            }

            if (token->kind == BASL_TOKEN_LBRACE) {
                body_depth += 1U;
            } else if (token->kind == BASL_TOKEN_RBRACE) {
                body_depth -= 1U;
                if (body_depth == 0U) {
                    decl->body_end = cursor;
                    cursor += 1U;
                    break;
                }
            }
            cursor += 1U;
        }

        program->functions.count += 1U;
        decl = NULL;
    }

    if (!found_main) {
        return basl_compile_report(
            program,
            basl_program_eof_span(program),
            "expected top-level function 'main'"
        );
    }

    return BASL_STATUS_OK;
}

static void basl_program_free(basl_program_state_t *program) {
    if (program == NULL) {
        return;
    }

    basl_binding_function_table_free(&program->functions);
}

static const basl_token_t *basl_parser_previous(const basl_parser_state_t *state);

static basl_source_span_t basl_parser_fallback_span(
    const basl_parser_state_t *state
) {
    basl_source_span_t span;
    const basl_token_t *token;

    basl_source_span_clear(&span);
    if (state == NULL || state->program == NULL || state->program->source == NULL) {
        return span;
    }

    span.source_id = state->program->source->id;
    token = basl_parser_previous(state);
    if (token != NULL) {
        return token->span;
    }

    return span;
}

static const basl_token_t *basl_parser_peek(const basl_parser_state_t *state) {
    if (state == NULL || state->current >= state->body_end) {
        return NULL;
    }

    return basl_program_token_at(state->program, state->current);
}

static const basl_token_t *basl_parser_peek_next(const basl_parser_state_t *state) {
    if (state == NULL || state->current + 1U >= state->body_end) {
        return NULL;
    }

    return basl_program_token_at(state->program, state->current + 1U);
}

static const basl_token_t *basl_parser_previous(const basl_parser_state_t *state) {
    if (state == NULL || state->current == 0U) {
        return NULL;
    }

    return basl_program_token_at(state->program, state->current - 1U);
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
    return basl_parser_peek(state) == NULL;
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
    return basl_compile_report(state->program, span, message);
}

static basl_status_t basl_parser_require_type(
    basl_parser_state_t *state,
    basl_source_span_t span,
    basl_parser_type_t actual_type,
    basl_parser_type_t expected_type,
    const char *message
) {
    if (basl_type_is_assignable(expected_type, actual_type)) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, span, message);
}

static basl_status_t basl_parser_require_bool_type(
    basl_parser_state_t *state,
    basl_source_span_t span,
    basl_parser_type_t actual_type,
    const char *message
) {
    return basl_parser_require_type(
        state,
        span,
        actual_type,
        BASL_TYPE_BOOL,
        message
    );
}

static basl_status_t basl_parser_require_same_type(
    basl_parser_state_t *state,
    basl_source_span_t span,
    basl_parser_type_t left_type,
    basl_parser_type_t right_type,
    const char *message
) {
    if (
        basl_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_EQUAL,
            left_type,
            right_type
        )
    ) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, span, message);
}

static basl_status_t basl_parser_require_i32_operands(
    basl_parser_state_t *state,
    basl_source_span_t span,
    basl_parser_type_t left_type,
    basl_parser_type_t right_type,
    basl_binary_operator_kind_t operator_kind,
    const char *message
) {
    if (basl_type_supports_binary_operator(operator_kind, left_type, right_type)) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, span, message);
}

static basl_status_t basl_parser_require_unary_operator(
    basl_parser_state_t *state,
    basl_source_span_t span,
    basl_unary_operator_kind_t operator_kind,
    basl_parser_type_t operand_type,
    const char *message
) {
    if (basl_type_supports_unary_operator(operator_kind, operand_type)) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, span, message);
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
        return basl_parser_report(
            state,
            basl_program_eof_span(state->program),
            message
        );
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
    return basl_program_token_text(state->program, token, out_length);
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
    return basl_chunk_write_opcode(&state->chunk, opcode, span, state->program->error);
}

static basl_status_t basl_parser_emit_u32(
    basl_parser_state_t *state,
    uint32_t value,
    basl_source_span_t span
) {
    return basl_chunk_write_u32(&state->chunk, value, span, state->program->error);
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
            state->program->error,
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
            state->program->error,
            BASL_STATUS_INTERNAL,
            "jump patch target is invalid"
        );
        return BASL_STATUS_INTERNAL;
    }

    jump_distance = code_size - (operand_offset + 4U);
    if (jump_distance > UINT32_MAX) {
        basl_error_set_literal(
            state->program->error,
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
            state->program->error,
            BASL_STATUS_INTERNAL,
            "loop target is invalid"
        );
        return BASL_STATUS_INTERNAL;
    }

    distance = loop_end - loop_start;
    if (distance > UINT32_MAX) {
        basl_error_set_literal(
            state->program->error,
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

static basl_status_t basl_parser_grow_loops(
    basl_parser_state_t *state,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= state->loop_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = state->loop_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*state->loops)) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "loop context allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = state->loops;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            state->program->registry->runtime,
            next_capacity * sizeof(*state->loops),
            &memory,
            state->program->error
        );
    } else {
        status = basl_runtime_realloc(
            state->program->registry->runtime,
            &memory,
            next_capacity * sizeof(*state->loops),
            state->program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_loop_context_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*state->loops)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    state->loops = (basl_loop_context_t *)memory;
    state->loop_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_loop_context_grow_breaks(
    basl_parser_state_t *state,
    basl_loop_context_t *loop,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= loop->break_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = loop->break_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*loop->break_jumps)) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "loop break table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = loop->break_jumps;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            state->program->registry->runtime,
            next_capacity * sizeof(*loop->break_jumps),
            &memory,
            state->program->error
        );
    } else {
        status = basl_runtime_realloc(
            state->program->registry->runtime,
            &memory,
            next_capacity * sizeof(*loop->break_jumps),
            state->program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_loop_jump_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*loop->break_jumps)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop->break_jumps = (basl_loop_jump_t *)memory;
    loop->break_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_loop_context_t *basl_parser_current_loop(
    basl_parser_state_t *state
) {
    if (state == NULL || state->loop_count == 0U) {
        return NULL;
    }

    return &state->loops[state->loop_count - 1U];
}

static basl_status_t basl_parser_push_loop(
    basl_parser_state_t *state,
    size_t loop_start
) {
    basl_status_t status;
    basl_loop_context_t *loop;

    status = basl_parser_grow_loops(state, state->loop_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop = &state->loops[state->loop_count];
    memset(loop, 0, sizeof(*loop));
    loop->loop_start = loop_start;
    loop->scope_depth = basl_binding_scope_stack_depth(&state->locals);
    state->loop_count += 1U;
    return BASL_STATUS_OK;
}

static void basl_parser_pop_loop(
    basl_parser_state_t *state
) {
    basl_loop_context_t *loop;
    void *memory;

    if (state == NULL || state->loop_count == 0U) {
        return;
    }

    loop = &state->loops[state->loop_count - 1U];
    memory = loop->break_jumps;
    basl_runtime_free(state->program->registry->runtime, &memory);
    memset(loop, 0, sizeof(*loop));
    state->loop_count -= 1U;
}

static basl_status_t basl_parser_emit_scope_cleanup_to_depth(
    basl_parser_state_t *state,
    size_t target_depth,
    basl_source_span_t span
) {
    basl_status_t status;
    size_t count;
    size_t index;

    count = basl_binding_scope_stack_count_above_depth(&state->locals, target_depth);
    for (index = 0U; index < count; index += 1U) {
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static int basl_parser_local_matches_token(
    const basl_parser_state_t *state,
    const basl_token_t *left,
    const basl_binding_local_t *right
) {
    size_t left_length;
    const char *left_text;

    left_text = basl_parser_token_text(state, left, &left_length);
    return right != NULL &&
           left_text != NULL &&
           left_length == right->length &&
           memcmp(left_text, right->name, left_length) == 0;
}

static int basl_parser_find_local_symbol(
    const basl_parser_state_t *state,
    const basl_token_t *name_token,
    size_t *out_index
) {
    size_t i;

    for (i = basl_binding_scope_stack_count(&state->locals); i > 0U; --i) {
        if (
            basl_parser_local_matches_token(
                state,
                name_token,
                basl_binding_scope_stack_local_at(&state->locals, i - 1U)
            )
        ) {
            if (out_index != NULL) {
                *out_index = i - 1U;
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_parser_declare_local_symbol(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_parser_type_t type,
    size_t *out_index
) {
    basl_status_t status;
    const char *name;
    size_t name_length;

    name = basl_parser_token_text(state, name_token, &name_length);
    status = basl_binding_scope_stack_declare_local(
        &state->locals,
        name,
        name_length,
        type,
        out_index,
        state->program->error
    );
    if (status == BASL_STATUS_INVALID_ARGUMENT) {
        return basl_parser_report(
            state,
            name_token->span,
            "local variable is already declared in this scope"
        );
    }

    return status;
}

static basl_status_t basl_parser_lookup_local_symbol(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    size_t *out_index,
    basl_parser_type_t *out_type
) {
    size_t local_index;

    if (basl_parser_find_local_symbol(state, name_token, &local_index)) {
        if (out_index != NULL) {
            *out_index = local_index;
        }
        if (out_type != NULL) {
            *out_type = basl_binding_scope_stack_local_at(&state->locals, local_index)->type;
        }
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, name_token->span, "unknown local variable");
}

static int basl_program_find_function_symbol(
    const basl_program_state_t *program,
    const basl_token_t *name_token,
    size_t *out_index,
    const basl_function_decl_t **out_decl
) {
    const char *name_text;
    size_t name_length;

    name_text = basl_program_token_text(program, name_token, &name_length);
    return basl_binding_function_table_find(
        &program->functions,
        name_text,
        name_length,
        out_index,
        out_decl
    );
}

static basl_status_t basl_parser_lookup_function_symbol(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    size_t *out_index,
    const basl_function_decl_t **out_decl
) {
    if (basl_program_find_function_symbol(
            state->program,
            name_token,
            out_index,
            out_decl
        )) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, name_token->span, "unknown function");
}

static void basl_parser_begin_scope(basl_parser_state_t *state) {
    basl_binding_scope_stack_begin_scope(&state->locals);
}

static basl_status_t basl_parser_end_scope(basl_parser_state_t *state) {
    basl_status_t status;
    basl_source_span_t span;
    size_t popped_count;
    size_t index;

    if (basl_binding_scope_stack_depth(&state->locals) == 0U) {
        return BASL_STATUS_OK;
    }

    basl_binding_scope_stack_end_scope(&state->locals, &popped_count);
    for (index = 0U; index < popped_count; index += 1U) {
        span = basl_parser_fallback_span(state);
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
);
static basl_status_t basl_parser_parse_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
);

static basl_status_t basl_parser_parse_call(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    size_t function_index;
    const basl_function_decl_t *decl;
    basl_expression_result_t arg_result;
    size_t arg_count;

    function_index = 0U;
    decl = NULL;
    basl_expression_result_clear(&arg_result);

    status = basl_parser_lookup_function_symbol(state, name_token, &function_index, &decl);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after function name",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    arg_count = 0U;
    if (!basl_parser_check(state, BASL_TOKEN_RPAREN)) {
        while (1) {
            status = basl_parser_parse_expression(state, &arg_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (arg_count >= decl->param_count) {
                return basl_parser_report(
                    state,
                    name_token->span,
                    "call argument count does not match function signature"
                );
            }
            status = basl_parser_require_type(
                state,
                name_token->span,
                arg_result.type,
                decl->params[arg_count].type,
                "call argument type does not match parameter type"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            arg_count += 1U;

            if (!basl_parser_match(state, BASL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after argument list",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (arg_count != decl->param_count) {
        return basl_parser_report(
            state,
            name_token->span,
            "call argument count does not match function signature"
        );
    }
    if (function_index > UINT32_MAX || arg_count > UINT32_MAX) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "call operand overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    status = basl_parser_emit_opcode(state, BASL_OPCODE_CALL, name_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)function_index, name_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)arg_count, name_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_expression_result_set_type(out_result, decl->return_type);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_primary(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *token;
    basl_value_t value;
    size_t local_index;
    basl_parser_type_t local_type;

    local_index = 0U;
    local_type = BASL_TYPE_INVALID;
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
                state->program->error
            );
            basl_value_release(&value);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, BASL_TYPE_I32);
            return BASL_STATUS_OK;
        case BASL_TOKEN_TRUE:
            basl_parser_advance(state);
            basl_expression_result_set_type(out_result, BASL_TYPE_BOOL);
            return basl_parser_emit_opcode(state, BASL_OPCODE_TRUE, token->span);
        case BASL_TOKEN_FALSE:
            basl_parser_advance(state);
            basl_expression_result_set_type(out_result, BASL_TYPE_BOOL);
            return basl_parser_emit_opcode(state, BASL_OPCODE_FALSE, token->span);
        case BASL_TOKEN_NIL:
            basl_parser_advance(state);
            basl_expression_result_set_type(out_result, BASL_TYPE_NIL);
            return basl_parser_emit_opcode(state, BASL_OPCODE_NIL, token->span);
        case BASL_TOKEN_IDENTIFIER:
            basl_parser_advance(state);
            if (basl_parser_check(state, BASL_TOKEN_LPAREN)) {
                return basl_parser_parse_call(state, token, out_result);
            }

            status = basl_parser_lookup_local_symbol(state, token, &local_index, &local_type);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, local_type);
            status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            return basl_parser_emit_u32(state, (uint32_t)local_index, token->span);
        case BASL_TOKEN_LPAREN:
            basl_parser_advance(state);
            status = basl_parser_parse_expression(state, out_result);
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
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *operator_token;
    basl_expression_result_t operand_result;

    basl_expression_result_clear(&operand_result);

    operator_token = basl_parser_peek(state);
    if (operator_token != NULL &&
        (operator_token->kind == BASL_TOKEN_MINUS ||
         operator_token->kind == BASL_TOKEN_BANG)) {
        basl_parser_advance(state);
        status = basl_parser_parse_unary(state, &operand_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        if (operator_token->kind == BASL_TOKEN_MINUS) {
            status = basl_parser_require_unary_operator(
                state,
                operator_token->span,
                BASL_UNARY_OPERATOR_NEGATE,
                operand_result.type,
                "unary '-' requires an i32 operand"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, BASL_TYPE_I32);
            return basl_parser_emit_opcode(state, BASL_OPCODE_NEGATE, operator_token->span);
        }

        status = basl_parser_require_unary_operator(
            state,
            operator_token->span,
            BASL_UNARY_OPERATOR_LOGICAL_NOT,
            operand_result.type,
            "logical '!' requires a bool operand"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, BASL_TYPE_BOOL);
        return basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_token->span);
    }

    return basl_parser_parse_primary(state, out_result);
}

static basl_status_t basl_parser_parse_factor(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_unary(state, &left_result);
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
        status = basl_parser_parse_unary(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            operator_kind == BASL_TOKEN_STAR
                ? BASL_BINARY_OPERATOR_MULTIPLY
                : (operator_kind == BASL_TOKEN_SLASH
                       ? BASL_BINARY_OPERATOR_DIVIDE
                       : BASL_BINARY_OPERATOR_MODULO),
            "arithmetic operators require i32 operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
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
        left_result.type = BASL_TYPE_I32;
    }

    basl_expression_result_set_type(out_result, left_result.type);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_term(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_factor(state, &left_result);
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
        status = basl_parser_parse_factor(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            operator_kind == BASL_TOKEN_PLUS
                ? BASL_BINARY_OPERATOR_ADD
                : BASL_BINARY_OPERATOR_SUBTRACT,
            "arithmetic operators require i32 operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_opcode(
            state,
            operator_kind == BASL_TOKEN_PLUS ? BASL_OPCODE_ADD : BASL_OPCODE_SUBTRACT,
            operator_span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = BASL_TYPE_I32;
    }

    basl_expression_result_set_type(out_result, left_result.type);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_comparison(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_term(state, &left_result);
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
        status = basl_parser_parse_term(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            operator_kind == BASL_TOKEN_GREATER
                ? BASL_BINARY_OPERATOR_GREATER
                : (operator_kind == BASL_TOKEN_GREATER_EQUAL
                       ? BASL_BINARY_OPERATOR_GREATER_EQUAL
                       : (operator_kind == BASL_TOKEN_LESS
                              ? BASL_BINARY_OPERATOR_LESS
                              : BASL_BINARY_OPERATOR_LESS_EQUAL)),
            "comparison operators require i32 operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
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
        left_result.type = BASL_TYPE_BOOL;
    }

    basl_expression_result_set_type(out_result, left_result.type);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_equality(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_comparison(state, &left_result);
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
        status = basl_parser_parse_comparison(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_same_type(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            "equality operators require operands of the same type"
        );
        if (status != BASL_STATUS_OK) {
            return status;
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
        left_result.type = BASL_TYPE_BOOL;
    }

    basl_expression_result_set_type(out_result, left_result.type);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_logical_and(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_source_span_t operator_span;
    size_t false_jump_offset;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_equality(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (basl_parser_check(state, BASL_TOKEN_AMPERSAND_AMPERSAND)) {
        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_require_bool_type(
            state,
            operator_span,
            left_result.type,
            "logical '&&' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_jump(
            state,
            BASL_OPCODE_JUMP_IF_FALSE,
            operator_span,
            &false_jump_offset
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_parse_equality(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_bool_type(
            state,
            operator_span,
            right_result.type,
            "logical '&&' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_patch_jump(state, false_jump_offset);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = BASL_TYPE_BOOL;
    }

    basl_expression_result_set_type(out_result, left_result.type);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_logical_or(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_source_span_t operator_span;
    size_t false_jump_offset;
    size_t end_jump_offset;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_logical_and(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (basl_parser_check(state, BASL_TOKEN_PIPE_PIPE)) {
        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_require_bool_type(
            state,
            operator_span,
            left_result.type,
            "logical '||' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_jump(
            state,
            BASL_OPCODE_JUMP_IF_FALSE,
            operator_span,
            &false_jump_offset
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_jump(
            state,
            BASL_OPCODE_JUMP,
            operator_span,
            &end_jump_offset
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_patch_jump(state, false_jump_offset);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_parse_logical_and(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_bool_type(
            state,
            operator_span,
            right_result.type,
            "logical '||' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_patch_jump(state, end_jump_offset);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = BASL_TYPE_BOOL;
    }

    basl_expression_result_set_type(out_result, left_result.type);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    return basl_parser_parse_logical_or(state, out_result);
}

static basl_status_t basl_parser_parse_block_contents(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
);

static basl_status_t basl_parser_parse_block_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    basl_statement_result_t block_result;

    basl_statement_result_clear(&block_result);

    status = basl_parser_expect(state, BASL_TOKEN_LBRACE, "expected '{'", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_parser_begin_scope(state);
    status = basl_parser_parse_block_contents(state, &block_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_RBRACE, "expected '}' after block", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_end_scope(state);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_statement_result_set_guaranteed_return(
        out_result,
        block_result.guaranteed_return
    );
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_return_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *return_token;
    basl_expression_result_t return_result;

    basl_expression_result_clear(&return_result);

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RETURN,
        "expected return statement",
        &return_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &return_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_type(
        state,
        return_token->span,
        return_result.type,
        state->expected_return_type,
        state->function_index == state->program->functions.main_index
            ? "main entrypoint must return an i32 expression"
            : "return expression type does not match function return type"
    );
    if (status != BASL_STATUS_OK) {
        return status;
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

    status = basl_parser_emit_opcode(state, BASL_OPCODE_RETURN, return_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_statement_result_set_guaranteed_return(out_result, 1);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_if_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *if_token;
    basl_expression_result_t condition_result;
    size_t false_jump_offset;
    size_t end_jump_offset;
    basl_statement_result_t then_result;
    basl_statement_result_t else_result;
    int has_else_branch;

    basl_expression_result_clear(&condition_result);
    basl_statement_result_clear(&then_result);
    basl_statement_result_clear(&else_result);
    has_else_branch = 0;

    status = basl_parser_expect(state, BASL_TOKEN_IF, "expected 'if'", &if_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_LPAREN, "expected '(' after 'if'", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &condition_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_bool_type(
        state,
        if_token->span,
        condition_result.type,
        "if condition must be bool"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_RPAREN, "expected ')' after if condition", NULL);
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

    status = basl_parser_parse_statement(state, &then_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (basl_parser_match(state, BASL_TOKEN_ELSE)) {
        has_else_branch = 1;
    }

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

    if (has_else_branch) {
        status = basl_parser_parse_statement(state, &else_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    status = basl_parser_patch_jump(state, end_jump_offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    basl_statement_result_set_guaranteed_return(
        out_result,
        has_else_branch &&
            then_result.guaranteed_return &&
            else_result.guaranteed_return
    );
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_while_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *while_token;
    basl_expression_result_t condition_result;
    size_t loop_start;
    size_t exit_jump_offset;
    basl_loop_context_t *loop;
    size_t i;

    while_token = NULL;
    basl_expression_result_clear(&condition_result);

    status = basl_parser_expect(state, BASL_TOKEN_WHILE, "expected 'while'", &while_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop_start = basl_chunk_code_size(&state->chunk);
    status = basl_parser_expect(state, BASL_TOKEN_LPAREN, "expected '(' after 'while'", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &condition_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_bool_type(
        state,
        while_token->span,
        condition_result.type,
        "while condition must be bool"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_RPAREN, "expected ')' after while condition", NULL);
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

    status = basl_parser_push_loop(state, loop_start);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_statement(state, NULL);
    if (status != BASL_STATUS_OK) {
        goto cleanup_loop;
    }

    status = basl_parser_emit_loop(state, loop_start, while_token->span);
    if (status != BASL_STATUS_OK) {
        goto cleanup_loop;
    }
    status = basl_parser_patch_jump(state, exit_jump_offset);
    if (status != BASL_STATUS_OK) {
        goto cleanup_loop;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, while_token->span);
    if (status != BASL_STATUS_OK) {
        goto cleanup_loop;
    }

    loop = basl_parser_current_loop(state);
    if (loop != NULL) {
        for (i = 0U; i < loop->break_count; ++i) {
            status = basl_parser_patch_jump(
                state,
                loop->break_jumps[i].operand_offset
            );
            if (status != BASL_STATUS_OK) {
                goto cleanup_loop;
            }
        }
    }

cleanup_loop:
    basl_parser_pop_loop(state);
    if (status == BASL_STATUS_OK) {
        basl_statement_result_set_guaranteed_return(out_result, 0);
    }
    return status;
}

static basl_status_t basl_parser_parse_break_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *break_token;
    basl_loop_context_t *loop;
    size_t operand_offset;

    status = basl_parser_expect(state, BASL_TOKEN_BREAK, "expected 'break'", &break_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop = basl_parser_current_loop(state);
    if (loop == NULL) {
        return basl_parser_report(state, break_token->span, "'break' is only valid inside a loop");
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_SEMICOLON,
        "expected ';' after break",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_scope_cleanup_to_depth(
        state,
        loop->scope_depth,
        break_token->span
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_loop_context_grow_breaks(state, loop, loop->break_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP,
        break_token->span,
        &operand_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop = basl_parser_current_loop(state);
    loop->break_jumps[loop->break_count].operand_offset = operand_offset;
    loop->break_jumps[loop->break_count].span = break_token->span;
    loop->break_count += 1U;
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_continue_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *continue_token;
    basl_loop_context_t *loop;

    status = basl_parser_expect(
        state,
        BASL_TOKEN_CONTINUE,
        "expected 'continue'",
        &continue_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop = basl_parser_current_loop(state);
    if (loop == NULL) {
        return basl_parser_report(
            state,
            continue_token->span,
            "'continue' is only valid inside a loop"
        );
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_SEMICOLON,
        "expected ';' after continue",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_scope_cleanup_to_depth(
        state,
        loop->scope_depth,
        continue_token->span
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_loop(state, loop->loop_start, continue_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_assignment_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *name_token;
    size_t local_index;
    basl_parser_type_t local_type;
    basl_expression_result_t value_result;

    local_index = 0U;
    local_type = BASL_TYPE_INVALID;
    basl_expression_result_clear(&value_result);

    status = basl_parser_expect(
        state,
        BASL_TOKEN_IDENTIFIER,
        "expected local variable name",
        &name_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_lookup_local_symbol(state, name_token, &local_index, &local_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_ASSIGN, "expected '=' in assignment", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &value_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_type(
        state,
        name_token->span,
        value_result.type,
        local_type,
        "assigned expression type does not match local variable type"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_SEMICOLON, "expected ';' after assignment", NULL);
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
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, name_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_expression_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t expression_result;
    const basl_token_t *last_token;

    basl_expression_result_clear(&expression_result);

    status = basl_parser_parse_expression(state, &expression_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    last_token = basl_parser_previous(state);
    status = basl_parser_expect(state, BASL_TOKEN_SEMICOLON, "expected ';' after expression", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_opcode(
        state,
        BASL_OPCODE_POP,
        last_token == NULL ? basl_parser_fallback_span(state) : last_token->span
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    if (basl_parser_check(state, BASL_TOKEN_RETURN)) {
        return basl_parser_parse_return_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_IF)) {
        return basl_parser_parse_if_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_WHILE)) {
        return basl_parser_parse_while_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_BREAK)) {
        return basl_parser_parse_break_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_CONTINUE)) {
        return basl_parser_parse_continue_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_LBRACE)) {
        return basl_parser_parse_block_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_IDENTIFIER) &&
        basl_parser_peek_next(state) != NULL &&
        basl_parser_peek_next(state)->kind == BASL_TOKEN_ASSIGN) {
        return basl_parser_parse_assignment_statement(state, out_result);
    }

    return basl_parser_parse_expression_statement(state, out_result);
}

static basl_status_t basl_parser_parse_variable_declaration(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *type_token;
    const basl_token_t *name_token;
    basl_parser_type_t declared_type;
    basl_expression_result_t initializer_result;

    basl_expression_result_clear(&initializer_result);

    status = basl_parser_expect(state, BASL_TOKEN_IDENTIFIER, "expected local type name", &type_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_expect(state, BASL_TOKEN_IDENTIFIER, "expected local variable name", &name_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_program_parse_type_name(
        state->program,
        type_token,
        "only i32 and bool local types are supported",
        &declared_type
    );
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

    status = basl_parser_parse_expression(state, &initializer_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_type(
        state,
        name_token->span,
        initializer_result.type,
        declared_type,
        "initializer type does not match local variable type"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_SEMICOLON, "expected ';' after local declaration", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_declare_local_symbol(state, name_token, declared_type, NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_declaration(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    const basl_token_t *token;
    const basl_token_t *next_token;

    token = basl_parser_peek(state);
    next_token = basl_parser_peek_next(state);
    if (token != NULL &&
        next_token != NULL &&
        token->kind == BASL_TOKEN_IDENTIFIER &&
        next_token->kind == BASL_TOKEN_IDENTIFIER) {
        return basl_parser_parse_variable_declaration(state, out_result);
    }

    return basl_parser_parse_statement(state, out_result);
}

static basl_status_t basl_parser_parse_block_contents(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    basl_statement_result_t declaration_result;
    basl_statement_result_t block_result;

    basl_statement_result_clear(&declaration_result);
    basl_statement_result_clear(&block_result);

    while (!basl_parser_is_at_end(state) && !basl_parser_check(state, BASL_TOKEN_RBRACE)) {
        status = basl_parser_parse_declaration(state, &declaration_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (declaration_result.guaranteed_return) {
            block_result.guaranteed_return = 1;
        }
    }

    basl_statement_result_set_guaranteed_return(
        out_result,
        block_result.guaranteed_return
    );
    return BASL_STATUS_OK;
}

static basl_status_t basl_compile_seed_parameter_symbols(
    basl_parser_state_t *state,
    const basl_function_decl_t *decl
) {
    basl_status_t status;
    size_t i;

    for (i = 0U; i < decl->param_count; ++i) {
        const basl_token_t fake_name = {
            BASL_TOKEN_IDENTIFIER,
            decl->params[i].span
        };

        status = basl_parser_declare_local_symbol(
            state,
            &fake_name,
            decl->params[i].type,
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_compile_require_function_returns(
    basl_program_state_t *program,
    const basl_function_decl_t *decl,
    size_t function_index,
    int guaranteed_return
) {
    if (guaranteed_return) {
        return BASL_STATUS_OK;
    }

    return basl_compile_report(
        program,
        decl->name_span,
        function_index == program->functions.main_index
            ? "main entrypoint must return an i32 value on all paths"
            : "function must return a value on all paths"
    );
}

static basl_status_t basl_compile_function(
    basl_program_state_t *program,
    size_t function_index
) {
    basl_status_t status;
    basl_parser_state_t state;
    basl_function_decl_t *decl;
    basl_object_t *object;
    basl_statement_result_t body_result;

    decl = &program->functions.functions[function_index];
    memset(&state, 0, sizeof(state));
    state.program = program;
    state.current = decl->body_start;
    state.body_end = decl->body_end;
    state.function_index = function_index;
    state.expected_return_type = decl->return_type;
    basl_chunk_init(&state.chunk, program->registry->runtime);
    basl_binding_scope_stack_init(&state.locals, program->registry->runtime);
    basl_binding_scope_stack_begin_scope(&state.locals);
    basl_statement_result_clear(&body_result);

    status = basl_compile_seed_parameter_symbols(&state, decl);
    if (status != BASL_STATUS_OK) {
        basl_chunk_free(&state.chunk);
        basl_parser_state_free(&state);
        return status;
    }

    status = basl_parser_parse_block_contents(&state, &body_result);
    if (status != BASL_STATUS_OK) {
        basl_chunk_free(&state.chunk);
        basl_parser_state_free(&state);
        return status;
    }

    status = basl_compile_require_function_returns(
        program,
        decl,
        function_index,
        body_result.guaranteed_return
    );
    if (status != BASL_STATUS_OK) {
        basl_chunk_free(&state.chunk);
        basl_parser_state_free(&state);
        return status;
    }

    object = NULL;
    status = basl_function_object_new(
        program->registry->runtime,
        decl->name,
        decl->name_length,
        decl->param_count,
        &state.chunk,
        &object,
        program->error
    );
    basl_parser_state_free(&state);
    if (status != BASL_STATUS_OK) {
        basl_chunk_free(&state.chunk);
        return status;
    }

    decl->object = object;
    return BASL_STATUS_OK;
}

static basl_status_t basl_compile_validate_inputs(
    const basl_source_registry_t *registry,
    basl_diagnostic_list_t *diagnostics,
    basl_object_t **out_function,
    basl_compile_mode_t mode,
    basl_error_t *error
) {
    if (registry == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "source registry must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (diagnostics == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "diagnostic list must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (mode == BASL_COMPILE_MODE_BUILD_ENTRYPOINT && out_function == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "out_function must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_compile_lex_source(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error,
    const basl_source_file_t **out_source,
    basl_token_list_t *out_tokens
) {
    basl_status_t status;
    const basl_source_file_t *source;

    source = basl_source_registry_get(registry, source_id);
    if (source == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "source_id must reference a registered source file");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    basl_token_list_init(out_tokens, registry->runtime);
    status = basl_lex_source(registry, source_id, out_tokens, diagnostics, error);
    if (status != BASL_STATUS_OK) {
        basl_token_list_free(out_tokens);
        return status;
    }

    *out_source = source;
    return BASL_STATUS_OK;
}

static basl_status_t basl_compile_all_functions(
    basl_program_state_t *program
) {
    basl_status_t status;
    size_t i;

    for (i = 0U; i < program->functions.count; ++i) {
        status = basl_compile_function(program, i);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_compile_attach_entrypoint(
    basl_program_state_t *program,
    basl_object_t **out_function
) {
    basl_status_t status;
    basl_object_t **function_table;
    size_t i;
    void *memory;

    memory = NULL;
    status = basl_runtime_alloc(
        program->registry->runtime,
        program->functions.count * sizeof(*function_table),
        &memory,
        program->error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    function_table = (basl_object_t **)memory;
    for (i = 0U; i < program->functions.count; ++i) {
        function_table[i] = program->functions.functions[i].object;
    }

    status = basl_function_object_attach_siblings(
        program->functions.functions[program->functions.main_index].object,
        function_table,
        program->functions.count,
        program->functions.main_index,
        program->error
    );
    if (status != BASL_STATUS_OK) {
        memory = function_table;
        basl_runtime_free(program->registry->runtime, &memory);
        return status;
    }

    *out_function = program->functions.functions[program->functions.main_index].object;
    for (i = 0U; i < program->functions.count; ++i) {
        program->functions.functions[i].object = NULL;
    }

    return BASL_STATUS_OK;
}

basl_status_t basl_compile_source_internal(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_compile_mode_t mode,
    basl_object_t **out_function,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
) {
    basl_status_t status;
    basl_token_list_t tokens;
    basl_program_state_t program;
    const basl_source_file_t *source;

    basl_error_clear(error);
    if (out_function != NULL) {
        *out_function = NULL;
    }

    status = basl_compile_validate_inputs(
        registry,
        diagnostics,
        out_function,
        mode,
        error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_compile_lex_source(
        registry,
        source_id,
        diagnostics,
        error,
        &source,
        &tokens
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    memset(&program, 0, sizeof(program));
    program.registry = registry;
    program.source = source;
    program.tokens = &tokens;
    program.diagnostics = diagnostics;
    program.error = error;
    basl_binding_function_table_init(&program.functions, registry->runtime);

    status = basl_program_parse_declarations(&program);
    if (status != BASL_STATUS_OK) {
        basl_program_free(&program);
        basl_token_list_free(&tokens);
        return status;
    }

    status = basl_compile_all_functions(&program);
    if (status != BASL_STATUS_OK) {
        basl_program_free(&program);
        basl_token_list_free(&tokens);
        return status;
    }

    if (mode == BASL_COMPILE_MODE_BUILD_ENTRYPOINT) {
        status = basl_compile_attach_entrypoint(&program, out_function);
        if (status != BASL_STATUS_OK) {
            basl_program_free(&program);
            basl_token_list_free(&tokens);
            return status;
        }
    }

    basl_program_free(&program);
    basl_token_list_free(&tokens);
    return BASL_STATUS_OK;
}

basl_status_t basl_compile_source(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_object_t **out_function,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
) {
    return basl_compile_source_internal(
        registry,
        source_id,
        BASL_COMPILE_MODE_BUILD_ENTRYPOINT,
        out_function,
        diagnostics,
        error
    );
}
