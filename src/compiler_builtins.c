#include <string.h>
#include "internal/vigil_compiler_types.h"
#include "internal/vigil_internal.h"

vigil_status_t vigil_parser_emit_default_value(
    vigil_parser_state_t *state,
    vigil_parser_type_t type,
    vigil_source_span_t span
) {
    if (state == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_parser_type_is_bool(type)) {
        return vigil_parser_emit_opcode(state, VIGIL_OPCODE_FALSE, span);
    }
    if (vigil_parser_type_is_integer(type) || vigil_parser_type_is_enum(type)) {
        return vigil_parser_emit_integer_constant(state, type, 0, span);
    }
    if (vigil_parser_type_is_f64(type)) {
        return vigil_parser_emit_f64_constant(state, 0.0, span);
    }
    if (vigil_parser_type_is_string(type)) {
        return vigil_parser_emit_string_constant_text(state, span, "", 0U);
    }
    if (vigil_parser_type_is_err(type)) {
        return vigil_parser_emit_ok_constant(state, span);
    }

    return vigil_parser_emit_opcode(state, VIGIL_OPCODE_NIL, span);
}

vigil_status_t vigil_parser_parse_string_method_call(
    vigil_parser_state_t *state,
    const vigil_token_t *method_token,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t arg_result;
    vigil_parser_type_t array_type;
    const char *method_name;
    size_t method_length;

    vigil_expression_result_clear(&arg_result);
    array_type = vigil_binding_type_invalid();
    method_name = vigil_parser_token_text(state, method_token, &method_length);

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after string method name",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "len", 3U)) {
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "string len() does not accept arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_STRING_SIZE, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_I32));
        return VIGIL_STATUS_OK;
    }

    if (vigil_program_names_equal(method_name, method_length, "trim", 4U) ||
        vigil_program_names_equal(method_name, method_length, "to_upper", 8U) ||
        vigil_program_names_equal(method_name, method_length, "to_lower", 8U) ||
        vigil_program_names_equal(method_name, method_length, "bytes", 5U) ||
        vigil_program_names_equal(method_name, method_length, "trim_left", 9U) ||
        vigil_program_names_equal(method_name, method_length, "trim_right", 10U) ||
        vigil_program_names_equal(method_name, method_length, "reverse", 7U) ||
        vigil_program_names_equal(method_name, method_length, "is_empty", 8U) ||
        vigil_program_names_equal(method_name, method_length, "char_count", 10U) ||
        vigil_program_names_equal(method_name, method_length, "to_c", 4U) ||
        vigil_program_names_equal(method_name, method_length, "fields", 6U)) {
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "string method does not accept arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "trim", 4U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_TRIM, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "to_upper", 8U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_TO_UPPER, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "to_lower", 8U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_TO_LOWER, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "trim_left", 9U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_TRIM_LEFT, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "trim_right", 10U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_TRIM_RIGHT, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "reverse", 7U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_REVERSE, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "is_empty", 8U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_IS_EMPTY, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "char_count", 10U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_CHAR_COUNT, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_I32)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "to_c", 4U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_TO_C, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "fields", 6U)) {
            status = vigil_program_intern_array_type(
                (vigil_program_state_t *)state->program,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING),
                &array_type
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_FIELDS, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(out_result, array_type);
            return VIGIL_STATUS_OK;
        }
        status = vigil_program_intern_array_type(
            (vigil_program_state_t *)state->program,
            vigil_binding_type_primitive(VIGIL_TYPE_U8),
            &array_type
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_BYTES, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(out_result, array_type);
        return VIGIL_STATUS_OK;
    }

    status = vigil_parser_parse_expression(state, &arg_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_scalar_expression(
        state,
        method_token->span,
        &arg_result,
        "string method arguments must be single values"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "contains", 8U) ||
        vigil_program_names_equal(method_name, method_length, "starts_with", 11U) ||
        vigil_program_names_equal(method_name, method_length, "ends_with", 9U) ||
        vigil_program_names_equal(method_name, method_length, "index_of", 8U) ||
        vigil_program_names_equal(method_name, method_length, "split", 5U) ||
        vigil_program_names_equal(method_name, method_length, "count", 5U) ||
        vigil_program_names_equal(method_name, method_length, "last_index_of", 13U) ||
        vigil_program_names_equal(method_name, method_length, "trim_prefix", 11U) ||
        vigil_program_names_equal(method_name, method_length, "trim_suffix", 11U) ||
        vigil_program_names_equal(method_name, method_length, "equal_fold", 10U) ||
        vigil_program_names_equal(method_name, method_length, "cut", 3U)) {
        status = vigil_parser_require_type(
            state,
            method_token->span,
            arg_result.type,
            vigil_binding_type_primitive(VIGIL_TYPE_STRING),
            "string method argument must be string"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "expected ')' after string method arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "contains", 8U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_CONTAINS, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "starts_with", 11U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_STARTS_WITH, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "ends_with", 9U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_ENDS_WITH, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "split", 5U)) {
            status = vigil_program_intern_array_type(
                (vigil_program_state_t *)state->program,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING),
                &array_type
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_SPLIT, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(out_result, array_type);
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "count", 5U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_COUNT, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_I32)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "last_index_of", 13U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_LAST_INDEX_OF, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_pair(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_I32),
                vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "trim_prefix", 11U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_TRIM_PREFIX, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "trim_suffix", 11U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_TRIM_SUFFIX, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "equal_fold", 10U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_EQUAL_FOLD, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "cut", 3U)) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_CUT, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_triple(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING),
                vigil_binding_type_primitive(VIGIL_TYPE_STRING),
                vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
            );
            return VIGIL_STATUS_OK;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_INDEX_OF, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_pair(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_I32),
            vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
        );
        return VIGIL_STATUS_OK;
    }

    if (vigil_program_names_equal(method_name, method_length, "replace", 7U)) {
        vigil_expression_result_t second_arg;

        vigil_expression_result_clear(&second_arg);
        status = vigil_parser_require_type(
            state,
            method_token->span,
            arg_result.type,
            vigil_binding_type_primitive(VIGIL_TYPE_STRING),
            "string replace() arguments must be strings"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_COMMA,
            "string replace() expects two arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_parse_expression(state, &second_arg);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            method_token->span,
            &second_arg,
            "string method arguments must be single values"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_type(
            state,
            method_token->span,
            second_arg.type,
            vigil_binding_type_primitive(VIGIL_TYPE_STRING),
            "string replace() arguments must be strings"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "expected ')' after string method arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_REPLACE, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_STRING));
        return VIGIL_STATUS_OK;
    }

    if (vigil_program_names_equal(method_name, method_length, "substr", 6U) ||
        vigil_program_names_equal(method_name, method_length, "char_at", 7U)) {
        vigil_expression_result_t second_arg;

        vigil_expression_result_clear(&second_arg);
        status = vigil_parser_require_type(
            state,
            method_token->span,
            arg_result.type,
            vigil_binding_type_primitive(VIGIL_TYPE_I32),
            "string index arguments must be i32"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "substr", 6U)) {
            status = vigil_parser_expect(
                state,
                VIGIL_TOKEN_COMMA,
                "string substr() expects two arguments",
                NULL
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_parse_expression(state, &second_arg);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state,
                method_token->span,
                &second_arg,
                "string method arguments must be single values"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_type(
                state,
                method_token->span,
                second_arg.type,
                vigil_binding_type_primitive(VIGIL_TYPE_I32),
                "string index arguments must be i32"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
        }
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "expected ')' after string method arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(
            state,
            vigil_program_names_equal(method_name, method_length, "substr", 6U)
                ? VIGIL_OPCODE_STRING_SUBSTR
                : VIGIL_OPCODE_STRING_CHAR_AT,
            method_token->span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_pair(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_STRING),
            vigil_binding_type_primitive(VIGIL_TYPE_ERR)
        );
        return VIGIL_STATUS_OK;
    }

    if (vigil_program_names_equal(method_name, method_length, "repeat", 6U)) {
        status = vigil_parser_require_type(
            state,
            method_token->span,
            arg_result.type,
            vigil_binding_type_primitive(VIGIL_TYPE_I32),
            "string repeat() argument must be i32"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "expected ')' after string method arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_REPEAT, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_STRING)
        );
        return VIGIL_STATUS_OK;
    }

    if (vigil_program_names_equal(method_name, method_length, "join", 4U)) {
        vigil_parser_type_t expected_array;

        status = vigil_program_intern_array_type(
            (vigil_program_state_t *)state->program,
            vigil_binding_type_primitive(VIGIL_TYPE_STRING),
            &expected_array
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_type(
            state,
            method_token->span,
            arg_result.type,
            expected_array,
            "string join() argument must be array<string>"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "expected ')' after string method arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_JOIN, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_STRING)
        );
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_report(state, method_token->span, "unknown string method");
}

vigil_status_t vigil_parser_parse_array_method_call(
    vigil_parser_state_t *state,
    vigil_parser_type_t receiver_type,
    const vigil_token_t *method_token,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t first_arg;
    vigil_expression_result_t second_arg;
    vigil_parser_type_t element_type;
    const char *method_name;
    size_t method_length;

    vigil_expression_result_clear(&first_arg);
    vigil_expression_result_clear(&second_arg);
    element_type = vigil_program_array_type_element(state->program, receiver_type);
    method_name = vigil_parser_token_text(state, method_token, &method_length);

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after array method name",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "len", 3U) ||
        vigil_program_names_equal(method_name, method_length, "pop", 3U)) {
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "array method does not accept arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "len", 3U)) {
            status = vigil_parser_emit_opcode(
                state,
                VIGIL_OPCODE_GET_COLLECTION_SIZE,
                method_token->span
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_I32)
            );
            return VIGIL_STATUS_OK;
        }

        status = vigil_parser_emit_default_value(state, element_type, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_POP, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_pair(
            out_result,
            element_type,
            vigil_binding_type_primitive(VIGIL_TYPE_ERR)
        );
        return VIGIL_STATUS_OK;
    }

    if (vigil_program_names_equal(method_name, method_length, "push", 4U) ||
        vigil_program_names_equal(method_name, method_length, "get", 3U) ||
        vigil_program_names_equal(method_name, method_length, "contains", 8U)) {
        status = vigil_parser_parse_expression(state, &first_arg);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            method_token->span,
            &first_arg,
            "array method arguments must be single values"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "expected ')' after array method arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        if (vigil_program_names_equal(method_name, method_length, "push", 4U)) {
            status = vigil_parser_require_type(
                state,
                method_token->span,
                first_arg.type,
                element_type,
                "array push() argument must match array element type"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_PUSH, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_VOID)
            );
            return VIGIL_STATUS_OK;
        }

        if (vigil_program_names_equal(method_name, method_length, "get", 3U)) {
            status = vigil_parser_require_type(
                state,
                method_token->span,
                first_arg.type,
                vigil_binding_type_primitive(VIGIL_TYPE_I32),
                "array get() index must be i32"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_default_value(state, element_type, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_opcode(
                state,
                VIGIL_OPCODE_ARRAY_GET_SAFE,
                method_token->span
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_pair(
                out_result,
                element_type,
                vigil_binding_type_primitive(VIGIL_TYPE_ERR)
            );
            return VIGIL_STATUS_OK;
        }

        status = vigil_parser_require_type(
            state,
            method_token->span,
            first_arg.type,
            element_type,
            "array contains() argument must match array element type"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_CONTAINS, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
        );
        return VIGIL_STATUS_OK;
    }

    if (vigil_program_names_equal(method_name, method_length, "set", 3U) ||
        vigil_program_names_equal(method_name, method_length, "slice", 5U)) {
        status = vigil_parser_parse_expression(state, &first_arg);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            method_token->span,
            &first_arg,
            "array method arguments must be single values"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_COMMA,
            "array method expects two arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_parse_expression(state, &second_arg);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            method_token->span,
            &second_arg,
            "array method arguments must be single values"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "expected ')' after array method arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_require_type(
            state,
            method_token->span,
            first_arg.type,
            vigil_binding_type_primitive(VIGIL_TYPE_I32),
            vigil_program_names_equal(method_name, method_length, "set", 3U)
                ? "array set() index must be i32"
                : "array slice() start and end must be i32"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "set", 3U)) {
            status = vigil_parser_require_type(
                state,
                method_token->span,
                second_arg.type,
                element_type,
                "array set() value must match array element type"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_opcode(
                state,
                VIGIL_OPCODE_ARRAY_SET_SAFE,
                method_token->span
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_ERR)
            );
            return VIGIL_STATUS_OK;
        }

        status = vigil_parser_require_type(
            state,
            method_token->span,
            second_arg.type,
            vigil_binding_type_primitive(VIGIL_TYPE_I32),
            "array slice() start and end must be i32"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_SLICE, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(out_result, receiver_type);
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_report(state, method_token->span, "unknown array method");
}

vigil_status_t vigil_parser_parse_map_method_call(
    vigil_parser_state_t *state,
    vigil_parser_type_t receiver_type,
    const vigil_token_t *method_token,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t first_arg;
    vigil_expression_result_t second_arg;
    vigil_parser_type_t key_type;
    vigil_parser_type_t value_type;
    vigil_parser_type_t array_type;
    const char *method_name;
    size_t method_length;

    vigil_expression_result_clear(&first_arg);
    vigil_expression_result_clear(&second_arg);
    key_type = vigil_program_map_type_key(state->program, receiver_type);
    value_type = vigil_program_map_type_value(state->program, receiver_type);
    array_type = vigil_binding_type_invalid();
    method_name = vigil_parser_token_text(state, method_token, &method_length);

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after map method name",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "len", 3U) ||
        vigil_program_names_equal(method_name, method_length, "keys", 4U) ||
        vigil_program_names_equal(method_name, method_length, "values", 6U)) {
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "map method does not accept arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "len", 3U)) {
            status = vigil_parser_emit_opcode(
                state,
                VIGIL_OPCODE_GET_COLLECTION_SIZE,
                method_token->span
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_I32)
            );
            return VIGIL_STATUS_OK;
        }
        status = vigil_program_intern_array_type(
            (vigil_program_state_t *)state->program,
            vigil_program_names_equal(method_name, method_length, "keys", 4U)
                ? key_type
                : value_type,
            &array_type
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(
            state,
            vigil_program_names_equal(method_name, method_length, "keys", 4U)
                ? VIGIL_OPCODE_MAP_KEYS
                : VIGIL_OPCODE_MAP_VALUES,
            method_token->span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(out_result, array_type);
        return VIGIL_STATUS_OK;
    }

    status = vigil_parser_parse_expression(state, &first_arg);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_scalar_expression(
        state,
        method_token->span,
        &first_arg,
        "map method arguments must be single values"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_type(
        state,
        method_token->span,
        first_arg.type,
        key_type,
        "map method key must match map key type"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "get", 3U) ||
        vigil_program_names_equal(method_name, method_length, "remove", 6U) ||
        vigil_program_names_equal(method_name, method_length, "has", 3U)) {
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "expected ')' after map method arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        if (vigil_program_names_equal(method_name, method_length, "get", 3U)) {
            status = vigil_parser_emit_default_value(state, value_type, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_MAP_GET_SAFE, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_pair(
                out_result,
                value_type,
                vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
            );
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_names_equal(method_name, method_length, "remove", 6U)) {
            status = vigil_parser_emit_default_value(state, value_type, method_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_opcode(
                state,
                VIGIL_OPCODE_MAP_REMOVE_SAFE,
                method_token->span
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_pair(
                out_result,
                value_type,
                vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
            );
            return VIGIL_STATUS_OK;
        }

        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_MAP_HAS, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
        );
        return VIGIL_STATUS_OK;
    }

    if (vigil_program_names_equal(method_name, method_length, "set", 3U)) {
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_COMMA,
            "map set() expects two arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_parse_expression(state, &second_arg);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            method_token->span,
            &second_arg,
            "map method arguments must be single values"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_type(
            state,
            method_token->span,
            second_arg.type,
            value_type,
            "map set() value must match map value type"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "expected ')' after map method arguments",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_MAP_SET_SAFE, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_ERR)
        );
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_report(state, method_token->span, "unknown map method");
}
