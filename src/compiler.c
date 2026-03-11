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

typedef struct basl_function_param {
    const char *name;
    size_t length;
    basl_parser_type_t type;
    basl_source_span_t span;
} basl_function_param_t;

typedef struct basl_function_decl {
    const char *name;
    size_t name_length;
    basl_source_span_t name_span;
    basl_parser_type_t return_type;
    basl_function_param_t *params;
    size_t param_count;
    size_t param_capacity;
    size_t body_start;
    size_t body_end;
    basl_object_t *object;
} basl_function_decl_t;

typedef struct basl_program_state {
    const basl_source_registry_t *registry;
    const basl_source_file_t *source;
    const basl_token_list_t *tokens;
    basl_diagnostic_list_t *diagnostics;
    basl_error_t *error;
    basl_function_decl_t *functions;
    size_t function_count;
    size_t function_capacity;
    size_t main_index;
} basl_program_state_t;

typedef struct basl_parser_state {
    const basl_program_state_t *program;
    size_t current;
    size_t body_end;
    size_t function_index;
    size_t scope_depth;
    basl_parser_type_t expected_return_type;
    basl_chunk_t chunk;
    basl_local_t *locals;
    size_t local_count;
    size_t local_capacity;
} basl_parser_state_t;

static basl_status_t basl_compile_report(
    const basl_program_state_t *program,
    basl_source_span_t span,
    const char *message
) {
    basl_status_t status;

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
        program->error->location.source_id = span.source_id;
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

static basl_status_t basl_program_resolve_type_name(
    const basl_program_state_t *program,
    const basl_token_t *token,
    const char *unsupported_message,
    basl_parser_type_t *out_type
) {
    const char *text;
    size_t length;

    text = basl_program_token_text(program, token, &length);
    if (length == 3U && memcmp(text, "i32", 3U) == 0) {
        *out_type = BASL_PARSER_TYPE_I32;
        return BASL_STATUS_OK;
    }
    if (length == 4U && memcmp(text, "bool", 4U) == 0) {
        *out_type = BASL_PARSER_TYPE_BOOL;
        return BASL_STATUS_OK;
    }

    return basl_compile_report(program, token->span, unsupported_message);
}

static basl_status_t basl_program_grow_functions(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    basl_status_t status;

    if (minimum_capacity <= program->function_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->function_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*program->functions)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "function table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->functions;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->functions),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->functions),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_function_decl_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->functions)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->functions = (basl_function_decl_t *)memory;
    program->function_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_function_decl_grow_params(
    basl_program_state_t *program,
    basl_function_decl_t *decl,
    size_t minimum_capacity
) {
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    basl_status_t status;

    if (minimum_capacity <= decl->param_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = decl->param_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*decl->params)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "parameter table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->params;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*decl->params),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*decl->params),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_function_param_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*decl->params)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->params = (basl_function_param_t *)memory;
    decl->param_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_add_param(
    basl_program_state_t *program,
    basl_function_decl_t *decl,
    basl_parser_type_t type,
    const basl_token_t *name_token
) {
    basl_status_t status;
    basl_function_param_t *param;
    const char *name;
    size_t name_length;
    size_t i;

    name = basl_program_token_text(program, name_token, &name_length);
    for (i = 0U; i < decl->param_count; ++i) {
        if (basl_program_names_equal(
                decl->params[i].name,
                decl->params[i].length,
                name,
                name_length
            )) {
            return basl_compile_report(
                program,
                name_token->span,
                "function parameter is already declared"
            );
        }
    }

    status = basl_function_decl_grow_params(program, decl, decl->param_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    param = &decl->params[decl->param_count];
    param->name = name;
    param->length = name_length;
    param->type = type;
    param->span = name_token->span;
    decl->param_count += 1U;
    return BASL_STATUS_OK;
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
    size_t i;
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
        for (i = 0U; i < program->function_count; ++i) {
            if (basl_program_names_equal(
                    program->functions[i].name,
                    program->functions[i].name_length,
                    name_text,
                    name_length
                )) {
                return basl_compile_report(
                    program,
                    name_token->span,
                    "function is already declared"
                );
            }
        }

        status = basl_program_grow_functions(program, program->function_count + 1U);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        decl = &program->functions[program->function_count];
        memset(decl, 0, sizeof(*decl));
        decl->name = name_text;
        decl->name_length = name_length;
        decl->name_span = name_token->span;
        cursor += 1U;

        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_LPAREN) {
            return basl_compile_report(
                program,
                name_token->span,
                "expected '(' after function name"
            );
        }
        cursor += 1U;

        token = basl_program_token_at(program, cursor);
        if (token != NULL && token->kind != BASL_TOKEN_RPAREN) {
            while (1) {
                type_token = basl_program_token_at(program, cursor);
                if (type_token == NULL || type_token->kind != BASL_TOKEN_IDENTIFIER) {
                    return basl_compile_report(
                        program,
                        decl->name_span,
                        "expected parameter type name"
                    );
                }
                status = basl_program_resolve_type_name(
                    program,
                    type_token,
                    "only i32 and bool function parameter types are supported",
                    &decl->return_type
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                cursor += 1U;

                param_name_token = basl_program_token_at(program, cursor);
                if (param_name_token == NULL ||
                    param_name_token->kind != BASL_TOKEN_IDENTIFIER) {
                    return basl_compile_report(
                        program,
                        type_token->span,
                        "expected parameter name"
                    );
                }
                status = basl_program_add_param(
                    program,
                    decl,
                    decl->return_type,
                    param_name_token
                );
                if (status != BASL_STATUS_OK) {
                    return status;
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
            return basl_compile_report(
                program,
                decl->name_span,
                "expected ')' after parameter list"
            );
        }
        cursor += 1U;

        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_ARROW) {
            return basl_compile_report(
                program,
                decl->name_span,
                "expected '->' after function signature"
            );
        }
        cursor += 1U;

        type_token = basl_program_token_at(program, cursor);
        if (type_token == NULL || type_token->kind != BASL_TOKEN_IDENTIFIER) {
            return basl_compile_report(
                program,
                decl->name_span,
                "expected return type name"
            );
        }
        if (basl_program_names_equal(name_text, name_length, "main", 4U)) {
            found_main = 1;
            program->main_index = program->function_count;
            status = basl_program_resolve_type_name(
                program,
                type_token,
                "main entrypoint must declare return type i32",
                &decl->return_type
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (decl->param_count != 0U) {
                return basl_compile_report(
                    program,
                    decl->name_span,
                    "main entrypoint must not declare parameters"
                );
            }
            if (decl->return_type != BASL_PARSER_TYPE_I32) {
                return basl_compile_report(
                    program,
                    type_token->span,
                    "main entrypoint must declare return type i32"
                );
            }
        } else {
            status = basl_program_resolve_type_name(
                program,
                type_token,
                "only i32 and bool function return types are supported",
                &decl->return_type
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
        }
        cursor += 1U;

        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_LBRACE) {
            return basl_compile_report(
                program,
                decl->name_span,
                "expected '{' before function body"
            );
        }
        cursor += 1U;
        decl->body_start = cursor;

        body_depth = 1U;
        while (body_depth > 0U) {
            token = basl_program_token_at(program, cursor);
            if (token == NULL || token->kind == BASL_TOKEN_EOF) {
                return basl_compile_report(
                    program,
                    basl_program_eof_span(program),
                    "expected '}' after function body"
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

        program->function_count += 1U;
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
    size_t i;
    void *memory;

    if (program == NULL || program->registry == NULL) {
        return;
    }

    for (i = 0U; i < program->function_count; ++i) {
        if (program->functions[i].object != NULL) {
            basl_object_release(&program->functions[i].object);
        }
        memory = program->functions[i].params;
        basl_runtime_free(program->registry->runtime, &memory);
    }

    memory = program->functions;
    basl_runtime_free(program->registry->runtime, &memory);
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
            state->program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "local table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = state->locals;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            state->program->registry->runtime,
            next_capacity * sizeof(*state->locals),
            &memory,
            state->program->error
        );
    } else {
        status = basl_runtime_realloc(
            state->program->registry->runtime,
            &memory,
            next_capacity * sizeof(*state->locals),
            state->program->error
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

static basl_status_t basl_parser_resolve_function(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    size_t *out_index,
    const basl_function_decl_t **out_decl
) {
    const char *name_text;
    size_t name_length;
    size_t i;

    name_text = basl_parser_token_text(state, name_token, &name_length);
    for (i = 0U; i < state->program->function_count; ++i) {
        if (basl_program_names_equal(
                state->program->functions[i].name,
                state->program->functions[i].name_length,
                name_text,
                name_length
            )) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_decl != NULL) {
                *out_decl = &state->program->functions[i];
            }
            return BASL_STATUS_OK;
        }
    }

    return basl_parser_report(state, name_token->span, "unknown function");
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
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, span);
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

static basl_status_t basl_parser_parse_call(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    size_t function_index;
    const basl_function_decl_t *decl;
    basl_parser_type_t arg_type;
    size_t arg_count;

    status = basl_parser_resolve_function(state, name_token, &function_index, &decl);
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
            status = basl_parser_parse_expression(state, &arg_type);
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
            if (arg_type != decl->params[arg_count].type) {
                return basl_parser_report(
                    state,
                    name_token->span,
                    "call argument type does not match parameter type"
                );
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

    *out_type = decl->return_type;
    return BASL_STATUS_OK;
}

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
                state->program->error
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
            if (basl_parser_check(state, BASL_TOKEN_LPAREN)) {
                return basl_parser_parse_call(state, token, out_type);
            }

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
    if (operator_token != NULL &&
        (operator_token->kind == BASL_TOKEN_MINUS ||
         operator_token->kind == BASL_TOKEN_BANG)) {
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
    if (return_type != state->expected_return_type) {
        return basl_parser_report(
            state,
            return_token->span,
            state->function_index == state->program->main_index
                ? "main entrypoint must return an i32 expression"
                : "return expression type does not match function return type"
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

    status = basl_parser_expect(state, BASL_TOKEN_LPAREN, "expected '(' after 'if'", NULL);
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
    status = basl_parser_expect(state, BASL_TOKEN_LPAREN, "expected '(' after 'while'", NULL);
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

    status = basl_parser_expect(state, BASL_TOKEN_ASSIGN, "expected '=' in assignment", NULL);
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
    status = basl_parser_expect(state, BASL_TOKEN_SEMICOLON, "expected ';' after expression", NULL);
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
    if (basl_parser_check(state, BASL_TOKEN_IDENTIFIER) &&
        basl_parser_peek_next(state) != NULL &&
        basl_parser_peek_next(state)->kind == BASL_TOKEN_ASSIGN) {
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

    status = basl_program_resolve_type_name(
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

    status = basl_parser_expect(state, BASL_TOKEN_SEMICOLON, "expected ';' after local declaration", NULL);
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
    if (token != NULL &&
        next_token != NULL &&
        token->kind == BASL_TOKEN_IDENTIFIER &&
        next_token->kind == BASL_TOKEN_IDENTIFIER) {
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

static basl_status_t basl_compile_function(
    basl_program_state_t *program,
    size_t function_index
) {
    basl_status_t status;
    basl_parser_state_t state;
    basl_function_decl_t *decl;
    basl_object_t *object;
    void *memory;
    size_t i;

    decl = &program->functions[function_index];
    memset(&state, 0, sizeof(state));
    state.program = program;
    state.current = decl->body_start;
    state.body_end = decl->body_end;
    state.function_index = function_index;
    state.scope_depth = 1U;
    state.expected_return_type = decl->return_type;
    basl_chunk_init(&state.chunk, program->registry->runtime);

    for (i = 0U; i < decl->param_count; ++i) {
        const basl_token_t fake_name = {
            BASL_TOKEN_IDENTIFIER,
            decl->params[i].span
        };

        status = basl_parser_declare_local(
            &state,
            &fake_name,
            decl->params[i].type,
            NULL
        );
        if (status != BASL_STATUS_OK) {
            basl_chunk_free(&state.chunk);
            memory = state.locals;
            basl_runtime_free(program->registry->runtime, &memory);
            return status;
        }
    }

    status = basl_parser_parse_block_contents(&state);
    if (status != BASL_STATUS_OK) {
        basl_chunk_free(&state.chunk);
        memory = state.locals;
        basl_runtime_free(program->registry->runtime, &memory);
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
    memory = state.locals;
    basl_runtime_free(program->registry->runtime, &memory);
    if (status != BASL_STATUS_OK) {
        basl_chunk_free(&state.chunk);
        return status;
    }

    decl->object = object;
    return BASL_STATUS_OK;
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
    basl_program_state_t program;
    const basl_source_file_t *source;
    size_t i;
    basl_object_t **function_table;
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

    memset(&program, 0, sizeof(program));
    program.registry = registry;
    program.source = source;
    program.tokens = &tokens;
    program.diagnostics = diagnostics;
    program.error = error;

    status = basl_program_parse_declarations(&program);
    if (status != BASL_STATUS_OK) {
        basl_program_free(&program);
        basl_token_list_free(&tokens);
        return status;
    }

    for (i = 0U; i < program.function_count; ++i) {
        status = basl_compile_function(&program, i);
        if (status != BASL_STATUS_OK) {
            basl_program_free(&program);
            basl_token_list_free(&tokens);
            return status;
        }
    }

    memory = NULL;
    status = basl_runtime_alloc(
        registry->runtime,
        program.function_count * sizeof(*function_table),
        &memory,
        error
    );
    if (status != BASL_STATUS_OK) {
        basl_program_free(&program);
        basl_token_list_free(&tokens);
        return status;
    }

    function_table = (basl_object_t **)memory;
    for (i = 0U; i < program.function_count; ++i) {
        function_table[i] = program.functions[i].object;
    }

    status = basl_function_object_attach_siblings(
        program.functions[program.main_index].object,
        function_table,
        program.function_count,
        program.main_index,
        error
    );
    if (status != BASL_STATUS_OK) {
        memory = function_table;
        basl_runtime_free(registry->runtime, &memory);
        basl_program_free(&program);
        basl_token_list_free(&tokens);
        return status;
    }

    *out_function = program.functions[program.main_index].object;
    for (i = 0U; i < program.function_count; ++i) {
        program.functions[i].object = NULL;
    }

    basl_program_free(&program);
    basl_token_list_free(&tokens);
    return BASL_STATUS_OK;
}
