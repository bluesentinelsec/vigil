#include <stdlib.h>
#include <string.h>
#include "basl/lexer.h"
#include "internal/basl_compiler_types.h"
#include "internal/basl_internal.h"

basl_status_t basl_program_append_decoded_string_range(
    const basl_program_state_t *program,
    basl_source_span_t span,
    const char *text,
    size_t start,
    size_t end,
    basl_string_t *out_text
) {
    size_t index;
    char decoded;
    basl_status_t status;

    if (program == NULL || text == NULL || out_text == NULL || start > end) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    for (index = start; index < end; index += 1U) {
        if (index + 1U < end && text[index] == '{' && text[index + 1U] == '{') {
            status = basl_string_append(out_text, "{", 1U, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            index += 1U;
            continue;
        }
        if (index + 1U < end && text[index] == '}' && text[index + 1U] == '}') {
            status = basl_string_append(out_text, "}", 1U, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            index += 1U;
            continue;
        }
        if (text[index] != '\\') {
            status = basl_string_append(out_text, text + index, 1U, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }

        index += 1U;
        if (index >= end) {
            return basl_compile_report(program, span, "invalid escape sequence");
        }

        switch (text[index]) {
            case 'n':
                decoded = '\n';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 't':
                decoded = '\t';
                break;
            case '\\':
                decoded = '\\';
                break;
            case '"':
                decoded = '"';
                break;
            case '\'':
                decoded = '\'';
                break;
            case '0':
                decoded = '\0';
                break;
            default:
                return basl_compile_report(program, span, "invalid escape sequence");
        }

        status = basl_string_append(out_text, &decoded, 1U, program->error);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

size_t basl_program_skip_quoted_text(
    const char *text,
    size_t length,
    size_t index,
    char delimiter
) {
    index += 1U;
    while (index < length) {
        if (text[index] == '\\' && delimiter != '`') {
            index += 2U;
            continue;
        }
        if (text[index] == delimiter) {
            return index + 1U;
        }
        index += 1U;
    }
    return length;
}

basl_status_t basl_parser_parse_embedded_expression(
    basl_parser_state_t *state,
    const char *text,
    size_t length,
    size_t absolute_offset,
    basl_source_span_t error_span,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_source_id_t source_id;
    const basl_source_file_t *source;
    basl_token_list_t tokens;
    size_t token_index;
    basl_program_state_t *program;
    const basl_token_list_t *previous_tokens;
    basl_parser_state_t nested;
    basl_expression_result_t nested_result;

    if (state == NULL || text == NULL || out_result == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    basl_source_registry_init(&registry, state->program->registry->runtime);
    basl_diagnostic_list_init(&diagnostics, state->program->registry->runtime);
    basl_token_list_init(&tokens, state->program->registry->runtime);
    source_id = 0U;
    status = basl_source_registry_register(
        &registry,
        "<fstring>",
        9U,
        text,
        length,
        &source_id,
        state->program->error
    );
    if (status != BASL_STATUS_OK) {
        basl_token_list_free(&tokens);
        basl_diagnostic_list_free(&diagnostics);
        basl_source_registry_free(&registry);
        return status;
    }

    status = basl_lex_source(&registry, source_id, &tokens, &diagnostics, state->program->error);
    if (status != BASL_STATUS_OK) {
        basl_token_list_free(&tokens);
        basl_diagnostic_list_free(&diagnostics);
        basl_source_registry_free(&registry);
        return basl_parser_report(state, error_span, "invalid f-string interpolation expression");
    }

    source = basl_source_registry_get(&registry, source_id);
    if (source == NULL) {
        basl_token_list_free(&tokens);
        basl_diagnostic_list_free(&diagnostics);
        basl_source_registry_free(&registry);
        return BASL_STATUS_INTERNAL;
    }

    for (token_index = 0U; token_index < tokens.count; token_index += 1U) {
        tokens.items[token_index].span.source_id = state->program->source->id;
        tokens.items[token_index].span.start_offset += absolute_offset;
        tokens.items[token_index].span.end_offset += absolute_offset;
    }

    program = (basl_program_state_t *)state->program;
    previous_tokens = program->tokens;
    program->tokens = &tokens;
    nested = *state;
    nested.current = 0U;
    nested.body_end = basl_token_list_count(&tokens);
    basl_expression_result_clear(&nested_result);
    status = basl_parser_parse_expression(&nested, &nested_result);
    if (status == BASL_STATUS_OK && !basl_parser_check(&nested, BASL_TOKEN_EOF)) {
        status = basl_parser_report(
            &nested,
            basl_parser_peek(&nested) == NULL ? error_span : basl_parser_peek(&nested)->span,
            "expected end of f-string interpolation expression"
        );
    }
    if (status == BASL_STATUS_OK) {
        state->chunk = nested.chunk;
        *out_result = nested_result;
    }
    program->tokens = previous_tokens;
    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    return status;
}

basl_status_t basl_parser_emit_fstring_part_string(
    basl_parser_state_t *state,
    basl_source_span_t span,
    int *part_count,
    const char *text,
    size_t length
) {
    basl_status_t status;

    if (state == NULL || part_count == NULL || text == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_parser_emit_string_constant_text(state, span, text, length);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (*part_count > 0) {
        status = basl_parser_emit_opcode(state, BASL_OPCODE_ADD, span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }
    *part_count += 1;
    return BASL_STATUS_OK;
}

basl_status_t basl_parser_emit_fstring_part_value(
    basl_parser_state_t *state,
    basl_source_span_t span,
    int *part_count
) {
    basl_status_t status;

    if (state == NULL || part_count == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (*part_count > 0) {
        status = basl_parser_emit_opcode(state, BASL_OPCODE_ADD, span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }
    *part_count += 1;
    return BASL_STATUS_OK;
}

basl_status_t basl_parser_parse_fstring_literal(
    basl_parser_state_t *state,
    const basl_token_t *token,
    basl_expression_result_t *out_result
) {
    const char *text;
    size_t length;
    size_t index;
    size_t segment_start;
    int part_count;
    basl_string_t segment;
    basl_status_t status;

    if (state == NULL || token == NULL || out_result == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    text = basl_parser_token_text(state, token, &length);
    if (text == NULL || length < 3U) {
        return basl_parser_report(state, token->span, "invalid f-string literal");
    }

    basl_string_init(&segment, state->program->registry->runtime);
    segment_start = 2U;
    part_count = 0;
    index = 2U;
    while (index < length - 1U) {
        size_t expression_start;
        size_t expression_end;
        size_t format_start;
        size_t trim_start;
        size_t trim_end;
        size_t absolute_offset;
        size_t paren_depth;
        size_t bracket_depth;
        size_t brace_depth;
        size_t cursor;
        basl_expression_result_t expression_result;
        unsigned long precision_value;
        char *end_ptr;

        if (text[index] == '\\') {
            index += 2U;
            continue;
        }
        if (text[index] == '{' && index + 1U < length - 1U && text[index + 1U] == '{') {
            index += 2U;
            continue;
        }
        if (text[index] == '}' && index + 1U < length - 1U && text[index + 1U] == '}') {
            index += 2U;
            continue;
        }
        if (text[index] != '{') {
            if (text[index] == '}') {
                basl_string_free(&segment);
                return basl_parser_report(state, token->span, "unmatched '}' in f-string");
            }
            index += 1U;
            continue;
        }

        basl_string_clear(&segment);
        status = basl_program_append_decoded_string_range(
            state->program,
            token->span,
            text,
            segment_start,
            index,
            &segment
        );
        if (status != BASL_STATUS_OK) {
            basl_string_free(&segment);
            return status;
        }
        if (basl_string_length(&segment) > 0U) {
            status = basl_parser_emit_fstring_part_string(
                state,
                token->span,
                &part_count,
                basl_string_c_str(&segment),
                basl_string_length(&segment)
            );
            if (status != BASL_STATUS_OK) {
                basl_string_free(&segment);
                return status;
            }
        }

        expression_start = index + 1U;
        expression_end = SIZE_MAX;
        format_start = SIZE_MAX;
        paren_depth = 0U;
        bracket_depth = 0U;
        brace_depth = 0U;
        cursor = expression_start;
        while (cursor < length - 1U) {
            if (text[cursor] == '"' || text[cursor] == '\'' || text[cursor] == '`') {
                cursor = basl_program_skip_quoted_text(text, length - 1U, cursor, text[cursor]);
                continue;
            }
            if (text[cursor] == '(') {
                paren_depth += 1U;
            } else if (text[cursor] == ')') {
                if (paren_depth > 0U) {
                    paren_depth -= 1U;
                }
            } else if (text[cursor] == '[') {
                bracket_depth += 1U;
            } else if (text[cursor] == ']') {
                if (bracket_depth > 0U) {
                    bracket_depth -= 1U;
                }
            } else if (text[cursor] == '{') {
                brace_depth += 1U;
            } else if (text[cursor] == '}') {
                if (paren_depth == 0U && bracket_depth == 0U && brace_depth == 0U) {
                    if (format_start == SIZE_MAX) {
                        expression_end = cursor;
                    }
                    break;
                }
                brace_depth -= 1U;
            } else if (
                text[cursor] == ':' &&
                paren_depth == 0U &&
                bracket_depth == 0U &&
                brace_depth == 0U &&
                format_start == SIZE_MAX
            ) {
                format_start = cursor + 1U;
                expression_end = cursor;
            }
            cursor += 1U;
        }

        if (expression_end == SIZE_MAX || cursor >= length - 1U) {
            basl_string_free(&segment);
            return basl_parser_report(state, token->span, "unterminated f-string interpolation");
        }

        basl_program_trim_text_range(text, expression_start, expression_end, &trim_start, &trim_end);
        if (trim_start == trim_end) {
            basl_string_free(&segment);
            return basl_parser_report(state, token->span, "f-string interpolation expression must not be empty");
        }

        absolute_offset = token->span.start_offset + trim_start;
        basl_expression_result_clear(&expression_result);
        status = basl_parser_parse_embedded_expression(
            state,
            text + trim_start,
            trim_end - trim_start,
            absolute_offset,
            token->span,
            &expression_result
        );
        if (status != BASL_STATUS_OK) {
            basl_string_free(&segment);
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            token->span,
            &expression_result,
            "f-string interpolation expressions must be single values"
        );
        if (status != BASL_STATUS_OK) {
            basl_string_free(&segment);
            return status;
        }

        if (format_start == SIZE_MAX) {
            if (!basl_parser_type_is_string(expression_result.type)) {
                if (
                    !basl_parser_type_is_integer(expression_result.type) &&
                    !basl_parser_type_is_f64(expression_result.type) &&
                    !basl_parser_type_is_bool(expression_result.type)
                ) {
                    basl_string_free(&segment);
                    return basl_parser_report(
                        state,
                        token->span,
                        "f-string interpolation requires a string, integer, f64, or bool value"
                    );
                }
                status = basl_parser_emit_opcode(state, BASL_OPCODE_TO_STRING, token->span);
                if (status != BASL_STATUS_OK) {
                    basl_string_free(&segment);
                    return status;
                }
            }
        } else {
            basl_program_trim_text_range(text, format_start, cursor, &trim_start, &trim_end);
            if (!basl_parser_type_is_f64(expression_result.type)) {
                basl_string_free(&segment);
                return basl_parser_report(state, token->span, "f-string format specifiers currently require f64 values");
            }
            if (trim_start >= cursor || trim_end > cursor) {
                basl_string_free(&segment);
                return basl_parser_report(state, token->span, "invalid f-string format specifier");
            }
            if (trim_end - trim_start < 3U || text[trim_start] != '.' || text[trim_end - 1U] != 'f') {
                basl_string_free(&segment);
                return basl_parser_report(state, token->span, "invalid f-string format specifier");
            }
            precision_value = strtoul(text + trim_start + 1U, &end_ptr, 10);
            if (end_ptr != text + trim_end - 1U || precision_value > UINT32_MAX) {
                basl_string_free(&segment);
                return basl_parser_report(state, token->span, "invalid f-string format specifier");
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_FORMAT_F64, token->span);
            if (status != BASL_STATUS_OK) {
                basl_string_free(&segment);
                return status;
            }
            status = basl_parser_emit_u32(state, (uint32_t)precision_value, token->span);
            if (status != BASL_STATUS_OK) {
                basl_string_free(&segment);
                return status;
            }
        }

        status = basl_parser_emit_fstring_part_value(state, token->span, &part_count);
        if (status != BASL_STATUS_OK) {
            basl_string_free(&segment);
            return status;
        }

        index = cursor + 1U;
        segment_start = index;
    }

    basl_string_clear(&segment);
    status = basl_program_append_decoded_string_range(
        state->program,
        token->span,
        text,
        segment_start,
        length - 1U,
        &segment
    );
    if (status != BASL_STATUS_OK) {
        basl_string_free(&segment);
        return status;
    }
    if (basl_string_length(&segment) > 0U || part_count == 0) {
        status = basl_parser_emit_fstring_part_string(
            state,
            token->span,
            &part_count,
            basl_string_c_str(&segment),
            basl_string_length(&segment)
        );
    }
    basl_string_free(&segment);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_expression_result_set_type(out_result, basl_binding_type_primitive(BASL_TYPE_STRING));
    return BASL_STATUS_OK;
}
