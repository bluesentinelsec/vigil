#include <string.h>

#include "vigil/type.h"

static int vigil_type_kind_is_integer(vigil_type_kind_t kind)
{
    return kind == VIGIL_TYPE_I32 || kind == VIGIL_TYPE_I64 || kind == VIGIL_TYPE_U8 || kind == VIGIL_TYPE_U32 ||
           kind == VIGIL_TYPE_U64;
}

static int vigil_type_kind_is_signed_integer(vigil_type_kind_t kind)
{
    return kind == VIGIL_TYPE_I32 || kind == VIGIL_TYPE_I64;
}

static int vigil_type_kinds_match(vigil_type_kind_t left_type, vigil_type_kind_t right_type)
{
    return vigil_type_kind_is_valid(left_type) && vigil_type_kind_is_valid(right_type) && left_type == right_type;
}

const char *vigil_type_kind_name(vigil_type_kind_t kind)
{
    switch (kind)
    {
    case VIGIL_TYPE_I32:
        return "i32";
    case VIGIL_TYPE_I64:
        return "i64";
    case VIGIL_TYPE_U8:
        return "u8";
    case VIGIL_TYPE_U32:
        return "u32";
    case VIGIL_TYPE_U64:
        return "u64";
    case VIGIL_TYPE_F64:
        return "f64";
    case VIGIL_TYPE_BOOL:
        return "bool";
    case VIGIL_TYPE_STRING:
        return "string";
    case VIGIL_TYPE_ERR:
        return "err";
    case VIGIL_TYPE_VOID:
        return "void";
    case VIGIL_TYPE_NIL:
        return "nil";
    case VIGIL_TYPE_OBJECT:
        return "object";
    default:
        return "invalid";
    }
}

int vigil_type_kind_is_valid(vigil_type_kind_t kind)
{
    return kind >= VIGIL_TYPE_I32 && kind <= VIGIL_TYPE_OBJECT;
}

vigil_type_kind_t vigil_type_kind_from_name(const char *text, size_t length)
{
    if (text == NULL)
    {
        return VIGIL_TYPE_INVALID;
    }

    if (length == 3U && memcmp(text, "i32", 3U) == 0)
    {
        return VIGIL_TYPE_I32;
    }

    if (length == 3U && memcmp(text, "i64", 3U) == 0)
    {
        return VIGIL_TYPE_I64;
    }

    if (length == 2U && memcmp(text, "u8", 2U) == 0)
    {
        return VIGIL_TYPE_U8;
    }

    if (length == 3U && memcmp(text, "u32", 3U) == 0)
    {
        return VIGIL_TYPE_U32;
    }

    if (length == 3U && memcmp(text, "u64", 3U) == 0)
    {
        return VIGIL_TYPE_U64;
    }

    if (length == 3U && memcmp(text, "f64", 3U) == 0)
    {
        return VIGIL_TYPE_F64;
    }

    if (length == 4U && memcmp(text, "bool", 4U) == 0)
    {
        return VIGIL_TYPE_BOOL;
    }

    if (length == 6U && memcmp(text, "string", 6U) == 0)
    {
        return VIGIL_TYPE_STRING;
    }

    if (length == 3U && memcmp(text, "err", 3U) == 0)
    {
        return VIGIL_TYPE_ERR;
    }

    if (length == 4U && memcmp(text, "void", 4U) == 0)
    {
        return VIGIL_TYPE_VOID;
    }

    if (length == 3U && memcmp(text, "nil", 3U) == 0)
    {
        return VIGIL_TYPE_NIL;
    }

    return VIGIL_TYPE_INVALID;
}

int vigil_type_is_assignable(vigil_type_kind_t target_type, vigil_type_kind_t source_type)
{
    return vigil_type_kinds_match(target_type, source_type);
}

int vigil_type_supports_unary_operator(vigil_unary_operator_kind_t operator_kind, vigil_type_kind_t operand_type)
{
    if (!vigil_type_kind_is_valid(operand_type))
    {
        return 0;
    }

    switch (operator_kind)
    {
    case VIGIL_UNARY_OPERATOR_NEGATE:
        return vigil_type_kind_is_signed_integer(operand_type) || operand_type == VIGIL_TYPE_F64;
    case VIGIL_UNARY_OPERATOR_LOGICAL_NOT:
        return operand_type == VIGIL_TYPE_BOOL;
    case VIGIL_UNARY_OPERATOR_BITWISE_NOT:
        return vigil_type_kind_is_signed_integer(operand_type);
    default:
        return 0;
    }
}

int vigil_type_supports_binary_operator(vigil_binary_operator_kind_t operator_kind, vigil_type_kind_t left_type,
                                        vigil_type_kind_t right_type)
{
    if (!vigil_type_kind_is_valid(left_type) || !vigil_type_kind_is_valid(right_type))
    {
        return 0;
    }

    switch (operator_kind)
    {
    case VIGIL_BINARY_OPERATOR_ADD:
        return (vigil_type_kind_is_integer(left_type) && vigil_type_kinds_match(left_type, right_type)) ||
               (left_type == VIGIL_TYPE_F64 && right_type == VIGIL_TYPE_F64) ||
               (left_type == VIGIL_TYPE_STRING && right_type == VIGIL_TYPE_STRING);
    case VIGIL_BINARY_OPERATOR_SUBTRACT:
    case VIGIL_BINARY_OPERATOR_MULTIPLY:
    case VIGIL_BINARY_OPERATOR_DIVIDE:
        return (vigil_type_kind_is_integer(left_type) && vigil_type_kinds_match(left_type, right_type)) ||
               (left_type == VIGIL_TYPE_F64 && right_type == VIGIL_TYPE_F64);
    case VIGIL_BINARY_OPERATOR_MODULO:
    case VIGIL_BINARY_OPERATOR_BITWISE_AND:
    case VIGIL_BINARY_OPERATOR_BITWISE_OR:
    case VIGIL_BINARY_OPERATOR_BITWISE_XOR:
    case VIGIL_BINARY_OPERATOR_SHIFT_LEFT:
    case VIGIL_BINARY_OPERATOR_SHIFT_RIGHT:
        return vigil_type_kind_is_integer(left_type) && vigil_type_kinds_match(left_type, right_type);
    case VIGIL_BINARY_OPERATOR_GREATER:
    case VIGIL_BINARY_OPERATOR_GREATER_EQUAL:
    case VIGIL_BINARY_OPERATOR_LESS:
    case VIGIL_BINARY_OPERATOR_LESS_EQUAL:
        return (vigil_type_kind_is_integer(left_type) && vigil_type_kinds_match(left_type, right_type)) ||
               (left_type == VIGIL_TYPE_F64 && right_type == VIGIL_TYPE_F64) ||
               (left_type == VIGIL_TYPE_STRING && right_type == VIGIL_TYPE_STRING);
    case VIGIL_BINARY_OPERATOR_EQUAL:
    case VIGIL_BINARY_OPERATOR_NOT_EQUAL:
        return vigil_type_kinds_match(left_type, right_type);
    case VIGIL_BINARY_OPERATOR_LOGICAL_AND:
    case VIGIL_BINARY_OPERATOR_LOGICAL_OR:
        return left_type == VIGIL_TYPE_BOOL && right_type == VIGIL_TYPE_BOOL;
    default:
        return 0;
    }
}

int vigil_function_signature_is_valid(const vigil_function_signature_t *signature)
{
    size_t index;

    if (signature == NULL || !vigil_type_kind_is_valid(signature->return_type))
    {
        return 0;
    }

    if (signature->parameter_count != 0U && signature->parameter_types == NULL)
    {
        return 0;
    }

    for (index = 0U; index < signature->parameter_count; index += 1U)
    {
        if (!vigil_type_kind_is_valid(signature->parameter_types[index]))
        {
            return 0;
        }
    }

    return 1;
}

int vigil_function_signature_accepts_arguments(const vigil_function_signature_t *signature,
                                               const vigil_type_kind_t *argument_types, size_t argument_count)
{
    size_t index;

    if (!vigil_function_signature_is_valid(signature))
    {
        return 0;
    }

    if (signature->parameter_count != argument_count)
    {
        return 0;
    }

    if (argument_count != 0U && argument_types == NULL)
    {
        return 0;
    }

    for (index = 0U; index < argument_count; index += 1U)
    {
        if (!vigil_type_is_assignable(signature->parameter_types[index], argument_types[index]))
        {
            return 0;
        }
    }

    return 1;
}
