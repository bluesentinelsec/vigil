#include <string.h>
#include "internal/basl_compiler_types.h"
#include "internal/basl_internal.h"

basl_status_t basl_parser_emit_default_value(
    basl_parser_state_t *state,
    basl_parser_type_t type,
    basl_source_span_t span
) {
    if (state == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (basl_parser_type_is_bool(type)) {
        return basl_parser_emit_opcode(state, BASL_OPCODE_FALSE, span);
    }
    if (basl_parser_type_is_integer(type) || basl_parser_type_is_enum(type)) {
        return basl_parser_emit_integer_constant(state, type, 0, span);
    }
    if (basl_parser_type_is_f64(type)) {
        return basl_parser_emit_f64_constant(state, 0.0, span);
    }
    if (basl_parser_type_is_string(type)) {
        return basl_parser_emit_string_constant_text(state, span, "", 0U);
    }
    if (basl_parser_type_is_err(type)) {
        return basl_parser_emit_ok_constant(state, span);
    }

    return basl_parser_emit_opcode(state, BASL_OPCODE_NIL, span);
}

basl_status_t basl_parser_parse_string_method_call(
    basl_parser_state_t *state,
    const basl_token_t *method_token,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t arg_result;
    basl_parser_type_t array_type;
    const char *method_name;
    size_t method_length;

    basl_expression_result_clear(&arg_result);
    array_type = basl_binding_type_invalid();
    method_name = basl_parser_token_text(state, method_token, &method_length);

    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after string method name",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (basl_program_names_equal(method_name, method_length, "len", 3U)) {
        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "string len() does not accept arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_STRING_SIZE, method_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, basl_binding_type_primitive(BASL_TYPE_I32));
        return BASL_STATUS_OK;
    }

    if (basl_program_names_equal(method_name, method_length, "trim", 4U) ||
        basl_program_names_equal(method_name, method_length, "to_upper", 8U) ||
        basl_program_names_equal(method_name, method_length, "to_lower", 8U) ||
        basl_program_names_equal(method_name, method_length, "bytes", 5U)) {
        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "string method does not accept arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (basl_program_names_equal(method_name, method_length, "trim", 4U)) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_STRING_TRIM, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_STRING)
            );
            return BASL_STATUS_OK;
        }
        if (basl_program_names_equal(method_name, method_length, "to_upper", 8U)) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_STRING_TO_UPPER, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_STRING)
            );
            return BASL_STATUS_OK;
        }
        if (basl_program_names_equal(method_name, method_length, "to_lower", 8U)) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_STRING_TO_LOWER, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_STRING)
            );
            return BASL_STATUS_OK;
        }
        status = basl_program_intern_array_type(
            (basl_program_state_t *)state->program,
            basl_binding_type_primitive(BASL_TYPE_U8),
            &array_type
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_STRING_BYTES, method_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, array_type);
        return BASL_STATUS_OK;
    }

    status = basl_parser_parse_expression(state, &arg_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_scalar_expression(
        state,
        method_token->span,
        &arg_result,
        "string method arguments must be single values"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (basl_program_names_equal(method_name, method_length, "contains", 8U) ||
        basl_program_names_equal(method_name, method_length, "starts_with", 11U) ||
        basl_program_names_equal(method_name, method_length, "ends_with", 9U) ||
        basl_program_names_equal(method_name, method_length, "index_of", 8U) ||
        basl_program_names_equal(method_name, method_length, "split", 5U)) {
        status = basl_parser_require_type(
            state,
            method_token->span,
            arg_result.type,
            basl_binding_type_primitive(BASL_TYPE_STRING),
            "string method argument must be string"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "expected ')' after string method arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (basl_program_names_equal(method_name, method_length, "contains", 8U)) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_STRING_CONTAINS, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_BOOL)
            );
            return BASL_STATUS_OK;
        }
        if (basl_program_names_equal(method_name, method_length, "starts_with", 11U)) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_STRING_STARTS_WITH, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_BOOL)
            );
            return BASL_STATUS_OK;
        }
        if (basl_program_names_equal(method_name, method_length, "ends_with", 9U)) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_STRING_ENDS_WITH, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_BOOL)
            );
            return BASL_STATUS_OK;
        }
        if (basl_program_names_equal(method_name, method_length, "split", 5U)) {
            status = basl_program_intern_array_type(
                (basl_program_state_t *)state->program,
                basl_binding_type_primitive(BASL_TYPE_STRING),
                &array_type
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_STRING_SPLIT, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, array_type);
            return BASL_STATUS_OK;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_STRING_INDEX_OF, method_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_pair(
            out_result,
            basl_binding_type_primitive(BASL_TYPE_I32),
            basl_binding_type_primitive(BASL_TYPE_BOOL)
        );
        return BASL_STATUS_OK;
    }

    if (basl_program_names_equal(method_name, method_length, "replace", 7U)) {
        basl_expression_result_t second_arg;

        basl_expression_result_clear(&second_arg);
        status = basl_parser_require_type(
            state,
            method_token->span,
            arg_result.type,
            basl_binding_type_primitive(BASL_TYPE_STRING),
            "string replace() arguments must be strings"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_expect(
            state,
            BASL_TOKEN_COMMA,
            "string replace() expects two arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_parse_expression(state, &second_arg);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            method_token->span,
            &second_arg,
            "string method arguments must be single values"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_type(
            state,
            method_token->span,
            second_arg.type,
            basl_binding_type_primitive(BASL_TYPE_STRING),
            "string replace() arguments must be strings"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "expected ')' after string method arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_STRING_REPLACE, method_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, basl_binding_type_primitive(BASL_TYPE_STRING));
        return BASL_STATUS_OK;
    }

    if (basl_program_names_equal(method_name, method_length, "substr", 6U) ||
        basl_program_names_equal(method_name, method_length, "char_at", 7U)) {
        basl_expression_result_t second_arg;

        basl_expression_result_clear(&second_arg);
        status = basl_parser_require_type(
            state,
            method_token->span,
            arg_result.type,
            basl_binding_type_primitive(BASL_TYPE_I32),
            "string index arguments must be i32"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (basl_program_names_equal(method_name, method_length, "substr", 6U)) {
            status = basl_parser_expect(
                state,
                BASL_TOKEN_COMMA,
                "string substr() expects two arguments",
                NULL
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_parse_expression(state, &second_arg);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_scalar_expression(
                state,
                method_token->span,
                &second_arg,
                "string method arguments must be single values"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_type(
                state,
                method_token->span,
                second_arg.type,
                basl_binding_type_primitive(BASL_TYPE_I32),
                "string index arguments must be i32"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
        }
        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "expected ')' after string method arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(
            state,
            basl_program_names_equal(method_name, method_length, "substr", 6U)
                ? BASL_OPCODE_STRING_SUBSTR
                : BASL_OPCODE_STRING_CHAR_AT,
            method_token->span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_pair(
            out_result,
            basl_binding_type_primitive(BASL_TYPE_STRING),
            basl_binding_type_primitive(BASL_TYPE_ERR)
        );
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, method_token->span, "unknown string method");
}

basl_status_t basl_parser_parse_array_method_call(
    basl_parser_state_t *state,
    basl_parser_type_t receiver_type,
    const basl_token_t *method_token,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t first_arg;
    basl_expression_result_t second_arg;
    basl_parser_type_t element_type;
    const char *method_name;
    size_t method_length;

    basl_expression_result_clear(&first_arg);
    basl_expression_result_clear(&second_arg);
    element_type = basl_program_array_type_element(state->program, receiver_type);
    method_name = basl_parser_token_text(state, method_token, &method_length);

    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after array method name",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (basl_program_names_equal(method_name, method_length, "len", 3U) ||
        basl_program_names_equal(method_name, method_length, "pop", 3U)) {
        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "array method does not accept arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (basl_program_names_equal(method_name, method_length, "len", 3U)) {
            status = basl_parser_emit_opcode(
                state,
                BASL_OPCODE_GET_COLLECTION_SIZE,
                method_token->span
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_I32)
            );
            return BASL_STATUS_OK;
        }

        status = basl_parser_emit_default_value(state, element_type, method_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_ARRAY_POP, method_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_pair(
            out_result,
            element_type,
            basl_binding_type_primitive(BASL_TYPE_ERR)
        );
        return BASL_STATUS_OK;
    }

    if (basl_program_names_equal(method_name, method_length, "push", 4U) ||
        basl_program_names_equal(method_name, method_length, "get", 3U) ||
        basl_program_names_equal(method_name, method_length, "contains", 8U)) {
        status = basl_parser_parse_expression(state, &first_arg);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            method_token->span,
            &first_arg,
            "array method arguments must be single values"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "expected ')' after array method arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        if (basl_program_names_equal(method_name, method_length, "push", 4U)) {
            status = basl_parser_require_type(
                state,
                method_token->span,
                first_arg.type,
                element_type,
                "array push() argument must match array element type"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_ARRAY_PUSH, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_VOID)
            );
            return BASL_STATUS_OK;
        }

        if (basl_program_names_equal(method_name, method_length, "get", 3U)) {
            status = basl_parser_require_type(
                state,
                method_token->span,
                first_arg.type,
                basl_binding_type_primitive(BASL_TYPE_I32),
                "array get() index must be i32"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_default_value(state, element_type, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_opcode(
                state,
                BASL_OPCODE_ARRAY_GET_SAFE,
                method_token->span
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_pair(
                out_result,
                element_type,
                basl_binding_type_primitive(BASL_TYPE_ERR)
            );
            return BASL_STATUS_OK;
        }

        status = basl_parser_require_type(
            state,
            method_token->span,
            first_arg.type,
            element_type,
            "array contains() argument must match array element type"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_ARRAY_CONTAINS, method_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(
            out_result,
            basl_binding_type_primitive(BASL_TYPE_BOOL)
        );
        return BASL_STATUS_OK;
    }

    if (basl_program_names_equal(method_name, method_length, "set", 3U) ||
        basl_program_names_equal(method_name, method_length, "slice", 5U)) {
        status = basl_parser_parse_expression(state, &first_arg);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            method_token->span,
            &first_arg,
            "array method arguments must be single values"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_expect(
            state,
            BASL_TOKEN_COMMA,
            "array method expects two arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_parse_expression(state, &second_arg);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            method_token->span,
            &second_arg,
            "array method arguments must be single values"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "expected ')' after array method arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_require_type(
            state,
            method_token->span,
            first_arg.type,
            basl_binding_type_primitive(BASL_TYPE_I32),
            basl_program_names_equal(method_name, method_length, "set", 3U)
                ? "array set() index must be i32"
                : "array slice() start and end must be i32"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (basl_program_names_equal(method_name, method_length, "set", 3U)) {
            status = basl_parser_require_type(
                state,
                method_token->span,
                second_arg.type,
                element_type,
                "array set() value must match array element type"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_opcode(
                state,
                BASL_OPCODE_ARRAY_SET_SAFE,
                method_token->span
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_ERR)
            );
            return BASL_STATUS_OK;
        }

        status = basl_parser_require_type(
            state,
            method_token->span,
            second_arg.type,
            basl_binding_type_primitive(BASL_TYPE_I32),
            "array slice() start and end must be i32"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_ARRAY_SLICE, method_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, receiver_type);
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, method_token->span, "unknown array method");
}

basl_status_t basl_parser_parse_map_method_call(
    basl_parser_state_t *state,
    basl_parser_type_t receiver_type,
    const basl_token_t *method_token,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t first_arg;
    basl_expression_result_t second_arg;
    basl_parser_type_t key_type;
    basl_parser_type_t value_type;
    basl_parser_type_t array_type;
    const char *method_name;
    size_t method_length;

    basl_expression_result_clear(&first_arg);
    basl_expression_result_clear(&second_arg);
    key_type = basl_program_map_type_key(state->program, receiver_type);
    value_type = basl_program_map_type_value(state->program, receiver_type);
    array_type = basl_binding_type_invalid();
    method_name = basl_parser_token_text(state, method_token, &method_length);

    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after map method name",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (basl_program_names_equal(method_name, method_length, "len", 3U) ||
        basl_program_names_equal(method_name, method_length, "keys", 4U) ||
        basl_program_names_equal(method_name, method_length, "values", 6U)) {
        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "map method does not accept arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (basl_program_names_equal(method_name, method_length, "len", 3U)) {
            status = basl_parser_emit_opcode(
                state,
                BASL_OPCODE_GET_COLLECTION_SIZE,
                method_token->span
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_I32)
            );
            return BASL_STATUS_OK;
        }
        status = basl_program_intern_array_type(
            (basl_program_state_t *)state->program,
            basl_program_names_equal(method_name, method_length, "keys", 4U)
                ? key_type
                : value_type,
            &array_type
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(
            state,
            basl_program_names_equal(method_name, method_length, "keys", 4U)
                ? BASL_OPCODE_MAP_KEYS
                : BASL_OPCODE_MAP_VALUES,
            method_token->span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, array_type);
        return BASL_STATUS_OK;
    }

    status = basl_parser_parse_expression(state, &first_arg);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_scalar_expression(
        state,
        method_token->span,
        &first_arg,
        "map method arguments must be single values"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_type(
        state,
        method_token->span,
        first_arg.type,
        key_type,
        "map method key must match map key type"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (basl_program_names_equal(method_name, method_length, "get", 3U) ||
        basl_program_names_equal(method_name, method_length, "remove", 6U) ||
        basl_program_names_equal(method_name, method_length, "has", 3U)) {
        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "expected ')' after map method arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        if (basl_program_names_equal(method_name, method_length, "get", 3U)) {
            status = basl_parser_emit_default_value(state, value_type, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_MAP_GET_SAFE, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_pair(
                out_result,
                value_type,
                basl_binding_type_primitive(BASL_TYPE_BOOL)
            );
            return BASL_STATUS_OK;
        }
        if (basl_program_names_equal(method_name, method_length, "remove", 6U)) {
            status = basl_parser_emit_default_value(state, value_type, method_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_opcode(
                state,
                BASL_OPCODE_MAP_REMOVE_SAFE,
                method_token->span
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_pair(
                out_result,
                value_type,
                basl_binding_type_primitive(BASL_TYPE_BOOL)
            );
            return BASL_STATUS_OK;
        }

        status = basl_parser_emit_opcode(state, BASL_OPCODE_MAP_HAS, method_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(
            out_result,
            basl_binding_type_primitive(BASL_TYPE_BOOL)
        );
        return BASL_STATUS_OK;
    }

    if (basl_program_names_equal(method_name, method_length, "set", 3U)) {
        status = basl_parser_expect(
            state,
            BASL_TOKEN_COMMA,
            "map set() expects two arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_parse_expression(state, &second_arg);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            method_token->span,
            &second_arg,
            "map method arguments must be single values"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_type(
            state,
            method_token->span,
            second_arg.type,
            value_type,
            "map set() value must match map value type"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "expected ')' after map method arguments",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_MAP_SET_SAFE, method_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(
            out_result,
            basl_binding_type_primitive(BASL_TYPE_ERR)
        );
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, method_token->span, "unknown map method");
}
