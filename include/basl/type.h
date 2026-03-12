#ifndef BASL_TYPE_H
#define BASL_TYPE_H

#include <stddef.h>

#include "basl/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum basl_type_kind {
    BASL_TYPE_INVALID = 0,
    BASL_TYPE_I32 = 1,
    BASL_TYPE_BOOL = 2,
    BASL_TYPE_NIL = 3
} basl_type_kind_t;

typedef enum basl_unary_operator_kind {
    BASL_UNARY_OPERATOR_NEGATE = 0,
    BASL_UNARY_OPERATOR_LOGICAL_NOT = 1
} basl_unary_operator_kind_t;

typedef enum basl_binary_operator_kind {
    BASL_BINARY_OPERATOR_ADD = 0,
    BASL_BINARY_OPERATOR_SUBTRACT = 1,
    BASL_BINARY_OPERATOR_MULTIPLY = 2,
    BASL_BINARY_OPERATOR_DIVIDE = 3,
    BASL_BINARY_OPERATOR_MODULO = 4,
    BASL_BINARY_OPERATOR_GREATER = 5,
    BASL_BINARY_OPERATOR_GREATER_EQUAL = 6,
    BASL_BINARY_OPERATOR_LESS = 7,
    BASL_BINARY_OPERATOR_LESS_EQUAL = 8,
    BASL_BINARY_OPERATOR_EQUAL = 9,
    BASL_BINARY_OPERATOR_NOT_EQUAL = 10,
    BASL_BINARY_OPERATOR_LOGICAL_AND = 11,
    BASL_BINARY_OPERATOR_LOGICAL_OR = 12
} basl_binary_operator_kind_t;

typedef struct basl_function_signature {
    basl_type_kind_t return_type;
    const basl_type_kind_t *parameter_types;
    size_t parameter_count;
} basl_function_signature_t;

BASL_API const char *basl_type_kind_name(basl_type_kind_t kind);
BASL_API int basl_type_kind_is_valid(basl_type_kind_t kind);
BASL_API basl_type_kind_t basl_type_kind_from_name(const char *text, size_t length);
BASL_API int basl_type_is_assignable(
    basl_type_kind_t target_type,
    basl_type_kind_t source_type
);
BASL_API int basl_type_supports_unary_operator(
    basl_unary_operator_kind_t operator_kind,
    basl_type_kind_t operand_type
);
BASL_API int basl_type_supports_binary_operator(
    basl_binary_operator_kind_t operator_kind,
    basl_type_kind_t left_type,
    basl_type_kind_t right_type
);
BASL_API int basl_function_signature_is_valid(
    const basl_function_signature_t *signature
);
BASL_API int basl_function_signature_accepts_arguments(
    const basl_function_signature_t *signature,
    const basl_type_kind_t *argument_types,
    size_t argument_count
);

#ifdef __cplusplus
}
#endif

#endif
