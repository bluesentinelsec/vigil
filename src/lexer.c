#include <ctype.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/lexer.h"

basl_status_t basl_token_list_append(
    basl_token_list_t *list,
    basl_token_kind_t kind,
    basl_source_span_t span,
    basl_error_t *error
);

typedef struct basl_lexer_state {
    const char *text;
    size_t length;
    size_t offset;
    basl_source_id_t source_id;
    basl_token_list_t *tokens;
    basl_diagnostic_list_t *diagnostics;
    basl_error_t *error;
    int had_error;
    basl_error_t last_error;
} basl_lexer_state_t;

static basl_source_span_t basl_lexer_span(
    const basl_lexer_state_t *state,
    size_t start,
    size_t end
) {
    basl_source_span_t span;

    span.source_id = state->source_id;
    span.start_offset = start;
    span.end_offset = end;
    return span;
}

static int basl_lexer_is_at_end(const basl_lexer_state_t *state) {
    return state->offset >= state->length;
}

static char basl_lexer_peek(const basl_lexer_state_t *state) {
    if (basl_lexer_is_at_end(state)) {
        return '\0';
    }

    return state->text[state->offset];
}

static char basl_lexer_peek_next(const basl_lexer_state_t *state) {
    if (state->offset + 1U >= state->length) {
        return '\0';
    }

    return state->text[state->offset + 1U];
}

static char basl_lexer_advance(basl_lexer_state_t *state) {
    char current;

    current = basl_lexer_peek(state);
    if (!basl_lexer_is_at_end(state)) {
        state->offset += 1U;
    }

    return current;
}

static int basl_lexer_match(basl_lexer_state_t *state, char expected) {
    if (basl_lexer_peek(state) != expected) {
        return 0;
    }

    state->offset += 1U;
    return 1;
}

static basl_status_t basl_lexer_emit(
    basl_lexer_state_t *state,
    basl_token_kind_t kind,
    size_t start,
    size_t end
) {
    return basl_token_list_append(
        state->tokens,
        kind,
        basl_lexer_span(state, start, end),
        state->error
    );
}

static basl_status_t basl_lexer_report(
    basl_lexer_state_t *state,
    size_t start,
    size_t end,
    const char *message
) {
    basl_status_t status;

    state->had_error = 1;
    status = basl_diagnostic_list_append_cstr(
        state->diagnostics,
        BASL_DIAGNOSTIC_ERROR,
        basl_lexer_span(state, start, end),
        message,
        state->error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_error_set_literal(state->error, BASL_STATUS_SYNTAX_ERROR, message);
    state->error->location.source_id = state->source_id;
    state->last_error = *state->error;
    return BASL_STATUS_SYNTAX_ERROR;
}

static int basl_lexer_is_prefixed_digit(char ch, char prefix) {
    switch (prefix) {
        case 'x':
        case 'X':
            return isxdigit((unsigned char)ch);
        case 'b':
        case 'B':
            return ch == '0' || ch == '1';
        case 'o':
        case 'O':
            return ch >= '0' && ch <= '7';
        default:
            return 0;
    }
}

static int basl_lexer_is_identifier_start(char ch) {
    return ch == '_' || isalpha((unsigned char)ch);
}

static int basl_lexer_is_identifier_part(char ch) {
    return ch == '_' || isalnum((unsigned char)ch);
}

static basl_token_kind_t basl_lexer_keyword_kind(
    const char *text,
    size_t length
) {
    switch (length) {
        case 2U:
            if (memcmp(text, "as", 2U) == 0) return BASL_TOKEN_AS;
            if (memcmp(text, "fn", 2U) == 0) return BASL_TOKEN_FN;
            if (memcmp(text, "if", 2U) == 0) return BASL_TOKEN_IF;
            break;
        case 3U:
            if (memcmp(text, "for", 3U) == 0) return BASL_TOKEN_FOR;
            if (memcmp(text, "nil", 3U) == 0) return BASL_TOKEN_NIL;
            if (memcmp(text, "pub", 3U) == 0) return BASL_TOKEN_PUB;
            break;
        case 4U:
            if (memcmp(text, "case", 4U) == 0) return BASL_TOKEN_CASE;
            if (memcmp(text, "else", 4U) == 0) return BASL_TOKEN_ELSE;
            if (memcmp(text, "enum", 4U) == 0) return BASL_TOKEN_ENUM;
            if (memcmp(text, "true", 4U) == 0) return BASL_TOKEN_TRUE;
            break;
        case 5U:
            if (memcmp(text, "break", 5U) == 0) return BASL_TOKEN_BREAK;
            if (memcmp(text, "class", 5U) == 0) return BASL_TOKEN_CLASS;
            if (memcmp(text, "const", 5U) == 0) return BASL_TOKEN_CONST;
            if (memcmp(text, "defer", 5U) == 0) return BASL_TOKEN_DEFER;
            if (memcmp(text, "false", 5U) == 0) return BASL_TOKEN_FALSE;
            if (memcmp(text, "while", 5U) == 0) return BASL_TOKEN_WHILE;
            break;
        case 6U:
            if (memcmp(text, "import", 6U) == 0) return BASL_TOKEN_IMPORT;
            if (memcmp(text, "return", 6U) == 0) return BASL_TOKEN_RETURN;
            if (memcmp(text, "switch", 6U) == 0) return BASL_TOKEN_SWITCH;
            break;
        case 7U:
            if (memcmp(text, "default", 7U) == 0) return BASL_TOKEN_DEFAULT;
            break;
        case 8U:
            if (memcmp(text, "continue", 8U) == 0) return BASL_TOKEN_CONTINUE;
            break;
        case 9U:
            if (memcmp(text, "interface", 9U) == 0) return BASL_TOKEN_INTERFACE;
            break;
        default:
            break;
    }

    return BASL_TOKEN_IDENTIFIER;
}

static basl_status_t basl_lexer_scan_identifier(
    basl_lexer_state_t *state,
    size_t start
) {
    while (basl_lexer_is_identifier_part(basl_lexer_peek(state))) {
        basl_lexer_advance(state);
    }

    return basl_lexer_emit(
        state,
        basl_lexer_keyword_kind(state->text + start, state->offset - start),
        start,
        state->offset
    );
}

static basl_status_t basl_lexer_scan_number(
    basl_lexer_state_t *state,
    size_t start
) {
    basl_token_kind_t kind;

    kind = BASL_TOKEN_INT_LITERAL;
    if (state->text[start] == '0') {
        char prefix;
        size_t digits_start;

        prefix = basl_lexer_peek(state);
        if (
            prefix == 'x' || prefix == 'X' ||
            prefix == 'b' || prefix == 'B' ||
            prefix == 'o' || prefix == 'O'
        ) {
            basl_lexer_advance(state);
            digits_start = state->offset;
            while (basl_lexer_is_prefixed_digit(basl_lexer_peek(state), prefix)) {
                basl_lexer_advance(state);
            }

            if (state->offset == digits_start) {
                return basl_lexer_report(
                    state,
                    start,
                    state->offset,
                    "expected digits after numeric base prefix"
                );
            }

            if (isalnum((unsigned char)basl_lexer_peek(state))) {
                while (isalnum((unsigned char)basl_lexer_peek(state))) {
                    basl_lexer_advance(state);
                }
                return basl_lexer_report(
                    state,
                    start,
                    state->offset,
                    "invalid digits for numeric base prefix"
                );
            }

            return basl_lexer_emit(state, kind, start, state->offset);
        }
    }

    while (isdigit((unsigned char)basl_lexer_peek(state))) {
        basl_lexer_advance(state);
    }

    if (basl_lexer_peek(state) == '.' && isdigit((unsigned char)basl_lexer_peek_next(state))) {
        kind = BASL_TOKEN_FLOAT_LITERAL;
        basl_lexer_advance(state);
        while (isdigit((unsigned char)basl_lexer_peek(state))) {
            basl_lexer_advance(state);
        }
    }

    if (basl_lexer_peek(state) == 'e' || basl_lexer_peek(state) == 'E') {
        kind = BASL_TOKEN_FLOAT_LITERAL;
        basl_lexer_advance(state);
        if (basl_lexer_peek(state) == '+' || basl_lexer_peek(state) == '-') {
            basl_lexer_advance(state);
        }
        while (isdigit((unsigned char)basl_lexer_peek(state))) {
            basl_lexer_advance(state);
        }
    }

    return basl_lexer_emit(state, kind, start, state->offset);
}

static basl_status_t basl_lexer_scan_quoted(
    basl_lexer_state_t *state,
    size_t start,
    char quote,
    basl_token_kind_t kind
) {
    while (!basl_lexer_is_at_end(state) && basl_lexer_peek(state) != quote) {
        if (quote != '`' && basl_lexer_peek(state) == '\\') {
            basl_lexer_advance(state);
            if (!basl_lexer_is_at_end(state)) {
                basl_lexer_advance(state);
            }
            continue;
        }

        basl_lexer_advance(state);
    }

    if (basl_lexer_is_at_end(state)) {
        return basl_lexer_report(
            state,
            start,
            state->offset,
            quote == '\'' ? "unterminated character literal" :
            (quote == '`' ? "unterminated raw string literal" :
                            "unterminated string literal")
        );
    }

    basl_lexer_advance(state);
    return basl_lexer_emit(state, kind, start, state->offset);
}

static basl_status_t basl_lexer_skip_block_comment(
    basl_lexer_state_t *state,
    size_t start
) {
    while (!basl_lexer_is_at_end(state)) {
        if (basl_lexer_peek(state) == '*' && basl_lexer_peek_next(state) == '/') {
            basl_lexer_advance(state);
            basl_lexer_advance(state);
            return BASL_STATUS_OK;
        }

        basl_lexer_advance(state);
    }

    return basl_lexer_report(state, start, state->offset, "unterminated block comment");
}

static basl_status_t basl_lexer_scan_token(
    basl_lexer_state_t *state
) {
    basl_status_t status;
    size_t start;
    char ch;

    start = state->offset;
    ch = basl_lexer_advance(state);
    switch (ch) {
        case ' ':
        case '\r':
        case '\t':
        case '\n':
            return BASL_STATUS_OK;
        case '(':
            return basl_lexer_emit(state, BASL_TOKEN_LPAREN, start, state->offset);
        case ')':
            return basl_lexer_emit(state, BASL_TOKEN_RPAREN, start, state->offset);
        case '{':
            return basl_lexer_emit(state, BASL_TOKEN_LBRACE, start, state->offset);
        case '}':
            return basl_lexer_emit(state, BASL_TOKEN_RBRACE, start, state->offset);
        case '[':
            return basl_lexer_emit(state, BASL_TOKEN_LBRACKET, start, state->offset);
        case ']':
            return basl_lexer_emit(state, BASL_TOKEN_RBRACKET, start, state->offset);
        case ',':
            return basl_lexer_emit(state, BASL_TOKEN_COMMA, start, state->offset);
        case '.':
            return basl_lexer_emit(state, BASL_TOKEN_DOT, start, state->offset);
        case ';':
            return basl_lexer_emit(state, BASL_TOKEN_SEMICOLON, start, state->offset);
        case ':':
            return basl_lexer_emit(state, BASL_TOKEN_COLON, start, state->offset);
        case '?':
            return basl_lexer_emit(state, BASL_TOKEN_QUESTION, start, state->offset);
        case '~':
            return basl_lexer_emit(state, BASL_TOKEN_TILDE, start, state->offset);
        case '+':
            if (basl_lexer_match(state, '+')) {
                return basl_lexer_emit(state, BASL_TOKEN_PLUS_PLUS, start, state->offset);
            }
            if (basl_lexer_match(state, '=')) {
                return basl_lexer_emit(state, BASL_TOKEN_PLUS_ASSIGN, start, state->offset);
            }
            return basl_lexer_emit(state, BASL_TOKEN_PLUS, start, state->offset);
        case '-':
            if (basl_lexer_match(state, '>')) {
                return basl_lexer_emit(state, BASL_TOKEN_ARROW, start, state->offset);
            }
            if (basl_lexer_match(state, '-')) {
                return basl_lexer_emit(state, BASL_TOKEN_MINUS_MINUS, start, state->offset);
            }
            if (basl_lexer_match(state, '=')) {
                return basl_lexer_emit(state, BASL_TOKEN_MINUS_ASSIGN, start, state->offset);
            }
            return basl_lexer_emit(state, BASL_TOKEN_MINUS, start, state->offset);
        case '*':
            if (basl_lexer_match(state, '=')) {
                return basl_lexer_emit(state, BASL_TOKEN_STAR_ASSIGN, start, state->offset);
            }
            return basl_lexer_emit(state, BASL_TOKEN_STAR, start, state->offset);
        case '/':
            if (basl_lexer_match(state, '/')) {
                while (!basl_lexer_is_at_end(state) && basl_lexer_peek(state) != '\n') {
                    basl_lexer_advance(state);
                }
                return BASL_STATUS_OK;
            }
            if (basl_lexer_match(state, '*')) {
                return basl_lexer_skip_block_comment(state, start);
            }
            if (basl_lexer_match(state, '=')) {
                return basl_lexer_emit(state, BASL_TOKEN_SLASH_ASSIGN, start, state->offset);
            }
            return basl_lexer_emit(state, BASL_TOKEN_SLASH, start, state->offset);
        case '%':
            if (basl_lexer_match(state, '=')) {
                return basl_lexer_emit(state, BASL_TOKEN_PERCENT_ASSIGN, start, state->offset);
            }
            return basl_lexer_emit(state, BASL_TOKEN_PERCENT, start, state->offset);
        case '=':
            if (basl_lexer_match(state, '=')) {
                return basl_lexer_emit(state, BASL_TOKEN_EQUAL_EQUAL, start, state->offset);
            }
            return basl_lexer_emit(state, BASL_TOKEN_ASSIGN, start, state->offset);
        case '!':
            if (basl_lexer_match(state, '=')) {
                return basl_lexer_emit(state, BASL_TOKEN_BANG_EQUAL, start, state->offset);
            }
            return basl_lexer_emit(state, BASL_TOKEN_BANG, start, state->offset);
        case '<':
            if (basl_lexer_match(state, '<')) {
                return basl_lexer_emit(state, BASL_TOKEN_SHIFT_LEFT, start, state->offset);
            }
            if (basl_lexer_match(state, '=')) {
                return basl_lexer_emit(state, BASL_TOKEN_LESS_EQUAL, start, state->offset);
            }
            return basl_lexer_emit(state, BASL_TOKEN_LESS, start, state->offset);
        case '>':
            if (basl_lexer_match(state, '>')) {
                return basl_lexer_emit(state, BASL_TOKEN_SHIFT_RIGHT, start, state->offset);
            }
            if (basl_lexer_match(state, '=')) {
                return basl_lexer_emit(state, BASL_TOKEN_GREATER_EQUAL, start, state->offset);
            }
            return basl_lexer_emit(state, BASL_TOKEN_GREATER, start, state->offset);
        case '&':
            if (basl_lexer_match(state, '&')) {
                return basl_lexer_emit(state, BASL_TOKEN_AMPERSAND_AMPERSAND, start, state->offset);
            }
            return basl_lexer_emit(state, BASL_TOKEN_AMPERSAND, start, state->offset);
        case '|':
            if (basl_lexer_match(state, '|')) {
                return basl_lexer_emit(state, BASL_TOKEN_PIPE_PIPE, start, state->offset);
            }
            return basl_lexer_emit(state, BASL_TOKEN_PIPE, start, state->offset);
        case '^':
            return basl_lexer_emit(state, BASL_TOKEN_CARET, start, state->offset);
        case '"':
            return basl_lexer_scan_quoted(state, start, '"', BASL_TOKEN_STRING_LITERAL);
        case '\'':
            return basl_lexer_scan_quoted(state, start, '\'', BASL_TOKEN_CHAR_LITERAL);
        case '`':
            return basl_lexer_scan_quoted(state, start, '`', BASL_TOKEN_RAW_STRING_LITERAL);
        case 'f':
            if (basl_lexer_peek(state) == '"') {
                basl_lexer_advance(state);
                return basl_lexer_scan_quoted(state, start, '"', BASL_TOKEN_FSTRING_LITERAL);
            }
            return basl_lexer_scan_identifier(state, start);
        default:
            if (isdigit((unsigned char)ch)) {
                return basl_lexer_scan_number(state, start);
            }
            if (basl_lexer_is_identifier_start(ch)) {
                return basl_lexer_scan_identifier(state, start);
            }
            status = basl_lexer_report(state, start, state->offset, "unexpected character");
            if (status != BASL_STATUS_SYNTAX_ERROR) {
                return status;
            }
            return BASL_STATUS_OK;
    }
}

basl_status_t basl_lex_source(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_token_list_t *tokens,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
) {
    basl_lexer_state_t state;
    const basl_source_file_t *source;
    basl_status_t status;

    basl_error_clear(error);
    if (registry == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "source registry must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (tokens == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "token list must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (diagnostics == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "diagnostic list must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    source = basl_source_registry_get(registry, source_id);
    if (source == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "source_id must reference a registered source file"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    basl_token_list_clear(tokens);
    memset(&state, 0, sizeof(state));
    state.text = basl_string_c_str(&source->text);
    state.length = basl_string_length(&source->text);
    state.source_id = source_id;
    state.tokens = tokens;
    state.diagnostics = diagnostics;
    state.error = error;

    while (!basl_lexer_is_at_end(&state)) {
        status = basl_lexer_scan_token(&state);
        if (status != BASL_STATUS_OK && status != BASL_STATUS_SYNTAX_ERROR) {
            return status;
        }
    }

    if (state.had_error) {
        status = basl_lexer_emit(&state, BASL_TOKEN_EOF, state.offset, state.offset);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        *error = state.last_error;
        return BASL_STATUS_SYNTAX_ERROR;
    }

    status = basl_lexer_emit(&state, BASL_TOKEN_EOF, state.offset, state.offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_error_clear(error);
    return BASL_STATUS_OK;
}
