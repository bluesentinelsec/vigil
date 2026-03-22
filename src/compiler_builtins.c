#include "internal/vigil_compiler_types.h"
#include "internal/vigil_internal.h"
#include <string.h>

vigil_status_t vigil_parser_emit_default_value(vigil_parser_state_t *state, vigil_parser_type_t type,
                                               vigil_source_span_t span)
{
    if (state == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_parser_type_is_bool(type))
    {
        return vigil_parser_emit_opcode(state, VIGIL_OPCODE_FALSE, span);
    }
    if (vigil_parser_type_is_integer(type) || vigil_parser_type_is_enum(type))
    {
        return vigil_parser_emit_integer_constant(state, type, 0, span);
    }
    if (vigil_parser_type_is_f64(type))
    {
        return vigil_parser_emit_f64_constant(state, 0.0, span);
    }
    if (vigil_parser_type_is_string(type))
    {
        return vigil_parser_emit_string_constant_text(state, span, "", 0U);
    }
    if (vigil_parser_type_is_err(type))
    {
        return vigil_parser_emit_ok_constant(state, span);
    }

    return vigil_parser_emit_opcode(state, VIGIL_OPCODE_NIL, span);
}

/* ── String method helpers ─────────────────────────────────────────── */

typedef enum
{
    STRING_RESULT_STRING,
    STRING_RESULT_BOOL,
    STRING_RESULT_I32,
    STRING_RESULT_PAIR_I32_BOOL,
    STRING_RESULT_TRIPLE_SSB,
    STRING_RESULT_ARRAY_STRING,
    STRING_RESULT_ARRAY_U8
} string_result_kind_t;

typedef struct
{
    const char *name;
    size_t name_length;
    vigil_opcode_t opcode;
    string_result_kind_t result_kind;
} string_method_entry_t;

static const string_method_entry_t string_noarg_methods[] = {
    {"trim", 4U, VIGIL_OPCODE_STRING_TRIM, STRING_RESULT_STRING},
    {"to_upper", 8U, VIGIL_OPCODE_STRING_TO_UPPER, STRING_RESULT_STRING},
    {"to_lower", 8U, VIGIL_OPCODE_STRING_TO_LOWER, STRING_RESULT_STRING},
    {"trim_left", 9U, VIGIL_OPCODE_STRING_TRIM_LEFT, STRING_RESULT_STRING},
    {"trim_right", 10U, VIGIL_OPCODE_STRING_TRIM_RIGHT, STRING_RESULT_STRING},
    {"reverse", 7U, VIGIL_OPCODE_STRING_REVERSE, STRING_RESULT_STRING},
    {"is_empty", 8U, VIGIL_OPCODE_STRING_IS_EMPTY, STRING_RESULT_BOOL},
    {"char_count", 10U, VIGIL_OPCODE_STRING_CHAR_COUNT, STRING_RESULT_I32},
    {"to_c", 4U, VIGIL_OPCODE_STRING_TO_C, STRING_RESULT_STRING},
    {"fields", 6U, VIGIL_OPCODE_STRING_FIELDS, STRING_RESULT_ARRAY_STRING},
    {"bytes", 5U, VIGIL_OPCODE_STRING_BYTES, STRING_RESULT_ARRAY_U8},
};

static const string_method_entry_t string_onearg_methods[] = {
    {"contains", 8U, VIGIL_OPCODE_STRING_CONTAINS, STRING_RESULT_BOOL},
    {"starts_with", 11U, VIGIL_OPCODE_STRING_STARTS_WITH, STRING_RESULT_BOOL},
    {"ends_with", 9U, VIGIL_OPCODE_STRING_ENDS_WITH, STRING_RESULT_BOOL},
    {"split", 5U, VIGIL_OPCODE_STRING_SPLIT, STRING_RESULT_ARRAY_STRING},
    {"count", 5U, VIGIL_OPCODE_STRING_COUNT, STRING_RESULT_I32},
    {"last_index_of", 13U, VIGIL_OPCODE_STRING_LAST_INDEX_OF, STRING_RESULT_PAIR_I32_BOOL},
    {"trim_prefix", 11U, VIGIL_OPCODE_STRING_TRIM_PREFIX, STRING_RESULT_STRING},
    {"trim_suffix", 11U, VIGIL_OPCODE_STRING_TRIM_SUFFIX, STRING_RESULT_STRING},
    {"equal_fold", 10U, VIGIL_OPCODE_STRING_EQUAL_FOLD, STRING_RESULT_BOOL},
    {"cut", 3U, VIGIL_OPCODE_STRING_CUT, STRING_RESULT_TRIPLE_SSB},
    {"index_of", 8U, VIGIL_OPCODE_STRING_INDEX_OF, STRING_RESULT_PAIR_I32_BOOL},
};

static vigil_status_t compile_string_set_result(vigil_parser_state_t *state, string_result_kind_t kind,
                                                vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_parser_type_t array_type;

    switch (kind)
    {
    case STRING_RESULT_STRING:
        vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_STRING));
        return VIGIL_STATUS_OK;
    case STRING_RESULT_BOOL:
        vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
        return VIGIL_STATUS_OK;
    case STRING_RESULT_I32:
        vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_I32));
        return VIGIL_STATUS_OK;
    case STRING_RESULT_PAIR_I32_BOOL:
        vigil_expression_result_set_pair(out_result, vigil_binding_type_primitive(VIGIL_TYPE_I32),
                                         vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
        return VIGIL_STATUS_OK;
    case STRING_RESULT_TRIPLE_SSB:
        vigil_expression_result_set_triple(out_result, vigil_binding_type_primitive(VIGIL_TYPE_STRING),
                                           vigil_binding_type_primitive(VIGIL_TYPE_STRING),
                                           vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
        return VIGIL_STATUS_OK;
    case STRING_RESULT_ARRAY_STRING:
        status = vigil_program_intern_array_type((vigil_program_state_t *)state->program,
                                                 vigil_binding_type_primitive(VIGIL_TYPE_STRING), &array_type);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        vigil_expression_result_set_type(out_result, array_type);
        return VIGIL_STATUS_OK;
    case STRING_RESULT_ARRAY_U8:
        status = vigil_program_intern_array_type((vigil_program_state_t *)state->program,
                                                 vigil_binding_type_primitive(VIGIL_TYPE_U8), &array_type);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        vigil_expression_result_set_type(out_result, array_type);
        return VIGIL_STATUS_OK;
    }
    return VIGIL_STATUS_INTERNAL;
}

static const string_method_entry_t *string_table_find(const char *method_name, size_t method_length,
                                                      const string_method_entry_t *table, size_t table_count)
{
    size_t i;

    for (i = 0U; i < table_count; i += 1U)
    {
        if (vigil_program_names_equal(method_name, method_length, table[i].name, table[i].name_length))
        {
            return &table[i];
        }
    }
    return NULL;
}

static vigil_status_t compile_string_table_emit(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                const string_method_entry_t *entry,
                                                vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_opcode(state, entry->opcode, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    return compile_string_set_result(state, entry->result_kind, out_result);
}

static vigil_status_t compile_string_replace(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                             vigil_expression_result_t *arg_result,
                                             vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_expression_result_t second_arg;

    vigil_expression_result_clear(&second_arg);
    status = vigil_parser_require_type(state, method_token->span, arg_result->type,
                                       vigil_binding_type_primitive(VIGIL_TYPE_STRING),
                                       "string replace() arguments must be strings");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_COMMA, "string replace() expects two arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_parse_expression(state, &second_arg);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, &second_arg,
                                                    "string method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_type(state, method_token->span, second_arg.type,
                                       vigil_binding_type_primitive(VIGIL_TYPE_STRING),
                                       "string replace() arguments must be strings");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after string method arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_REPLACE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_STRING));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_string_substr_or_char_at(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                       const char *method_name, size_t method_length,
                                                       vigil_expression_result_t *arg_result,
                                                       vigil_expression_result_t *out_result)
{
    vigil_status_t status;

    status =
        vigil_parser_require_type(state, method_token->span, arg_result->type,
                                  vigil_binding_type_primitive(VIGIL_TYPE_I32), "string index arguments must be i32");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    if (vigil_program_names_equal(method_name, method_length, "substr", 6U))
    {
        vigil_expression_result_t second_arg;
        vigil_expression_result_clear(&second_arg);
        status = vigil_parser_expect(state, VIGIL_TOKEN_COMMA, "string substr() expects two arguments", NULL);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        status = vigil_parser_parse_expression(state, &second_arg);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        status = vigil_parser_require_scalar_expression(state, method_token->span, &second_arg,
                                                        "string method arguments must be single values");
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        status = vigil_parser_require_type(state, method_token->span, second_arg.type,
                                           vigil_binding_type_primitive(VIGIL_TYPE_I32),
                                           "string index arguments must be i32");
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after string method arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state,
                                      vigil_program_names_equal(method_name, method_length, "substr", 6U)
                                          ? VIGIL_OPCODE_STRING_SUBSTR
                                          : VIGIL_OPCODE_STRING_CHAR_AT,
                                      method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_pair(out_result, vigil_binding_type_primitive(VIGIL_TYPE_STRING),
                                     vigil_binding_type_primitive(VIGIL_TYPE_ERR));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_string_repeat(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                            vigil_expression_result_t *arg_result,
                                            vigil_expression_result_t *out_result)
{
    vigil_status_t status;

    status =
        vigil_parser_require_type(state, method_token->span, arg_result->type,
                                  vigil_binding_type_primitive(VIGIL_TYPE_I32), "string repeat() argument must be i32");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after string method arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_REPEAT, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_STRING));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_string_join(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                          vigil_expression_result_t *arg_result, vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_parser_type_t expected_array;

    status = vigil_program_intern_array_type((vigil_program_state_t *)state->program,
                                             vigil_binding_type_primitive(VIGIL_TYPE_STRING), &expected_array);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_type(state, method_token->span, arg_result->type, expected_array,
                                       "string join() argument must be array<string>");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after string method arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_STRING_JOIN, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_STRING));
    return VIGIL_STATUS_OK;
}

static const string_method_entry_t *string_find_noarg_method(const char *name, size_t length)
{
    return string_table_find(name, length, string_noarg_methods,
                             sizeof(string_noarg_methods) / sizeof(string_noarg_methods[0]));
}

static const string_method_entry_t *string_find_onearg_method(const char *name, size_t length)
{
    return string_table_find(name, length, string_onearg_methods,
                             sizeof(string_onearg_methods) / sizeof(string_onearg_methods[0]));
}

/* ── Refactored string method dispatch ─────────────────────────────── */

vigil_status_t vigil_parser_parse_string_method_call(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                     vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_expression_result_t arg_result;
    const char *method_name;
    size_t method_length;
    const string_method_entry_t *entry;

    vigil_expression_result_clear(&arg_result);
    method_name = vigil_parser_token_text(state, method_token, &method_length);

    status = vigil_parser_expect(state, VIGIL_TOKEN_LPAREN, "expected '(' after string method name", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "len", 3U))
    {
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "string len() does not accept arguments", NULL);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_STRING_SIZE, method_token->span);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_I32));
        return VIGIL_STATUS_OK;
    }

    entry = string_find_noarg_method(method_name, method_length);
    if (entry != NULL)
    {
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "string method does not accept arguments", NULL);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        return compile_string_table_emit(state, method_token, entry, out_result);
    }

    status = vigil_parser_parse_expression(state, &arg_result);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, &arg_result,
                                                    "string method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    entry = string_find_onearg_method(method_name, method_length);
    if (entry != NULL)
    {
        status = vigil_parser_require_type(state, method_token->span, arg_result.type,
                                           vigil_binding_type_primitive(VIGIL_TYPE_STRING),
                                           "string method argument must be string");
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after string method arguments", NULL);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        return compile_string_table_emit(state, method_token, entry, out_result);
    }

    if (vigil_program_names_equal(method_name, method_length, "replace", 7U))
    {
        return compile_string_replace(state, method_token, &arg_result, out_result);
    }

    if (vigil_program_names_equal(method_name, method_length, "substr", 6U) ||
        vigil_program_names_equal(method_name, method_length, "char_at", 7U))
    {
        return compile_string_substr_or_char_at(state, method_token, method_name, method_length, &arg_result,
                                                out_result);
    }

    if (vigil_program_names_equal(method_name, method_length, "repeat", 6U))
    {
        return compile_string_repeat(state, method_token, &arg_result, out_result);
    }

    if (vigil_program_names_equal(method_name, method_length, "join", 4U))
    {
        return compile_string_join(state, method_token, &arg_result, out_result);
    }

    return vigil_parser_report(state, method_token->span, "unknown string method");
}

/* ── Array method helpers ──────────────────────────────────────────── */

static vigil_status_t compile_array_len(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                        vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_COLLECTION_SIZE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_I32));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_pop(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                        vigil_parser_type_t element_type, vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_default_value(state, element_type, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_POP, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_pair(out_result, element_type, vigil_binding_type_primitive(VIGIL_TYPE_ERR));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_push(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                         vigil_parser_type_t element_type, vigil_expression_result_t *first_arg,
                                         vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_require_type(state, method_token->span, first_arg->type, element_type,
                                                      "array push() argument must match array element type");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_PUSH, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_VOID));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_get(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                        vigil_parser_type_t element_type, vigil_expression_result_t *first_arg,
                                        vigil_expression_result_t *out_result)
{
    vigil_status_t status =
        vigil_parser_require_type(state, method_token->span, first_arg->type,
                                  vigil_binding_type_primitive(VIGIL_TYPE_I32), "array get() index must be i32");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_default_value(state, element_type, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_GET_SAFE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_pair(out_result, element_type, vigil_binding_type_primitive(VIGIL_TYPE_ERR));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_contains(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                             vigil_parser_type_t element_type, vigil_expression_result_t *first_arg,
                                             vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_require_type(state, method_token->span, first_arg->type, element_type,
                                                      "array contains() argument must match array element type");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_CONTAINS, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_set(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                        vigil_parser_type_t element_type, vigil_expression_result_t *first_arg,
                                        vigil_expression_result_t *second_arg, vigil_expression_result_t *out_result)
{
    vigil_status_t status =
        vigil_parser_require_type(state, method_token->span, first_arg->type,
                                  vigil_binding_type_primitive(VIGIL_TYPE_I32), "array set() index must be i32");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_type(state, method_token->span, second_arg->type, element_type,
                                       "array set() value must match array element type");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_SET_SAFE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_ERR));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_array_slice(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                          vigil_parser_type_t receiver_type, vigil_expression_result_t *first_arg,
                                          vigil_expression_result_t *second_arg, vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_require_type(state, method_token->span, first_arg->type,
                                                      vigil_binding_type_primitive(VIGIL_TYPE_I32),
                                                      "array slice() start and end must be i32");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_type(state, method_token->span, second_arg->type,
                                       vigil_binding_type_primitive(VIGIL_TYPE_I32),
                                       "array slice() start and end must be i32");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ARRAY_SLICE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, receiver_type);
    return VIGIL_STATUS_OK;
}

/* ── Refactored array method dispatch ──────────────────────────────── */

static vigil_status_t compile_array_parse_one_arg(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                  vigil_expression_result_t *first_arg)
{
    vigil_status_t status = vigil_parser_parse_expression(state, first_arg);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, first_arg,
                                                    "array method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    return vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after array method arguments", NULL);
}

static vigil_status_t compile_array_parse_two_args(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                   vigil_expression_result_t *first_arg,
                                                   vigil_expression_result_t *second_arg)
{
    vigil_status_t status = vigil_parser_parse_expression(state, first_arg);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, first_arg,
                                                    "array method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_COMMA, "array method expects two arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_parse_expression(state, second_arg);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, second_arg,
                                                    "array method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    return vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after array method arguments", NULL);
}

vigil_status_t vigil_parser_parse_array_method_call(vigil_parser_state_t *state, vigil_parser_type_t receiver_type,
                                                    const vigil_token_t *method_token,
                                                    vigil_expression_result_t *out_result)
{
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

    status = vigil_parser_expect(state, VIGIL_TOKEN_LPAREN, "expected '(' after array method name", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "len", 3U) ||
        vigil_program_names_equal(method_name, method_length, "pop", 3U))
    {
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "array method does not accept arguments", NULL);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "len", 3U))
        {
            return compile_array_len(state, method_token, out_result);
        }
        return compile_array_pop(state, method_token, element_type, out_result);
    }

    if (vigil_program_names_equal(method_name, method_length, "push", 4U) ||
        vigil_program_names_equal(method_name, method_length, "get", 3U) ||
        vigil_program_names_equal(method_name, method_length, "contains", 8U))
    {
        status = compile_array_parse_one_arg(state, method_token, &first_arg);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "push", 4U))
        {
            return compile_array_push(state, method_token, element_type, &first_arg, out_result);
        }
        if (vigil_program_names_equal(method_name, method_length, "get", 3U))
        {
            return compile_array_get(state, method_token, element_type, &first_arg, out_result);
        }
        return compile_array_contains(state, method_token, element_type, &first_arg, out_result);
    }

    if (vigil_program_names_equal(method_name, method_length, "set", 3U) ||
        vigil_program_names_equal(method_name, method_length, "slice", 5U))
    {
        status = compile_array_parse_two_args(state, method_token, &first_arg, &second_arg);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "set", 3U))
        {
            return compile_array_set(state, method_token, element_type, &first_arg, &second_arg, out_result);
        }
        return compile_array_slice(state, method_token, receiver_type, &first_arg, &second_arg, out_result);
    }

    return vigil_parser_report(state, method_token->span, "unknown array method");
}

/* ── Map method helpers ────────────────────────────────────────────── */

static vigil_status_t compile_map_len(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                      vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_COLLECTION_SIZE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_I32));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_map_keys_or_values(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                                 int is_keys, vigil_parser_type_t elem_type,
                                                 vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_parser_type_t array_type;

    status = vigil_program_intern_array_type((vigil_program_state_t *)state->program, elem_type, &array_type);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status =
        vigil_parser_emit_opcode(state, is_keys ? VIGIL_OPCODE_MAP_KEYS : VIGIL_OPCODE_MAP_VALUES, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, array_type);
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_map_get(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                      vigil_parser_type_t value_type, vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_default_value(state, value_type, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_MAP_GET_SAFE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_pair(out_result, value_type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_map_remove(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                         vigil_parser_type_t value_type, vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_default_value(state, value_type, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_MAP_REMOVE_SAFE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_pair(out_result, value_type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_map_has(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                      vigil_expression_result_t *out_result)
{
    vigil_status_t status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_MAP_HAS, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
    return VIGIL_STATUS_OK;
}

static vigil_status_t compile_map_set(vigil_parser_state_t *state, const vigil_token_t *method_token,
                                      vigil_parser_type_t value_type, vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_expression_result_t second_arg;

    vigil_expression_result_clear(&second_arg);
    status = vigil_parser_expect(state, VIGIL_TOKEN_COMMA, "map set() expects two arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_parse_expression(state, &second_arg);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, &second_arg,
                                                    "map method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_type(state, method_token->span, second_arg.type, value_type,
                                       "map set() value must match map value type");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after map method arguments", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_MAP_SET_SAFE, method_token->span);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_ERR));
    return VIGIL_STATUS_OK;
}

/* ── Refactored map method dispatch ────────────────────────────────── */

vigil_status_t vigil_parser_parse_map_method_call(vigil_parser_state_t *state, vigil_parser_type_t receiver_type,
                                                  const vigil_token_t *method_token,
                                                  vigil_expression_result_t *out_result)
{
    vigil_status_t status;
    vigil_expression_result_t first_arg;
    vigil_parser_type_t key_type;
    vigil_parser_type_t value_type;
    const char *method_name;
    size_t method_length;

    vigil_expression_result_clear(&first_arg);
    key_type = vigil_program_map_type_key(state->program, receiver_type);
    value_type = vigil_program_map_type_value(state->program, receiver_type);
    method_name = vigil_parser_token_text(state, method_token, &method_length);

    status = vigil_parser_expect(state, VIGIL_TOKEN_LPAREN, "expected '(' after map method name", NULL);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "len", 3U) ||
        vigil_program_names_equal(method_name, method_length, "keys", 4U) ||
        vigil_program_names_equal(method_name, method_length, "values", 6U))
    {
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "map method does not accept arguments", NULL);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "len", 3U))
        {
            return compile_map_len(state, method_token, out_result);
        }
        if (vigil_program_names_equal(method_name, method_length, "keys", 4U))
        {
            return compile_map_keys_or_values(state, method_token, 1, key_type, out_result);
        }
        return compile_map_keys_or_values(state, method_token, 0, value_type, out_result);
    }

    status = vigil_parser_parse_expression(state, &first_arg);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_scalar_expression(state, method_token->span, &first_arg,
                                                    "map method arguments must be single values");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    status = vigil_parser_require_type(state, method_token->span, first_arg.type, key_type,
                                       "map method key must match map key type");
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    if (vigil_program_names_equal(method_name, method_length, "get", 3U) ||
        vigil_program_names_equal(method_name, method_length, "remove", 6U) ||
        vigil_program_names_equal(method_name, method_length, "has", 3U))
    {
        status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after map method arguments", NULL);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        if (vigil_program_names_equal(method_name, method_length, "get", 3U))
        {
            return compile_map_get(state, method_token, value_type, out_result);
        }
        if (vigil_program_names_equal(method_name, method_length, "remove", 6U))
        {
            return compile_map_remove(state, method_token, value_type, out_result);
        }
        return compile_map_has(state, method_token, out_result);
    }

    if (vigil_program_names_equal(method_name, method_length, "set", 3U))
    {
        return compile_map_set(state, method_token, value_type, out_result);
    }

    return vigil_parser_report(state, method_token->span, "unknown map method");
}
