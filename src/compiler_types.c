#include "internal/basl_compiler_types.h"

int basl_parser_type_is_primitive(
    basl_parser_type_t type
) {
    return type.kind != BASL_TYPE_INVALID &&
           type.kind != BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

int basl_parser_type_is_class(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_CLASS &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

int basl_parser_type_is_interface(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_INTERFACE &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

int basl_parser_type_is_enum(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_I32 &&
           type.object_kind == BASL_BINDING_OBJECT_ENUM &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

int basl_parser_type_is_array(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_ARRAY &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

int basl_parser_type_is_map(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_MAP &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

int basl_parser_type_supports_map_key(
    basl_parser_type_t type
) {
    return (type.kind == BASL_TYPE_STRING &&
            type.object_kind == BASL_BINDING_OBJECT_NONE) ||
           (type.kind == BASL_TYPE_BOOL &&
            type.object_kind == BASL_BINDING_OBJECT_NONE) ||
           ((type.kind == BASL_TYPE_I32 ||
             type.kind == BASL_TYPE_I64 ||
             type.kind == BASL_TYPE_U8 ||
             type.kind == BASL_TYPE_U32 ||
             type.kind == BASL_TYPE_U64) &&
            type.object_kind == BASL_BINDING_OBJECT_NONE) ||
           (type.kind == BASL_TYPE_I32 &&
            type.object_kind == BASL_BINDING_OBJECT_ENUM &&
            type.object_index != BASL_BINDING_INVALID_CLASS_INDEX);
}

int basl_parser_type_is_function(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_FUNCTION &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

int basl_parser_type_is_void(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_VOID &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

int basl_parser_type_is_i32(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_I32 &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

int basl_parser_type_is_i64(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_I64 &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

int basl_parser_type_is_u8(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_U8 &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

int basl_parser_type_is_u32(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_U32 &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

int basl_parser_type_is_u64(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_U64 &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

int basl_parser_type_is_integer(
    basl_parser_type_t type
) {
    return basl_parser_type_is_i32(type) ||
           basl_parser_type_is_i64(type) ||
           basl_parser_type_is_u8(type) ||
           basl_parser_type_is_u32(type) ||
           basl_parser_type_is_u64(type);
}

int basl_parser_type_is_signed_integer(
    basl_parser_type_t type
) {
    return basl_parser_type_is_i32(type) || basl_parser_type_is_i64(type);
}

int basl_parser_type_is_unsigned_integer(
    basl_parser_type_t type
) {
    return basl_parser_type_is_u8(type) ||
           basl_parser_type_is_u32(type) ||
           basl_parser_type_is_u64(type);
}

int basl_parser_type_is_f64(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_F64 &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

int basl_parser_type_is_bool(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_BOOL &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

int basl_parser_type_is_string(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_STRING &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

int basl_parser_type_is_err(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_ERR &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

basl_parser_type_t basl_function_return_type_at(
    const basl_function_decl_t *decl,
    size_t index
) {
    if (decl == NULL || index >= decl->return_count) {
        return basl_binding_type_invalid();
    }

    if (decl->return_count == 1U || decl->return_types == NULL) {
        return decl->return_type;
    }

    return decl->return_types[index];
}

const basl_parser_type_t *basl_function_return_types(
    const basl_function_decl_t *decl
) {
    if (decl == NULL || decl->return_count == 0U) {
        return NULL;
    }

    if (decl->return_count == 1U || decl->return_types == NULL) {
        return &decl->return_type;
    }

    return decl->return_types;
}

basl_parser_type_t basl_interface_method_return_type_at(
    const basl_interface_method_t *method,
    size_t index
) {
    if (method == NULL || index >= method->return_count) {
        return basl_binding_type_invalid();
    }

    if (method->return_count == 1U || method->return_types == NULL) {
        return method->return_type;
    }

    return method->return_types[index];
}

const basl_parser_type_t *basl_interface_method_return_types(
    const basl_interface_method_t *method
) {
    if (method == NULL || method->return_count == 0U) {
        return NULL;
    }

    if (method->return_count == 1U || method->return_types == NULL) {
        return &method->return_type;
    }

    return method->return_types;
}

int basl_parser_type_equal(
    basl_parser_type_t left,
    basl_parser_type_t right
) {
    return basl_binding_type_equal(left, right);
}

int basl_program_type_is_assignable(
    const basl_program_state_t *program,
    basl_parser_type_t target_type,
    basl_parser_type_t source_type
) {
    if (!basl_binding_type_is_valid(target_type) || !basl_binding_type_is_valid(source_type)) {
        return 0;
    }

    if (basl_parser_type_is_interface(target_type) && basl_parser_type_is_class(source_type)) {
        if (program == NULL) {
            return 0;
        }

        return basl_class_decl_implements_interface(
            &program->classes[source_type.object_index],
            target_type.object_index
        );
    }

    if (basl_parser_type_is_function(target_type)) {
        const basl_function_type_decl_t *target_decl;
        const basl_function_type_decl_t *source_decl;
        size_t index;

        if (program == NULL || !basl_parser_type_is_function(source_type)) {
            return 0;
        }
        target_decl = basl_program_function_type_decl(program, target_type);
        source_decl = basl_program_function_type_decl(program, source_type);
        if (target_decl == NULL || source_decl == NULL) {
            return 0;
        }
        if (target_decl->is_any) {
            return 1;
        }
        if (source_decl->is_any) {
            return 0;
        }
        if (
            target_decl->param_count != source_decl->param_count ||
            target_decl->return_count != source_decl->return_count
        ) {
            return 0;
        }
        for (index = 0U; index < target_decl->param_count; index += 1U) {
            if (!basl_parser_type_equal(target_decl->param_types[index], source_decl->param_types[index])) {
                return 0;
            }
        }
        for (index = 0U; index < target_decl->return_count; index += 1U) {
            if (!basl_parser_type_equal(target_decl->return_types[index], source_decl->return_types[index])) {
                return 0;
            }
        }
        return 1;
    }

    if (
        basl_parser_type_is_class(target_type) ||
        basl_parser_type_is_class(source_type) ||
        basl_parser_type_is_interface(target_type) ||
        basl_parser_type_is_interface(source_type) ||
        basl_parser_type_is_enum(target_type) ||
        basl_parser_type_is_enum(source_type) ||
        basl_parser_type_is_array(target_type) ||
        basl_parser_type_is_array(source_type) ||
        basl_parser_type_is_function(target_type) ||
        basl_parser_type_is_function(source_type) ||
        basl_parser_type_is_map(target_type) ||
        basl_parser_type_is_map(source_type)
    ) {
        return basl_parser_type_equal(target_type, source_type);
    }

    return basl_type_is_assignable(target_type.kind, source_type.kind);
}

int basl_parser_type_supports_unary_operator(
    basl_unary_operator_kind_t operator_kind,
    basl_parser_type_t operand_type
) {
    if (!basl_parser_type_is_primitive(operand_type)) {
        return 0;
    }

    return basl_type_supports_unary_operator(operator_kind, operand_type.kind);
}

int basl_parser_type_supports_binary_operator(
    basl_binary_operator_kind_t operator_kind,
    basl_parser_type_t left_type,
    basl_parser_type_t right_type
) {
    if (!basl_binding_type_is_valid(left_type) || !basl_binding_type_is_valid(right_type)) {
        return 0;
    }

    if (
        operator_kind == BASL_BINARY_OPERATOR_EQUAL ||
        operator_kind == BASL_BINARY_OPERATOR_NOT_EQUAL
    ) {
        if (basl_parser_type_is_function(left_type) && basl_parser_type_is_function(right_type)) {
            return 1;
        }

        return basl_parser_type_equal(left_type, right_type);
    }

    if (!basl_parser_type_is_primitive(left_type) || !basl_parser_type_is_primitive(right_type)) {
        return 0;
    }

    return basl_type_supports_binary_operator(
        operator_kind,
        left_type.kind,
        right_type.kind
    );
}


const basl_function_type_decl_t *basl_program_function_type_decl(
    const basl_program_state_t *program,
    basl_parser_type_t type
) {
    if (
        program == NULL ||
        !basl_parser_type_is_function(type) ||
        type.object_index >= program->function_type_count
    ) {
        return NULL;
    }

    return &program->function_types[type.object_index];
}

int basl_class_decl_implements_interface(
    const basl_class_decl_t *decl,
    size_t interface_index
) {
    size_t i;

    if (decl == NULL) {
        return 0;
    }

    for (i = 0U; i < decl->implemented_interface_count; i += 1U) {
        if (decl->implemented_interfaces[i] == interface_index) {
            return 1;
        }
    }

    return 0;
}
