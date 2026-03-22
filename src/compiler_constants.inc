/*
 * compiler_constants.inc — Constant expression evaluator, extracted from compiler.c for readability.
 * This file is #included directly into compiler.c and is not compiled separately.
 */

static int vigil_parse_integer_literal_text(const char *text, size_t length, unsigned long long *out_value)
{
    char buffer[128];
    char *digits;
    char *end;
    int base;
    unsigned long long parsed;

    if (text == NULL || out_value == NULL || length == 0U || length >= sizeof(buffer))
    {
        return 0;
    }

    memcpy(buffer, text, length);
    buffer[length] = '\0';

    digits = buffer;
    base = 0;
    if (length > 2U && buffer[0] == '0')
    {
        if (buffer[1] == 'b' || buffer[1] == 'B')
        {
            digits = buffer + 2;
            base = 2;
        }
        else if (buffer[1] == 'o' || buffer[1] == 'O')
        {
            digits = buffer + 2;
            base = 8;
        }
    }
    if (*digits == '\0')
    {
        return 0;
    }

    errno = 0;
    parsed = strtoull(digits, &end, base);
    if (errno != 0 || end == digits || *end != '\0')
    {
        return 0;
    }

    *out_value = parsed;
    return 1;
}

static vigil_status_t vigil_program_parse_constant_int(vigil_program_state_t *program, const vigil_token_t *token,
                                                       vigil_value_t *out_value)
{
    const char *text;
    size_t length;
    unsigned long long parsed;

    text = vigil_program_token_text(program, token, &length);
    if (text == NULL || length == 0U)
    {
        return vigil_compile_report(program, token->span, "invalid integer literal");
    }
    if (!vigil_parse_integer_literal_text(text, length, &parsed))
    {
        return vigil_compile_report(program, token->span, "invalid integer literal");
    }

    if (parsed <= (unsigned long long)INT64_MAX)
    {
        vigil_value_init_int(out_value, (int64_t)parsed);
    }
    else
    {
        vigil_value_init_uint(out_value, (uint64_t)parsed);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_constant_float(vigil_program_state_t *program, const vigil_token_t *token,
                                                         vigil_value_t *out_value)
{
    const char *text;
    size_t length;
    char buffer[128];
    char *end;
    double parsed;

    text = vigil_program_token_text(program, token, &length);
    if (text == NULL || length == 0U)
    {
        return vigil_compile_report(program, token->span, "invalid float literal");
    }
    if (length >= sizeof(buffer))
    {
        return vigil_compile_report(program, token->span, "float literal is too long");
    }

    memcpy(buffer, text, length);
    buffer[length] = '\0';
    errno = 0;
    parsed = strtod(buffer, &end);
    if (end == buffer || *end != '\0')
    {
        return vigil_compile_report(program, token->span, "invalid float literal");
    }
    /* Reject overflow (HUGE_VAL) but allow underflow to zero/denormal. */
    if (errno == ERANGE && (parsed == HUGE_VAL || parsed == -HUGE_VAL))
    {
        return vigil_compile_report(program, token->span, "invalid float literal");
    }

    vigil_value_init_float(out_value, parsed);
    return VIGIL_STATUS_OK;
}

static int vigil_program_integer_type_signed_bounds(vigil_type_kind_t kind, int64_t *out_minimum, int64_t *out_maximum)
{
    int64_t minimum_value;
    int64_t maximum_value;

    switch (kind)
    {
    case VIGIL_TYPE_I32:
        minimum_value = (int64_t)INT32_MIN;
        maximum_value = (int64_t)INT32_MAX;
        break;
    case VIGIL_TYPE_I64:
        minimum_value = INT64_MIN;
        maximum_value = INT64_MAX;
        break;
    default:
        return 0;
    }

    if (out_minimum != NULL)
    {
        *out_minimum = minimum_value;
    }
    if (out_maximum != NULL)
    {
        *out_maximum = maximum_value;
    }
    return 1;
}

static int vigil_program_integer_type_unsigned_max(vigil_type_kind_t kind, uint64_t *out_maximum)
{
    uint64_t maximum_value;

    switch (kind)
    {
    case VIGIL_TYPE_U8:
        maximum_value = (uint64_t)UINT8_MAX;
        break;
    case VIGIL_TYPE_U32:
        maximum_value = (uint64_t)UINT32_MAX;
        break;
    case VIGIL_TYPE_U64:
        maximum_value = UINT64_MAX;
        break;
    default:
        return 0;
    }

    if (out_maximum != NULL)
    {
        *out_maximum = maximum_value;
    }
    return 1;
}

static vigil_parser_type_t vigil_program_integer_literal_type(const vigil_value_t *value)
{
    if (value == NULL)
    {
        return vigil_binding_type_invalid();
    }
    if (vigil_value_kind(value) == VIGIL_VALUE_UINT)
    {
        return vigil_binding_type_primitive(VIGIL_TYPE_U64);
    }
    if (vigil_value_kind(value) == VIGIL_VALUE_INT && vigil_value_as_int(value) > (int64_t)INT32_MAX)
    {
        return vigil_binding_type_primitive(VIGIL_TYPE_I64);
    }
    return vigil_binding_type_primitive(VIGIL_TYPE_I32);
}

static vigil_status_t vigil_program_validate_integer_value_for_type(vigil_program_state_t *program,
                                                                    vigil_source_span_t span,
                                                                    vigil_parser_type_t target_type,
                                                                    const vigil_value_t *value)
{
    int64_t minimum_value;
    int64_t maximum_value;
    uint64_t maximum_unsigned;

    if (!vigil_parser_type_is_integer(target_type) || value == NULL)
    {
        return VIGIL_STATUS_OK;
    }

    if (vigil_parser_type_is_signed_integer(target_type))
    {
        if (vigil_value_kind(value) == VIGIL_VALUE_UINT)
        {
            return vigil_compile_report(program, span, "integer arithmetic overflow or invalid operation");
        }
        if (!vigil_program_integer_type_signed_bounds(target_type.kind, &minimum_value, &maximum_value))
        {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        if (vigil_value_as_int(value) < minimum_value || vigil_value_as_int(value) > maximum_value)
        {
            return vigil_compile_report(program, span, "integer arithmetic overflow or invalid operation");
        }
        return VIGIL_STATUS_OK;
    }

    if (!vigil_program_integer_type_unsigned_max(target_type.kind, &maximum_unsigned))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (vigil_value_kind(value) == VIGIL_VALUE_INT)
    {
        if (vigil_value_as_int(value) < 0)
        {
            return vigil_compile_report(program, span, "integer arithmetic overflow or invalid operation");
        }
        if ((uint64_t)vigil_value_as_int(value) > maximum_unsigned)
        {
            return vigil_compile_report(program, span, "integer arithmetic overflow or invalid operation");
        }
        return VIGIL_STATUS_OK;
    }
    if (vigil_value_kind(value) != VIGIL_VALUE_UINT || vigil_value_as_uint(value) > maximum_unsigned)
    {
        return vigil_compile_report(program, span, "integer arithmetic overflow or invalid operation");
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_convert_constant_to_integer(vigil_program_state_t *program,
                                                                vigil_source_span_t span, vigil_type_kind_t target_kind,
                                                                const vigil_constant_result_t *argument,
                                                                vigil_constant_result_t *out_result)
{
    int64_t minimum_value;
    int64_t maximum_value;
    uint64_t maximum_unsigned;
    double float_value;

    if (program == NULL || argument == NULL || out_result == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_parser_type_equal(argument->type, vigil_binding_type_primitive(target_kind)))
    {
        out_result->type = vigil_binding_type_primitive(target_kind);
        out_result->value = vigil_value_copy(&argument->value);
        return VIGIL_STATUS_OK;
    }

    if (vigil_parser_type_is_integer(argument->type))
    {
        out_result->type = vigil_binding_type_primitive(target_kind);
        if (vigil_parser_type_is_signed_integer(out_result->type))
        {
            if (!vigil_program_integer_type_signed_bounds(target_kind, &minimum_value, &maximum_value))
            {
                return VIGIL_STATUS_INVALID_ARGUMENT;
            }
            if (vigil_value_kind(&argument->value) == VIGIL_VALUE_UINT)
            {
                if (vigil_value_as_uint(&argument->value) > (uint64_t)maximum_value)
                {
                    return vigil_compile_report(program, span, "integer conversion overflow or invalid value");
                }
                vigil_value_init_int(&out_result->value, (int64_t)vigil_value_as_uint(&argument->value));
            }
            else
            {
                if (vigil_value_as_int(&argument->value) < minimum_value ||
                    vigil_value_as_int(&argument->value) > maximum_value)
                {
                    return vigil_compile_report(program, span, "integer conversion overflow or invalid value");
                }
                vigil_value_init_int(&out_result->value, vigil_value_as_int(&argument->value));
            }
        }
        else
        {
            if (!vigil_program_integer_type_unsigned_max(target_kind, &maximum_unsigned))
            {
                return VIGIL_STATUS_INVALID_ARGUMENT;
            }
            if (vigil_value_kind(&argument->value) == VIGIL_VALUE_INT)
            {
                if (vigil_value_as_int(&argument->value) < 0 ||
                    (uint64_t)vigil_value_as_int(&argument->value) > maximum_unsigned)
                {
                    return vigil_compile_report(program, span, "integer conversion overflow or invalid value");
                }
                vigil_value_init_uint(&out_result->value, (uint64_t)vigil_value_as_int(&argument->value));
            }
            else
            {
                if (vigil_value_as_uint(&argument->value) > maximum_unsigned)
                {
                    return vigil_compile_report(program, span, "integer conversion overflow or invalid value");
                }
                vigil_value_init_uint(&out_result->value, vigil_value_as_uint(&argument->value));
            }
        }
        return VIGIL_STATUS_OK;
    }

    if (!vigil_parser_type_is_f64(argument->type))
    {
        return vigil_compile_report(program, span, "integer conversions require an integer or f64 argument");
    }

    float_value = vigil_value_as_float(&argument->value);
    out_result->type = vigil_binding_type_primitive(target_kind);
    if (vigil_parser_type_is_signed_integer(out_result->type))
    {
        int64_t integer_value;

        if (!vigil_program_integer_type_signed_bounds(target_kind, &minimum_value, &maximum_value))
        {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        if (!isfinite(float_value) || float_value < (double)INT64_MIN || float_value > (double)INT64_MAX)
        {
            return vigil_compile_report(program, span, "integer conversion overflow or invalid value");
        }

        integer_value = (int64_t)float_value;
        if (integer_value < minimum_value || integer_value > maximum_value)
        {
            return vigil_compile_report(program, span, "integer conversion overflow or invalid value");
        }
        vigil_value_init_int(&out_result->value, integer_value);
    }
    else
    {
        uint64_t integer_value;

        if (!vigil_program_integer_type_unsigned_max(target_kind, &maximum_unsigned))
        {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        if (!isfinite(float_value) || float_value < 0.0 || float_value > (double)UINT64_MAX)
        {
            return vigil_compile_report(program, span, "integer conversion overflow or invalid value");
        }

        integer_value = (uint64_t)float_value;
        if (integer_value > maximum_unsigned)
        {
            return vigil_compile_report(program, span, "integer conversion overflow or invalid value");
        }
        vigil_value_init_uint(&out_result->value, integer_value);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_convert_constant_to_string(vigil_program_state_t *program, vigil_source_span_t span,
                                                               const vigil_constant_result_t *argument,
                                                               vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_object_t *object;
    const char *text;
    char buffer[128];
    int written;

    if (program == NULL || argument == NULL || out_result == NULL)
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_parser_type_is_string(argument->type))
    {
        out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_STRING);
        out_result->value = vigil_value_copy(&argument->value);
        return VIGIL_STATUS_OK;
    }

    object = NULL;
    if (vigil_parser_type_is_bool(argument->type))
    {
        text = vigil_value_as_bool(&argument->value) ? "true" : "false";
        status = vigil_string_object_new_cstr(program->registry->runtime, text, &object, program->error);
    }
    else if (vigil_parser_type_is_integer(argument->type))
    {
        if (vigil_value_kind(&argument->value) == VIGIL_VALUE_UINT)
        {
            written =
                snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)vigil_value_as_uint(&argument->value));
        }
        else
        {
            written = snprintf(buffer, sizeof(buffer), "%lld", (long long)vigil_value_as_int(&argument->value));
        }
        if (written < 0 || (size_t)written >= sizeof(buffer))
        {
            return vigil_compile_report(program, span, "failed to format integer constant");
        }
        status = vigil_string_object_new(program->registry->runtime, buffer, (size_t)written, &object, program->error);
    }
    else if (vigil_parser_type_is_f64(argument->type))
    {
        written = snprintf(buffer, sizeof(buffer), "%.17g", vigil_value_as_float(&argument->value));
        if (written < 0 || (size_t)written >= sizeof(buffer))
        {
            return vigil_compile_report(program, span, "failed to format float constant");
        }
        status = vigil_string_object_new(program->registry->runtime, buffer, (size_t)written, &object, program->error);
    }
    else
    {
        return vigil_compile_report(program, span, "string(...) requires a string, integer, f64, or bool argument");
    }
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    vigil_value_init_object(&out_result->value, &object);
    vigil_object_release(&object);
    out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_STRING);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_constant_builtin_conversion(vigil_program_state_t *program, size_t *cursor,
                                                                      const vigil_token_t *name_token,
                                                                      vigil_type_kind_t target_kind,
                                                                      vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t argument;
    const vigil_token_t *token;

    vigil_constant_result_clear(&argument);
    token = vigil_program_cursor_peek(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_LPAREN)
    {
        return vigil_compile_report(program, name_token->span, "expected '(' after conversion name");
    }
    vigil_program_cursor_advance(program, cursor);

    status = vigil_program_parse_constant_expression(program, cursor, &argument);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }
    if (vigil_program_cursor_peek(program, *cursor) != NULL &&
        vigil_program_cursor_peek(program, *cursor)->kind == VIGIL_TOKEN_COMMA)
    {
        vigil_constant_result_release(&argument);
        return vigil_compile_report(program, name_token->span, "built-in conversions accept exactly one argument");
    }
    token = vigil_program_cursor_peek(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_RPAREN)
    {
        vigil_constant_result_release(&argument);
        return vigil_compile_report(program, name_token->span, "expected ')' after conversion argument");
    }
    vigil_program_cursor_advance(program, cursor);

    switch (target_kind)
    {
    case VIGIL_TYPE_I32:
    case VIGIL_TYPE_I64:
    case VIGIL_TYPE_U8:
    case VIGIL_TYPE_U32:
    case VIGIL_TYPE_U64:
        status =
            vigil_program_convert_constant_to_integer(program, name_token->span, target_kind, &argument, out_result);
        break;
    case VIGIL_TYPE_F64:
        if (vigil_parser_type_is_f64(argument.type))
        {
            out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_F64);
            out_result->value = vigil_value_copy(&argument.value);
            status = VIGIL_STATUS_OK;
        }
        else if (vigil_parser_type_is_integer(argument.type))
        {
            if (vigil_value_kind(&argument.value) == VIGIL_VALUE_UINT)
            {
                vigil_value_init_float(&out_result->value, (double)vigil_value_as_uint(&argument.value));
            }
            else
            {
                vigil_value_init_float(&out_result->value, (double)vigil_value_as_int(&argument.value));
            }
            out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_F64);
            status = VIGIL_STATUS_OK;
        }
        else
        {
            status = vigil_compile_report(program, name_token->span, "f64(...) requires an integer or f64 argument");
        }
        break;
    case VIGIL_TYPE_STRING:
        status = vigil_program_convert_constant_to_string(program, name_token->span, &argument, out_result);
        break;
    case VIGIL_TYPE_BOOL:
        if (!vigil_parser_type_is_bool(argument.type))
        {
            status = vigil_compile_report(program, name_token->span, "bool(...) requires a bool argument");
        }
        else
        {
            out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
            out_result->value = vigil_value_copy(&argument.value);
            status = VIGIL_STATUS_OK;
        }
        break;
    default:
        status = vigil_compile_report(program, name_token->span, "unsupported built-in conversion");
        break;
    }

    vigil_constant_result_release(&argument);
    return status;
}

static vigil_status_t vigil_program_parse_constant_primary(vigil_program_state_t *program, size_t *cursor,
                                                           vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    const vigil_token_t *token;
    const vigil_global_constant_t *constant;
    const vigil_token_t *member_token;
    const vigil_token_t *enum_member_token;
    const vigil_enum_member_t *enum_member;
    vigil_source_id_t source_id;
    const char *name_text;
    const char *member_text;
    const char *enum_member_text;
    size_t name_length;
    size_t member_length;
    size_t enum_member_length;
    size_t enum_index;

    token = vigil_program_cursor_peek(program, *cursor);
    if (token == NULL)
    {
        return vigil_compile_report(program, vigil_program_eof_span(program), "expected constant expression");
    }

    switch (token->kind)
    {
    case VIGIL_TOKEN_INT_LITERAL:
        vigil_program_cursor_advance(program, cursor);
        status = vigil_program_parse_constant_int(program, token, &out_result->value);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        out_result->type = vigil_program_integer_literal_type(&out_result->value);
        return VIGIL_STATUS_OK;
    case VIGIL_TOKEN_FLOAT_LITERAL:
        vigil_program_cursor_advance(program, cursor);
        status = vigil_program_parse_constant_float(program, token, &out_result->value);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_F64);
        return VIGIL_STATUS_OK;
    case VIGIL_TOKEN_TRUE:
        vigil_program_cursor_advance(program, cursor);
        vigil_value_init_bool(&out_result->value, 1);
        out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        return VIGIL_STATUS_OK;
    case VIGIL_TOKEN_FALSE:
        vigil_program_cursor_advance(program, cursor);
        vigil_value_init_bool(&out_result->value, 0);
        out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        return VIGIL_STATUS_OK;
    case VIGIL_TOKEN_STRING_LITERAL:
    case VIGIL_TOKEN_RAW_STRING_LITERAL:
    case VIGIL_TOKEN_CHAR_LITERAL:
        vigil_program_cursor_advance(program, cursor);
        status = vigil_program_parse_string_literal_value(program, token, &out_result->value);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_STRING);
        return VIGIL_STATUS_OK;
    case VIGIL_TOKEN_IDENTIFIER:
        vigil_program_cursor_advance(program, cursor);
        name_text = vigil_program_token_text(program, token, &name_length);
        if (vigil_program_cursor_peek(program, *cursor) != NULL &&
            vigil_program_cursor_peek(program, *cursor)->kind == VIGIL_TOKEN_LPAREN)
        {
            vigil_type_kind_t conversion_kind;

            conversion_kind = vigil_type_kind_from_name(name_text, name_length);
            if (conversion_kind == VIGIL_TYPE_I32 || conversion_kind == VIGIL_TYPE_I64 ||
                conversion_kind == VIGIL_TYPE_U8 || conversion_kind == VIGIL_TYPE_U32 ||
                conversion_kind == VIGIL_TYPE_U64 || conversion_kind == VIGIL_TYPE_F64 ||
                conversion_kind == VIGIL_TYPE_STRING || conversion_kind == VIGIL_TYPE_BOOL)
            {
                return vigil_program_parse_constant_builtin_conversion(program, cursor, token, conversion_kind,
                                                                       out_result);
            }
        }
        enum_member = NULL;
        enum_index = 0U;
        if (vigil_program_cursor_peek(program, *cursor) != NULL &&
            vigil_program_cursor_peek(program, *cursor)->kind == VIGIL_TOKEN_DOT &&
            vigil_program_resolve_import_alias(program, name_text, name_length, &source_id))
        {
            vigil_program_cursor_advance(program, cursor);
            member_token = vigil_program_cursor_peek(program, *cursor);
            if (member_token == NULL || member_token->kind != VIGIL_TOKEN_IDENTIFIER)
            {
                return vigil_compile_report(program, token->span, "unknown global constant");
            }
            vigil_program_cursor_advance(program, cursor);
            member_text = vigil_program_token_text(program, member_token, &member_length);
            if (vigil_program_cursor_peek(program, *cursor) != NULL &&
                vigil_program_cursor_peek(program, *cursor)->kind == VIGIL_TOKEN_DOT)
            {
                vigil_program_cursor_advance(program, cursor);
                enum_member_token = vigil_program_cursor_peek(program, *cursor);
                if (enum_member_token == NULL || enum_member_token->kind != VIGIL_TOKEN_IDENTIFIER)
                {
                    return vigil_compile_report(program, member_token->span, "unknown enum member");
                }
                vigil_program_cursor_advance(program, cursor);
                enum_member_text = vigil_program_token_text(program, enum_member_token, &enum_member_length);
                if (!vigil_program_lookup_enum_member_in_source(program, source_id, member_text, member_length,
                                                                enum_member_text, enum_member_length, &enum_index,
                                                                &enum_member))
                {
                    return vigil_compile_report(program, enum_member_token->span, "unknown enum member");
                }
                if (!vigil_program_is_enum_public(&program->enums[enum_index]))
                {
                    return vigil_compile_report(program, member_token->span, "module member is not public");
                }
                vigil_value_init_int(&out_result->value, enum_member->value);
                out_result->type = vigil_binding_type_enum(enum_index);
                return VIGIL_STATUS_OK;
            }
            if (!vigil_program_find_constant_in_source(program, source_id, member_text, member_length, &constant))
            {
                return vigil_compile_report(program, member_token->span, "unknown global constant");
            }
            if (!vigil_program_is_constant_public(constant))
            {
                return vigil_compile_report(program, member_token->span, "module member is not public");
            }
            out_result->type = constant->type;
            out_result->value = vigil_value_copy(&constant->value);
            return VIGIL_STATUS_OK;
        }
        if (vigil_program_cursor_peek(program, *cursor) != NULL &&
            vigil_program_cursor_peek(program, *cursor)->kind == VIGIL_TOKEN_DOT)
        {
            vigil_program_cursor_advance(program, cursor);
            member_token = vigil_program_cursor_peek(program, *cursor);
            if (member_token == NULL || member_token->kind != VIGIL_TOKEN_IDENTIFIER)
            {
                return vigil_compile_report(program, token->span, "unknown enum member");
            }
            vigil_program_cursor_advance(program, cursor);
            member_text = vigil_program_token_text(program, member_token, &member_length);
            if (!vigil_program_lookup_enum_member(program, name_text, name_length, member_text, member_length,
                                                  &enum_index, &enum_member))
            {
                return vigil_compile_report(program, member_token->span, "unknown enum member");
            }
            vigil_value_init_int(&out_result->value, enum_member->value);
            out_result->type = vigil_binding_type_enum(enum_index);
            return VIGIL_STATUS_OK;
        }
        if (!vigil_program_find_constant(program, name_text, name_length, &constant))
        {
            return vigil_compile_report(program, token->span, "unknown global constant");
        }
        out_result->type = constant->type;
        out_result->value = vigil_value_copy(&constant->value);
        return VIGIL_STATUS_OK;
    case VIGIL_TOKEN_LPAREN:
        vigil_program_cursor_advance(program, cursor);
        status = vigil_program_parse_constant_expression(program, cursor, out_result);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_RPAREN)
        {
            return vigil_compile_report(program, token == NULL ? vigil_program_eof_span(program) : token->span,
                                        "expected ')' after constant expression");
        }
        vigil_program_cursor_advance(program, cursor);
        return VIGIL_STATUS_OK;
    default:
        return vigil_compile_report(program, token->span, "expected constant expression");
    }
}

static vigil_status_t vigil_program_parse_constant_unary(vigil_program_state_t *program, size_t *cursor,
                                                         vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    const vigil_token_t *token;
    vigil_constant_result_t operand;
    int64_t integer_result;
    double float_result;

    vigil_constant_result_clear(&operand);
    token = vigil_program_cursor_peek(program, *cursor);
    if (token != NULL && (token->kind == VIGIL_TOKEN_MINUS || token->kind == VIGIL_TOKEN_BANG))
    {
        vigil_program_cursor_advance(program, cursor);
        status = vigil_program_parse_constant_unary(program, cursor, &operand);
        if (status != VIGIL_STATUS_OK)
        {
            return status;
        }

        if (token->kind == VIGIL_TOKEN_MINUS)
        {
            if (vigil_parser_type_is_signed_integer(operand.type))
            {
                vigil_parser_type_t integer_type;

                integer_type = operand.type;
                status = vigil_program_checked_negate(vigil_value_as_int(&operand.value), &integer_result);
                vigil_constant_result_release(&operand);
                if (status != VIGIL_STATUS_OK)
                {
                    return vigil_compile_report(program, token->span,
                                                "integer arithmetic overflow or invalid operation");
                }
                status = vigil_program_validate_integer_value_for_type(
                    program, token->span, integer_type, &(vigil_value_t){vigil_nanbox_encode_int(integer_result)});
                if (status != VIGIL_STATUS_OK)
                {
                    return status;
                }
                vigil_value_init_int(&out_result->value, integer_result);
                out_result->type = integer_type;
                return VIGIL_STATUS_OK;
            }
            if (!vigil_parser_type_is_f64(operand.type))
            {
                vigil_constant_result_release(&operand);
                return vigil_compile_report(program, token->span, "unary '-' requires a signed integer or f64 operand");
            }
            float_result = -vigil_value_as_float(&operand.value);
            vigil_constant_result_release(&operand);
            vigil_value_init_float(&out_result->value, float_result);
            out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_F64);
            return VIGIL_STATUS_OK;
        }

        if (!vigil_parser_type_equal(operand.type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL)))
        {
            vigil_constant_result_release(&operand);
            return vigil_compile_report(program, token->span, "logical '!' requires a bool operand");
        }
        vigil_value_init_bool(&out_result->value, !vigil_value_as_bool(&operand.value));
        vigil_constant_result_release(&operand);
        out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        return VIGIL_STATUS_OK;
    }

    return vigil_program_parse_constant_primary(program, cursor, out_result);
}

static vigil_status_t vigil_program_parse_constant_factor(vigil_program_state_t *program, size_t *cursor,
                                                          vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;
    double float_result;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_unary(program, cursor, &left);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL ||
            (token->kind != VIGIL_TOKEN_STAR && token->kind != VIGIL_TOKEN_SLASH && token->kind != VIGIL_TOKEN_PERCENT))
        {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_unary(program, cursor, &right);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            return status;
        }
        if (vigil_parser_type_is_integer(left.type) && vigil_parser_type_equal(left.type, right.type))
        {
            if (vigil_parser_type_is_unsigned_integer(left.type))
            {
                switch (token->kind)
                {
                case VIGIL_TOKEN_STAR:
                    status = vigil_program_checked_umultiply(vigil_value_as_uint(&left.value),
                                                             vigil_value_as_uint(&right.value), &uinteger_result);
                    break;
                case VIGIL_TOKEN_SLASH:
                    status = vigil_program_checked_udivide(vigil_value_as_uint(&left.value),
                                                           vigil_value_as_uint(&right.value), &uinteger_result);
                    break;
                default:
                    status = vigil_program_checked_umodulo(vigil_value_as_uint(&left.value),
                                                           vigil_value_as_uint(&right.value), &uinteger_result);
                    break;
                }
            }
            else
            {
                switch (token->kind)
                {
                case VIGIL_TOKEN_STAR:
                    status = vigil_program_checked_multiply(vigil_value_as_int(&left.value),
                                                            vigil_value_as_int(&right.value), &integer_result);
                    break;
                case VIGIL_TOKEN_SLASH:
                    status = vigil_program_checked_divide(vigil_value_as_int(&left.value),
                                                          vigil_value_as_int(&right.value), &integer_result);
                    break;
                default:
                    status = vigil_program_checked_modulo(vigil_value_as_int(&left.value),
                                                          vigil_value_as_int(&right.value), &integer_result);
                    break;
                }
            }
            if (status != VIGIL_STATUS_OK)
            {
                vigil_constant_result_release(&left);
                vigil_constant_result_release(&right);
                return vigil_compile_report(program, token->span, "integer arithmetic overflow or invalid operation");
            }
            status = vigil_program_validate_integer_value_for_type(
                program, token->span, left.type,
                vigil_parser_type_is_unsigned_integer(left.type)
                    ? &(vigil_value_t){vigil_nanbox_encode_uint(uinteger_result)}
                    : &(vigil_value_t){vigil_nanbox_encode_int(integer_result)});
            if (status != VIGIL_STATUS_OK)
            {
                vigil_constant_result_release(&left);
                vigil_constant_result_release(&right);
                return status;
            }

            {
                vigil_parser_type_t integer_type = left.type;

                vigil_constant_result_release(&left);
                if (vigil_parser_type_is_unsigned_integer(integer_type))
                {
                    vigil_value_init_uint(&left.value, uinteger_result);
                }
                else
                {
                    vigil_value_init_int(&left.value, integer_result);
                }
                left.type = integer_type;
            }
            vigil_constant_result_release(&right);
            continue;
        }
        if (token->kind != VIGIL_TOKEN_PERCENT && vigil_parser_type_is_f64(left.type) &&
            vigil_parser_type_is_f64(right.type))
        {
            switch (token->kind)
            {
            case VIGIL_TOKEN_STAR:
                float_result = vigil_value_as_float(&left.value) * vigil_value_as_float(&right.value);
                break;
            default:
                float_result = vigil_value_as_float(&left.value) / vigil_value_as_float(&right.value);
                break;
            }
            vigil_constant_result_release(&left);
            vigil_value_init_float(&left.value, float_result);
            left.type = vigil_binding_type_primitive(VIGIL_TYPE_F64);
            vigil_constant_result_release(&right);
            continue;
        }
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(program, token->span,
                                        token->kind == VIGIL_TOKEN_PERCENT
                                            ? "modulo requires matching integer operands"
                                            : "arithmetic operators require matching integer or f64 operands");
        }
    }
}

static vigil_status_t vigil_program_parse_constant_term(vigil_program_state_t *program, size_t *cursor,
                                                        vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;
    double float_result;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_factor(program, cursor, &left);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || (token->kind != VIGIL_TOKEN_PLUS && token->kind != VIGIL_TOKEN_MINUS))
        {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_factor(program, cursor, &right);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            return status;
        }
        if (token->kind == VIGIL_TOKEN_PLUS &&
            vigil_parser_type_equal(left.type, vigil_binding_type_primitive(VIGIL_TYPE_STRING)) &&
            vigil_parser_type_equal(right.type, vigil_binding_type_primitive(VIGIL_TYPE_STRING)))
        {
            vigil_value_t concatenated;

            vigil_value_init_nil(&concatenated);
            status = vigil_program_concat_string_values(program, &left.value, &right.value, &concatenated);
            if (status != VIGIL_STATUS_OK)
            {
                vigil_constant_result_release(&left);
                vigil_constant_result_release(&right);
                return status;
            }
            vigil_constant_result_release(&left);
            left.value = concatenated;
            left.type = vigil_binding_type_primitive(VIGIL_TYPE_STRING);
            vigil_constant_result_release(&right);
            continue;
        }
        if (vigil_parser_type_is_integer(left.type) && vigil_parser_type_equal(left.type, right.type))
        {
            if (vigil_parser_type_is_unsigned_integer(left.type))
            {
                if (token->kind == VIGIL_TOKEN_PLUS)
                {
                    status = vigil_program_checked_uadd(vigil_value_as_uint(&left.value),
                                                        vigil_value_as_uint(&right.value), &uinteger_result);
                }
                else
                {
                    status = vigil_program_checked_usubtract(vigil_value_as_uint(&left.value),
                                                             vigil_value_as_uint(&right.value), &uinteger_result);
                }
            }
            else
            {
                if (token->kind == VIGIL_TOKEN_PLUS)
                {
                    status = vigil_program_checked_add(vigil_value_as_int(&left.value),
                                                       vigil_value_as_int(&right.value), &integer_result);
                }
                else
                {
                    status = vigil_program_checked_subtract(vigil_value_as_int(&left.value),
                                                            vigil_value_as_int(&right.value), &integer_result);
                }
            }
            if (status != VIGIL_STATUS_OK)
            {
                vigil_constant_result_release(&left);
                vigil_constant_result_release(&right);
                return vigil_compile_report(program, token->span, "integer arithmetic overflow or invalid operation");
            }
            status = vigil_program_validate_integer_value_for_type(
                program, token->span, left.type,
                vigil_parser_type_is_unsigned_integer(left.type)
                    ? &(vigil_value_t){vigil_nanbox_encode_uint(uinteger_result)}
                    : &(vigil_value_t){vigil_nanbox_encode_int(integer_result)});
            if (status != VIGIL_STATUS_OK)
            {
                vigil_constant_result_release(&left);
                vigil_constant_result_release(&right);
                return status;
            }

            {
                vigil_parser_type_t integer_type = left.type;

                vigil_constant_result_release(&left);
                if (vigil_parser_type_is_unsigned_integer(integer_type))
                {
                    vigil_value_init_uint(&left.value, uinteger_result);
                }
                else
                {
                    vigil_value_init_int(&left.value, integer_result);
                }
                left.type = integer_type;
            }
            vigil_constant_result_release(&right);
            continue;
        }
        if (vigil_parser_type_is_f64(left.type) && vigil_parser_type_is_f64(right.type))
        {
            float_result = token->kind == VIGIL_TOKEN_PLUS
                               ? vigil_value_as_float(&left.value) + vigil_value_as_float(&right.value)
                               : vigil_value_as_float(&left.value) - vigil_value_as_float(&right.value);
            vigil_constant_result_release(&left);
            vigil_value_init_float(&left.value, float_result);
            left.type = vigil_binding_type_primitive(VIGIL_TYPE_F64);
            vigil_constant_result_release(&right);
            continue;
        }
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(program, token->span,
                                        token->kind == VIGIL_TOKEN_PLUS
                                            ? "'+' requires matching integer, f64, or string operands"
                                            : "arithmetic operators require matching integer or f64 operands");
        }
    }
}

static vigil_status_t vigil_program_parse_constant_shift(vigil_program_state_t *program, size_t *cursor,
                                                         vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_term(program, cursor, &left);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || (token->kind != VIGIL_TOKEN_SHIFT_LEFT && token->kind != VIGIL_TOKEN_SHIFT_RIGHT))
        {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_term(program, cursor, &right);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_is_integer(left.type) || !vigil_parser_type_equal(left.type, right.type))
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(program, token->span, "shift operators require matching integer operands");
        }

        if (vigil_parser_type_is_unsigned_integer(left.type))
        {
            if (token->kind == VIGIL_TOKEN_SHIFT_LEFT)
            {
                status = vigil_program_checked_ushift_left(vigil_value_as_uint(&left.value),
                                                           vigil_value_as_uint(&right.value), &uinteger_result);
            }
            else
            {
                status = vigil_program_checked_ushift_right(vigil_value_as_uint(&left.value),
                                                            vigil_value_as_uint(&right.value), &uinteger_result);
            }
        }
        else
        {
            if (token->kind == VIGIL_TOKEN_SHIFT_LEFT)
            {
                status = vigil_program_checked_shift_left(vigil_value_as_int(&left.value),
                                                          vigil_value_as_int(&right.value), &integer_result);
            }
            else
            {
                status = vigil_program_checked_shift_right(vigil_value_as_int(&left.value),
                                                           vigil_value_as_int(&right.value), &integer_result);
            }
        }
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(program, token->span, "integer arithmetic overflow or invalid operation");
        }
        status = vigil_program_validate_integer_value_for_type(
            program, token->span, left.type,
            vigil_parser_type_is_unsigned_integer(left.type)
                ? &(vigil_value_t){vigil_nanbox_encode_uint(uinteger_result)}
                : &(vigil_value_t){vigil_nanbox_encode_int(integer_result)});
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return status;
        }

        {
            vigil_parser_type_t integer_type = left.type;

            vigil_constant_result_release(&left);
            if (vigil_parser_type_is_unsigned_integer(integer_type))
            {
                vigil_value_init_uint(&left.value, uinteger_result);
            }
            else
            {
                vigil_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_comparison(vigil_program_state_t *program, size_t *cursor,
                                                              vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int comparison_result;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_shift(program, cursor, &left);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || (token->kind != VIGIL_TOKEN_GREATER && token->kind != VIGIL_TOKEN_GREATER_EQUAL &&
                              token->kind != VIGIL_TOKEN_LESS && token->kind != VIGIL_TOKEN_LESS_EQUAL))
        {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_shift(program, cursor, &right);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            return status;
        }
        if ((!vigil_parser_type_is_integer(left.type) || !vigil_parser_type_equal(left.type, right.type)) &&
            (!vigil_parser_type_is_f64(left.type) || !vigil_parser_type_is_f64(right.type)) &&
            !(vigil_parser_type_is_string(left.type) && vigil_parser_type_is_string(right.type)))
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(program, token->span,
                                        "comparison operators require matching integer, f64, or string operands");
        }

        if (vigil_parser_type_is_integer(left.type))
        {
            if (vigil_parser_type_is_unsigned_integer(left.type))
            {
                switch (token->kind)
                {
                case VIGIL_TOKEN_GREATER:
                    comparison_result = vigil_value_as_uint(&left.value) > vigil_value_as_uint(&right.value);
                    break;
                case VIGIL_TOKEN_GREATER_EQUAL:
                    comparison_result = vigil_value_as_uint(&left.value) >= vigil_value_as_uint(&right.value);
                    break;
                case VIGIL_TOKEN_LESS:
                    comparison_result = vigil_value_as_uint(&left.value) < vigil_value_as_uint(&right.value);
                    break;
                default:
                    comparison_result = vigil_value_as_uint(&left.value) <= vigil_value_as_uint(&right.value);
                    break;
                }
            }
            else
            {
                switch (token->kind)
                {
                case VIGIL_TOKEN_GREATER:
                    comparison_result = vigil_value_as_int(&left.value) > vigil_value_as_int(&right.value);
                    break;
                case VIGIL_TOKEN_GREATER_EQUAL:
                    comparison_result = vigil_value_as_int(&left.value) >= vigil_value_as_int(&right.value);
                    break;
                case VIGIL_TOKEN_LESS:
                    comparison_result = vigil_value_as_int(&left.value) < vigil_value_as_int(&right.value);
                    break;
                default:
                    comparison_result = vigil_value_as_int(&left.value) <= vigil_value_as_int(&right.value);
                    break;
                }
            }
        }
        else
        {
            switch (token->kind)
            {
            case VIGIL_TOKEN_GREATER:
                comparison_result = vigil_value_as_float(&left.value) > vigil_value_as_float(&right.value);
                break;
            case VIGIL_TOKEN_GREATER_EQUAL:
                comparison_result = vigil_value_as_float(&left.value) >= vigil_value_as_float(&right.value);
                break;
            case VIGIL_TOKEN_LESS:
                comparison_result = vigil_value_as_float(&left.value) < vigil_value_as_float(&right.value);
                break;
            default:
                comparison_result = vigil_value_as_float(&left.value) <= vigil_value_as_float(&right.value);
                break;
            }
        }

        vigil_constant_result_release(&left);
        vigil_value_init_bool(&left.value, comparison_result);
        left.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_equality(vigil_program_state_t *program, size_t *cursor,
                                                            vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int is_equal;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_comparison(program, cursor, &left);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || (token->kind != VIGIL_TOKEN_EQUAL_EQUAL && token->kind != VIGIL_TOKEN_BANG_EQUAL))
        {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_comparison(program, cursor, &right);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_equal(left.type, right.type))
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(program, token->span, "equality operands must have matching types");
        }

        is_equal = vigil_program_values_equal(&left.value, &right.value);
        if (token->kind == VIGIL_TOKEN_BANG_EQUAL)
        {
            is_equal = !is_equal;
        }
        vigil_constant_result_release(&left);
        vigil_value_init_bool(&left.value, is_equal);
        left.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_bitwise_and(vigil_program_state_t *program, size_t *cursor,
                                                               vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_equality(program, cursor, &left);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_AMPERSAND)
        {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_equality(program, cursor, &right);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_is_integer(left.type) || !vigil_parser_type_equal(left.type, right.type))
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(program, token->span, "bitwise operators require matching integer operands");
        }

        if (vigil_parser_type_is_unsigned_integer(left.type))
        {
            uinteger_result = vigil_value_as_uint(&left.value) & vigil_value_as_uint(&right.value);
        }
        else
        {
            integer_result = vigil_value_as_int(&left.value) & vigil_value_as_int(&right.value);
        }
        status = vigil_program_validate_integer_value_for_type(
            program, token->span, left.type,
            vigil_parser_type_is_unsigned_integer(left.type)
                ? &(vigil_value_t){vigil_nanbox_encode_uint(uinteger_result)}
                : &(vigil_value_t){vigil_nanbox_encode_int(integer_result)});
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return status;
        }
        {
            vigil_parser_type_t integer_type = left.type;

            vigil_constant_result_release(&left);
            if (vigil_parser_type_is_unsigned_integer(integer_type))
            {
                vigil_value_init_uint(&left.value, uinteger_result);
            }
            else
            {
                vigil_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_bitwise_xor(vigil_program_state_t *program, size_t *cursor,
                                                               vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_bitwise_and(program, cursor, &left);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_CARET)
        {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_bitwise_and(program, cursor, &right);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_is_integer(left.type) || !vigil_parser_type_equal(left.type, right.type))
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(program, token->span, "bitwise operators require matching integer operands");
        }

        if (vigil_parser_type_is_unsigned_integer(left.type))
        {
            uinteger_result = vigil_value_as_uint(&left.value) ^ vigil_value_as_uint(&right.value);
        }
        else
        {
            integer_result = vigil_value_as_int(&left.value) ^ vigil_value_as_int(&right.value);
        }
        status = vigil_program_validate_integer_value_for_type(
            program, token->span, left.type,
            vigil_parser_type_is_unsigned_integer(left.type)
                ? &(vigil_value_t){vigil_nanbox_encode_uint(uinteger_result)}
                : &(vigil_value_t){vigil_nanbox_encode_int(integer_result)});
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return status;
        }
        {
            vigil_parser_type_t integer_type = left.type;

            vigil_constant_result_release(&left);
            if (vigil_parser_type_is_unsigned_integer(integer_type))
            {
                vigil_value_init_uint(&left.value, uinteger_result);
            }
            else
            {
                vigil_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_bitwise_or(vigil_program_state_t *program, size_t *cursor,
                                                              vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_bitwise_xor(program, cursor, &left);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_PIPE)
        {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_bitwise_xor(program, cursor, &right);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_is_integer(left.type) || !vigil_parser_type_equal(left.type, right.type))
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(program, token->span, "bitwise operators require matching integer operands");
        }

        if (vigil_parser_type_is_unsigned_integer(left.type))
        {
            uinteger_result = vigil_value_as_uint(&left.value) | vigil_value_as_uint(&right.value);
        }
        else
        {
            integer_result = vigil_value_as_int(&left.value) | vigil_value_as_int(&right.value);
        }
        status = vigil_program_validate_integer_value_for_type(
            program, token->span, left.type,
            vigil_parser_type_is_unsigned_integer(left.type)
                ? &(vigil_value_t){vigil_nanbox_encode_uint(uinteger_result)}
                : &(vigil_value_t){vigil_nanbox_encode_int(integer_result)});
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return status;
        }
        {
            vigil_parser_type_t integer_type = left.type;

            vigil_constant_result_release(&left);
            if (vigil_parser_type_is_unsigned_integer(integer_type))
            {
                vigil_value_init_uint(&left.value, uinteger_result);
            }
            else
            {
                vigil_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_logical_and(vigil_program_state_t *program, size_t *cursor,
                                                               vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int boolean_result;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_bitwise_or(program, cursor, &left);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_AMPERSAND_AMPERSAND)
        {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_bitwise_or(program, cursor, &right);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_equal(left.type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL)) ||
            !vigil_parser_type_equal(right.type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL)))
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(program, token->span, "logical '&&' requires bool operands");
        }

        boolean_result = vigil_value_as_bool(&left.value) && vigil_value_as_bool(&right.value);
        vigil_constant_result_release(&left);
        vigil_value_init_bool(&left.value, boolean_result);
        left.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_logical_or(vigil_program_state_t *program, size_t *cursor,
                                                              vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int boolean_result;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_logical_and(program, cursor, &left);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    while (1)
    {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_PIPE_PIPE)
        {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_logical_and(program, cursor, &right);
        if (status != VIGIL_STATUS_OK)
        {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_equal(left.type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL)) ||
            !vigil_parser_type_equal(right.type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL)))
        {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(program, token->span, "logical '||' requires bool operands");
        }

        boolean_result = vigil_value_as_bool(&left.value) || vigil_value_as_bool(&right.value);
        vigil_constant_result_release(&left);
        vigil_value_init_bool(&left.value, boolean_result);
        left.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        vigil_constant_result_release(&right);
    }
}

vigil_status_t vigil_program_parse_constant_expression(vigil_program_state_t *program, size_t *cursor,
                                                       vigil_constant_result_t *out_result)
{
    vigil_status_t status;
    vigil_constant_result_t condition_result;
    vigil_constant_result_t then_result;
    vigil_constant_result_t else_result;
    const vigil_token_t *question_token;
    const vigil_token_t *colon_token;
    int take_then_branch;

    vigil_constant_result_clear(&condition_result);
    vigil_constant_result_clear(&then_result);
    vigil_constant_result_clear(&else_result);

    status = vigil_program_parse_constant_logical_or(program, cursor, &condition_result);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    question_token = vigil_program_cursor_peek(program, *cursor);
    if (question_token == NULL || question_token->kind != VIGIL_TOKEN_QUESTION)
    {
        *out_result = condition_result;
        return VIGIL_STATUS_OK;
    }

    if (!vigil_parser_type_equal(condition_result.type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL)))
    {
        vigil_constant_result_release(&condition_result);
        return vigil_compile_report(program, question_token->span, "ternary condition must be bool");
    }

    vigil_program_cursor_advance(program, cursor);
    status = vigil_program_parse_constant_expression(program, cursor, &then_result);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_constant_result_release(&condition_result);
        return status;
    }

    colon_token = vigil_program_cursor_peek(program, *cursor);
    if (colon_token == NULL || colon_token->kind != VIGIL_TOKEN_COLON)
    {
        vigil_constant_result_release(&condition_result);
        vigil_constant_result_release(&then_result);
        return vigil_compile_report(program, question_token->span, "expected ':' in ternary expression");
    }
    vigil_program_cursor_advance(program, cursor);

    status = vigil_program_parse_constant_expression(program, cursor, &else_result);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_constant_result_release(&condition_result);
        vigil_constant_result_release(&then_result);
        return status;
    }

    if (!vigil_parser_type_equal(then_result.type, else_result.type))
    {
        vigil_constant_result_release(&condition_result);
        vigil_constant_result_release(&then_result);
        vigil_constant_result_release(&else_result);
        return vigil_compile_report(program, colon_token->span, "ternary branches must have the same type");
    }

    take_then_branch = vigil_value_as_bool(&condition_result.value);
    if (take_then_branch)
    {
        *out_result = then_result;
        vigil_constant_result_clear(&then_result);
    }
    else
    {
        *out_result = else_result;
        vigil_constant_result_clear(&else_result);
    }

    vigil_constant_result_release(&condition_result);
    vigil_constant_result_release(&then_result);
    vigil_constant_result_release(&else_result);
    return VIGIL_STATUS_OK;
}
