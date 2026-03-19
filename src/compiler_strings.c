#include <stdlib.h>
#include <string.h>
#include "vigil/lexer.h"
#include "internal/vigil_compiler_types.h"
#include "internal/vigil_internal.h"

vigil_status_t vigil_program_append_decoded_string_range(
    const vigil_program_state_t *program,
    vigil_source_span_t span,
    const char *text,
    size_t start,
    size_t end,
    vigil_string_t *out_text
) {
    size_t index;
    char decoded;
    vigil_status_t status;

    if (program == NULL || text == NULL || out_text == NULL || start > end) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    for (index = start; index < end; index += 1U) {
        if (index + 1U < end && text[index] == '{' && text[index + 1U] == '{') {
            status = vigil_string_append(out_text, "{", 1U, program->error);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            index += 1U;
            continue;
        }
        if (index + 1U < end && text[index] == '}' && text[index + 1U] == '}') {
            status = vigil_string_append(out_text, "}", 1U, program->error);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            index += 1U;
            continue;
        }
        if (text[index] != '\\') {
            status = vigil_string_append(out_text, text + index, 1U, program->error);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }

        index += 1U;
        if (index >= end) {
            return vigil_compile_report(program, span, "invalid escape sequence");
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
            case 'x': {
                unsigned int hi;
                unsigned int lo;
                if (index + 2U >= end) {
                    return vigil_compile_report(program, span,
                        "\\x escape requires two hex digits");
                }
                hi = (unsigned int)text[index + 1U];
                lo = (unsigned int)text[index + 2U];
                if (hi >= '0' && hi <= '9') { hi = hi - '0'; }
                else if (hi >= 'a' && hi <= 'f') { hi = hi - 'a' + 10U; }
                else if (hi >= 'A' && hi <= 'F') { hi = hi - 'A' + 10U; }
                else {
                    return vigil_compile_report(program, span,
                        "\\x escape requires two hex digits");
                }
                if (lo >= '0' && lo <= '9') { lo = lo - '0'; }
                else if (lo >= 'a' && lo <= 'f') { lo = lo - 'a' + 10U; }
                else if (lo >= 'A' && lo <= 'F') { lo = lo - 'A' + 10U; }
                else {
                    return vigil_compile_report(program, span,
                        "\\x escape requires two hex digits");
                }
                decoded = (char)((hi << 4U) | lo);
                index += 2U;
                break;
            }
            default:
                return vigil_compile_report(program, span, "invalid escape sequence");
        }

        status = vigil_string_append(out_text, &decoded, 1U, program->error);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}

size_t vigil_program_skip_quoted_text(
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

vigil_status_t vigil_parser_parse_embedded_expression(
    vigil_parser_state_t *state,
    const char *text,
    size_t length,
    size_t absolute_offset,
    vigil_source_span_t error_span,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_source_registry_t registry;
    vigil_diagnostic_list_t diagnostics;
    vigil_source_id_t source_id;
    const vigil_source_file_t *source;
    vigil_token_list_t tokens;
    size_t token_index;
    vigil_program_state_t *program;
    const vigil_token_list_t *previous_tokens;
    vigil_parser_state_t nested;
    vigil_expression_result_t nested_result;

    if (state == NULL || text == NULL || out_result == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_source_registry_init(&registry, state->program->registry->runtime);
    vigil_diagnostic_list_init(&diagnostics, state->program->registry->runtime);
    vigil_token_list_init(&tokens, state->program->registry->runtime);
    source_id = 0U;
    status = vigil_source_registry_register(
        &registry,
        "<fstring>",
        9U,
        text,
        length,
        &source_id,
        state->program->error
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_token_list_free(&tokens);
        vigil_diagnostic_list_free(&diagnostics);
        vigil_source_registry_free(&registry);
        return status;
    }

    status = vigil_lex_source(&registry, source_id, &tokens, &diagnostics, state->program->error);
    if (status != VIGIL_STATUS_OK) {
        vigil_token_list_free(&tokens);
        vigil_diagnostic_list_free(&diagnostics);
        vigil_source_registry_free(&registry);
        return vigil_parser_report(state, error_span, "invalid f-string interpolation expression");
    }

    source = vigil_source_registry_get(&registry, source_id);
    if (source == NULL) {
        vigil_token_list_free(&tokens);
        vigil_diagnostic_list_free(&diagnostics);
        vigil_source_registry_free(&registry);
        return VIGIL_STATUS_INTERNAL;
    }

    for (token_index = 0U; token_index < tokens.count; token_index += 1U) {
        if (absolute_offset != 0U) {
            tokens.items[token_index].span.source_id = state->program->source->id;
            tokens.items[token_index].span.start_offset += absolute_offset;
            tokens.items[token_index].span.end_offset += absolute_offset;
        }
    }

    program = (vigil_program_state_t *)state->program;
    previous_tokens = program->tokens;
    program->tokens = &tokens;
    {
        const vigil_source_file_t *previous_source = NULL;
        if (absolute_offset == 0U) {
            previous_source = program->source;
            program->source = source;
        }
        nested = *state;
        nested.current = 0U;
        nested.body_end = vigil_token_list_count(&tokens);
        vigil_expression_result_clear(&nested_result);
        status = vigil_parser_parse_expression(&nested, &nested_result);
        if (status == VIGIL_STATUS_OK && !vigil_parser_check(&nested, VIGIL_TOKEN_EOF)) {
            status = vigil_parser_report(
                &nested,
                vigil_parser_peek(&nested) == NULL ? error_span : vigil_parser_peek(&nested)->span,
                "expected end of f-string interpolation expression"
            );
        }
        if (status == VIGIL_STATUS_OK) {
            state->chunk = nested.chunk;
            *out_result = nested_result;
        }
        if (absolute_offset == 0U) {
            program->source = previous_source;
        }
    }
    program->tokens = previous_tokens;
    vigil_token_list_free(&tokens);
    vigil_diagnostic_list_free(&diagnostics);
    vigil_source_registry_free(&registry);
    return status;
}

vigil_status_t vigil_parser_emit_fstring_part_string(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    int *part_count,
    const char *text,
    size_t length
) {
    vigil_status_t status;

    if (state == NULL || part_count == NULL || text == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_parser_emit_string_constant_text(state, span, text, length);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    if (*part_count > 0) {
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ADD, span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }
    *part_count += 1;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_parser_emit_fstring_part_value(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    int *part_count
) {
    vigil_status_t status;

    if (state == NULL || part_count == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (*part_count > 0) {
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ADD, span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }
    *part_count += 1;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_parser_parse_fstring_literal(
    vigil_parser_state_t *state,
    const vigil_token_t *token,
    vigil_expression_result_t *out_result
) {
    const char *text;
    size_t length;
    size_t index;
    size_t segment_start;
    int part_count;
    vigil_string_t segment;
    vigil_status_t status;

    if (state == NULL || token == NULL || out_result == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    text = vigil_parser_token_text(state, token, &length);
    if (text == NULL || length < 3U) {
        return vigil_parser_report(state, token->span, "invalid f-string literal");
    }

    vigil_string_init(&segment, state->program->registry->runtime);
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
        vigil_expression_result_t expression_result;

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
                vigil_string_free(&segment);
                return vigil_parser_report(state, token->span, "unmatched '}' in f-string");
            }
            index += 1U;
            continue;
        }

        vigil_string_clear(&segment);
        status = vigil_program_append_decoded_string_range(
            state->program,
            token->span,
            text,
            segment_start,
            index,
            &segment
        );
        if (status != VIGIL_STATUS_OK) {
            vigil_string_free(&segment);
            return status;
        }
        if (vigil_string_length(&segment) > 0U) {
            status = vigil_parser_emit_fstring_part_string(
                state,
                token->span,
                &part_count,
                vigil_string_c_str(&segment),
                vigil_string_length(&segment)
            );
            if (status != VIGIL_STATUS_OK) {
                vigil_string_free(&segment);
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
            if (text[cursor] == '\\' && cursor + 1U < length - 1U &&
                (text[cursor + 1U] == '"' || text[cursor + 1U] == '\'' || text[cursor + 1U] == '`')) {
                /* Escaped quote starts a string literal inside the interpolation.
                   Skip \<quote> ... \<quote> as a unit. */
                char delim = text[cursor + 1U];
                cursor += 2U; /* skip \<quote> */
                while (cursor + 1U < length - 1U) {
                    if (text[cursor] == '\\' && text[cursor + 1U] == delim) {
                        cursor += 2U; /* skip closing \<quote> */
                        break;
                    }
                    if (text[cursor] == '\\' && cursor + 1U < length - 1U) {
                        cursor += 2U;
                        continue;
                    }
                    cursor += 1U;
                }
                continue;
            }
            if (text[cursor] == '\\' && cursor + 1U < length - 1U) {
                cursor += 2U;
                continue;
            }
            if (text[cursor] == '"' || text[cursor] == '\'' || text[cursor] == '`') {
                cursor = vigil_program_skip_quoted_text(text, length - 1U, cursor, text[cursor]);
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
            vigil_string_free(&segment);
            return vigil_parser_report(state, token->span, "unterminated f-string interpolation");
        }

        vigil_program_trim_text_range(text, expression_start, expression_end, &trim_start, &trim_end);
        if (trim_start == trim_end) {
            vigil_string_free(&segment);
            return vigil_parser_report(state, token->span, "f-string interpolation expression must not be empty");
        }

        absolute_offset = token->span.start_offset + trim_start;
        vigil_expression_result_clear(&expression_result);

        /* Decode escape sequences in the expression text so that e.g.
           f"sizeof({\"i32\"})" passes "i32" to the embedded parser.
           When decoding changes the text length, pass offset 0 so the
           embedded parser reads token values from its own copy. */
        {
            const char *expr_text = text + trim_start;
            size_t expr_len = trim_end - trim_start;
            vigil_string_t decoded_expr;
            int has_escapes = 0;
            size_t ei;

            for (ei = 0; ei < expr_len && !has_escapes; ei++) {
                if (expr_text[ei] == '\\') has_escapes = 1;
            }

            if (has_escapes) {
                vigil_string_init(&decoded_expr, state->program->registry->runtime);
                for (ei = 0; ei < expr_len; ei++) {
                    if (expr_text[ei] == '\\' && ei + 1U < expr_len) {
                        char next = expr_text[ei + 1U];
                        if (next == '"' || next == '\'' || next == '\\') {
                            vigil_string_append(&decoded_expr, &next, 1U,
                                                state->program->error);
                            ei++;
                            continue;
                        }
                    }
                    vigil_string_append(&decoded_expr, expr_text + ei, 1U,
                                        state->program->error);
                }
                status = vigil_parser_parse_embedded_expression(
                    state,
                    vigil_string_c_str(&decoded_expr),
                    vigil_string_length(&decoded_expr),
                    0U,
                    token->span,
                    &expression_result
                );
                vigil_string_free(&decoded_expr);
            } else {
                status = vigil_parser_parse_embedded_expression(
                    state,
                    expr_text,
                    expr_len,
                    absolute_offset,
                    token->span,
                    &expression_result
                );
            }
        }
        if (status != VIGIL_STATUS_OK) {
            vigil_string_free(&segment);
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            token->span,
            &expression_result,
            "f-string interpolation expressions must be single values"
        );
        if (status != VIGIL_STATUS_OK) {
            vigil_string_free(&segment);
            return status;
        }

        if (format_start == SIZE_MAX) {
            if (!vigil_parser_type_is_string(expression_result.type)) {
                if (
                    !vigil_parser_type_is_integer(expression_result.type) &&
                    !vigil_parser_type_is_f64(expression_result.type) &&
                    !vigil_parser_type_is_bool(expression_result.type)
                ) {
                    vigil_string_free(&segment);
                    return vigil_parser_report(
                        state,
                        token->span,
                        "f-string interpolation requires a string, integer, f64, or bool value"
                    );
                }
                status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_TO_STRING, token->span);
                if (status != VIGIL_STATUS_OK) {
                    vigil_string_free(&segment);
                    return status;
                }
            }
        } else {
            /* ── General format specifier parser ──────────────────────
               Syntax: [[fill]align][width][grouping][.precision][type]
               fill:      any single ASCII char (only if followed by align)
               align:     < (left) > (right) ^ (center)
               width:     integer
               grouping:  , (thousands separator)
               precision: .N
               type:      d x X b o f
            */
            size_t fs;
            size_t fe;
            char fill_char;
            unsigned int align_val;
            unsigned int width_val;
            unsigned int prec_val;
            unsigned int fmt_type;
            unsigned int grouping_val;
            uint32_t word1;
            uint32_t word2;

            vigil_program_trim_text_range(text, format_start, cursor, &trim_start, &trim_end);
            if (trim_start >= trim_end) {
                vigil_string_free(&segment);
                return vigil_parser_report(state, token->span, "empty format specifier");
            }

            fs = trim_start;
            fe = trim_end;
            fill_char = 0;
            align_val = 0U;
            width_val = 0U;
            prec_val = 0U;
            fmt_type = 0U;
            grouping_val = 0U;

            /* Check for [fill]align — fill is any char before <, >, ^ */
            if (fe - fs >= 2U && (text[fs + 1U] == '<' || text[fs + 1U] == '>' || text[fs + 1U] == '^')) {
                fill_char = text[fs];
                if (text[fs + 1U] == '<') align_val = 1U;
                else if (text[fs + 1U] == '>') align_val = 2U;
                else align_val = 3U;
                fs += 2U;
            } else if (fe - fs >= 1U && (text[fs] == '<' || text[fs] == '>' || text[fs] == '^')) {
                if (text[fs] == '<') align_val = 1U;
                else if (text[fs] == '>') align_val = 2U;
                else align_val = 3U;
                fs += 1U;
            }

            /* Parse width (digits) */
            while (fs < fe && text[fs] >= '0' && text[fs] <= '9') {
                width_val = width_val * 10U + (unsigned int)(text[fs] - '0');
                fs += 1U;
            }

            /* Check for grouping ',' */
            if (fs < fe && text[fs] == ',') {
                grouping_val = 1U;
                fs += 1U;
            }

            /* Check for precision '.N' */
            if (fs < fe && text[fs] == '.') {
                fs += 1U;
                while (fs < fe && text[fs] >= '0' && text[fs] <= '9') {
                    prec_val = prec_val * 10U + (unsigned int)(text[fs] - '0');
                    fs += 1U;
                }
            }

            /* Check for type character */
            if (fs < fe) {
                char tc = text[fs];
                if (tc == 'd') { fmt_type = 1U; fs += 1U; }
                else if (tc == 'x') { fmt_type = 2U; fs += 1U; }
                else if (tc == 'X') { fmt_type = 3U; fs += 1U; }
                else if (tc == 'b') { fmt_type = 4U; fs += 1U; }
                else if (tc == 'o') { fmt_type = 5U; fs += 1U; }
                else if (tc == 'f') { fmt_type = 6U; fs += 1U; }
                else {
                    vigil_string_free(&segment);
                    return vigil_parser_report(state, token->span,
                        "invalid format type character (expected d, x, X, b, o, or f)");
                }
            }

            if (fs != fe) {
                vigil_string_free(&segment);
                return vigil_parser_report(state, token->span, "invalid f-string format specifier");
            }

            /* Type-check: float formats require f64, integer formats require integer */
            if (fmt_type == 6U) {
                if (!vigil_parser_type_is_f64(expression_result.type)) {
                    vigil_string_free(&segment);
                    return vigil_parser_report(state, token->span,
                        "float format specifier 'f' requires an f64 value");
                }
            } else if (fmt_type >= 1U && fmt_type <= 5U) {
                if (!vigil_parser_type_is_integer(expression_result.type)) {
                    vigil_string_free(&segment);
                    return vigil_parser_report(state, token->span,
                        "integer format specifier requires an integer value");
                }
            } else if (grouping_val) {
                /* Bare ',' with no type — infer decimal for integers */
                if (vigil_parser_type_is_integer(expression_result.type)) {
                    fmt_type = 1U;
                } else {
                    vigil_string_free(&segment);
                    return vigil_parser_report(state, token->span,
                        "grouping ',' requires an integer value");
                }
            }

            /* If only width/align specified with no type, use type 0 (string).
               The value will be stringified first. */
            if (fmt_type == 0U && !grouping_val) {
                /* Convert to string first, then FORMAT_SPEC will pad. */
                if (!vigil_parser_type_is_string(expression_result.type)) {
                    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_TO_STRING, token->span);
                    if (status != VIGIL_STATUS_OK) {
                        vigil_string_free(&segment);
                        return status;
                    }
                }
            }

            /* Encode and emit FORMAT_SPEC with two u32 operands. */
            word1 = ((uint32_t)(unsigned char)fill_char)
                  | (align_val << 8U)
                  | (fmt_type << 10U)
                  | (grouping_val << 14U);
            word2 = (width_val & 0xFFFFU) | ((prec_val & 0xFFFFU) << 16U);

            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_FORMAT_SPEC, token->span);
            if (status != VIGIL_STATUS_OK) {
                vigil_string_free(&segment);
                return status;
            }
            status = vigil_parser_emit_u32(state, word1, token->span);
            if (status != VIGIL_STATUS_OK) {
                vigil_string_free(&segment);
                return status;
            }
            status = vigil_parser_emit_u32(state, word2, token->span);
            if (status != VIGIL_STATUS_OK) {
                vigil_string_free(&segment);
                return status;
            }
        }

        status = vigil_parser_emit_fstring_part_value(state, token->span, &part_count);
        if (status != VIGIL_STATUS_OK) {
            vigil_string_free(&segment);
            return status;
        }

        index = cursor + 1U;
        segment_start = index;
    }

    vigil_string_clear(&segment);
    status = vigil_program_append_decoded_string_range(
        state->program,
        token->span,
        text,
        segment_start,
        length - 1U,
        &segment
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_string_free(&segment);
        return status;
    }
    if (vigil_string_length(&segment) > 0U || part_count == 0) {
        status = vigil_parser_emit_fstring_part_string(
            state,
            token->span,
            &part_count,
            vigil_string_c_str(&segment),
            vigil_string_length(&segment)
        );
    }
    vigil_string_free(&segment);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_STRING));
    return VIGIL_STATUS_OK;
}
