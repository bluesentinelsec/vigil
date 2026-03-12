#include <string.h>

#include "basl/type.h"

static int basl_type_kinds_match(basl_type_kind_t left_type, basl_type_kind_t right_type) {
    return basl_type_kind_is_valid(left_type) &&
           basl_type_kind_is_valid(right_type) &&
           left_type == right_type;
}

const char *basl_type_kind_name(basl_type_kind_t kind) {
    switch (kind) {
        case BASL_TYPE_I32:
            return "i32";
        case BASL_TYPE_BOOL:
            return "bool";
        case BASL_TYPE_NIL:
            return "nil";
        case BASL_TYPE_OBJECT:
            return "object";
        default:
            return "invalid";
    }
}

int basl_type_kind_is_valid(basl_type_kind_t kind) {
    return kind >= BASL_TYPE_I32 && kind <= BASL_TYPE_OBJECT;
}

basl_type_kind_t basl_type_kind_from_name(const char *text, size_t length) {
    if (text == NULL) {
        return BASL_TYPE_INVALID;
    }

    if (length == 3U && memcmp(text, "i32", 3U) == 0) {
        return BASL_TYPE_I32;
    }

    if (length == 4U && memcmp(text, "bool", 4U) == 0) {
        return BASL_TYPE_BOOL;
    }

    if (length == 3U && memcmp(text, "nil", 3U) == 0) {
        return BASL_TYPE_NIL;
    }

    return BASL_TYPE_INVALID;
}

int basl_type_is_assignable(basl_type_kind_t target_type, basl_type_kind_t source_type) {
    return basl_type_kinds_match(target_type, source_type);
}

int basl_type_supports_unary_operator(
    basl_unary_operator_kind_t operator_kind,
    basl_type_kind_t operand_type
) {
    if (!basl_type_kind_is_valid(operand_type)) {
        return 0;
    }

    switch (operator_kind) {
        case BASL_UNARY_OPERATOR_NEGATE:
            return operand_type == BASL_TYPE_I32;
        case BASL_UNARY_OPERATOR_LOGICAL_NOT:
            return operand_type == BASL_TYPE_BOOL;
        default:
            return 0;
    }
}

int basl_type_supports_binary_operator(
    basl_binary_operator_kind_t operator_kind,
    basl_type_kind_t left_type,
    basl_type_kind_t right_type
) {
    if (!basl_type_kind_is_valid(left_type) || !basl_type_kind_is_valid(right_type)) {
        return 0;
    }

    switch (operator_kind) {
        case BASL_BINARY_OPERATOR_ADD:
        case BASL_BINARY_OPERATOR_SUBTRACT:
        case BASL_BINARY_OPERATOR_MULTIPLY:
        case BASL_BINARY_OPERATOR_DIVIDE:
        case BASL_BINARY_OPERATOR_MODULO:
        case BASL_BINARY_OPERATOR_GREATER:
        case BASL_BINARY_OPERATOR_GREATER_EQUAL:
        case BASL_BINARY_OPERATOR_LESS:
        case BASL_BINARY_OPERATOR_LESS_EQUAL:
            return left_type == BASL_TYPE_I32 && right_type == BASL_TYPE_I32;
        case BASL_BINARY_OPERATOR_EQUAL:
        case BASL_BINARY_OPERATOR_NOT_EQUAL:
            return basl_type_kinds_match(left_type, right_type);
        case BASL_BINARY_OPERATOR_LOGICAL_AND:
        case BASL_BINARY_OPERATOR_LOGICAL_OR:
            return left_type == BASL_TYPE_BOOL && right_type == BASL_TYPE_BOOL;
        default:
            return 0;
    }
}

int basl_function_signature_is_valid(const basl_function_signature_t *signature) {
    size_t index;

    if (signature == NULL || !basl_type_kind_is_valid(signature->return_type)) {
        return 0;
    }

    if (signature->parameter_count != 0U && signature->parameter_types == NULL) {
        return 0;
    }

    for (index = 0U; index < signature->parameter_count; index += 1U) {
        if (!basl_type_kind_is_valid(signature->parameter_types[index])) {
            return 0;
        }
    }

    return 1;
}

int basl_function_signature_accepts_arguments(
    const basl_function_signature_t *signature,
    const basl_type_kind_t *argument_types,
    size_t argument_count
) {
    size_t index;

    if (!basl_function_signature_is_valid(signature)) {
        return 0;
    }

    if (signature->parameter_count != argument_count) {
        return 0;
    }

    if (argument_count != 0U && argument_types == NULL) {
        return 0;
    }

    for (index = 0U; index < argument_count; index += 1U) {
        if (!basl_type_is_assignable(signature->parameter_types[index], argument_types[index])) {
            return 0;
        }
    }

    return 1;
}
