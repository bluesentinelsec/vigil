#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/compiler.h"
#include "basl/chunk.h"
#include "basl/lexer.h"
#include "basl/string.h"
#include "basl/token.h"
#include "internal/basl_internal.h"

typedef struct basl_parser_state {
    const basl_source_registry_t *registry;
    const basl_source_file_t *source;
    const basl_token_list_t *tokens;
    size_t current;
    basl_diagnostic_list_t *diagnostics;
    basl_error_t *error;
    basl_chunk_t chunk;
} basl_parser_state_t;

static const basl_token_t *basl_parser_peek(const basl_parser_state_t *state) {
    return basl_token_list_get(state->tokens, state->current);
}

static const basl_token_t *basl_parser_previous(const basl_parser_state_t *state) {
    if (state->current == 0U) {
        return NULL;
    }

    return basl_token_list_get(state->tokens, state->current - 1U);
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

static basl_status_t basl_parser_copy_slice(
    basl_parser_state_t *state,
    const char *text,
    size_t length,
    basl_string_t *out
) {
    basl_string_init(out, state->chunk.runtime);
    return basl_string_assign(out, text, length, state->error);
}

static basl_status_t basl_parser_parse_int(
    basl_parser_state_t *state,
    const basl_token_t *token,
    basl_value_t *out_value
) {
    basl_status_t status;
    basl_string_t text;
    char *end;
    long long parsed;

    status = basl_parser_copy_slice(
        state,
        basl_parser_token_text(state, token, NULL),
        token->span.end_offset - token->span.start_offset,
        &text
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    errno = 0;
    parsed = strtoll(basl_string_c_str(&text), &end, 0);
    if (errno != 0 || end == basl_string_c_str(&text) || *end != '\0') {
        basl_string_free(&text);
        return basl_parser_report(state, token->span, "invalid integer literal");
    }

    basl_value_init_int(out_value, (int64_t)parsed);
    basl_string_free(&text);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_float(
    basl_parser_state_t *state,
    const basl_token_t *token,
    basl_value_t *out_value
) {
    basl_status_t status;
    basl_string_t text;
    char *end;
    double parsed;

    status = basl_parser_copy_slice(
        state,
        basl_parser_token_text(state, token, NULL),
        token->span.end_offset - token->span.start_offset,
        &text
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    errno = 0;
    parsed = strtod(basl_string_c_str(&text), &end);
    if (errno != 0 || end == basl_string_c_str(&text) || *end != '\0') {
        basl_string_free(&text);
        return basl_parser_report(state, token->span, "invalid float literal");
    }

    basl_value_init_float(out_value, parsed);
    basl_string_free(&text);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_decode_string_literal(
    basl_parser_state_t *state,
    const basl_token_t *token,
    basl_string_t *out
) {
    const char *text;
    size_t length;
    size_t index;
    char quote;
    basl_status_t status;

    text = basl_parser_token_text(state, token, &length);
    if (text == NULL || length < 2U) {
        return basl_parser_report(state, token->span, "invalid string literal");
    }

    quote = text[0];
    basl_string_init(out, state->chunk.runtime);
    if (quote == '`') {
        return basl_string_assign(out, text + 1U, length - 2U, state->error);
    }

    for (index = 1U; index + 1U < length; index += 1U) {
        char ch;

        ch = text[index];
        if (ch == '\\') {
            index += 1U;
            if (index + 1U > length) {
                basl_string_free(out);
                return basl_parser_report(state, token->span, "invalid string escape");
            }

            switch (text[index]) {
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case '\\': ch = '\\'; break;
                case '"': ch = '"'; break;
                case '\'': ch = '\''; break;
                default:
                    basl_string_free(out);
                    return basl_parser_report(state, token->span, "unsupported string escape");
            }
        }

        status = basl_string_append(out, &ch, 1U, state->error);
        if (status != BASL_STATUS_OK) {
            basl_string_free(out);
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_emit_literal(
    basl_parser_state_t *state,
    const basl_token_t *token
) {
    basl_status_t status;
    basl_value_t value;
    basl_object_t *object;
    basl_string_t decoded;

    switch (token->kind) {
        case BASL_TOKEN_NIL:
            return basl_chunk_write_opcode(
                &state->chunk,
                BASL_OPCODE_NIL,
                token->span,
                state->error
            );
        case BASL_TOKEN_TRUE:
            return basl_chunk_write_opcode(
                &state->chunk,
                BASL_OPCODE_TRUE,
                token->span,
                state->error
            );
        case BASL_TOKEN_FALSE:
            return basl_chunk_write_opcode(
                &state->chunk,
                BASL_OPCODE_FALSE,
                token->span,
                state->error
            );
        case BASL_TOKEN_INT_LITERAL:
            status = basl_parser_parse_int(state, token, &value);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_chunk_write_constant(&state->chunk, &value, token->span, NULL, state->error);
            basl_value_release(&value);
            return status;
        case BASL_TOKEN_FLOAT_LITERAL:
            status = basl_parser_parse_float(state, token, &value);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_chunk_write_constant(&state->chunk, &value, token->span, NULL, state->error);
            basl_value_release(&value);
            return status;
        case BASL_TOKEN_STRING_LITERAL:
        case BASL_TOKEN_RAW_STRING_LITERAL:
        case BASL_TOKEN_CHAR_LITERAL:
            status = basl_parser_decode_string_literal(state, token, &decoded);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            object = NULL;
            status = basl_string_object_new(
                state->chunk.runtime,
                basl_string_c_str(&decoded),
                basl_string_length(&decoded),
                &object,
                state->error
            );
            basl_string_free(&decoded);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_value_init_object(&value, &object);
            status = basl_chunk_write_constant(&state->chunk, &value, token->span, NULL, state->error);
            basl_value_release(&value);
            return status;
        case BASL_TOKEN_FSTRING_LITERAL:
            return basl_parser_report(state, token->span, "f-strings are not yet supported");
        default:
            return basl_parser_report(state, token->span, "expected a literal expression");
    }
}

static basl_status_t basl_parser_parse_return_statement(
    basl_parser_state_t *state
) {
    basl_status_t status;
    const basl_token_t *return_token;
    const basl_token_t *literal_token;

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RETURN,
        "expected return statement",
        &return_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    literal_token = basl_parser_peek(state);
    if (literal_token == NULL) {
        return basl_parser_report(state, return_token->span, "expected literal after return");
    }

    basl_parser_advance(state);
    status = basl_parser_emit_literal(state, literal_token);
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

    return basl_chunk_write_opcode(
        &state->chunk,
        BASL_OPCODE_RETURN,
        literal_token->span,
        state->error
    );
}

static basl_status_t basl_parser_parse_script(
    basl_parser_state_t *state,
    basl_object_t **out_function
) {
    basl_status_t status;
    const basl_token_t *name_token;
    const char *name_text;
    size_t name_length;

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

    status = basl_parser_expect(state, BASL_TOKEN_IDENTIFIER, "expected return type name", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_LBRACE, "expected '{' before function body", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_return_statement(state);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_RBRACE, "expected '}' after function body", NULL);
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

    basl_diagnostic_list_clear(diagnostics);
    basl_token_list_init(&tokens, registry->runtime);
    status = basl_lex_source(registry, source_id, &tokens, diagnostics, error);
    if (status != BASL_STATUS_OK && status != BASL_STATUS_SYNTAX_ERROR) {
        basl_token_list_free(&tokens);
        return status;
    }
    if (status == BASL_STATUS_SYNTAX_ERROR) {
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
    return status;
}
