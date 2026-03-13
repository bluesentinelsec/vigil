#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "basl/chunk.h"
#include "basl/lexer.h"
#include "basl/string.h"
#include "basl/token.h"
#include "basl/type.h"
#include "internal/basl_binding.h"
#include "internal/basl_compiler_internal.h"
#include "internal/basl_internal.h"

typedef basl_binding_type_t basl_parser_type_t;
typedef basl_binding_function_t basl_function_decl_t;
typedef basl_binding_function_param_t basl_function_param_t;

typedef struct basl_class_field {
    const char *name;
    size_t name_length;
    basl_source_span_t name_span;
    int is_public;
    basl_parser_type_t type;
} basl_class_field_t;

typedef struct basl_class_method {
    const char *name;
    size_t name_length;
    basl_source_span_t name_span;
    int is_public;
    size_t function_index;
} basl_class_method_t;

typedef struct basl_interface_method {
    const char *name;
    size_t name_length;
    basl_source_span_t name_span;
    basl_parser_type_t return_type;
    basl_parser_type_t *return_types;
    size_t return_count;
    size_t return_capacity;
    basl_parser_type_t *param_types;
    size_t param_count;
    size_t param_capacity;
} basl_interface_method_t;

typedef struct basl_class_interface_impl {
    size_t interface_index;
    size_t *function_indices;
    size_t function_count;
} basl_class_interface_impl_t;

typedef struct basl_class_decl {
    basl_source_id_t source_id;
    const char *name;
    size_t name_length;
    basl_source_span_t name_span;
    int is_public;
    basl_class_field_t *fields;
    size_t field_count;
    size_t field_capacity;
    basl_class_method_t *methods;
    size_t method_count;
    size_t method_capacity;
    size_t *implemented_interfaces;
    size_t implemented_interface_count;
    size_t implemented_interface_capacity;
    basl_class_interface_impl_t *interface_impls;
    size_t interface_impl_count;
    size_t interface_impl_capacity;
    size_t constructor_function_index;
} basl_class_decl_t;

typedef struct basl_interface_decl {
    basl_source_id_t source_id;
    const char *name;
    size_t name_length;
    basl_source_span_t name_span;
    int is_public;
    basl_interface_method_t *methods;
    size_t method_count;
    size_t method_capacity;
} basl_interface_decl_t;

typedef struct basl_enum_member {
    const char *name;
    size_t name_length;
    basl_source_span_t name_span;
    int64_t value;
} basl_enum_member_t;

typedef struct basl_enum_decl {
    basl_source_id_t source_id;
    const char *name;
    size_t name_length;
    basl_source_span_t name_span;
    int is_public;
    basl_enum_member_t *members;
    size_t member_count;
    size_t member_capacity;
} basl_enum_decl_t;

typedef struct basl_array_type_decl {
    basl_parser_type_t element_type;
} basl_array_type_decl_t;

typedef struct basl_map_type_decl {
    basl_parser_type_t key_type;
    basl_parser_type_t value_type;
} basl_map_type_decl_t;

typedef struct basl_function_type_decl {
    int is_any;
    basl_parser_type_t return_type;
    basl_parser_type_t *return_types;
    size_t return_count;
    size_t return_capacity;
    basl_parser_type_t *param_types;
    size_t param_count;
    size_t param_capacity;
} basl_function_type_decl_t;

typedef struct basl_loop_jump {
    size_t operand_offset;
    basl_source_span_t span;
} basl_loop_jump_t;

typedef struct basl_loop_context {
    size_t loop_start;
    size_t scope_depth;
    basl_loop_jump_t *break_jumps;
    size_t break_count;
    size_t break_capacity;
} basl_loop_context_t;

typedef enum basl_module_compile_state {
    BASL_MODULE_UNSEEN = 0,
    BASL_MODULE_PARSING = 1,
    BASL_MODULE_PARSED = 2
} basl_module_compile_state_t;

typedef struct basl_program_module {
    basl_source_id_t source_id;
    const basl_source_file_t *source;
    basl_token_list_t *tokens;
    basl_module_compile_state_t state;
    struct basl_module_import *imports;
    size_t import_count;
    size_t import_capacity;
} basl_program_module_t;

typedef struct basl_module_import {
    char *owned_alias;
    const char *alias;
    size_t alias_length;
    basl_source_span_t alias_span;
    basl_source_id_t source_id;
} basl_module_import_t;

typedef struct basl_global_constant {
    basl_source_id_t source_id;
    const char *name;
    size_t name_length;
    basl_source_span_t name_span;
    int is_public;
    basl_parser_type_t type;
    basl_value_t value;
} basl_global_constant_t;

typedef struct basl_global_variable {
    basl_source_id_t source_id;
    const char *name;
    size_t name_length;
    basl_source_span_t name_span;
    int is_public;
    basl_parser_type_t type;
    const basl_source_file_t *source;
    const basl_token_list_t *tokens;
    size_t initializer_start;
    size_t initializer_end;
} basl_global_variable_t;

typedef struct basl_program_state {
    const basl_source_registry_t *registry;
    const basl_source_file_t *source;
    const basl_token_list_t *tokens;
    basl_diagnostic_list_t *diagnostics;
    basl_error_t *error;
    basl_binding_function_table_t functions;
    basl_program_module_t *modules;
    size_t module_count;
    size_t module_capacity;
    basl_class_decl_t *classes;
    size_t class_count;
    size_t class_capacity;
    basl_interface_decl_t *interfaces;
    size_t interface_count;
    size_t interface_capacity;
    basl_enum_decl_t *enums;
    size_t enum_count;
    size_t enum_capacity;
    basl_array_type_decl_t *array_types;
    size_t array_type_count;
    size_t array_type_capacity;
    basl_map_type_decl_t *map_types;
    size_t map_type_count;
    size_t map_type_capacity;
    basl_function_type_decl_t *function_types;
    size_t function_type_count;
    size_t function_type_capacity;
    basl_global_constant_t *constants;
    size_t constant_count;
    size_t constant_capacity;
    basl_global_variable_t *globals;
    size_t global_count;
    size_t global_capacity;
} basl_program_state_t;

typedef struct basl_parser_state {
    const basl_program_state_t *program;
    struct basl_parser_state *parent;
    size_t current;
    size_t body_end;
    size_t function_index;
    basl_parser_type_t expected_return_type;
    const basl_parser_type_t *expected_return_types;
    size_t expected_return_count;
    basl_chunk_t chunk;
    basl_binding_scope_stack_t locals;
    basl_loop_context_t *loops;
    size_t loop_count;
    size_t loop_capacity;
    int defer_mode;
    int defer_emitted;
} basl_parser_state_t;

typedef struct basl_expression_result {
    basl_parser_type_t type;
    const basl_parser_type_t *types;
    size_t type_count;
} basl_expression_result_t;

typedef struct basl_constant_result {
    basl_parser_type_t type;
    basl_value_t value;
} basl_constant_result_t;

typedef struct basl_statement_result {
    int guaranteed_return;
} basl_statement_result_t;

typedef struct basl_binding_target {
    basl_parser_type_t type;
    const basl_token_t *name_token;
    int is_discard;
} basl_binding_target_t;

typedef struct basl_binding_target_list {
    basl_binding_target_t *items;
    size_t count;
    size_t capacity;
} basl_binding_target_list_t;

static int basl_parser_is_assignment_start(
    const basl_parser_state_t *state
);
static basl_status_t basl_parser_report(
    basl_parser_state_t *state,
    basl_source_span_t span,
    const char *message
);
static basl_status_t basl_parser_parse_assignment_statement_internal(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result,
    int expect_semicolon
);
static basl_status_t basl_parser_parse_expression_statement_internal(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result,
    int expect_semicolon
);
static basl_status_t basl_parser_parse_variable_declaration(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
);
static basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
);
static int basl_parser_is_variable_declaration_start(
    const basl_parser_state_t *state
);
static basl_status_t basl_parser_parse_declaration(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
);
static basl_status_t basl_parser_parse_switch_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
);
static const basl_function_type_decl_t *basl_program_function_type_decl(
    const basl_program_state_t *program,
    basl_parser_type_t type
);
static basl_status_t basl_program_require_non_void_type(
    const basl_program_state_t *program,
    basl_source_span_t span,
    basl_parser_type_t type,
    const char *message
);
static basl_status_t basl_parser_emit_i32_constant(
    basl_parser_state_t *state,
    int64_t value,
    basl_source_span_t span
);
static basl_status_t basl_compile_function_with_parent(
    basl_program_state_t *program,
    size_t function_index,
    const basl_parser_state_t *parent_state
);
static const basl_token_t *basl_parser_peek(const basl_parser_state_t *state);
static int basl_parser_check(
    const basl_parser_state_t *state,
    basl_token_kind_t kind
);
static const char *basl_parser_token_text(
    const basl_parser_state_t *state,
    const basl_token_t *token,
    size_t *out_length
);
static basl_status_t basl_parser_emit_opcode(
    basl_parser_state_t *state,
    basl_opcode_t opcode,
    basl_source_span_t span
);
static basl_status_t basl_parser_emit_u32(
    basl_parser_state_t *state,
    uint32_t value,
    basl_source_span_t span
);

static void basl_parser_state_free(
    basl_parser_state_t *state
) {
    size_t i;
    void *memory;

    if (state == NULL || state->program == NULL) {
        return;
    }

    for (i = 0U; i < state->loop_count; ++i) {
        memory = state->loops[i].break_jumps;
        basl_runtime_free(state->program->registry->runtime, &memory);
    }

    memory = state->loops;
    basl_runtime_free(state->program->registry->runtime, &memory);
    basl_binding_scope_stack_free(&state->locals);

    state->loops = NULL;
    state->loop_count = 0U;
    state->loop_capacity = 0U;
}

static void basl_expression_result_clear(
    basl_expression_result_t *result
) {
    if (result == NULL) {
        return;
    }

    result->type = basl_binding_type_invalid();
    result->types = NULL;
    result->type_count = 0U;
}

static void basl_expression_result_set_type(
    basl_expression_result_t *result,
    basl_parser_type_t type
) {
    if (result == NULL) {
        return;
    }

    result->type = type;
    result->types = NULL;
    result->type_count = basl_binding_type_is_valid(type) ? 1U : 0U;
}

static void basl_expression_result_set_return_types(
    basl_expression_result_t *result,
    basl_parser_type_t first_type,
    const basl_parser_type_t *types,
    size_t type_count
) {
    if (result == NULL) {
        return;
    }

    result->type = first_type;
    result->types = types;
    result->type_count = type_count;
}

static void basl_expression_result_copy(
    basl_expression_result_t *result,
    const basl_expression_result_t *source
) {
    if (result == NULL || source == NULL) {
        return;
    }

    basl_expression_result_set_return_types(
        result,
        source->type,
        source->types,
        source->type_count
    );
}

static basl_status_t basl_parser_require_scalar_expression(
    basl_parser_state_t *state,
    basl_source_span_t span,
    const basl_expression_result_t *result,
    const char *message
) {
    if (result == NULL || result->type_count == 1U) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, span, message);
}

static void basl_constant_result_clear(
    basl_constant_result_t *result
) {
    if (result == NULL) {
        return;
    }

    result->type = basl_binding_type_invalid();
    basl_value_init_nil(&result->value);
}

static void basl_constant_result_release(
    basl_constant_result_t *result
) {
    if (result == NULL) {
        return;
    }

    basl_value_release(&result->value);
    result->type = basl_binding_type_invalid();
}

static void basl_statement_result_clear(
    basl_statement_result_t *result
) {
    if (result == NULL) {
        return;
    }

    result->guaranteed_return = 0;
}

static void basl_statement_result_set_guaranteed_return(
    basl_statement_result_t *result,
    int guaranteed_return
) {
    if (result == NULL) {
        return;
    }

    result->guaranteed_return = guaranteed_return;
}

static void basl_binding_target_list_init(
    basl_binding_target_list_t *list
) {
    if (list == NULL) {
        return;
    }

    memset(list, 0, sizeof(*list));
}

static void basl_binding_target_list_free(
    basl_program_state_t *program,
    basl_binding_target_list_t *list
) {
    void *memory;

    if (program == NULL || list == NULL) {
        return;
    }

    memory = list->items;
    basl_runtime_free(program->registry->runtime, &memory);
    memset(list, 0, sizeof(*list));
}

static basl_status_t basl_binding_target_list_grow(
    basl_program_state_t *program,
    basl_binding_target_list_t *list,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= list->capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = list->capacity;
    next_capacity = old_capacity == 0U ? 2U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*list->items)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "binding target allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = list->items;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*list->items),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*list->items),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_binding_target_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*list->items)
            );
        }
    }
    if (status != BASL_STATUS_OK) {
        return status;
    }

    list->items = (basl_binding_target_t *)memory;
    list->capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_binding_target_list_append(
    basl_program_state_t *program,
    basl_binding_target_list_t *list,
    basl_parser_type_t type,
    const basl_token_t *name_token,
    int is_discard
) {
    basl_status_t status;
    basl_binding_target_t *target;

    status = basl_binding_target_list_grow(program, list, list->count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    target = &list->items[list->count];
    target->type = type;
    target->name_token = name_token;
    target->is_discard = is_discard != 0;
    list->count += 1U;
    return BASL_STATUS_OK;
}

static basl_parser_type_t basl_expression_result_type_at(
    const basl_expression_result_t *result,
    size_t index
) {
    if (result == NULL || index >= result->type_count) {
        return basl_binding_type_invalid();
    }
    if (index == 0U) {
        return result->type;
    }
    if (result->types == NULL) {
        return basl_binding_type_invalid();
    }
    return result->types[index];
}

static basl_status_t basl_compile_report(
    const basl_program_state_t *program,
    basl_source_span_t span,
    const char *message
) {
    basl_status_t status;
    basl_source_location_t location;

    status = basl_diagnostic_list_append_cstr(
        program->diagnostics,
        BASL_DIAGNOSTIC_ERROR,
        span,
        message,
        program->error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_error_set_literal(program->error, BASL_STATUS_SYNTAX_ERROR, message);
    if (program->error != NULL) {
        basl_source_location_clear(&location);
        location.source_id = span.source_id;
        location.offset = span.start_offset;
        if (basl_source_registry_resolve_location(program->registry, &location, NULL) == BASL_STATUS_OK) {
            program->error->location = location;
        } else {
            program->error->location.source_id = span.source_id;
            program->error->location.offset = span.start_offset;
        }
    }
    return BASL_STATUS_SYNTAX_ERROR;
}

static const basl_token_t *basl_program_token_at(
    const basl_program_state_t *program,
    size_t index
) {
    return basl_token_list_get(program->tokens, index);
}

static int basl_program_names_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
);
static int basl_class_decl_implements_interface(
    const basl_class_decl_t *decl,
    size_t interface_index
);

static const char *basl_program_token_text(
    const basl_program_state_t *program,
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

    return basl_string_c_str(&program->source->text) + token->span.start_offset;
}

static int basl_program_token_is_identifier_text(
    const basl_program_state_t *program,
    const basl_token_t *token,
    const char *text,
    size_t text_length
) {
    const char *token_text;
    size_t token_length;

    if (token == NULL || token->kind != BASL_TOKEN_IDENTIFIER) {
        return 0;
    }

    token_text = basl_program_token_text(program, token, &token_length);
    return basl_program_names_equal(token_text, token_length, text, text_length);
}

static basl_source_span_t basl_program_eof_span(const basl_program_state_t *program) {
    basl_source_span_t span;

    basl_source_span_clear(&span);
    if (program == NULL || program->source == NULL) {
        return span;
    }

    span.source_id = program->source->id;
    span.start_offset = basl_string_length(&program->source->text);
    span.end_offset = span.start_offset;
    return span;
}

static int basl_program_names_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
) {
    return left != NULL &&
           right != NULL &&
           left_length == right_length &&
           memcmp(left, right, left_length) == 0;
}

static int basl_parser_type_is_primitive(
    basl_parser_type_t type
) {
    return type.kind != BASL_TYPE_INVALID &&
           type.kind != BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

static int basl_parser_type_is_class(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_CLASS &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

static int basl_parser_type_is_interface(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_INTERFACE &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

static int basl_parser_type_is_enum(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_I32 &&
           type.object_kind == BASL_BINDING_OBJECT_ENUM &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

static int basl_parser_type_is_array(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_ARRAY &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

static int basl_parser_type_is_map(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_MAP &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

static int basl_parser_type_supports_map_key(
    basl_parser_type_t type
) {
    return (type.kind == BASL_TYPE_STRING &&
            type.object_kind == BASL_BINDING_OBJECT_NONE) ||
           (type.kind == BASL_TYPE_BOOL &&
            type.object_kind == BASL_BINDING_OBJECT_NONE) ||
           (type.kind == BASL_TYPE_I32 &&
            type.object_kind == BASL_BINDING_OBJECT_NONE) ||
           (type.kind == BASL_TYPE_I32 &&
            type.object_kind == BASL_BINDING_OBJECT_ENUM &&
            type.object_index != BASL_BINDING_INVALID_CLASS_INDEX);
}

static int basl_parser_type_is_function(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_OBJECT &&
           type.object_kind == BASL_BINDING_OBJECT_FUNCTION &&
           type.object_index != BASL_BINDING_INVALID_CLASS_INDEX;
}

static int basl_parser_type_is_void(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_VOID &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

static int basl_parser_type_is_i32(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_I32 &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

static int basl_parser_type_is_f64(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_F64 &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

static int basl_parser_type_is_bool(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_BOOL &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

static int basl_parser_type_is_string(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_STRING &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

static int basl_parser_type_is_err(
    basl_parser_type_t type
) {
    return type.kind == BASL_TYPE_ERR &&
           type.object_kind == BASL_BINDING_OBJECT_NONE;
}

static basl_parser_type_t basl_function_return_type_at(
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

static const basl_parser_type_t *basl_function_return_types(
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

static basl_parser_type_t basl_interface_method_return_type_at(
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

static const basl_parser_type_t *basl_interface_method_return_types(
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

static int basl_parser_type_equal(
    basl_parser_type_t left,
    basl_parser_type_t right
) {
    return basl_binding_type_equal(left, right);
}

static int basl_program_type_is_assignable(
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

static int basl_parser_type_supports_unary_operator(
    basl_unary_operator_kind_t operator_kind,
    basl_parser_type_t operand_type
) {
    if (!basl_parser_type_is_primitive(operand_type)) {
        return 0;
    }

    return basl_type_supports_unary_operator(operator_kind, operand_type.kind);
}

static int basl_parser_type_supports_binary_operator(
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

static void basl_program_set_module_context(
    basl_program_state_t *program,
    const basl_source_file_t *source,
    const basl_token_list_t *tokens
) {
    if (program == NULL) {
        return;
    }

    program->source = source;
    program->tokens = tokens;
}

static int basl_program_module_find(
    const basl_program_state_t *program,
    basl_source_id_t source_id,
    size_t *out_index
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (program == NULL || source_id == 0U) {
        return 0;
    }

    for (i = 0U; i < program->module_count; i += 1U) {
        if (program->modules[i].source_id == source_id) {
            if (out_index != NULL) {
                *out_index = i;
            }
            return 1;
        }
    }

    return 0;
}

static basl_program_module_t *basl_program_current_module(
    basl_program_state_t *program
) {
    size_t module_index;

    module_index = 0U;
    if (
        program == NULL ||
        program->source == NULL ||
        !basl_program_module_find(program, program->source->id, &module_index)
    ) {
        return NULL;
    }

    return &program->modules[module_index];
}

static const basl_program_module_t *basl_program_current_module_const(
    const basl_program_state_t *program
) {
    size_t module_index;

    module_index = 0U;
    if (
        program == NULL ||
        program->source == NULL ||
        !basl_program_module_find(program, program->source->id, &module_index)
    ) {
        return NULL;
    }

    return &program->modules[module_index];
}

static basl_status_t basl_program_grow_modules(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->module_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->module_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*program->modules)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "module table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->modules;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->modules),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->modules),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_program_module_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->modules)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->modules = (basl_program_module_t *)memory;
    program->module_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_add_module(
    basl_program_state_t *program,
    basl_source_id_t source_id,
    const basl_source_file_t *source,
    const basl_token_list_t *tokens,
    basl_module_compile_state_t state,
    size_t *out_index
) {
    basl_status_t status;
    basl_program_module_t *module;
    void *memory;

    status = basl_program_grow_modules(program, program->module_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    module = &program->modules[program->module_count];
    memset(module, 0, sizeof(*module));
    module->source_id = source_id;
    module->source = source;
    memory = NULL;
    status = basl_runtime_alloc(
        program->registry->runtime,
        sizeof(*module->tokens),
        &memory,
        program->error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    module->tokens = (basl_token_list_t *)memory;
    *module->tokens = *tokens;
    module->state = state;
    if (out_index != NULL) {
        *out_index = program->module_count;
    }
    program->module_count += 1U;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_module_grow_imports(
    basl_program_state_t *program,
    basl_program_module_t *module,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= module->import_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = module->import_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*module->imports)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "module import table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = module->imports;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*module->imports),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*module->imports),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_module_import_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*module->imports)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    module->imports = (basl_module_import_t *)memory;
    module->import_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_program_module_find_import(
    const basl_program_module_t *module,
    const char *alias,
    size_t alias_length,
    basl_source_id_t *out_source_id
) {
    size_t i;

    if (out_source_id != NULL) {
        *out_source_id = 0U;
    }
    if (module == NULL || alias == NULL) {
        return 0;
    }

    for (i = 0U; i < module->import_count; i += 1U) {
        if (
            basl_program_names_equal(
                module->imports[i].alias,
                module->imports[i].alias_length,
                alias,
                alias_length
            )
        ) {
            if (out_source_id != NULL) {
                *out_source_id = module->imports[i].source_id;
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_program_add_module_import(
    basl_program_state_t *program,
    basl_program_module_t *module,
    const char *alias,
    size_t alias_length,
    basl_source_span_t alias_span,
    basl_source_id_t source_id
) {
    basl_status_t status;
    basl_module_import_t *import_decl;
    void *memory;

    if (basl_program_module_find_import(module, alias, alias_length, NULL)) {
        return basl_compile_report(program, alias_span, "import alias is already declared");
    }

    status = basl_program_module_grow_imports(program, module, module->import_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    import_decl = &module->imports[module->import_count];
    memset(import_decl, 0, sizeof(*import_decl));
    memory = NULL;
    status = basl_runtime_alloc(
        program->registry->runtime,
        alias_length + 1U,
        &memory,
        program->error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    memcpy(memory, alias, alias_length);
    ((char *)memory)[alias_length] = '\0';
    import_decl->owned_alias = (char *)memory;
    import_decl->alias = import_decl->owned_alias;
    import_decl->alias_length = alias_length;
    import_decl->alias_span = alias_span;
    import_decl->source_id = source_id;
    module->import_count += 1U;
    return BASL_STATUS_OK;
}

static int basl_program_resolve_import_alias(
    const basl_program_state_t *program,
    const char *alias,
    size_t alias_length,
    basl_source_id_t *out_source_id
) {
    const basl_program_module_t *module;

    if (out_source_id != NULL) {
        *out_source_id = 0U;
    }
    if (program == NULL || alias == NULL) {
        return 0;
    }

    module = basl_program_current_module_const(program);
    if (module == NULL) {
        return 0;
    }

    return basl_program_module_find_import(module, alias, alias_length, out_source_id);
}

static int basl_program_path_has_basl_extension(
    const char *path,
    size_t length
) {
    return path != NULL &&
           length >= 5U &&
           memcmp(path + length - 5U, ".basl", 5U) == 0;
}

static int basl_program_path_is_absolute(
    const char *path,
    size_t length
) {
    if (path == NULL || length == 0U) {
        return 0;
    }

    if (path[0] == '/' || path[0] == '\\') {
        return 1;
    }

    return length >= 2U &&
           ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':';
}

static void basl_program_import_default_alias(
    const char *path,
    size_t path_length,
    const char **out_alias,
    size_t *out_alias_length
) {
    size_t start;
    size_t end;

    if (out_alias != NULL) {
        *out_alias = path;
    }
    if (out_alias_length != NULL) {
        *out_alias_length = path_length;
    }
    if (path == NULL) {
        return;
    }

    start = path_length;
    while (start > 0U) {
        char current = path[start - 1U];

        if (current == '/' || current == '\\') {
            break;
        }
        start -= 1U;
    }

    end = path_length;
    if (end >= 5U && memcmp(path + end - 5U, ".basl", 5U) == 0) {
        end -= 5U;
    }

    if (out_alias != NULL) {
        *out_alias = path + start;
    }
    if (out_alias_length != NULL) {
        *out_alias_length = end - start;
    }
}

static basl_status_t basl_program_resolve_import_path(
    const basl_program_state_t *program,
    const char *import_text,
    size_t import_length,
    basl_string_t *out_path
) {
    basl_status_t status;
    const char *base_path;
    size_t base_length;
    size_t prefix_length;

    basl_string_clear(out_path);
    if (program == NULL || program->source == NULL || import_text == NULL) {
        if (program != NULL) {
            basl_error_set_literal(
                program->error,
                BASL_STATUS_INVALID_ARGUMENT,
                "import path inputs must not be null"
            );
        }
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (basl_program_path_is_absolute(import_text, import_length)) {
        status = basl_string_assign(out_path, import_text, import_length, program->error);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        base_path = basl_string_c_str(&program->source->path);
        base_length = basl_string_length(&program->source->path);
        prefix_length = base_length;
        while (prefix_length > 0U) {
            char current = base_path[prefix_length - 1U];

            if (current == '/' || current == '\\') {
                break;
            }
            prefix_length -= 1U;
        }

        if (prefix_length != 0U) {
            status = basl_string_assign(out_path, base_path, prefix_length, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_string_append(out_path, import_text, import_length, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
        } else {
            status = basl_string_assign(out_path, import_text, import_length, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
        }
    }

    if (
        !basl_program_path_has_basl_extension(
            basl_string_c_str(out_path),
            basl_string_length(out_path)
        )
    ) {
        status = basl_string_append_cstr(out_path, ".basl", program->error);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static int basl_program_find_source_by_path(
    const basl_program_state_t *program,
    const char *path,
    size_t path_length,
    basl_source_id_t *out_source_id
) {
    size_t i;
    const basl_source_file_t *source;

    if (out_source_id != NULL) {
        *out_source_id = 0U;
    }
    if (program == NULL || path == NULL) {
        return 0;
    }

    for (i = 1U; i <= basl_source_registry_count(program->registry); i += 1U) {
        source = basl_source_registry_get(program->registry, (basl_source_id_t)i);
        if (source == NULL) {
            continue;
        }
        if (
            basl_program_names_equal(
                basl_string_c_str(&source->path),
                basl_string_length(&source->path),
                path,
                path_length
            )
        ) {
            if (out_source_id != NULL) {
                *out_source_id = source->id;
            }
            return 1;
        }
    }

    return 0;
}

static void basl_class_decl_free(
    basl_program_state_t *program,
    basl_class_decl_t *decl
) {
    void *memory;
    size_t i;

    if (program == NULL || decl == NULL) {
        return;
    }

    memory = decl->fields;
    basl_runtime_free(program->registry->runtime, &memory);
    memory = decl->methods;
    basl_runtime_free(program->registry->runtime, &memory);
    memory = decl->implemented_interfaces;
    basl_runtime_free(program->registry->runtime, &memory);
    for (i = 0U; i < decl->interface_impl_count; i += 1U) {
        memory = decl->interface_impls[i].function_indices;
        basl_runtime_free(program->registry->runtime, &memory);
    }
    memory = decl->interface_impls;
    basl_runtime_free(program->registry->runtime, &memory);
    memset(decl, 0, sizeof(*decl));
}

static basl_status_t basl_program_grow_classes(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->class_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->class_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*program->classes)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "class table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->classes;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->classes),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->classes),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_class_decl_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->classes)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->classes = (basl_class_decl_t *)memory;
    program->class_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_program_find_class(
    const basl_program_state_t *program,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_class_decl_t **out_class
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_class != NULL) {
        *out_class = NULL;
    }
    if (program == NULL || name == NULL) {
        return 0;
    }

    for (i = 0U; i < program->class_count; i += 1U) {
        if (
            basl_program_names_equal(
                program->classes[i].name,
                program->classes[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_class != NULL) {
                *out_class = &program->classes[i];
            }
            return 1;
        }
    }

    return 0;
}

static int basl_program_find_class_in_source(
    const basl_program_state_t *program,
    basl_source_id_t source_id,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_class_decl_t **out_class
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_class != NULL) {
        *out_class = NULL;
    }
    if (program == NULL || source_id == 0U || name == NULL) {
        return 0;
    }

    for (i = 0U; i < program->class_count; i += 1U) {
        if (program->classes[i].source_id != source_id) {
            continue;
        }
        if (
            basl_program_names_equal(
                program->classes[i].name,
                program->classes[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_class != NULL) {
                *out_class = &program->classes[i];
            }
            return 1;
        }
    }

    return 0;
}

static void basl_interface_decl_free(
    basl_program_state_t *program,
    basl_interface_decl_t *decl
) {
    void *memory;
    size_t i;

    if (program == NULL || decl == NULL) {
        return;
    }

    for (i = 0U; i < decl->method_count; i += 1U) {
        memory = decl->methods[i].param_types;
        basl_runtime_free(program->registry->runtime, &memory);
        memory = decl->methods[i].return_types;
        basl_runtime_free(program->registry->runtime, &memory);
    }
    memory = decl->methods;
    basl_runtime_free(program->registry->runtime, &memory);
    memset(decl, 0, sizeof(*decl));
}

static basl_status_t basl_program_grow_interfaces(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->interface_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->interface_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*program->interfaces)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "interface table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->interfaces;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->interfaces),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->interfaces),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_interface_decl_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->interfaces)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->interfaces = (basl_interface_decl_t *)memory;
    program->interface_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_program_find_interface(
    const basl_program_state_t *program,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_interface_decl_t **out_interface
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_interface != NULL) {
        *out_interface = NULL;
    }
    if (program == NULL || name == NULL) {
        return 0;
    }

    for (i = 0U; i < program->interface_count; i += 1U) {
        if (
            basl_program_names_equal(
                program->interfaces[i].name,
                program->interfaces[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_interface != NULL) {
                *out_interface = &program->interfaces[i];
            }
            return 1;
        }
    }

    return 0;
}

static int basl_program_find_interface_in_source(
    const basl_program_state_t *program,
    basl_source_id_t source_id,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_interface_decl_t **out_interface
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_interface != NULL) {
        *out_interface = NULL;
    }
    if (program == NULL || source_id == 0U || name == NULL) {
        return 0;
    }

    for (i = 0U; i < program->interface_count; i += 1U) {
        if (program->interfaces[i].source_id != source_id) {
            continue;
        }
        if (
            basl_program_names_equal(
                program->interfaces[i].name,
                program->interfaces[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_interface != NULL) {
                *out_interface = &program->interfaces[i];
            }
            return 1;
        }
    }

    return 0;
}

static void basl_enum_decl_free(
    basl_program_state_t *program,
    basl_enum_decl_t *decl
) {
    void *memory;

    if (program == NULL || decl == NULL) {
        return;
    }

    memory = decl->members;
    basl_runtime_free(program->registry->runtime, &memory);
    memset(decl, 0, sizeof(*decl));
}

static basl_status_t basl_program_grow_enums(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->enum_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->enum_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*program->enums)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "enum table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->enums;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->enums),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->enums),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_enum_decl_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->enums)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->enums = (basl_enum_decl_t *)memory;
    program->enum_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_enum_decl_grow_members(
    basl_program_state_t *program,
    basl_enum_decl_t *decl,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->member_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = decl->member_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*decl->members)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "enum member table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->members;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*decl->members),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*decl->members),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_enum_member_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*decl->members)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->members = (basl_enum_member_t *)memory;
    decl->member_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_program_find_enum(
    const basl_program_state_t *program,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_enum_decl_t **out_decl
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_decl != NULL) {
        *out_decl = NULL;
    }
    if (program == NULL || name == NULL) {
        return 0;
    }

    for (i = 0U; i < program->enum_count; i += 1U) {
        if (
            basl_program_names_equal(
                program->enums[i].name,
                program->enums[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_decl != NULL) {
                *out_decl = &program->enums[i];
            }
            return 1;
        }
    }

    return 0;
}

static int basl_program_find_enum_in_source(
    const basl_program_state_t *program,
    basl_source_id_t source_id,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_enum_decl_t **out_decl
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_decl != NULL) {
        *out_decl = NULL;
    }
    if (program == NULL || source_id == 0U || name == NULL) {
        return 0;
    }

    for (i = 0U; i < program->enum_count; i += 1U) {
        if (program->enums[i].source_id != source_id) {
            continue;
        }
        if (
            basl_program_names_equal(
                program->enums[i].name,
                program->enums[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_decl != NULL) {
                *out_decl = &program->enums[i];
            }
            return 1;
        }
    }

    return 0;
}

static int basl_enum_decl_find_member(
    const basl_enum_decl_t *decl,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_enum_member_t **out_member
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_member != NULL) {
        *out_member = NULL;
    }
    if (decl == NULL || name == NULL) {
        return 0;
    }

    for (i = 0U; i < decl->member_count; i += 1U) {
        if (
            basl_program_names_equal(
                decl->members[i].name,
                decl->members[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_member != NULL) {
                *out_member = &decl->members[i];
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_program_grow_array_types(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->array_type_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->array_type_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*program->array_types)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "array type table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->array_types;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->array_types),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->array_types),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_array_type_decl_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->array_types)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->array_types = (basl_array_type_decl_t *)memory;
    program->array_type_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_program_find_array_type(
    const basl_program_state_t *program,
    basl_parser_type_t element_type,
    size_t *out_index
) {
    size_t index;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (program == NULL) {
        return 0;
    }

    for (index = 0U; index < program->array_type_count; ++index) {
        if (basl_parser_type_equal(program->array_types[index].element_type, element_type)) {
            if (out_index != NULL) {
                *out_index = index;
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_program_intern_array_type(
    basl_program_state_t *program,
    basl_parser_type_t element_type,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    size_t index;

    if (out_type != NULL) {
        *out_type = basl_binding_type_invalid();
    }
    if (program == NULL || !basl_binding_type_is_valid(element_type) || out_type == NULL) {
        basl_error_set_literal(
            program == NULL ? NULL : program->error,
            BASL_STATUS_INVALID_ARGUMENT,
            "array type arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (basl_program_find_array_type(program, element_type, &index)) {
        *out_type = basl_binding_type_array(index);
        return BASL_STATUS_OK;
    }

    status = basl_program_grow_array_types(program, program->array_type_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    index = program->array_type_count;
    program->array_types[index].element_type = element_type;
    program->array_type_count += 1U;
    *out_type = basl_binding_type_array(index);
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_grow_map_types(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->map_type_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->map_type_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*program->map_types)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "map type table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->map_types;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->map_types),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->map_types),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_map_type_decl_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->map_types)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->map_types = (basl_map_type_decl_t *)memory;
    program->map_type_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_program_find_map_type(
    const basl_program_state_t *program,
    basl_parser_type_t key_type,
    basl_parser_type_t value_type,
    size_t *out_index
) {
    size_t index;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (program == NULL) {
        return 0;
    }

    for (index = 0U; index < program->map_type_count; ++index) {
        if (
            basl_parser_type_equal(program->map_types[index].key_type, key_type) &&
            basl_parser_type_equal(program->map_types[index].value_type, value_type)
        ) {
            if (out_index != NULL) {
                *out_index = index;
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_program_intern_map_type(
    basl_program_state_t *program,
    basl_parser_type_t key_type,
    basl_parser_type_t value_type,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    size_t index;

    if (out_type != NULL) {
        *out_type = basl_binding_type_invalid();
    }
    if (
        program == NULL ||
        !basl_binding_type_is_valid(key_type) ||
        !basl_binding_type_is_valid(value_type) ||
        out_type == NULL
    ) {
        basl_error_set_literal(
            program == NULL ? NULL : program->error,
            BASL_STATUS_INVALID_ARGUMENT,
            "map type arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (basl_program_find_map_type(program, key_type, value_type, &index)) {
        *out_type = basl_binding_type_map(index);
        return BASL_STATUS_OK;
    }

    status = basl_program_grow_map_types(program, program->map_type_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    index = program->map_type_count;
    program->map_types[index].key_type = key_type;
    program->map_types[index].value_type = value_type;
    program->map_type_count += 1U;
    *out_type = basl_binding_type_map(index);
    return BASL_STATUS_OK;
}

static basl_parser_type_t basl_program_array_type_element(
    const basl_program_state_t *program,
    basl_parser_type_t array_type
) {
    if (
        program == NULL ||
        !basl_parser_type_is_array(array_type) ||
        array_type.object_index >= program->array_type_count
    ) {
        return basl_binding_type_invalid();
    }

    return program->array_types[array_type.object_index].element_type;
}

static basl_parser_type_t basl_program_map_type_key(
    const basl_program_state_t *program,
    basl_parser_type_t map_type
) {
    if (
        program == NULL ||
        !basl_parser_type_is_map(map_type) ||
        map_type.object_index >= program->map_type_count
    ) {
        return basl_binding_type_invalid();
    }

    return program->map_types[map_type.object_index].key_type;
}

static basl_parser_type_t basl_program_map_type_value(
    const basl_program_state_t *program,
    basl_parser_type_t map_type
) {
    if (
        program == NULL ||
        !basl_parser_type_is_map(map_type) ||
        map_type.object_index >= program->map_type_count
    ) {
        return basl_binding_type_invalid();
    }

    return program->map_types[map_type.object_index].value_type;
}

static void basl_function_type_decl_free(
    basl_program_state_t *program,
    basl_function_type_decl_t *decl
) {
    void *memory;

    if (program == NULL || decl == NULL) {
        return;
    }

    memory = decl->return_types;
    basl_runtime_free(program->registry->runtime, &memory);
    memory = decl->param_types;
    basl_runtime_free(program->registry->runtime, &memory);
    memset(decl, 0, sizeof(*decl));
}

static basl_status_t basl_function_type_decl_grow_params(
    basl_program_state_t *program,
    basl_function_type_decl_t *decl,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->param_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = decl->param_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }
    if (next_capacity > SIZE_MAX / sizeof(*decl->param_types)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "function type parameter table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->param_types;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*decl->param_types),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*decl->param_types),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_parser_type_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*decl->param_types)
            );
        }
    }
    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->param_types = (basl_parser_type_t *)memory;
    decl->param_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_function_type_decl_add_param(
    basl_program_state_t *program,
    basl_function_type_decl_t *decl,
    basl_parser_type_t type
) {
    basl_status_t status;

    status = basl_function_type_decl_grow_params(program, decl, decl->param_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->param_types[decl->param_count] = type;
    decl->param_count += 1U;
    return BASL_STATUS_OK;
}

static basl_status_t basl_function_type_decl_grow_returns(
    basl_program_state_t *program,
    basl_function_type_decl_t *decl,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->return_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = decl->return_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }
    if (next_capacity > SIZE_MAX / sizeof(*decl->return_types)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "function type return table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->return_types;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*decl->return_types),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*decl->return_types),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_parser_type_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*decl->return_types)
            );
        }
    }
    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->return_types = (basl_parser_type_t *)memory;
    decl->return_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_function_type_decl_add_return(
    basl_program_state_t *program,
    basl_function_type_decl_t *decl,
    basl_parser_type_t type
) {
    basl_status_t status;

    status = basl_function_type_decl_grow_returns(program, decl, decl->return_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->return_types[decl->return_count] = type;
    decl->return_count += 1U;
    decl->return_type = decl->return_types[0];
    return BASL_STATUS_OK;
}

static int basl_function_type_decl_matches(
    const basl_function_type_decl_t *left,
    const basl_function_type_decl_t *right
) {
    size_t index;

    if (left == NULL || right == NULL) {
        return 0;
    }
    if (left->is_any || right->is_any) {
        return left->is_any == right->is_any;
    }
    if (
        left->param_count != right->param_count ||
        left->return_count != right->return_count
    ) {
        return 0;
    }

    for (index = 0U; index < left->param_count; index += 1U) {
        if (!basl_parser_type_equal(left->param_types[index], right->param_types[index])) {
            return 0;
        }
    }
    for (index = 0U; index < left->return_count; index += 1U) {
        if (!basl_parser_type_equal(left->return_types[index], right->return_types[index])) {
            return 0;
        }
    }

    return 1;
}

static basl_status_t basl_program_grow_function_types(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->function_type_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->function_type_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }
    if (next_capacity > SIZE_MAX / sizeof(*program->function_types)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "function type table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->function_types;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->function_types),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->function_types),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_function_type_decl_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->function_types)
            );
        }
    }
    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->function_types = (basl_function_type_decl_t *)memory;
    program->function_type_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_program_find_function_type(
    const basl_program_state_t *program,
    const basl_function_type_decl_t *needle,
    size_t *out_index
) {
    size_t index;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (program == NULL || needle == NULL) {
        return 0;
    }

    for (index = 0U; index < program->function_type_count; index += 1U) {
        if (basl_function_type_decl_matches(&program->function_types[index], needle)) {
            if (out_index != NULL) {
                *out_index = index;
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_program_intern_function_type(
    basl_program_state_t *program,
    const basl_function_type_decl_t *decl,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    size_t index;

    if (out_type != NULL) {
        *out_type = basl_binding_type_invalid();
    }
    if (program == NULL || decl == NULL || out_type == NULL) {
        basl_error_set_literal(
            program == NULL ? NULL : program->error,
            BASL_STATUS_INVALID_ARGUMENT,
            "function type arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (basl_program_find_function_type(program, decl, &index)) {
        *out_type = basl_binding_type_function(index);
        return BASL_STATUS_OK;
    }

    status = basl_program_grow_function_types(program, program->function_type_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    index = program->function_type_count;
    program->function_types[index].is_any = decl->is_any;
    program->function_types[index].return_type = decl->return_type;
    if (decl->param_count != 0U) {
        status = basl_function_type_decl_grow_params(
            program,
            &program->function_types[index],
            decl->param_count
        );
        if (status != BASL_STATUS_OK) {
            basl_function_type_decl_free(program, &program->function_types[index]);
            return status;
        }
        memcpy(
            program->function_types[index].param_types,
            decl->param_types,
            decl->param_count * sizeof(*decl->param_types)
        );
        program->function_types[index].param_count = decl->param_count;
    }
    if (decl->return_count != 0U) {
        status = basl_function_type_decl_grow_returns(
            program,
            &program->function_types[index],
            decl->return_count
        );
        if (status != BASL_STATUS_OK) {
            basl_function_type_decl_free(program, &program->function_types[index]);
            return status;
        }
        memcpy(
            program->function_types[index].return_types,
            decl->return_types,
            decl->return_count * sizeof(*decl->return_types)
        );
        program->function_types[index].return_count = decl->return_count;
        program->function_types[index].return_type = decl->return_type;
    }
    program->function_type_count += 1U;
    *out_type = basl_binding_type_function(index);
    return BASL_STATUS_OK;
}

static const basl_function_type_decl_t *basl_program_function_type_decl(
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

static basl_status_t basl_program_intern_function_type_from_decl(
    basl_program_state_t *program,
    const basl_function_decl_t *decl,
    basl_parser_type_t *out_type
) {
    basl_function_type_decl_t function_type;
    basl_status_t status;
    size_t index;

    memset(&function_type, 0, sizeof(function_type));
    for (index = 0U; index < decl->param_count; index += 1U) {
        status = basl_function_type_decl_add_param(program, &function_type, decl->params[index].type);
        if (status != BASL_STATUS_OK) {
            basl_function_type_decl_free(program, &function_type);
            return status;
        }
    }
    for (index = 0U; index < decl->return_count; index += 1U) {
        status = basl_function_type_decl_add_return(
            program,
            &function_type,
            basl_function_return_type_at(decl, index)
        );
        if (status != BASL_STATUS_OK) {
            basl_function_type_decl_free(program, &function_type);
            return status;
        }
    }

    status = basl_program_intern_function_type(program, &function_type, out_type);
    basl_function_type_decl_free(program, &function_type);
    return status;
}

static basl_status_t basl_binding_function_grow_captures(
    basl_program_state_t *program,
    basl_function_decl_t *decl,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->capture_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = decl->capture_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }
    if (next_capacity > SIZE_MAX / sizeof(*decl->captures)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "binding capture table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->captures;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*decl->captures),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*decl->captures),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_binding_capture_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*decl->captures)
            );
        }
    }
    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->captures = (basl_binding_capture_t *)memory;
    decl->capture_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_binding_function_find_capture(
    const basl_function_decl_t *decl,
    const char *name,
    size_t name_length,
    size_t *out_index
) {
    size_t index;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (decl == NULL || name == NULL) {
        return 0;
    }

    for (index = 0U; index < decl->capture_count; ++index) {
        if (
            basl_program_names_equal(
                decl->captures[index].name,
                decl->captures[index].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = index;
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_class_decl_grow_fields(
    basl_program_state_t *program,
    basl_class_decl_t *decl,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->field_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = decl->field_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*decl->fields)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "class field table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->fields;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*decl->fields),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*decl->fields),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_class_field_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*decl->fields)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->fields = (basl_class_field_t *)memory;
    decl->field_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_class_decl_find_field(
    const basl_class_decl_t *decl,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_class_field_t **out_field
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_field != NULL) {
        *out_field = NULL;
    }
    if (decl == NULL || name == NULL) {
        return 0;
    }

    for (i = 0U; i < decl->field_count; i += 1U) {
        if (
            basl_program_names_equal(
                decl->fields[i].name,
                decl->fields[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_field != NULL) {
                *out_field = &decl->fields[i];
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_class_decl_grow_methods(
    basl_program_state_t *program,
    basl_class_decl_t *decl,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->method_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = decl->method_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*decl->methods)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "class method table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->methods;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*decl->methods),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*decl->methods),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_class_method_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*decl->methods)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->methods = (basl_class_method_t *)memory;
    decl->method_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_class_decl_find_method(
    const basl_class_decl_t *decl,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_class_method_t **out_method
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_method != NULL) {
        *out_method = NULL;
    }
    if (decl == NULL || name == NULL) {
        return 0;
    }

    for (i = 0U; i < decl->method_count; i += 1U) {
        if (
            basl_program_names_equal(
                decl->methods[i].name,
                decl->methods[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_method != NULL) {
                *out_method = &decl->methods[i];
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_class_decl_grow_implemented_interfaces(
    basl_program_state_t *program,
    basl_class_decl_t *decl,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->implemented_interface_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = decl->implemented_interface_capacity;
    next_capacity = old_capacity == 0U ? 2U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*decl->implemented_interfaces)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "class interface list allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->implemented_interfaces;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*decl->implemented_interfaces),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*decl->implemented_interfaces),
            program->error
        );
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->implemented_interfaces = (size_t *)memory;
    decl->implemented_interface_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_class_decl_grow_interface_impls(
    basl_program_state_t *program,
    basl_class_decl_t *decl,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->interface_impl_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = decl->interface_impl_capacity;
    next_capacity = old_capacity == 0U ? 2U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*decl->interface_impls)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "class interface implementation table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->interface_impls;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*decl->interface_impls),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*decl->interface_impls),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_class_interface_impl_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*decl->interface_impls)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->interface_impls = (basl_class_interface_impl_t *)memory;
    decl->interface_impl_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_class_decl_implements_interface(
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

static basl_status_t basl_interface_decl_grow_methods(
    basl_program_state_t *program,
    basl_interface_decl_t *decl,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->method_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = decl->method_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*decl->methods)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "interface method table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->methods;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*decl->methods),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*decl->methods),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_interface_method_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*decl->methods)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->methods = (basl_interface_method_t *)memory;
    decl->method_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_interface_method_grow_params(
    basl_program_state_t *program,
    basl_interface_method_t *method,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= method->param_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = method->param_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*method->param_types)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "interface parameter table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = method->param_types;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*method->param_types),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*method->param_types),
            program->error
        );
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    method->param_types = (basl_parser_type_t *)memory;
    method->param_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_interface_method_grow_returns(
    basl_program_state_t *program,
    basl_interface_method_t *method,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= method->return_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = method->return_capacity;
    next_capacity = old_capacity == 0U ? 2U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*method->return_types)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "interface return type table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = method->return_types;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*method->return_types),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*method->return_types),
            program->error
        );
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    method->return_types = (basl_parser_type_t *)memory;
    method->return_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_interface_decl_find_method(
    const basl_interface_decl_t *decl,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_interface_method_t **out_method
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_method != NULL) {
        *out_method = NULL;
    }
    if (decl == NULL || name == NULL) {
        return 0;
    }

    for (i = 0U; i < decl->method_count; i += 1U) {
        if (
            basl_program_names_equal(
                decl->methods[i].name,
                decl->methods[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_method != NULL) {
                *out_method = &decl->methods[i];
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_program_grow_constants(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->constant_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->constant_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*program->constants)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "global constant table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->constants;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->constants),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->constants),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_global_constant_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->constants)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->constants = (basl_global_constant_t *)memory;
    program->constant_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_program_find_constant(
    const basl_program_state_t *program,
    const char *name,
    size_t name_length,
    const basl_global_constant_t **out_constant
) {
    size_t i;

    if (out_constant != NULL) {
        *out_constant = NULL;
    }
    if (program == NULL || name == NULL) {
        return 0;
    }

    for (i = 0U; i < program->constant_count; i += 1U) {
        if (
            basl_program_names_equal(
                program->constants[i].name,
                program->constants[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_constant != NULL) {
                *out_constant = &program->constants[i];
            }
            return 1;
        }
    }

    return 0;
}

static int basl_program_find_constant_in_source(
    const basl_program_state_t *program,
    basl_source_id_t source_id,
    const char *name,
    size_t name_length,
    const basl_global_constant_t **out_constant
) {
    size_t i;

    if (out_constant != NULL) {
        *out_constant = NULL;
    }
    if (program == NULL || source_id == 0U || name == NULL) {
        return 0;
    }

    for (i = 0U; i < program->constant_count; i += 1U) {
        if (program->constants[i].source_id != source_id) {
            continue;
        }
        if (
            basl_program_names_equal(
                program->constants[i].name,
                program->constants[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_constant != NULL) {
                *out_constant = &program->constants[i];
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_program_grow_globals(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= program->global_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->global_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*program->globals)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "global variable table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->globals;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->globals),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->globals),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_global_variable_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->globals)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->globals = (basl_global_variable_t *)memory;
    program->global_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static int basl_program_find_global(
    const basl_program_state_t *program,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_global_variable_t **out_global
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_global != NULL) {
        *out_global = NULL;
    }
    if (program == NULL || name == NULL) {
        return 0;
    }

    for (i = 0U; i < program->global_count; i += 1U) {
        if (
            basl_program_names_equal(
                program->globals[i].name,
                program->globals[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_global != NULL) {
                *out_global = &program->globals[i];
            }
            return 1;
        }
    }

    return 0;
}

static int basl_program_find_global_in_source(
    const basl_program_state_t *program,
    basl_source_id_t source_id,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_global_variable_t **out_global
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_global != NULL) {
        *out_global = NULL;
    }
    if (program == NULL || source_id == 0U || name == NULL) {
        return 0;
    }

    for (i = 0U; i < program->global_count; i += 1U) {
        if (program->globals[i].source_id != source_id) {
            continue;
        }
        if (
            basl_program_names_equal(
                program->globals[i].name,
                program->globals[i].name_length,
                name,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_global != NULL) {
                *out_global = &program->globals[i];
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_program_checked_add(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (
        (right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)
    ) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left + right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_subtract(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (
        (right > 0 && left < INT64_MIN + right) ||
        (right < 0 && left > INT64_MAX + right)
    ) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left - right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_multiply(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (left == 0 || right == 0) {
        *out_result = 0;
        return BASL_STATUS_OK;
    }

    if (
        (left == -1 && right == INT64_MIN) ||
        (right == -1 && left == INT64_MIN)
    ) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (left > 0) {
        if (right > 0) {
            if (left > INT64_MAX / right) {
                return BASL_STATUS_INVALID_ARGUMENT;
            }
        } else if (right < INT64_MIN / left) {
            return BASL_STATUS_INVALID_ARGUMENT;
        }
    } else if (right > 0) {
        if (left < INT64_MIN / right) {
            return BASL_STATUS_INVALID_ARGUMENT;
        }
    } else if (left != 0 && right < INT64_MAX / left) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left * right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_divide(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (right == 0 || (left == INT64_MIN && right == -1)) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left / right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_modulo(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (right == 0 || (left == INT64_MIN && right == -1)) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left % right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_negate(
    int64_t value,
    int64_t *out_result
) {
    if (value == INT64_MIN) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = -value;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_shift_left(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (right < 0 || right >= 64) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = (int64_t)(((uint64_t)left) << (uint32_t)right);
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_shift_right(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    uint64_t shifted;

    if (right < 0 || right >= 64) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (right == 0) {
        *out_result = left;
        return BASL_STATUS_OK;
    }

    shifted = ((uint64_t)left) >> (uint32_t)right;
    if (left < 0) {
        shifted |= UINT64_MAX << (64U - (uint32_t)right);
    }

    *out_result = (int64_t)shifted;
    return BASL_STATUS_OK;
}

static int basl_program_values_equal(
    const basl_value_t *left,
    const basl_value_t *right
) {
    const basl_object_t *left_object;
    const basl_object_t *right_object;
    const char *left_text;
    const char *right_text;
    size_t left_length;
    size_t right_length;

    if (left == NULL || right == NULL || left->kind != right->kind) {
        return 0;
    }

    switch (left->kind) {
        case BASL_VALUE_NIL:
            return 1;
        case BASL_VALUE_BOOL:
            return left->as.boolean == right->as.boolean;
        case BASL_VALUE_INT:
            return left->as.integer == right->as.integer;
        case BASL_VALUE_FLOAT:
            return left->as.number == right->as.number;
        case BASL_VALUE_OBJECT:
            left_object = left->as.object;
            right_object = right->as.object;
            if (left_object == right_object) {
                return 1;
            }
            if (left_object == NULL || right_object == NULL) {
                return 0;
            }
            if (
                basl_object_type(left_object) != BASL_OBJECT_STRING ||
                basl_object_type(right_object) != BASL_OBJECT_STRING
            ) {
                return 0;
            }
            left_text = basl_string_object_c_str(left_object);
            right_text = basl_string_object_c_str(right_object);
            left_length = basl_string_object_length(left_object);
            right_length = basl_string_object_length(right_object);
            return left_length == right_length &&
                   left_text != NULL &&
                   right_text != NULL &&
                   memcmp(left_text, right_text, left_length) == 0;
        default:
            return 0;
    }
}

static size_t basl_program_utf8_codepoint_count(const char *text, size_t length) {
    size_t count;
    size_t index;
    unsigned char lead;

    if (text == NULL) {
        return 0U;
    }

    count = 0U;
    index = 0U;
    while (index < length) {
        lead = (unsigned char)text[index];
        if ((lead & 0x80U) == 0U) {
            index += 1U;
        } else if (
            (lead & 0xE0U) == 0xC0U &&
            index + 1U < length &&
            (((unsigned char)text[index + 1U]) & 0xC0U) == 0x80U
        ) {
            index += 2U;
        } else if (
            (lead & 0xF0U) == 0xE0U &&
            index + 2U < length &&
            (((unsigned char)text[index + 1U]) & 0xC0U) == 0x80U &&
            (((unsigned char)text[index + 2U]) & 0xC0U) == 0x80U
        ) {
            index += 3U;
        } else if (
            (lead & 0xF8U) == 0xF0U &&
            index + 3U < length &&
            (((unsigned char)text[index + 1U]) & 0xC0U) == 0x80U &&
            (((unsigned char)text[index + 2U]) & 0xC0U) == 0x80U &&
            (((unsigned char)text[index + 3U]) & 0xC0U) == 0x80U
        ) {
            index += 4U;
        } else {
            return 0U;
        }

        count += 1U;
    }

    return count;
}

static basl_status_t basl_program_decode_string_literal(
    const basl_program_state_t *program,
    const basl_token_t *token,
    basl_string_t *out_text
) {
    const char *text;
    size_t length;
    size_t index;
    size_t start;
    size_t end;
    char decoded;
    basl_status_t status;

    if (program == NULL || token == NULL || out_text == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    text = basl_program_token_text(program, token, &length);
    if (text == NULL || length < 2U) {
        return basl_compile_report(program, token->span, "invalid string literal");
    }

    start = token->kind == BASL_TOKEN_FSTRING_LITERAL ? 2U : 1U;
    if (length < start + 1U) {
        return basl_compile_report(program, token->span, "invalid string literal");
    }
    end = length - 1U;

    basl_string_clear(out_text);
    if (token->kind == BASL_TOKEN_RAW_STRING_LITERAL) {
        return basl_string_assign(
            out_text,
            text + start,
            end - start,
            program->error
        );
    }

    for (index = start; index < end; index += 1U) {
        if (index + 1U < end && text[index] == '{' && text[index + 1U] == '{') {
            status = basl_string_append(out_text, "{", 1U, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            index += 1U;
            continue;
        }
        if (index + 1U < end && text[index] == '}' && text[index + 1U] == '}') {
            status = basl_string_append(out_text, "}", 1U, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            index += 1U;
            continue;
        }
        if (text[index] != '\\') {
            status = basl_string_append(out_text, text + index, 1U, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }

        index += 1U;
        if (index >= end) {
            return basl_compile_report(program, token->span, "invalid escape sequence");
        }

        switch (text[index]) {
            case 'n':
                decoded = '\n';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 't':
                decoded = '\t';
                break;
            case '\\':
                decoded = '\\';
                break;
            case '"':
                decoded = '"';
                break;
            case '\'':
                decoded = '\'';
                break;
            case '0':
                decoded = '\0';
                break;
            default:
                return basl_compile_report(program, token->span, "invalid escape sequence");
        }

        status = basl_string_append(out_text, &decoded, 1U, program->error);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    if (token->kind == BASL_TOKEN_CHAR_LITERAL) {
        if (basl_program_utf8_codepoint_count(basl_string_c_str(out_text), basl_string_length(out_text)) != 1U) {
            return basl_compile_report(
                program,
                token->span,
                "character literals must contain exactly one character"
            );
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_program_parse_string_literal_value(
    const basl_program_state_t *program,
    const basl_token_t *token,
    basl_value_t *out_value
) {
    basl_status_t status;
    basl_string_t decoded;
    basl_object_t *object;

    if (program == NULL || token == NULL || out_value == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    object = NULL;
    basl_string_init(&decoded, program->registry->runtime);
    status = basl_program_decode_string_literal(program, token, &decoded);
    if (status == BASL_STATUS_OK) {
        status = basl_string_object_new(
            program->registry->runtime,
            basl_string_c_str(&decoded),
            basl_string_length(&decoded),
            &object,
            program->error
        );
    }
    if (status == BASL_STATUS_OK) {
        basl_value_init_object(out_value, &object);
    }
    basl_object_release(&object);
    basl_string_free(&decoded);
    return status;
}

static basl_status_t basl_program_concat_string_values(
    const basl_program_state_t *program,
    const basl_value_t *left,
    const basl_value_t *right,
    basl_value_t *out_value
) {
    basl_status_t status;
    basl_string_t text;
    const basl_object_t *left_object;
    const basl_object_t *right_object;
    basl_object_t *object;

    if (program == NULL || left == NULL || right == NULL || out_value == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    left_object = basl_value_as_object(left);
    right_object = basl_value_as_object(right);
    if (
        left_object == NULL ||
        right_object == NULL ||
        basl_object_type(left_object) != BASL_OBJECT_STRING ||
        basl_object_type(right_object) != BASL_OBJECT_STRING
    ) {
        return basl_compile_report(program, basl_program_eof_span(program), "string operands are required");
    }

    object = NULL;
    basl_string_init(&text, program->registry->runtime);
    status = basl_string_append(
        &text,
        basl_string_object_c_str(left_object),
        basl_string_object_length(left_object),
        program->error
    );
    if (status == BASL_STATUS_OK) {
        status = basl_string_append(
            &text,
            basl_string_object_c_str(right_object),
            basl_string_object_length(right_object),
            program->error
        );
    }
    if (status == BASL_STATUS_OK) {
        status = basl_string_object_new(
            program->registry->runtime,
            basl_string_c_str(&text),
            basl_string_length(&text),
            &object,
            program->error
        );
    }
    if (status == BASL_STATUS_OK) {
        basl_value_init_object(out_value, &object);
    }
    basl_object_release(&object);
    basl_string_free(&text);
    return status;
}

static basl_status_t basl_parser_emit_string_constant_text(
    basl_parser_state_t *state,
    basl_source_span_t span,
    const char *text,
    size_t length
) {
    basl_status_t status;
    basl_object_t *object;
    basl_value_t value;

    if (state == NULL || text == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    object = NULL;
    basl_value_init_nil(&value);
    status = basl_string_object_new(
        state->program->registry->runtime,
        text,
        length,
        &object,
        state->program->error
    );
    if (status == BASL_STATUS_OK) {
        basl_value_init_object(&value, &object);
        status = basl_chunk_write_constant(
            &state->chunk,
            &value,
            span,
            NULL,
            state->program->error
        );
    }
    basl_value_release(&value);
    basl_object_release(&object);
    return status;
}

static basl_status_t basl_program_append_decoded_string_range(
    const basl_program_state_t *program,
    basl_source_span_t span,
    const char *text,
    size_t start,
    size_t end,
    basl_string_t *out_text
) {
    size_t index;
    char decoded;
    basl_status_t status;

    if (program == NULL || text == NULL || out_text == NULL || start > end) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    for (index = start; index < end; index += 1U) {
        if (index + 1U < end && text[index] == '{' && text[index + 1U] == '{') {
            status = basl_string_append(out_text, "{", 1U, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            index += 1U;
            continue;
        }
        if (index + 1U < end && text[index] == '}' && text[index + 1U] == '}') {
            status = basl_string_append(out_text, "}", 1U, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            index += 1U;
            continue;
        }
        if (text[index] != '\\') {
            status = basl_string_append(out_text, text + index, 1U, program->error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }

        index += 1U;
        if (index >= end) {
            return basl_compile_report(program, span, "invalid escape sequence");
        }

        switch (text[index]) {
            case 'n':
                decoded = '\n';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 't':
                decoded = '\t';
                break;
            case '\\':
                decoded = '\\';
                break;
            case '"':
                decoded = '"';
                break;
            case '\'':
                decoded = '\'';
                break;
            case '0':
                decoded = '\0';
                break;
            default:
                return basl_compile_report(program, span, "invalid escape sequence");
        }

        status = basl_string_append(out_text, &decoded, 1U, program->error);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static void basl_program_trim_text_range(
    const char *text,
    size_t start,
    size_t end,
    size_t *out_start,
    size_t *out_end
) {
    while (start < end && (text[start] == ' ' || text[start] == '\t' || text[start] == '\n' || text[start] == '\r')) {
        start += 1U;
    }
    while (end > start && (text[end - 1U] == ' ' || text[end - 1U] == '\t' || text[end - 1U] == '\n' || text[end - 1U] == '\r')) {
        end -= 1U;
    }
    if (out_start != NULL) {
        *out_start = start;
    }
    if (out_end != NULL) {
        *out_end = end;
    }
}

static size_t basl_program_skip_quoted_text(
    const char *text,
    size_t length,
    size_t index,
    char delimiter
) {
    index += 1U;
    while (index < length) {
        if (text[index] == '\\' && delimiter != '`') {
            index += 2U;
            continue;
        }
        if (text[index] == delimiter) {
            return index + 1U;
        }
        index += 1U;
    }
    return length;
}

static basl_status_t basl_parser_parse_embedded_expression(
    basl_parser_state_t *state,
    const char *text,
    size_t length,
    size_t absolute_offset,
    basl_source_span_t error_span,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_source_id_t source_id;
    const basl_source_file_t *source;
    basl_token_list_t tokens;
    size_t token_index;
    basl_program_state_t *program;
    const basl_token_list_t *previous_tokens;
    basl_parser_state_t nested;
    basl_expression_result_t nested_result;

    if (state == NULL || text == NULL || out_result == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    basl_source_registry_init(&registry, state->program->registry->runtime);
    basl_diagnostic_list_init(&diagnostics, state->program->registry->runtime);
    basl_token_list_init(&tokens, state->program->registry->runtime);
    source_id = 0U;
    status = basl_source_registry_register(
        &registry,
        "<fstring>",
        9U,
        text,
        length,
        &source_id,
        state->program->error
    );
    if (status != BASL_STATUS_OK) {
        basl_token_list_free(&tokens);
        basl_diagnostic_list_free(&diagnostics);
        basl_source_registry_free(&registry);
        return status;
    }

    status = basl_lex_source(&registry, source_id, &tokens, &diagnostics, state->program->error);
    if (status != BASL_STATUS_OK) {
        basl_token_list_free(&tokens);
        basl_diagnostic_list_free(&diagnostics);
        basl_source_registry_free(&registry);
        return basl_parser_report(state, error_span, "invalid f-string interpolation expression");
    }

    source = basl_source_registry_get(&registry, source_id);
    if (source == NULL) {
        basl_token_list_free(&tokens);
        basl_diagnostic_list_free(&diagnostics);
        basl_source_registry_free(&registry);
        return BASL_STATUS_INTERNAL;
    }

    for (token_index = 0U; token_index < tokens.count; token_index += 1U) {
        tokens.items[token_index].span.source_id = state->program->source->id;
        tokens.items[token_index].span.start_offset += absolute_offset;
        tokens.items[token_index].span.end_offset += absolute_offset;
    }

    program = (basl_program_state_t *)state->program;
    previous_tokens = program->tokens;
    program->tokens = &tokens;
    nested = *state;
    nested.current = 0U;
    nested.body_end = basl_token_list_count(&tokens);
    basl_expression_result_clear(&nested_result);
    status = basl_parser_parse_expression(&nested, &nested_result);
    if (status == BASL_STATUS_OK && !basl_parser_check(&nested, BASL_TOKEN_EOF)) {
        status = basl_parser_report(
            &nested,
            basl_parser_peek(&nested) == NULL ? error_span : basl_parser_peek(&nested)->span,
            "expected end of f-string interpolation expression"
        );
    }
    if (status == BASL_STATUS_OK) {
        state->chunk = nested.chunk;
        *out_result = nested_result;
    }
    program->tokens = previous_tokens;
    basl_token_list_free(&tokens);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    return status;
}

static basl_status_t basl_parser_emit_fstring_part_string(
    basl_parser_state_t *state,
    basl_source_span_t span,
    int *part_count,
    const char *text,
    size_t length
) {
    basl_status_t status;

    if (state == NULL || part_count == NULL || text == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    status = basl_parser_emit_string_constant_text(state, span, text, length);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (*part_count > 0) {
        status = basl_parser_emit_opcode(state, BASL_OPCODE_ADD, span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }
    *part_count += 1;
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_emit_fstring_part_value(
    basl_parser_state_t *state,
    basl_source_span_t span,
    int *part_count
) {
    basl_status_t status;

    if (state == NULL || part_count == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (*part_count > 0) {
        status = basl_parser_emit_opcode(state, BASL_OPCODE_ADD, span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }
    *part_count += 1;
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_fstring_literal(
    basl_parser_state_t *state,
    const basl_token_t *token,
    basl_expression_result_t *out_result
) {
    const char *text;
    size_t length;
    size_t index;
    size_t segment_start;
    int part_count;
    basl_string_t segment;
    basl_status_t status;

    if (state == NULL || token == NULL || out_result == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    text = basl_parser_token_text(state, token, &length);
    if (text == NULL || length < 3U) {
        return basl_parser_report(state, token->span, "invalid f-string literal");
    }

    basl_string_init(&segment, state->program->registry->runtime);
    segment_start = 2U;
    part_count = 0;
    index = 2U;
    while (index < length - 1U) {
        size_t expression_start;
        size_t expression_end;
        size_t format_start;
        size_t trim_start;
        size_t trim_end;
        size_t absolute_offset;
        size_t paren_depth;
        size_t bracket_depth;
        size_t brace_depth;
        size_t cursor;
        basl_expression_result_t expression_result;
        unsigned long precision_value;
        char *end_ptr;

        if (text[index] == '\\') {
            index += 2U;
            continue;
        }
        if (text[index] == '{' && index + 1U < length - 1U && text[index + 1U] == '{') {
            index += 2U;
            continue;
        }
        if (text[index] == '}' && index + 1U < length - 1U && text[index + 1U] == '}') {
            index += 2U;
            continue;
        }
        if (text[index] != '{') {
            if (text[index] == '}') {
                basl_string_free(&segment);
                return basl_parser_report(state, token->span, "unmatched '}' in f-string");
            }
            index += 1U;
            continue;
        }

        basl_string_clear(&segment);
        status = basl_program_append_decoded_string_range(
            state->program,
            token->span,
            text,
            segment_start,
            index,
            &segment
        );
        if (status != BASL_STATUS_OK) {
            basl_string_free(&segment);
            return status;
        }
        if (basl_string_length(&segment) > 0U) {
            status = basl_parser_emit_fstring_part_string(
                state,
                token->span,
                &part_count,
                basl_string_c_str(&segment),
                basl_string_length(&segment)
            );
            if (status != BASL_STATUS_OK) {
                basl_string_free(&segment);
                return status;
            }
        }

        expression_start = index + 1U;
        expression_end = SIZE_MAX;
        format_start = SIZE_MAX;
        paren_depth = 0U;
        bracket_depth = 0U;
        brace_depth = 0U;
        cursor = expression_start;
        while (cursor < length - 1U) {
            if (text[cursor] == '"' || text[cursor] == '\'' || text[cursor] == '`') {
                cursor = basl_program_skip_quoted_text(text, length - 1U, cursor, text[cursor]);
                continue;
            }
            if (text[cursor] == '(') {
                paren_depth += 1U;
            } else if (text[cursor] == ')') {
                if (paren_depth > 0U) {
                    paren_depth -= 1U;
                }
            } else if (text[cursor] == '[') {
                bracket_depth += 1U;
            } else if (text[cursor] == ']') {
                if (bracket_depth > 0U) {
                    bracket_depth -= 1U;
                }
            } else if (text[cursor] == '{') {
                brace_depth += 1U;
            } else if (text[cursor] == '}') {
                if (paren_depth == 0U && bracket_depth == 0U && brace_depth == 0U) {
                    if (format_start == SIZE_MAX) {
                        expression_end = cursor;
                    }
                    break;
                }
                brace_depth -= 1U;
            } else if (
                text[cursor] == ':' &&
                paren_depth == 0U &&
                bracket_depth == 0U &&
                brace_depth == 0U &&
                format_start == SIZE_MAX
            ) {
                format_start = cursor + 1U;
                expression_end = cursor;
            }
            cursor += 1U;
        }

        if (expression_end == SIZE_MAX || cursor >= length - 1U) {
            basl_string_free(&segment);
            return basl_parser_report(state, token->span, "unterminated f-string interpolation");
        }

        basl_program_trim_text_range(text, expression_start, expression_end, &trim_start, &trim_end);
        if (trim_start == trim_end) {
            basl_string_free(&segment);
            return basl_parser_report(state, token->span, "f-string interpolation expression must not be empty");
        }

        absolute_offset = token->span.start_offset + trim_start;
        basl_expression_result_clear(&expression_result);
        status = basl_parser_parse_embedded_expression(
            state,
            text + trim_start,
            trim_end - trim_start,
            absolute_offset,
            token->span,
            &expression_result
        );
        if (status != BASL_STATUS_OK) {
            basl_string_free(&segment);
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            token->span,
            &expression_result,
            "f-string interpolation expressions must be single values"
        );
        if (status != BASL_STATUS_OK) {
            basl_string_free(&segment);
            return status;
        }

        if (format_start == SIZE_MAX) {
            if (!basl_parser_type_is_string(expression_result.type)) {
                if (
                    !basl_parser_type_is_i32(expression_result.type) &&
                    !basl_parser_type_is_f64(expression_result.type) &&
                    !basl_parser_type_is_bool(expression_result.type)
                ) {
                    basl_string_free(&segment);
                    return basl_parser_report(
                        state,
                        token->span,
                        "f-string interpolation requires a string, i32, f64, or bool value"
                    );
                }
                status = basl_parser_emit_opcode(state, BASL_OPCODE_TO_STRING, token->span);
                if (status != BASL_STATUS_OK) {
                    basl_string_free(&segment);
                    return status;
                }
            }
        } else {
            basl_program_trim_text_range(text, format_start, cursor, &trim_start, &trim_end);
            if (!basl_parser_type_is_f64(expression_result.type)) {
                basl_string_free(&segment);
                return basl_parser_report(state, token->span, "f-string format specifiers currently require f64 values");
            }
            if (trim_start >= cursor || trim_end > cursor) {
                basl_string_free(&segment);
                return basl_parser_report(state, token->span, "invalid f-string format specifier");
            }
            if (trim_end - trim_start < 3U || text[trim_start] != '.' || text[trim_end - 1U] != 'f') {
                basl_string_free(&segment);
                return basl_parser_report(state, token->span, "invalid f-string format specifier");
            }
            precision_value = strtoul(text + trim_start + 1U, &end_ptr, 10);
            if (end_ptr != text + trim_end - 1U || precision_value > UINT32_MAX) {
                basl_string_free(&segment);
                return basl_parser_report(state, token->span, "invalid f-string format specifier");
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_FORMAT_F64, token->span);
            if (status != BASL_STATUS_OK) {
                basl_string_free(&segment);
                return status;
            }
            status = basl_parser_emit_u32(state, (uint32_t)precision_value, token->span);
            if (status != BASL_STATUS_OK) {
                basl_string_free(&segment);
                return status;
            }
        }

        status = basl_parser_emit_fstring_part_value(state, token->span, &part_count);
        if (status != BASL_STATUS_OK) {
            basl_string_free(&segment);
            return status;
        }

        index = cursor + 1U;
        segment_start = index;
    }

    basl_string_clear(&segment);
    status = basl_program_append_decoded_string_range(
        state->program,
        token->span,
        text,
        segment_start,
        length - 1U,
        &segment
    );
    if (status != BASL_STATUS_OK) {
        basl_string_free(&segment);
        return status;
    }
    if (basl_string_length(&segment) > 0U || part_count == 0) {
        status = basl_parser_emit_fstring_part_string(
            state,
            token->span,
            &part_count,
            basl_string_c_str(&segment),
            basl_string_length(&segment)
        );
    }
    basl_string_free(&segment);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_expression_result_set_type(out_result, basl_binding_type_primitive(BASL_TYPE_STRING));
    return BASL_STATUS_OK;
}

static int basl_program_is_class_public(const basl_class_decl_t *decl);
static int basl_program_is_interface_public(const basl_interface_decl_t *decl);
static int basl_program_is_enum_public(const basl_enum_decl_t *decl);
static int basl_program_is_function_public(const basl_function_decl_t *decl);
static int basl_program_is_constant_public(const basl_global_constant_t *decl);
static int basl_program_is_global_public(const basl_global_variable_t *decl);

static basl_status_t basl_program_parse_type_name(
    const basl_program_state_t *program,
    const basl_token_t *token,
    const char *unsupported_message,
    basl_parser_type_t *out_type
) {
    const char *text;
    size_t length;
    basl_type_kind_t type_kind;
    size_t object_index;

    text = basl_program_token_text(program, token, &length);
    type_kind = basl_type_kind_from_name(text, length);
    if (
        type_kind == BASL_TYPE_I32 ||
        type_kind == BASL_TYPE_F64 ||
        type_kind == BASL_TYPE_BOOL ||
        type_kind == BASL_TYPE_STRING ||
        type_kind == BASL_TYPE_ERR ||
        type_kind == BASL_TYPE_VOID
    ) {
        *out_type = basl_binding_type_primitive(type_kind);
        return BASL_STATUS_OK;
    }
    if (type_kind == BASL_TYPE_NIL) {
        return basl_compile_report(program, token->span, unsupported_message);
    }
    if (basl_program_find_class(program, text, length, &object_index, NULL)) {
        *out_type = basl_binding_type_class(object_index);
        return BASL_STATUS_OK;
    }
    if (basl_program_find_interface(program, text, length, &object_index, NULL)) {
        *out_type = basl_binding_type_interface(object_index);
        return BASL_STATUS_OK;
    }
    if (basl_program_find_enum(program, text, length, &object_index, NULL)) {
        *out_type = basl_binding_type_enum(object_index);
        return BASL_STATUS_OK;
    }

    return basl_compile_report(program, token->span, unsupported_message);
}

static basl_status_t basl_program_parse_type_reference(
    const basl_program_state_t *program,
    size_t *cursor,
    const char *unsupported_message,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    const basl_token_t *token;
    const basl_token_t *next_token;
    const basl_token_t *member_token;
    const char *name_text;
    const char *member_text;
    size_t name_length;
    size_t member_length;
    size_t object_index;
    basl_source_id_t source_id;
    basl_parser_type_t element_type;
    basl_parser_type_t key_type;
    basl_parser_type_t value_type;
    basl_function_type_decl_t function_type;
    basl_parser_type_t parsed_type;

    memset(&function_type, 0, sizeof(function_type));
    token = basl_program_token_at(program, *cursor);
    if (token != NULL && token->kind == BASL_TOKEN_FN) {
        *cursor += 1U;
        next_token = basl_program_token_at(program, *cursor);
        if (next_token == NULL || next_token->kind != BASL_TOKEN_LPAREN) {
            function_type.is_any = 1;
            function_type.return_type = basl_binding_type_primitive(BASL_TYPE_VOID);
            status = basl_program_intern_function_type(
                (basl_program_state_t *)program,
                &function_type,
                out_type
            );
            basl_function_type_decl_free((basl_program_state_t *)program, &function_type);
            return status;
        }

        *cursor += 1U;
        next_token = basl_program_token_at(program, *cursor);
        if (next_token != NULL && next_token->kind != BASL_TOKEN_RPAREN) {
            while (1) {
                status = basl_program_parse_type_reference(
                    program,
                    cursor,
                    unsupported_message,
                    &parsed_type
                );
                if (status != BASL_STATUS_OK) {
                    basl_function_type_decl_free((basl_program_state_t *)program, &function_type);
                    return status;
                }
                status = basl_program_require_non_void_type(
                    program,
                    basl_program_token_at(program, *cursor - 1U)->span,
                    parsed_type,
                    "function type parameters cannot use type void"
                );
                if (status != BASL_STATUS_OK) {
                    basl_function_type_decl_free((basl_program_state_t *)program, &function_type);
                    return status;
                }
                status = basl_function_type_decl_add_param(
                    (basl_program_state_t *)program,
                    &function_type,
                    parsed_type
                );
                if (status != BASL_STATUS_OK) {
                    basl_function_type_decl_free((basl_program_state_t *)program, &function_type);
                    return status;
                }

                next_token = basl_program_token_at(program, *cursor);
                if (next_token != NULL && next_token->kind == BASL_TOKEN_COMMA) {
                    *cursor += 1U;
                    continue;
                }
                break;
            }
        }

        next_token = basl_program_token_at(program, *cursor);
        if (next_token == NULL || next_token->kind != BASL_TOKEN_RPAREN) {
            basl_function_type_decl_free((basl_program_state_t *)program, &function_type);
            return basl_compile_report(
                program,
                next_token == NULL ? token->span : next_token->span,
                "expected ')' after function type parameters"
            );
        }
        *cursor += 1U;

        next_token = basl_program_token_at(program, *cursor);
        if (next_token == NULL || next_token->kind != BASL_TOKEN_ARROW) {
            status = basl_function_type_decl_add_return(
                (basl_program_state_t *)program,
                &function_type,
                basl_binding_type_primitive(BASL_TYPE_VOID)
            );
            if (status != BASL_STATUS_OK) {
                basl_function_type_decl_free((basl_program_state_t *)program, &function_type);
                return status;
            }
        } else {
            *cursor += 1U;
            next_token = basl_program_token_at(program, *cursor);
            if (next_token != NULL && next_token->kind == BASL_TOKEN_LPAREN) {
                *cursor += 1U;
                while (1) {
                    status = basl_program_parse_type_reference(
                        program,
                        cursor,
                        unsupported_message,
                        &parsed_type
                    );
                    if (status != BASL_STATUS_OK) {
                        basl_function_type_decl_free(
                            (basl_program_state_t *)program,
                            &function_type
                        );
                        return status;
                    }
                    status = basl_function_type_decl_add_return(
                        (basl_program_state_t *)program,
                        &function_type,
                        parsed_type
                    );
                    if (status != BASL_STATUS_OK) {
                        basl_function_type_decl_free(
                            (basl_program_state_t *)program,
                            &function_type
                        );
                        return status;
                    }

                    next_token = basl_program_token_at(program, *cursor);
                    if (next_token != NULL && next_token->kind == BASL_TOKEN_COMMA) {
                        *cursor += 1U;
                        continue;
                    }
                    if (next_token != NULL && next_token->kind == BASL_TOKEN_RPAREN) {
                        *cursor += 1U;
                        break;
                    }
                    basl_function_type_decl_free(
                        (basl_program_state_t *)program,
                        &function_type
                    );
                    return basl_compile_report(
                        program,
                        next_token == NULL ? token->span : next_token->span,
                        "expected ')' after function type returns"
                    );
                }
            } else {
                status = basl_program_parse_type_reference(
                    program,
                    cursor,
                    unsupported_message,
                    &parsed_type
                );
                if (status != BASL_STATUS_OK) {
                    basl_function_type_decl_free((basl_program_state_t *)program, &function_type);
                    return status;
                }
                status = basl_function_type_decl_add_return(
                    (basl_program_state_t *)program,
                    &function_type,
                    parsed_type
                );
                if (status != BASL_STATUS_OK) {
                    basl_function_type_decl_free((basl_program_state_t *)program, &function_type);
                    return status;
                }
            }
        }

        status = basl_program_intern_function_type(
            (basl_program_state_t *)program,
            &function_type,
            out_type
        );
        basl_function_type_decl_free((basl_program_state_t *)program, &function_type);
        return status;
    }

    if (token == NULL || token->kind != BASL_TOKEN_IDENTIFIER) {
        return basl_compile_report(
            program,
            token == NULL ? basl_program_eof_span(program) : token->span,
            unsupported_message
        );
    }

    name_text = basl_program_token_text(program, token, &name_length);
    next_token = basl_program_token_at(program, *cursor + 1U);
    if (
        next_token != NULL &&
        next_token->kind == BASL_TOKEN_LESS &&
        basl_program_names_equal(name_text, name_length, "array", 5U)
    ) {
        element_type = basl_binding_type_invalid();
        *cursor += 2U;
        if (
            basl_program_token_at(program, *cursor) != NULL &&
            basl_program_token_at(program, *cursor)->kind == BASL_TOKEN_GREATER
        ) {
            return basl_compile_report(program, token->span, "array types require an element type");
        }
        if (
            basl_program_token_at(program, *cursor) != NULL &&
            basl_program_token_at(program, *cursor)->kind == BASL_TOKEN_SHIFT_RIGHT
        ) {
            return basl_compile_report(program, token->span, "array types require an element type");
        }
        status = basl_program_parse_type_reference(
            program,
            cursor,
            unsupported_message,
            &element_type
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        next_token = basl_program_token_at(program, *cursor);
        if (next_token == NULL || next_token->kind != BASL_TOKEN_GREATER) {
            return basl_compile_report(
                program,
                next_token == NULL ? token->span : next_token->span,
                "expected '>' after array element type"
            );
        }
        *cursor += 1U;
        return basl_program_intern_array_type((basl_program_state_t *)program, element_type, out_type);
    }
    if (
        next_token != NULL &&
        next_token->kind == BASL_TOKEN_LESS &&
        basl_program_names_equal(name_text, name_length, "map", 3U)
    ) {
        *cursor += 2U;
        key_type = basl_binding_type_invalid();
        value_type = basl_binding_type_invalid();
        status = basl_program_parse_type_reference(
            program,
            cursor,
            unsupported_message,
            &key_type
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (!basl_parser_type_supports_map_key(key_type)) {
            return basl_compile_report(
                program,
                token->span,
                "map keys must use type i32, bool, string, or enum"
            );
        }
        next_token = basl_program_token_at(program, *cursor);
        if (next_token == NULL || next_token->kind != BASL_TOKEN_COMMA) {
            return basl_compile_report(
                program,
                next_token == NULL ? token->span : next_token->span,
                "expected ',' after map key type"
            );
        }
        *cursor += 1U;
        status = basl_program_parse_type_reference(
            program,
            cursor,
            unsupported_message,
            &value_type
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        next_token = basl_program_token_at(program, *cursor);
        if (next_token == NULL || next_token->kind != BASL_TOKEN_GREATER) {
            return basl_compile_report(
                program,
                next_token == NULL ? token->span : next_token->span,
                "expected '>' after map value type"
            );
        }
        *cursor += 1U;
        return basl_program_intern_map_type((basl_program_state_t *)program, key_type, value_type, out_type);
    }

    if (next_token == NULL || next_token->kind != BASL_TOKEN_DOT) {
        status = basl_program_parse_type_name(program, token, unsupported_message, out_type);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        *cursor += 1U;
        return BASL_STATUS_OK;
    }

    member_token = basl_program_token_at(program, *cursor + 2U);
    if (member_token == NULL || member_token->kind != BASL_TOKEN_IDENTIFIER) {
        return basl_compile_report(
            program,
            next_token->span,
            unsupported_message
        );
    }

    if (!basl_program_resolve_import_alias(program, name_text, name_length, &source_id)) {
        return basl_compile_report(program, token->span, unsupported_message);
    }

    member_text = basl_program_token_text(program, member_token, &member_length);
    if (basl_program_find_class_in_source(
            program,
            source_id,
            member_text,
            member_length,
            &object_index,
            NULL
        )) {
        if (!basl_program_is_class_public(&program->classes[object_index])) {
            return basl_compile_report(program, member_token->span, "module member is not public");
        }
        *out_type = basl_binding_type_class(object_index);
        *cursor += 3U;
        return BASL_STATUS_OK;
    }
    if (basl_program_find_interface_in_source(
            program,
            source_id,
            member_text,
            member_length,
            &object_index,
            NULL
        )) {
        if (!basl_program_is_interface_public(&program->interfaces[object_index])) {
            return basl_compile_report(program, member_token->span, "module member is not public");
        }
        *out_type = basl_binding_type_interface(object_index);
        *cursor += 3U;
        return BASL_STATUS_OK;
    }
    if (basl_program_find_enum_in_source(
            program,
            source_id,
            member_text,
            member_length,
            &object_index,
            NULL
        )) {
        if (!basl_program_is_enum_public(&program->enums[object_index])) {
            return basl_compile_report(program, member_token->span, "module member is not public");
        }
        *out_type = basl_binding_type_enum(object_index);
        *cursor += 3U;
        return BASL_STATUS_OK;
    }

    return basl_compile_report(program, member_token->span, unsupported_message);
}

static basl_status_t basl_program_parse_primitive_type_reference(
    const basl_program_state_t *program,
    size_t *cursor,
    const char *unsupported_message,
    basl_parser_type_t *out_type
) {
    basl_status_t status;
    const basl_token_t *type_token;

    type_token = basl_program_token_at(program, *cursor);
    status = basl_program_parse_type_reference(program, cursor, unsupported_message, out_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (
        out_type->kind == BASL_TYPE_I32 ||
        out_type->kind == BASL_TYPE_F64 ||
        out_type->kind == BASL_TYPE_BOOL ||
        out_type->kind == BASL_TYPE_STRING
    ) {
        return BASL_STATUS_OK;
    }

    return basl_compile_report(
        program,
        type_token == NULL ? basl_program_eof_span(program) : type_token->span,
        unsupported_message
    );
}

static basl_status_t basl_program_require_non_void_type(
    const basl_program_state_t *program,
    basl_source_span_t span,
    basl_parser_type_t type,
    const char *message
) {
    if (basl_parser_type_is_void(type)) {
        return basl_compile_report(program, span, message);
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_program_parse_function_return_types(
    basl_program_state_t *program,
    size_t *cursor,
    const char *unsupported_message,
    basl_function_decl_t *decl
) {
    basl_status_t status;
    const basl_token_t *token;
    basl_parser_type_t return_type;

    token = basl_program_token_at(program, *cursor);
    if (token != NULL && token->kind == BASL_TOKEN_LPAREN) {
        *cursor += 1U;
        while (1) {
            status = basl_program_parse_type_reference(
                program,
                cursor,
                unsupported_message,
                &return_type
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_binding_function_add_return_type(
                program->registry->runtime,
                decl,
                return_type,
                program->error
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }

            token = basl_program_token_at(program, *cursor);
            if (token != NULL && token->kind == BASL_TOKEN_COMMA) {
                *cursor += 1U;
                continue;
            }
            if (token != NULL && token->kind == BASL_TOKEN_RPAREN) {
                *cursor += 1U;
                break;
            }
            return basl_compile_report(
                program,
                token == NULL ? basl_program_eof_span(program) : token->span,
                unsupported_message
            );
        }
        return BASL_STATUS_OK;
    }

    status = basl_program_parse_type_reference(program, cursor, unsupported_message, &return_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    return basl_binding_function_add_return_type(
        program->registry->runtime,
        decl,
        return_type,
        program->error
    );
}

static basl_status_t basl_program_parse_interface_method_return_types(
    basl_program_state_t *program,
    size_t *cursor,
    const char *unsupported_message,
    basl_interface_method_t *method
) {
    basl_status_t status;
    const basl_token_t *token;
    basl_parser_type_t return_type;

    token = basl_program_token_at(program, *cursor);
    if (token != NULL && token->kind == BASL_TOKEN_LPAREN) {
        *cursor += 1U;
        while (1) {
            status = basl_program_parse_type_reference(
                program,
                cursor,
                unsupported_message,
                &return_type
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_interface_method_grow_returns(
                program,
                method,
                method->return_count + 1U
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            method->return_types[method->return_count] = return_type;
            method->return_count += 1U;
            method->return_type = method->return_types[0];

            token = basl_program_token_at(program, *cursor);
            if (token != NULL && token->kind == BASL_TOKEN_COMMA) {
                *cursor += 1U;
                continue;
            }
            if (token != NULL && token->kind == BASL_TOKEN_RPAREN) {
                *cursor += 1U;
                break;
            }
            return basl_compile_report(
                program,
                token == NULL ? basl_program_eof_span(program) : token->span,
                unsupported_message
            );
        }
        return BASL_STATUS_OK;
    }

    status = basl_program_parse_type_reference(program, cursor, unsupported_message, &return_type);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_interface_method_grow_returns(program, method, 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    method->return_types[0] = return_type;
    method->return_count = 1U;
    method->return_type = return_type;
    return BASL_STATUS_OK;
}

static int basl_program_is_class_public(
    const basl_class_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

static int basl_program_is_interface_public(
    const basl_interface_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

static int basl_program_is_enum_public(
    const basl_enum_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

static int basl_program_is_function_public(
    const basl_function_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

static int basl_program_is_constant_public(
    const basl_global_constant_t *decl
) {
    return decl != NULL && decl->is_public;
}

static int basl_program_is_global_public(
    const basl_global_variable_t *decl
) {
    return decl != NULL && decl->is_public;
}

static int basl_program_lookup_enum_member(
    const basl_program_state_t *program,
    const char *enum_name,
    size_t enum_name_length,
    const char *member_name,
    size_t member_name_length,
    size_t *out_enum_index,
    const basl_enum_member_t **out_member
) {
    const basl_enum_decl_t *decl;
    size_t enum_index;

    decl = NULL;
    enum_index = 0U;
    if (
        !basl_program_find_enum(
            program,
            enum_name,
            enum_name_length,
            &enum_index,
            &decl
        )
    ) {
        return 0;
    }
    if (!basl_enum_decl_find_member(decl, member_name, member_name_length, NULL, out_member)) {
        return 0;
    }
    if (out_enum_index != NULL) {
        *out_enum_index = enum_index;
    }
    return 1;
}

static int basl_program_lookup_enum_member_in_source(
    const basl_program_state_t *program,
    basl_source_id_t source_id,
    const char *enum_name,
    size_t enum_name_length,
    const char *member_name,
    size_t member_name_length,
    size_t *out_enum_index,
    const basl_enum_member_t **out_member
) {
    const basl_enum_decl_t *decl;
    size_t enum_index;

    decl = NULL;
    enum_index = 0U;
    if (
        !basl_program_find_enum_in_source(
            program,
            source_id,
            enum_name,
            enum_name_length,
            &enum_index,
            &decl
        )
    ) {
        return 0;
    }
    if (!basl_enum_decl_find_member(decl, member_name, member_name_length, NULL, out_member)) {
        return 0;
    }
    if (out_enum_index != NULL) {
        *out_enum_index = enum_index;
    }
    return 1;
}

static basl_status_t basl_program_validate_main_signature(
    basl_program_state_t *program,
    basl_function_decl_t *decl,
    const basl_token_t *type_token
) {
    if (decl->param_count != 0U) {
        return basl_compile_report(
            program,
            decl->name_span,
            "main entrypoint must not declare parameters"
        );
    }

    if (
        decl->return_count != 1U ||
        !basl_parser_type_equal(decl->return_type, basl_binding_type_primitive(BASL_TYPE_I32))
    ) {
        return basl_compile_report(
            program,
            type_token->span,
            "main entrypoint must declare return type i32"
        );
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_program_grow_functions(
    basl_program_state_t *program,
    size_t minimum_capacity
) {
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    basl_status_t status;

    if (minimum_capacity <= program->functions.capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = program->functions.capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*program->functions.functions)) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "function table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = program->functions.functions;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*program->functions.functions),
            &memory,
            program->error
        );
    } else {
        status = basl_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*program->functions.functions),
            program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_function_decl_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*program->functions.functions)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->functions.functions = (basl_function_decl_t *)memory;
    program->functions.capacity = next_capacity;
    return BASL_STATUS_OK;
}

static void basl_function_decl_free(
    basl_program_state_t *program,
    basl_function_decl_t *decl
) {
    if (program == NULL || program->registry == NULL || decl == NULL) {
        return;
    }

    basl_binding_function_free(program->registry->runtime, decl);
}

static basl_status_t basl_program_fail_partial_decl(
    basl_program_state_t *program,
    basl_function_decl_t *decl,
    basl_status_t status
) {
    basl_function_decl_free(program, decl);
    return status;
}

static basl_status_t basl_program_add_param(
    basl_program_state_t *program,
    basl_function_decl_t *decl,
    basl_parser_type_t type,
    const basl_token_t *name_token
) {
    basl_status_t status;
    const char *name;
    size_t name_length;

    name = basl_program_token_text(program, name_token, &name_length);
    status = basl_binding_function_add_param(
        program->registry->runtime,
        decl,
        name,
        name_length,
        name_token->span,
        type,
        program->error
    );
    if (status == BASL_STATUS_INVALID_ARGUMENT) {
        return basl_compile_report(
            program,
            name_token->span,
            "function parameter is already declared"
        );
    }

    return status;
}

static basl_status_t basl_program_parse_source(
    basl_program_state_t *program,
    basl_source_id_t source_id
);
static const basl_token_t *basl_program_cursor_peek(
    const basl_program_state_t *program,
    size_t cursor
);
static const basl_token_t *basl_program_cursor_advance(
    const basl_program_state_t *program,
    size_t *cursor
);
static int basl_program_find_top_level_function_name(
    const basl_program_state_t *program,
    const char *name_text,
    size_t name_length,
    size_t *out_index,
    const basl_function_decl_t **out_decl
);
static int basl_program_find_top_level_function_name_in_source(
    const basl_program_state_t *program,
    basl_source_id_t source_id,
    const char *name_text,
    size_t name_length,
    size_t *out_index,
    const basl_function_decl_t **out_decl
);

static basl_status_t basl_program_parse_import_target(
    const basl_program_state_t *program,
    const basl_token_t *token,
    basl_string_t *out_path
) {
    const char *text;
    size_t length;

    if (
        token == NULL ||
        (token->kind != BASL_TOKEN_STRING_LITERAL &&
         token->kind != BASL_TOKEN_RAW_STRING_LITERAL)
    ) {
        return basl_compile_report(
            program,
            token == NULL ? basl_program_eof_span(program) : token->span,
            "expected import path string literal"
        );
    }

    text = basl_program_token_text(program, token, &length);
    if (text == NULL || length < 2U) {
        return basl_compile_report(program, token->span, "import path is invalid");
    }

    return basl_program_resolve_import_path(program, text + 1U, length - 2U, out_path);
}

static basl_status_t basl_program_parse_import(
    basl_program_state_t *program,
    size_t *cursor
) {
    basl_status_t status;
    const basl_token_t *token;
    const basl_token_t *alias_token;
    const char *alias_text;
    size_t alias_length;
    basl_string_t import_path;
    basl_source_id_t imported_source_id;
    basl_program_module_t *module;

    alias_token = NULL;
    alias_text = NULL;
    alias_length = 0U;
    imported_source_id = 0U;
    module = NULL;
    basl_string_init(&import_path, program->registry->runtime);

    token = basl_program_token_at(program, *cursor);
    if (token == NULL || token->kind != BASL_TOKEN_IMPORT) {
        basl_string_free(&import_path);
        return basl_compile_report(
            program,
            token == NULL ? basl_program_eof_span(program) : token->span,
            "expected 'import'"
        );
    }
    *cursor += 1U;

    token = basl_program_token_at(program, *cursor);
    status = basl_program_parse_import_target(program, token, &import_path);
    if (status != BASL_STATUS_OK) {
        basl_string_free(&import_path);
        return status;
    }
    *cursor += 1U;

    token = basl_program_token_at(program, *cursor);
    if (token != NULL && token->kind == BASL_TOKEN_AS) {
        *cursor += 1U;
        alias_token = basl_program_token_at(program, *cursor);
        if (alias_token == NULL || alias_token->kind != BASL_TOKEN_IDENTIFIER) {
            basl_string_free(&import_path);
            return basl_compile_report(
                program,
                token->span,
                "expected import alias name"
            );
        }
        alias_text = basl_program_token_text(program, alias_token, &alias_length);
        *cursor += 1U;
    }

    token = basl_program_token_at(program, *cursor);
    if (token == NULL || token->kind != BASL_TOKEN_SEMICOLON) {
        basl_string_free(&import_path);
        return basl_compile_report(
            program,
            token == NULL ? basl_program_eof_span(program) : token->span,
            "expected ';' after import"
        );
    }
    *cursor += 1U;

    if (
        !basl_program_find_source_by_path(
            program,
            basl_string_c_str(&import_path),
            basl_string_length(&import_path),
            &imported_source_id
        )
    ) {
        basl_string_free(&import_path);
        return basl_compile_report(
            program,
            alias_token == NULL ? token->span : alias_token->span,
            "imported source is not registered"
        );
    }

    module = basl_program_current_module(program);
    if (module == NULL) {
        basl_string_free(&import_path);
        basl_error_set_literal(
            program->error,
            BASL_STATUS_INTERNAL,
            "current module must be available while parsing imports"
        );
        return BASL_STATUS_INTERNAL;
    }
    if (alias_text == NULL) {
        basl_program_import_default_alias(
            basl_string_c_str(&import_path),
            basl_string_length(&import_path),
            &alias_text,
            &alias_length
        );
    }
    status = basl_program_add_module_import(
        program,
        module,
        alias_text,
        alias_length,
        alias_token == NULL ? token->span : alias_token->span,
        imported_source_id
    );
    if (status != BASL_STATUS_OK) {
        basl_string_free(&import_path);
        return status;
    }

    status = basl_program_parse_source(program, imported_source_id);
    basl_string_free(&import_path);
    return status;
}

static const basl_token_t *basl_program_cursor_peek(
    const basl_program_state_t *program,
    size_t cursor
) {
    return basl_program_token_at(program, cursor);
}

static const basl_token_t *basl_program_cursor_advance(
    const basl_program_state_t *program,
    size_t *cursor
) {
    const basl_token_t *token;

    token = basl_program_cursor_peek(program, *cursor);
    if (token != NULL && token->kind != BASL_TOKEN_EOF) {
        *cursor += 1U;
    }
    return token;
}

static int basl_program_parse_optional_pub(
    const basl_program_state_t *program,
    size_t *cursor
) {
    const basl_token_t *token;

    token = basl_program_cursor_peek(program, *cursor);
    if (token != NULL && token->kind == BASL_TOKEN_PUB) {
        *cursor += 1U;
        return 1;
    }

    return 0;
}

static int basl_program_is_global_variable_declaration_start(
    const basl_program_state_t *program,
    size_t cursor
) {
    const basl_token_t *token;
    const basl_token_t *next_token;
    const basl_token_t *third_token;
    const basl_token_t *fourth_token;

    token = basl_program_token_at(program, cursor);
    next_token = basl_program_token_at(program, cursor + 1U);
    if (
        token != NULL &&
        next_token != NULL &&
        token->kind == BASL_TOKEN_IDENTIFIER &&
        next_token->kind == BASL_TOKEN_IDENTIFIER
    ) {
        return 1;
    }

    third_token = basl_program_token_at(program, cursor + 2U);
    fourth_token = basl_program_token_at(program, cursor + 3U);
    return token != NULL &&
           next_token != NULL &&
           third_token != NULL &&
           fourth_token != NULL &&
           token->kind == BASL_TOKEN_IDENTIFIER &&
           next_token->kind == BASL_TOKEN_DOT &&
           third_token->kind == BASL_TOKEN_IDENTIFIER &&
           fourth_token->kind == BASL_TOKEN_IDENTIFIER;
}

static basl_status_t basl_program_parse_constant_expression(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
);

static basl_status_t basl_program_parse_constant_int(
    basl_program_state_t *program,
    const basl_token_t *token,
    basl_value_t *out_value
) {
    const char *text;
    size_t length;
    char buffer[128];
    char *end;
    long long parsed;

    text = basl_program_token_text(program, token, &length);
    if (text == NULL || length == 0U) {
        return basl_compile_report(program, token->span, "invalid integer literal");
    }
    if (length >= sizeof(buffer)) {
        return basl_compile_report(program, token->span, "integer literal is too long");
    }

    memcpy(buffer, text, length);
    buffer[length] = '\0';
    errno = 0;
    parsed = strtoll(buffer, &end, 0);
    if (errno != 0 || end == buffer || *end != '\0') {
        return basl_compile_report(program, token->span, "invalid integer literal");
    }

    basl_value_init_int(out_value, (int64_t)parsed);
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_parse_constant_float(
    basl_program_state_t *program,
    const basl_token_t *token,
    basl_value_t *out_value
) {
    const char *text;
    size_t length;
    char buffer[128];
    char *end;
    double parsed;

    text = basl_program_token_text(program, token, &length);
    if (text == NULL || length == 0U) {
        return basl_compile_report(program, token->span, "invalid float literal");
    }
    if (length >= sizeof(buffer)) {
        return basl_compile_report(program, token->span, "float literal is too long");
    }

    memcpy(buffer, text, length);
    buffer[length] = '\0';
    errno = 0;
    parsed = strtod(buffer, &end);
    if (errno != 0 || end == buffer || *end != '\0') {
        return basl_compile_report(program, token->span, "invalid float literal");
    }

    basl_value_init_float(out_value, parsed);
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_parse_constant_primary(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *token;
    const basl_global_constant_t *constant;
    const basl_token_t *member_token;
    const basl_token_t *enum_member_token;
    const basl_enum_member_t *enum_member;
    basl_source_id_t source_id;
    const char *name_text;
    const char *member_text;
    const char *enum_member_text;
    size_t name_length;
    size_t member_length;
    size_t enum_member_length;
    size_t enum_index;

    token = basl_program_cursor_peek(program, *cursor);
    if (token == NULL) {
        return basl_compile_report(
            program,
            basl_program_eof_span(program),
            "expected constant expression"
        );
    }

    switch (token->kind) {
        case BASL_TOKEN_INT_LITERAL:
            basl_program_cursor_advance(program, cursor);
            status = basl_program_parse_constant_int(program, token, &out_result->value);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            out_result->type = basl_binding_type_primitive(BASL_TYPE_I32);
            return BASL_STATUS_OK;
        case BASL_TOKEN_FLOAT_LITERAL:
            basl_program_cursor_advance(program, cursor);
            status = basl_program_parse_constant_float(program, token, &out_result->value);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            out_result->type = basl_binding_type_primitive(BASL_TYPE_F64);
            return BASL_STATUS_OK;
        case BASL_TOKEN_TRUE:
            basl_program_cursor_advance(program, cursor);
            basl_value_init_bool(&out_result->value, 1);
            out_result->type = basl_binding_type_primitive(BASL_TYPE_BOOL);
            return BASL_STATUS_OK;
        case BASL_TOKEN_FALSE:
            basl_program_cursor_advance(program, cursor);
            basl_value_init_bool(&out_result->value, 0);
            out_result->type = basl_binding_type_primitive(BASL_TYPE_BOOL);
            return BASL_STATUS_OK;
        case BASL_TOKEN_STRING_LITERAL:
        case BASL_TOKEN_RAW_STRING_LITERAL:
        case BASL_TOKEN_CHAR_LITERAL:
            basl_program_cursor_advance(program, cursor);
            status = basl_program_parse_string_literal_value(
                program,
                token,
                &out_result->value
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            out_result->type = basl_binding_type_primitive(BASL_TYPE_STRING);
            return BASL_STATUS_OK;
        case BASL_TOKEN_IDENTIFIER:
            basl_program_cursor_advance(program, cursor);
            name_text = basl_program_token_text(program, token, &name_length);
            enum_member = NULL;
            enum_index = 0U;
            if (
                basl_program_cursor_peek(program, *cursor) != NULL &&
                basl_program_cursor_peek(program, *cursor)->kind == BASL_TOKEN_DOT &&
                basl_program_resolve_import_alias(program, name_text, name_length, &source_id)
            ) {
                basl_program_cursor_advance(program, cursor);
                member_token = basl_program_cursor_peek(program, *cursor);
                if (member_token == NULL || member_token->kind != BASL_TOKEN_IDENTIFIER) {
                    return basl_compile_report(
                        program,
                        token->span,
                        "unknown global constant"
                    );
                }
                basl_program_cursor_advance(program, cursor);
                member_text = basl_program_token_text(program, member_token, &member_length);
                if (
                    basl_program_cursor_peek(program, *cursor) != NULL &&
                    basl_program_cursor_peek(program, *cursor)->kind == BASL_TOKEN_DOT
                ) {
                    basl_program_cursor_advance(program, cursor);
                    enum_member_token = basl_program_cursor_peek(program, *cursor);
                    if (
                        enum_member_token == NULL ||
                        enum_member_token->kind != BASL_TOKEN_IDENTIFIER
                    ) {
                        return basl_compile_report(
                            program,
                            member_token->span,
                            "unknown enum member"
                        );
                    }
                    basl_program_cursor_advance(program, cursor);
                    enum_member_text = basl_program_token_text(
                        program,
                        enum_member_token,
                        &enum_member_length
                    );
                    if (
                        !basl_program_lookup_enum_member_in_source(
                            program,
                            source_id,
                            member_text,
                            member_length,
                            enum_member_text,
                            enum_member_length,
                            &enum_index,
                            &enum_member
                        )
                    ) {
                        return basl_compile_report(
                            program,
                            enum_member_token->span,
                            "unknown enum member"
                        );
                    }
                    if (!basl_program_is_enum_public(&program->enums[enum_index])) {
                        return basl_compile_report(
                            program,
                            member_token->span,
                            "module member is not public"
                        );
                    }
                    basl_value_init_int(&out_result->value, enum_member->value);
                    out_result->type = basl_binding_type_enum(enum_index);
                    return BASL_STATUS_OK;
                }
                if (
                    !basl_program_find_constant_in_source(
                        program,
                        source_id,
                        member_text,
                        member_length,
                        &constant
                    )
                ) {
                    return basl_compile_report(
                        program,
                        member_token->span,
                        "unknown global constant"
                    );
                }
                if (!basl_program_is_constant_public(constant)) {
                    return basl_compile_report(
                        program,
                        member_token->span,
                        "module member is not public"
                    );
                }
                out_result->type = constant->type;
                out_result->value = basl_value_copy(&constant->value);
                return BASL_STATUS_OK;
            }
            if (
                basl_program_cursor_peek(program, *cursor) != NULL &&
                basl_program_cursor_peek(program, *cursor)->kind == BASL_TOKEN_DOT
            ) {
                basl_program_cursor_advance(program, cursor);
                member_token = basl_program_cursor_peek(program, *cursor);
                if (member_token == NULL || member_token->kind != BASL_TOKEN_IDENTIFIER) {
                    return basl_compile_report(program, token->span, "unknown enum member");
                }
                basl_program_cursor_advance(program, cursor);
                member_text = basl_program_token_text(program, member_token, &member_length);
                if (
                    !basl_program_lookup_enum_member(
                        program,
                        name_text,
                        name_length,
                        member_text,
                        member_length,
                        &enum_index,
                        &enum_member
                    )
                ) {
                    return basl_compile_report(program, member_token->span, "unknown enum member");
                }
                basl_value_init_int(&out_result->value, enum_member->value);
                out_result->type = basl_binding_type_enum(enum_index);
                return BASL_STATUS_OK;
            }
            if (!basl_program_find_constant(program, name_text, name_length, &constant)) {
                return basl_compile_report(
                    program,
                    token->span,
                    "unknown global constant"
                );
            }
            out_result->type = constant->type;
            out_result->value = basl_value_copy(&constant->value);
            return BASL_STATUS_OK;
        case BASL_TOKEN_LPAREN:
            basl_program_cursor_advance(program, cursor);
            status = basl_program_parse_constant_expression(program, cursor, out_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            token = basl_program_cursor_peek(program, *cursor);
            if (token == NULL || token->kind != BASL_TOKEN_RPAREN) {
                return basl_compile_report(
                    program,
                    token == NULL ? basl_program_eof_span(program) : token->span,
                    "expected ')' after constant expression"
                );
            }
            basl_program_cursor_advance(program, cursor);
            return BASL_STATUS_OK;
        default:
            return basl_compile_report(
                program,
                token->span,
                "expected constant expression"
            );
    }
}

static basl_status_t basl_program_parse_constant_unary(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *token;
    basl_constant_result_t operand;
    int64_t integer_result;
    double float_result;

    basl_constant_result_clear(&operand);
    token = basl_program_cursor_peek(program, *cursor);
    if (
        token != NULL &&
        (token->kind == BASL_TOKEN_MINUS || token->kind == BASL_TOKEN_BANG)
    ) {
        basl_program_cursor_advance(program, cursor);
        status = basl_program_parse_constant_unary(program, cursor, &operand);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        if (token->kind == BASL_TOKEN_MINUS) {
            if (basl_parser_type_is_i32(operand.type)) {
                status = basl_program_checked_negate(
                    basl_value_as_int(&operand.value),
                    &integer_result
                );
                basl_constant_result_release(&operand);
                if (status != BASL_STATUS_OK) {
                    return basl_compile_report(
                        program,
                        token->span,
                        "integer arithmetic overflow or invalid operation"
                    );
                }
                basl_value_init_int(&out_result->value, integer_result);
                out_result->type = basl_binding_type_primitive(BASL_TYPE_I32);
                return BASL_STATUS_OK;
            }
            if (!basl_parser_type_is_f64(operand.type)) {
                basl_constant_result_release(&operand);
                return basl_compile_report(
                    program,
                    token->span,
                    "unary '-' requires an i32 or f64 operand"
                );
            }
            float_result = -basl_value_as_float(&operand.value);
            basl_constant_result_release(&operand);
            basl_value_init_float(&out_result->value, float_result);
            out_result->type = basl_binding_type_primitive(BASL_TYPE_F64);
            return BASL_STATUS_OK;
        }

        if (!basl_parser_type_equal(operand.type, basl_binding_type_primitive(BASL_TYPE_BOOL))) {
            basl_constant_result_release(&operand);
            return basl_compile_report(
                program,
                token->span,
                "logical '!' requires a bool operand"
            );
        }
        basl_value_init_bool(&out_result->value, !basl_value_as_bool(&operand.value));
        basl_constant_result_release(&operand);
        out_result->type = basl_binding_type_primitive(BASL_TYPE_BOOL);
        return BASL_STATUS_OK;
    }

    return basl_program_parse_constant_primary(program, cursor, out_result);
}

static basl_status_t basl_program_parse_constant_factor(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t left;
    basl_constant_result_t right;
    const basl_token_t *token;
    int64_t integer_result;
    double float_result;

    basl_constant_result_clear(&left);
    basl_constant_result_clear(&right);
    status = basl_program_parse_constant_unary(program, cursor, &left);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (
            token == NULL ||
            (token->kind != BASL_TOKEN_STAR &&
             token->kind != BASL_TOKEN_SLASH &&
             token->kind != BASL_TOKEN_PERCENT)
        ) {
            *out_result = left;
            return BASL_STATUS_OK;
        }
        basl_program_cursor_advance(program, cursor);
        basl_constant_result_clear(&right);
        status = basl_program_parse_constant_unary(program, cursor, &right);
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            return status;
        }
        if (basl_parser_type_is_i32(left.type) && basl_parser_type_is_i32(right.type)) {
            switch (token->kind) {
                case BASL_TOKEN_STAR:
                    status = basl_program_checked_multiply(
                        basl_value_as_int(&left.value),
                        basl_value_as_int(&right.value),
                        &integer_result
                    );
                    break;
                case BASL_TOKEN_SLASH:
                    status = basl_program_checked_divide(
                        basl_value_as_int(&left.value),
                        basl_value_as_int(&right.value),
                        &integer_result
                    );
                    break;
                default:
                    status = basl_program_checked_modulo(
                        basl_value_as_int(&left.value),
                        basl_value_as_int(&right.value),
                        &integer_result
                    );
                    break;
            }
            if (status != BASL_STATUS_OK) {
                basl_constant_result_release(&left);
                basl_constant_result_release(&right);
                return basl_compile_report(
                    program,
                    token->span,
                    "integer arithmetic overflow or invalid operation"
                );
            }

            basl_constant_result_release(&left);
            basl_value_init_int(&left.value, integer_result);
            left.type = basl_binding_type_primitive(BASL_TYPE_I32);
            basl_constant_result_release(&right);
            continue;
        }
        if (
            token->kind != BASL_TOKEN_PERCENT &&
            basl_parser_type_is_f64(left.type) &&
            basl_parser_type_is_f64(right.type)
        ) {
            switch (token->kind) {
                case BASL_TOKEN_STAR:
                    float_result = basl_value_as_float(&left.value) *
                                   basl_value_as_float(&right.value);
                    break;
                default:
                    float_result = basl_value_as_float(&left.value) /
                                   basl_value_as_float(&right.value);
                    break;
            }
            basl_constant_result_release(&left);
            basl_value_init_float(&left.value, float_result);
            left.type = basl_binding_type_primitive(BASL_TYPE_F64);
            basl_constant_result_release(&right);
            continue;
        }
        {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                token->kind == BASL_TOKEN_PERCENT
                    ? "modulo requires i32 operands"
                    : "arithmetic operators require matching i32 or f64 operands"
            );
        }
    }
}

static basl_status_t basl_program_parse_constant_term(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t left;
    basl_constant_result_t right;
    const basl_token_t *token;
    int64_t integer_result;
    double float_result;

    basl_constant_result_clear(&left);
    basl_constant_result_clear(&right);
    status = basl_program_parse_constant_factor(program, cursor, &left);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (token == NULL || (token->kind != BASL_TOKEN_PLUS && token->kind != BASL_TOKEN_MINUS)) {
            *out_result = left;
            return BASL_STATUS_OK;
        }
        basl_program_cursor_advance(program, cursor);
        basl_constant_result_clear(&right);
        status = basl_program_parse_constant_factor(program, cursor, &right);
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            return status;
        }
        if (token->kind == BASL_TOKEN_PLUS &&
            basl_parser_type_equal(left.type, basl_binding_type_primitive(BASL_TYPE_STRING)) &&
            basl_parser_type_equal(right.type, basl_binding_type_primitive(BASL_TYPE_STRING))) {
            basl_value_t concatenated;

            basl_value_init_nil(&concatenated);
            status = basl_program_concat_string_values(
                program,
                &left.value,
                &right.value,
                &concatenated
            );
            if (status != BASL_STATUS_OK) {
                basl_constant_result_release(&left);
                basl_constant_result_release(&right);
                return status;
            }
            basl_constant_result_release(&left);
            left.value = concatenated;
            left.type = basl_binding_type_primitive(BASL_TYPE_STRING);
            basl_constant_result_release(&right);
            continue;
        }
        if (basl_parser_type_is_i32(left.type) && basl_parser_type_is_i32(right.type)) {
            if (token->kind == BASL_TOKEN_PLUS) {
                status = basl_program_checked_add(
                    basl_value_as_int(&left.value),
                    basl_value_as_int(&right.value),
                    &integer_result
                );
            } else {
                status = basl_program_checked_subtract(
                    basl_value_as_int(&left.value),
                    basl_value_as_int(&right.value),
                    &integer_result
                );
            }
            if (status != BASL_STATUS_OK) {
                basl_constant_result_release(&left);
                basl_constant_result_release(&right);
                return basl_compile_report(
                    program,
                    token->span,
                    "integer arithmetic overflow or invalid operation"
                );
            }

            basl_constant_result_release(&left);
            basl_value_init_int(&left.value, integer_result);
            left.type = basl_binding_type_primitive(BASL_TYPE_I32);
            basl_constant_result_release(&right);
            continue;
        }
        if (basl_parser_type_is_f64(left.type) && basl_parser_type_is_f64(right.type)) {
            float_result = token->kind == BASL_TOKEN_PLUS
                ? basl_value_as_float(&left.value) + basl_value_as_float(&right.value)
                : basl_value_as_float(&left.value) - basl_value_as_float(&right.value);
            basl_constant_result_release(&left);
            basl_value_init_float(&left.value, float_result);
            left.type = basl_binding_type_primitive(BASL_TYPE_F64);
            basl_constant_result_release(&right);
            continue;
        }
        {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                token->kind == BASL_TOKEN_PLUS
                    ? "'+' requires matching i32, f64, or string operands"
                    : "arithmetic operators require matching i32 or f64 operands"
            );
        }
    }
}

static basl_status_t basl_program_parse_constant_shift(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t left;
    basl_constant_result_t right;
    const basl_token_t *token;
    int64_t integer_result;

    basl_constant_result_clear(&left);
    basl_constant_result_clear(&right);
    status = basl_program_parse_constant_term(program, cursor, &left);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (
            token == NULL ||
            (token->kind != BASL_TOKEN_SHIFT_LEFT &&
             token->kind != BASL_TOKEN_SHIFT_RIGHT)
        ) {
            *out_result = left;
            return BASL_STATUS_OK;
        }
        basl_program_cursor_advance(program, cursor);
        basl_constant_result_clear(&right);
        status = basl_program_parse_constant_term(program, cursor, &right);
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            return status;
        }
        if (
            !basl_parser_type_equal(left.type, basl_binding_type_primitive(BASL_TYPE_I32)) ||
            !basl_parser_type_equal(right.type, basl_binding_type_primitive(BASL_TYPE_I32))
        ) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(program, token->span, "shift operators require i32 operands");
        }

        if (token->kind == BASL_TOKEN_SHIFT_LEFT) {
            status = basl_program_checked_shift_left(
                basl_value_as_int(&left.value),
                basl_value_as_int(&right.value),
                &integer_result
            );
        } else {
            status = basl_program_checked_shift_right(
                basl_value_as_int(&left.value),
                basl_value_as_int(&right.value),
                &integer_result
            );
        }
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "integer arithmetic overflow or invalid operation"
            );
        }

        basl_constant_result_release(&left);
        basl_value_init_int(&left.value, integer_result);
        left.type = basl_binding_type_primitive(BASL_TYPE_I32);
        basl_constant_result_release(&right);
    }
}

static basl_status_t basl_program_parse_constant_comparison(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t left;
    basl_constant_result_t right;
    const basl_token_t *token;
    int comparison_result;

    basl_constant_result_clear(&left);
    basl_constant_result_clear(&right);
    status = basl_program_parse_constant_shift(program, cursor, &left);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (
            token == NULL ||
            (token->kind != BASL_TOKEN_GREATER &&
             token->kind != BASL_TOKEN_GREATER_EQUAL &&
             token->kind != BASL_TOKEN_LESS &&
             token->kind != BASL_TOKEN_LESS_EQUAL)
        ) {
            *out_result = left;
            return BASL_STATUS_OK;
        }
        basl_program_cursor_advance(program, cursor);
        basl_constant_result_clear(&right);
        status = basl_program_parse_constant_shift(program, cursor, &right);
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            return status;
        }
        if (
            (!basl_parser_type_is_i32(left.type) || !basl_parser_type_is_i32(right.type)) &&
            (!basl_parser_type_is_f64(left.type) || !basl_parser_type_is_f64(right.type))
        ) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "comparison operators require matching i32 or f64 operands"
            );
        }

        if (basl_parser_type_is_i32(left.type)) {
            switch (token->kind) {
                case BASL_TOKEN_GREATER:
                    comparison_result =
                        basl_value_as_int(&left.value) > basl_value_as_int(&right.value);
                    break;
                case BASL_TOKEN_GREATER_EQUAL:
                    comparison_result =
                        basl_value_as_int(&left.value) >= basl_value_as_int(&right.value);
                    break;
                case BASL_TOKEN_LESS:
                    comparison_result =
                        basl_value_as_int(&left.value) < basl_value_as_int(&right.value);
                    break;
                default:
                    comparison_result =
                        basl_value_as_int(&left.value) <= basl_value_as_int(&right.value);
                    break;
            }
        } else {
            switch (token->kind) {
                case BASL_TOKEN_GREATER:
                    comparison_result =
                        basl_value_as_float(&left.value) > basl_value_as_float(&right.value);
                    break;
                case BASL_TOKEN_GREATER_EQUAL:
                    comparison_result =
                        basl_value_as_float(&left.value) >= basl_value_as_float(&right.value);
                    break;
                case BASL_TOKEN_LESS:
                    comparison_result =
                        basl_value_as_float(&left.value) < basl_value_as_float(&right.value);
                    break;
                default:
                    comparison_result =
                        basl_value_as_float(&left.value) <= basl_value_as_float(&right.value);
                    break;
            }
        }

        basl_constant_result_release(&left);
        basl_value_init_bool(&left.value, comparison_result);
        left.type = basl_binding_type_primitive(BASL_TYPE_BOOL);
        basl_constant_result_release(&right);
    }
}

static basl_status_t basl_program_parse_constant_equality(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t left;
    basl_constant_result_t right;
    const basl_token_t *token;
    int is_equal;

    basl_constant_result_clear(&left);
    basl_constant_result_clear(&right);
    status = basl_program_parse_constant_comparison(program, cursor, &left);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (
            token == NULL ||
            (token->kind != BASL_TOKEN_EQUAL_EQUAL && token->kind != BASL_TOKEN_BANG_EQUAL)
        ) {
            *out_result = left;
            return BASL_STATUS_OK;
        }
        basl_program_cursor_advance(program, cursor);
        basl_constant_result_clear(&right);
        status = basl_program_parse_constant_comparison(program, cursor, &right);
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            return status;
        }
        if (!basl_parser_type_equal(left.type, right.type)) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "equality operands must have matching types"
            );
        }

        is_equal = basl_program_values_equal(&left.value, &right.value);
        if (token->kind == BASL_TOKEN_BANG_EQUAL) {
            is_equal = !is_equal;
        }
        basl_constant_result_release(&left);
        basl_value_init_bool(&left.value, is_equal);
        left.type = basl_binding_type_primitive(BASL_TYPE_BOOL);
        basl_constant_result_release(&right);
    }
}

static basl_status_t basl_program_parse_constant_bitwise_and(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t left;
    basl_constant_result_t right;
    const basl_token_t *token;
    int64_t integer_result;

    basl_constant_result_clear(&left);
    basl_constant_result_clear(&right);
    status = basl_program_parse_constant_equality(program, cursor, &left);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != BASL_TOKEN_AMPERSAND) {
            *out_result = left;
            return BASL_STATUS_OK;
        }
        basl_program_cursor_advance(program, cursor);
        basl_constant_result_clear(&right);
        status = basl_program_parse_constant_equality(program, cursor, &right);
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            return status;
        }
        if (
            !basl_parser_type_equal(left.type, basl_binding_type_primitive(BASL_TYPE_I32)) ||
            !basl_parser_type_equal(right.type, basl_binding_type_primitive(BASL_TYPE_I32))
        ) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "bitwise operators require i32 operands"
            );
        }

        integer_result = basl_value_as_int(&left.value) & basl_value_as_int(&right.value);
        basl_constant_result_release(&left);
        basl_value_init_int(&left.value, integer_result);
        left.type = basl_binding_type_primitive(BASL_TYPE_I32);
        basl_constant_result_release(&right);
    }
}

static basl_status_t basl_program_parse_constant_bitwise_xor(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t left;
    basl_constant_result_t right;
    const basl_token_t *token;
    int64_t integer_result;

    basl_constant_result_clear(&left);
    basl_constant_result_clear(&right);
    status = basl_program_parse_constant_bitwise_and(program, cursor, &left);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != BASL_TOKEN_CARET) {
            *out_result = left;
            return BASL_STATUS_OK;
        }
        basl_program_cursor_advance(program, cursor);
        basl_constant_result_clear(&right);
        status = basl_program_parse_constant_bitwise_and(program, cursor, &right);
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            return status;
        }
        if (
            !basl_parser_type_equal(left.type, basl_binding_type_primitive(BASL_TYPE_I32)) ||
            !basl_parser_type_equal(right.type, basl_binding_type_primitive(BASL_TYPE_I32))
        ) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "bitwise operators require i32 operands"
            );
        }

        integer_result = basl_value_as_int(&left.value) ^ basl_value_as_int(&right.value);
        basl_constant_result_release(&left);
        basl_value_init_int(&left.value, integer_result);
        left.type = basl_binding_type_primitive(BASL_TYPE_I32);
        basl_constant_result_release(&right);
    }
}

static basl_status_t basl_program_parse_constant_bitwise_or(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t left;
    basl_constant_result_t right;
    const basl_token_t *token;
    int64_t integer_result;

    basl_constant_result_clear(&left);
    basl_constant_result_clear(&right);
    status = basl_program_parse_constant_bitwise_xor(program, cursor, &left);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != BASL_TOKEN_PIPE) {
            *out_result = left;
            return BASL_STATUS_OK;
        }
        basl_program_cursor_advance(program, cursor);
        basl_constant_result_clear(&right);
        status = basl_program_parse_constant_bitwise_xor(program, cursor, &right);
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            return status;
        }
        if (
            !basl_parser_type_equal(left.type, basl_binding_type_primitive(BASL_TYPE_I32)) ||
            !basl_parser_type_equal(right.type, basl_binding_type_primitive(BASL_TYPE_I32))
        ) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "bitwise operators require i32 operands"
            );
        }

        integer_result = basl_value_as_int(&left.value) | basl_value_as_int(&right.value);
        basl_constant_result_release(&left);
        basl_value_init_int(&left.value, integer_result);
        left.type = basl_binding_type_primitive(BASL_TYPE_I32);
        basl_constant_result_release(&right);
    }
}

static basl_status_t basl_program_parse_constant_logical_and(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t left;
    basl_constant_result_t right;
    const basl_token_t *token;
    int boolean_result;

    basl_constant_result_clear(&left);
    basl_constant_result_clear(&right);
    status = basl_program_parse_constant_bitwise_or(program, cursor, &left);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != BASL_TOKEN_AMPERSAND_AMPERSAND) {
            *out_result = left;
            return BASL_STATUS_OK;
        }
        basl_program_cursor_advance(program, cursor);
        basl_constant_result_clear(&right);
        status = basl_program_parse_constant_bitwise_or(program, cursor, &right);
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            return status;
        }
        if (
            !basl_parser_type_equal(left.type, basl_binding_type_primitive(BASL_TYPE_BOOL)) ||
            !basl_parser_type_equal(right.type, basl_binding_type_primitive(BASL_TYPE_BOOL))
        ) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "logical '&&' requires bool operands"
            );
        }

        boolean_result =
            basl_value_as_bool(&left.value) && basl_value_as_bool(&right.value);
        basl_constant_result_release(&left);
        basl_value_init_bool(&left.value, boolean_result);
        left.type = basl_binding_type_primitive(BASL_TYPE_BOOL);
        basl_constant_result_release(&right);
    }
}

static basl_status_t basl_program_parse_constant_logical_or(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t left;
    basl_constant_result_t right;
    const basl_token_t *token;
    int boolean_result;

    basl_constant_result_clear(&left);
    basl_constant_result_clear(&right);
    status = basl_program_parse_constant_logical_and(program, cursor, &left);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != BASL_TOKEN_PIPE_PIPE) {
            *out_result = left;
            return BASL_STATUS_OK;
        }
        basl_program_cursor_advance(program, cursor);
        basl_constant_result_clear(&right);
        status = basl_program_parse_constant_logical_and(program, cursor, &right);
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            return status;
        }
        if (
            !basl_parser_type_equal(left.type, basl_binding_type_primitive(BASL_TYPE_BOOL)) ||
            !basl_parser_type_equal(right.type, basl_binding_type_primitive(BASL_TYPE_BOOL))
        ) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "logical '||' requires bool operands"
            );
        }

        boolean_result =
            basl_value_as_bool(&left.value) || basl_value_as_bool(&right.value);
        basl_constant_result_release(&left);
        basl_value_init_bool(&left.value, boolean_result);
        left.type = basl_binding_type_primitive(BASL_TYPE_BOOL);
        basl_constant_result_release(&right);
    }
}

static basl_status_t basl_program_parse_constant_expression(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t condition_result;
    basl_constant_result_t then_result;
    basl_constant_result_t else_result;
    const basl_token_t *question_token;
    const basl_token_t *colon_token;
    int take_then_branch;

    basl_constant_result_clear(&condition_result);
    basl_constant_result_clear(&then_result);
    basl_constant_result_clear(&else_result);

    status = basl_program_parse_constant_logical_or(
        program,
        cursor,
        &condition_result
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    question_token = basl_program_cursor_peek(program, *cursor);
    if (question_token == NULL || question_token->kind != BASL_TOKEN_QUESTION) {
        *out_result = condition_result;
        return BASL_STATUS_OK;
    }

    if (
        !basl_parser_type_equal(
            condition_result.type,
            basl_binding_type_primitive(BASL_TYPE_BOOL)
        )
    ) {
        basl_constant_result_release(&condition_result);
        return basl_compile_report(
            program,
            question_token->span,
            "ternary condition must be bool"
        );
    }

    basl_program_cursor_advance(program, cursor);
    status = basl_program_parse_constant_expression(program, cursor, &then_result);
    if (status != BASL_STATUS_OK) {
        basl_constant_result_release(&condition_result);
        return status;
    }

    colon_token = basl_program_cursor_peek(program, *cursor);
    if (colon_token == NULL || colon_token->kind != BASL_TOKEN_COLON) {
        basl_constant_result_release(&condition_result);
        basl_constant_result_release(&then_result);
        return basl_compile_report(
            program,
            question_token->span,
            "expected ':' in ternary expression"
        );
    }
    basl_program_cursor_advance(program, cursor);

    status = basl_program_parse_constant_expression(program, cursor, &else_result);
    if (status != BASL_STATUS_OK) {
        basl_constant_result_release(&condition_result);
        basl_constant_result_release(&then_result);
        return status;
    }

    if (!basl_parser_type_equal(then_result.type, else_result.type)) {
        basl_constant_result_release(&condition_result);
        basl_constant_result_release(&then_result);
        basl_constant_result_release(&else_result);
        return basl_compile_report(
            program,
            colon_token->span,
            "ternary branches must have the same type"
        );
    }

    take_then_branch = basl_value_as_bool(&condition_result.value);
    if (take_then_branch) {
        *out_result = then_result;
        basl_constant_result_clear(&then_result);
    } else {
        *out_result = else_result;
        basl_constant_result_clear(&else_result);
    }

    basl_constant_result_release(&condition_result);
    basl_constant_result_release(&then_result);
    basl_constant_result_release(&else_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_parse_global_variable_declaration(
    basl_program_state_t *program,
    size_t *cursor,
    int is_public
) {
    basl_status_t status;
    const basl_token_t *type_token;
    const basl_token_t *name_token;
    const basl_token_t *token;
    basl_parser_type_t declared_type;
    const basl_function_decl_t *existing_function;
    const basl_global_constant_t *existing_constant;
    const basl_global_variable_t *existing_global;
    basl_global_variable_t *global;
    const char *name_text;
    size_t name_length;
    size_t initializer_start;
    size_t initializer_end;

    type_token = basl_program_cursor_peek(program, *cursor);
    status = basl_program_parse_type_reference(
        program,
        cursor,
        "unsupported global variable type",
        &declared_type
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_program_require_non_void_type(
        program,
        type_token == NULL ? basl_program_eof_span(program) : type_token->span,
        declared_type,
        "global variables cannot use type void"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    name_token = basl_program_cursor_peek(program, *cursor);
    if (name_token == NULL || name_token->kind != BASL_TOKEN_IDENTIFIER) {
        return basl_compile_report(
            program,
            type_token == NULL ? basl_program_eof_span(program) : type_token->span,
            "expected global variable name"
        );
    }
    name_text = basl_program_token_text(program, name_token, &name_length);
    if (
        basl_program_find_top_level_function_name(
            program,
            name_text,
            name_length,
            NULL,
            &existing_function
        )
    ) {
        return basl_compile_report(
            program,
            name_token->span,
            "global variable name conflicts with function"
        );
    }
    if (basl_program_find_interface(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "global variable name conflicts with interface"
        );
    }
    if (basl_program_find_enum(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "global variable name conflicts with enum"
        );
    }
    if (basl_program_find_class(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "global variable name conflicts with class"
        );
    }
    if (basl_program_find_constant(program, name_text, name_length, &existing_constant)) {
        return basl_compile_report(
            program,
            name_token->span,
            "global variable name conflicts with global constant"
        );
    }
    if (basl_program_find_global(program, name_text, name_length, NULL, &existing_global)) {
        return basl_compile_report(
            program,
            name_token->span,
            "global variable is already declared"
        );
    }
    basl_program_cursor_advance(program, cursor);

    token = basl_program_cursor_peek(program, *cursor);
    if (token == NULL || token->kind != BASL_TOKEN_ASSIGN) {
        return basl_compile_report(
            program,
            name_token->span,
            "expected '=' in global variable declaration"
        );
    }
    basl_program_cursor_advance(program, cursor);

    initializer_start = *cursor;
    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind == BASL_TOKEN_EOF) {
            return basl_compile_report(
                program,
                basl_program_eof_span(program),
                "expected ';' after global variable declaration"
            );
        }
        if (token->kind == BASL_TOKEN_SEMICOLON) {
            break;
        }
        *cursor += 1U;
    }
    initializer_end = *cursor;
    if (initializer_start == initializer_end) {
        return basl_compile_report(
            program,
            name_token->span,
            "expected initializer expression for global variable"
        );
    }
    basl_program_cursor_advance(program, cursor);

    status = basl_program_grow_globals(program, program->global_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    global = &program->globals[program->global_count];
    memset(global, 0, sizeof(*global));
    global->source_id = program->source->id;
    global->name = name_text;
    global->name_length = name_length;
    global->name_span = name_token->span;
    global->is_public = is_public;
    global->type = declared_type;
    global->source = program->source;
    global->tokens = program->tokens;
    global->initializer_start = initializer_start;
    global->initializer_end = initializer_end;
    program->global_count += 1U;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_parse_constant_declaration(
    basl_program_state_t *program,
    size_t *cursor,
    int is_public
) {
    basl_status_t status;
    const basl_token_t *const_token;
    const basl_token_t *type_token;
    const basl_token_t *name_token;
    basl_parser_type_t declared_type;
    basl_constant_result_t value_result;
    const char *name_text;
    size_t name_length;
    const basl_function_decl_t *existing_function;
    const basl_global_constant_t *existing_constant;
    const basl_global_variable_t *existing_global;
    basl_global_constant_t *constant;

    basl_constant_result_clear(&value_result);
    const_token = basl_program_cursor_peek(program, *cursor);
    if (const_token == NULL || const_token->kind != BASL_TOKEN_CONST) {
        return basl_compile_report(
            program,
            const_token == NULL ? basl_program_eof_span(program) : const_token->span,
            "expected 'const'"
        );
    }
    basl_program_cursor_advance(program, cursor);

    type_token = basl_program_cursor_peek(program, *cursor);
    status = basl_program_parse_primitive_type_reference(
        program,
        cursor,
        "only i32 and bool global constant types are supported",
        &declared_type
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    name_token = basl_program_cursor_peek(program, *cursor);
    if (name_token == NULL || name_token->kind != BASL_TOKEN_IDENTIFIER) {
        return basl_compile_report(
            program,
            type_token->span,
            "expected global constant name"
        );
    }
    name_text = basl_program_token_text(program, name_token, &name_length);
    if (
        basl_program_find_top_level_function_name(
            program,
            name_text,
            name_length,
            NULL,
            &existing_function
        )
    ) {
        return basl_compile_report(
            program,
            name_token->span,
            "global constant name conflicts with function"
        );
    }
    if (basl_program_find_interface(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "global constant name conflicts with interface"
        );
    }
    if (basl_program_find_enum(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "global constant name conflicts with enum"
        );
    }
    if (basl_program_find_class(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "global constant name conflicts with class"
        );
    }
    if (basl_program_find_constant(program, name_text, name_length, &existing_constant)) {
        return basl_compile_report(
            program,
            name_token->span,
            "global constant is already declared"
        );
    }
    if (basl_program_find_global(program, name_text, name_length, NULL, &existing_global)) {
        return basl_compile_report(
            program,
            name_token->span,
            "global constant name conflicts with global variable"
        );
    }
    basl_program_cursor_advance(program, cursor);

    type_token = basl_program_cursor_peek(program, *cursor);
    if (type_token == NULL || type_token->kind != BASL_TOKEN_ASSIGN) {
        return basl_compile_report(
            program,
            name_token->span,
            "expected '=' in global constant declaration"
        );
    }
    basl_program_cursor_advance(program, cursor);

    status = basl_program_parse_constant_expression(program, cursor, &value_result);
    if (status != BASL_STATUS_OK) {
        basl_constant_result_release(&value_result);
        return status;
    }
    if (!basl_program_type_is_assignable(program, declared_type, value_result.type)) {
        basl_constant_result_release(&value_result);
        return basl_compile_report(
            program,
            name_token->span,
            "initializer type does not match global constant type"
        );
    }

    type_token = basl_program_cursor_peek(program, *cursor);
    if (type_token == NULL || type_token->kind != BASL_TOKEN_SEMICOLON) {
        basl_constant_result_release(&value_result);
        return basl_compile_report(
            program,
            type_token == NULL ? basl_program_eof_span(program) : type_token->span,
            "expected ';' after global constant declaration"
        );
    }
    basl_program_cursor_advance(program, cursor);

    status = basl_program_grow_constants(program, program->constant_count + 1U);
    if (status != BASL_STATUS_OK) {
        basl_constant_result_release(&value_result);
        return status;
    }

    constant = &program->constants[program->constant_count];
    memset(constant, 0, sizeof(*constant));
    constant->source_id = program->source->id;
    constant->name = name_text;
    constant->name_length = name_length;
    constant->name_span = name_token->span;
    constant->is_public = is_public;
    constant->type = declared_type;
    constant->value = value_result.value;
    value_result.type = basl_binding_type_invalid();
    basl_value_init_nil(&value_result.value);
    program->constant_count += 1U;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_parse_enum_declaration(
    basl_program_state_t *program,
    size_t *cursor,
    int is_public
) {
    basl_status_t status;
    const basl_token_t *enum_token;
    const basl_token_t *name_token;
    const basl_token_t *member_token;
    const basl_token_t *token;
    const char *name_text;
    size_t name_length;
    basl_enum_decl_t *decl;
    basl_enum_member_t *member;
    basl_constant_result_t value_result;
    int64_t next_value;

    basl_constant_result_clear(&value_result);
    next_value = 0;

    enum_token = basl_program_cursor_peek(program, *cursor);
    if (enum_token == NULL || enum_token->kind != BASL_TOKEN_ENUM) {
        return basl_compile_report(
            program,
            enum_token == NULL ? basl_program_eof_span(program) : enum_token->span,
            "expected 'enum'"
        );
    }
    basl_program_cursor_advance(program, cursor);

    name_token = basl_program_cursor_peek(program, *cursor);
    if (name_token == NULL || name_token->kind != BASL_TOKEN_IDENTIFIER) {
        return basl_compile_report(program, enum_token->span, "expected enum name");
    }
    name_text = basl_program_token_text(program, name_token, &name_length);
    if (basl_program_find_enum(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(program, name_token->span, "enum is already declared");
    }
    if (basl_program_find_class(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(program, name_token->span, "enum name conflicts with class");
    }
    if (basl_program_find_interface(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "enum name conflicts with interface"
        );
    }
    if (basl_program_find_constant(program, name_text, name_length, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "enum name conflicts with global constant"
        );
    }
    if (basl_program_find_global(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "enum name conflicts with global variable"
        );
    }
    if (basl_program_find_top_level_function_name(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(program, name_token->span, "enum name conflicts with function");
    }

    status = basl_program_grow_enums(program, program->enum_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl = &program->enums[program->enum_count];
    memset(decl, 0, sizeof(*decl));
    decl->source_id = program->source->id;
    decl->name = name_text;
    decl->name_length = name_length;
    decl->name_span = name_token->span;
    decl->is_public = is_public;
    program->enum_count += 1U;
    basl_program_cursor_advance(program, cursor);

    token = basl_program_cursor_peek(program, *cursor);
    if (token == NULL || token->kind != BASL_TOKEN_LBRACE) {
        return basl_compile_report(program, name_token->span, "expected '{' after enum name");
    }
    basl_program_cursor_advance(program, cursor);

    while (1) {
        token = basl_program_cursor_peek(program, *cursor);
        if (token == NULL) {
            return basl_compile_report(
                program,
                basl_program_eof_span(program),
                "expected '}' after enum body"
            );
        }
        if (token->kind == BASL_TOKEN_RBRACE) {
            basl_program_cursor_advance(program, cursor);
            break;
        }
        if (token->kind != BASL_TOKEN_IDENTIFIER) {
            return basl_compile_report(program, token->span, "expected enum member name");
        }
        member_token = token;
        name_text = basl_program_token_text(program, member_token, &name_length);
        if (basl_enum_decl_find_member(decl, name_text, name_length, NULL, NULL)) {
            return basl_compile_report(
                program,
                member_token->span,
                "enum member is already declared"
            );
        }
        basl_program_cursor_advance(program, cursor);

        if (basl_program_cursor_peek(program, *cursor) != NULL &&
            basl_program_cursor_peek(program, *cursor)->kind == BASL_TOKEN_ASSIGN) {
            basl_program_cursor_advance(program, cursor);
            basl_constant_result_release(&value_result);
            basl_constant_result_clear(&value_result);
            status = basl_program_parse_constant_expression(program, cursor, &value_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (
                !basl_parser_type_equal(
                    value_result.type,
                    basl_binding_type_primitive(BASL_TYPE_I32)
                )
            ) {
                basl_constant_result_release(&value_result);
                return basl_compile_report(
                    program,
                    member_token->span,
                    "enum member value must be i32"
                );
            }
            next_value = basl_value_as_int(&value_result.value);
            basl_constant_result_release(&value_result);
            basl_constant_result_clear(&value_result);
        }

        status = basl_enum_decl_grow_members(program, decl, decl->member_count + 1U);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        member = &decl->members[decl->member_count];
        memset(member, 0, sizeof(*member));
        member->name = name_text;
        member->name_length = name_length;
        member->name_span = member_token->span;
        member->value = next_value;
        decl->member_count += 1U;
        next_value += 1;

        token = basl_program_cursor_peek(program, *cursor);
        if (token == NULL) {
            return basl_compile_report(
                program,
                basl_program_eof_span(program),
                "expected '}' after enum body"
            );
        }
        if (token->kind == BASL_TOKEN_COMMA) {
            basl_program_cursor_advance(program, cursor);
            continue;
        }
        if (token->kind == BASL_TOKEN_RBRACE) {
            continue;
        }
        return basl_compile_report(
            program,
            token->span,
            "expected ',' or '}' after enum member"
        );
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_program_parse_interface_declaration(
    basl_program_state_t *program,
    size_t *cursor,
    int is_public
) {
    basl_status_t status;
    const basl_token_t *interface_token;
    const basl_token_t *name_token;
    const basl_token_t *type_token;
    const basl_token_t *method_name_token;
    const basl_token_t *param_name_token;
    const char *name_text;
    size_t name_length;
    basl_interface_decl_t *decl;
    basl_interface_method_t *method;
    basl_parser_type_t param_type;

    interface_token = basl_program_cursor_peek(program, *cursor);
    if (interface_token == NULL || interface_token->kind != BASL_TOKEN_INTERFACE) {
        return basl_compile_report(
            program,
            interface_token == NULL ? basl_program_eof_span(program) : interface_token->span,
            "expected 'interface'"
        );
    }
    basl_program_cursor_advance(program, cursor);

    name_token = basl_program_cursor_peek(program, *cursor);
    if (name_token == NULL || name_token->kind != BASL_TOKEN_IDENTIFIER) {
        return basl_compile_report(program, interface_token->span, "expected interface name");
    }
    name_text = basl_program_token_text(program, name_token, &name_length);
    if (basl_program_find_interface(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(program, name_token->span, "interface is already declared");
    }
    if (basl_program_find_class(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(program, name_token->span, "interface name conflicts with class");
    }
    if (basl_program_find_constant(program, name_text, name_length, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "interface name conflicts with global constant"
        );
    }
    if (basl_program_find_enum(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(program, name_token->span, "interface name conflicts with enum");
    }
    if (basl_program_find_global(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "interface name conflicts with global variable"
        );
    }
    if (basl_program_find_top_level_function_name(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(program, name_token->span, "interface name conflicts with function");
    }

    status = basl_program_grow_interfaces(program, program->interface_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl = &program->interfaces[program->interface_count];
    memset(decl, 0, sizeof(*decl));
    decl->source_id = program->source->id;
    decl->name = name_text;
    decl->name_length = name_length;
    decl->name_span = name_token->span;
    decl->is_public = is_public;
    program->interface_count += 1U;
    basl_program_cursor_advance(program, cursor);

    type_token = basl_program_cursor_peek(program, *cursor);
    if (type_token == NULL || type_token->kind != BASL_TOKEN_LBRACE) {
        return basl_compile_report(program, name_token->span, "expected '{' after interface name");
    }
    basl_program_cursor_advance(program, cursor);

    while (1) {
        type_token = basl_program_cursor_peek(program, *cursor);
        if (type_token == NULL) {
            return basl_compile_report(
                program,
                basl_program_eof_span(program),
                "expected '}' after interface body"
            );
        }
        if (type_token->kind == BASL_TOKEN_RBRACE) {
            basl_program_cursor_advance(program, cursor);
            break;
        }
        if (type_token->kind != BASL_TOKEN_FN) {
            return basl_compile_report(
                program,
                type_token->span,
                "expected interface method declaration"
            );
        }
        basl_program_cursor_advance(program, cursor);

        method_name_token = basl_program_cursor_peek(program, *cursor);
        if (method_name_token == NULL || method_name_token->kind != BASL_TOKEN_IDENTIFIER) {
            return basl_compile_report(program, type_token->span, "expected method name");
        }
        name_text = basl_program_token_text(program, method_name_token, &name_length);
        if (basl_interface_decl_find_method(decl, name_text, name_length, NULL, NULL)) {
            return basl_compile_report(
                program,
                method_name_token->span,
                "interface method is already declared"
            );
        }
        basl_program_cursor_advance(program, cursor);

        status = basl_interface_decl_grow_methods(program, decl, decl->method_count + 1U);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        method = &decl->methods[decl->method_count];
        memset(method, 0, sizeof(*method));
        method->name = name_text;
        method->name_length = name_length;
        method->name_span = method_name_token->span;
        decl->method_count += 1U;

        type_token = basl_program_cursor_peek(program, *cursor);
        if (type_token == NULL || type_token->kind != BASL_TOKEN_LPAREN) {
            return basl_compile_report(
                program,
                method_name_token->span,
                "expected '(' after method name"
            );
        }
        basl_program_cursor_advance(program, cursor);

        type_token = basl_program_cursor_peek(program, *cursor);
        if (type_token != NULL && type_token->kind != BASL_TOKEN_RPAREN) {
            while (1) {
                status = basl_program_parse_type_reference(
                    program,
                    cursor,
                    "unsupported function parameter type",
                    &param_type
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_program_require_non_void_type(
                    program,
                    type_token == NULL ? method_name_token->span : type_token->span,
                    param_type,
                    "function parameters cannot use type void"
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }

                param_name_token = basl_program_cursor_peek(program, *cursor);
                if (param_name_token == NULL || param_name_token->kind != BASL_TOKEN_IDENTIFIER) {
                    return basl_compile_report(program, type_token->span, "expected parameter name");
                }
                basl_program_cursor_advance(program, cursor);

                status = basl_interface_method_grow_params(
                    program,
                    method,
                    method->param_count + 1U
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                method->param_types[method->param_count] = param_type;
                method->param_count += 1U;

                type_token = basl_program_cursor_peek(program, *cursor);
                if (type_token != NULL && type_token->kind == BASL_TOKEN_COMMA) {
                    basl_program_cursor_advance(program, cursor);
                    type_token = basl_program_cursor_peek(program, *cursor);
                    continue;
                }
                break;
            }
        }

        type_token = basl_program_cursor_peek(program, *cursor);
        if (type_token == NULL || type_token->kind != BASL_TOKEN_RPAREN) {
            return basl_compile_report(
                program,
                method_name_token->span,
                "expected ')' after parameter list"
            );
        }
        basl_program_cursor_advance(program, cursor);

        type_token = basl_program_cursor_peek(program, *cursor);
        if (type_token == NULL || type_token->kind != BASL_TOKEN_ARROW) {
            return basl_compile_report(
                program,
                method_name_token->span,
                "expected '->' after method signature"
            );
        }
        basl_program_cursor_advance(program, cursor);

        status = basl_program_parse_interface_method_return_types(
            program,
            cursor,
            "unsupported function return type",
            method
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        type_token = basl_program_cursor_peek(program, *cursor);
        if (type_token == NULL || type_token->kind != BASL_TOKEN_SEMICOLON) {
            return basl_compile_report(
                program,
                method_name_token->span,
                "expected ';' after interface method"
            );
        }
        basl_program_cursor_advance(program, cursor);
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_program_validate_class_interface_conformance(
    basl_program_state_t *program,
    basl_class_decl_t *decl
) {
    basl_status_t status;
    size_t i;

    for (i = 0U; i < decl->implemented_interface_count; i += 1U) {
        const basl_interface_decl_t *interface_decl;
        basl_class_interface_impl_t *impl;
        size_t j;

        interface_decl = &program->interfaces[decl->implemented_interfaces[i]];
        status = basl_class_decl_grow_interface_impls(
            program,
            decl,
            decl->interface_impl_count + 1U
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        impl = &decl->interface_impls[decl->interface_impl_count];
        memset(impl, 0, sizeof(*impl));
        impl->interface_index = decl->implemented_interfaces[i];
        impl->function_count = interface_decl->method_count;
        decl->interface_impl_count += 1U;
        if (impl->function_count != 0U) {
            void *memory = NULL;
            status = basl_runtime_alloc(
                program->registry->runtime,
                impl->function_count * sizeof(*impl->function_indices),
                &memory,
                program->error
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            impl->function_indices = (size_t *)memory;
        }

        for (j = 0U; j < interface_decl->method_count; j += 1U) {
            const basl_interface_method_t *interface_method = &interface_decl->methods[j];
            const basl_class_method_t *class_method;
            const basl_function_decl_t *method_decl;
            size_t param_index;

            if (
                !basl_class_decl_find_method(
                    decl,
                    interface_method->name,
                    interface_method->name_length,
                    NULL,
                    &class_method
                )
            ) {
                return basl_compile_report(
                    program,
                    decl->name_span,
                    "class does not implement required interface method"
                );
            }

            method_decl = basl_binding_function_table_get(
                &program->functions,
                class_method->function_index
            );
            if (
                method_decl == NULL ||
                method_decl->return_count != interface_method->return_count ||
                method_decl->param_count != interface_method->param_count + 1U
            ) {
                return basl_compile_report(
                    program,
                    class_method->name_span,
                    "class method signature does not match interface"
                );
            }

            for (param_index = 0U; param_index < interface_method->return_count; param_index += 1U) {
                if (
                    !basl_parser_type_equal(
                        basl_function_return_type_at(method_decl, param_index),
                        basl_interface_method_return_type_at(interface_method, param_index)
                    )
                ) {
                    return basl_compile_report(
                        program,
                        class_method->name_span,
                        "class method signature does not match interface"
                    );
                }
            }

            for (param_index = 0U; param_index < interface_method->param_count; param_index += 1U) {
                if (
                    !basl_parser_type_equal(
                        method_decl->params[param_index + 1U].type,
                        interface_method->param_types[param_index]
                    )
                ) {
                    return basl_compile_report(
                        program,
                        class_method->name_span,
                        "class method signature does not match interface"
                    );
                }
            }

            impl->function_indices[j] = class_method->function_index;
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_program_synthesize_class_constructor(
    basl_program_state_t *program,
    basl_class_decl_t *decl
) {
    basl_status_t status;
    const basl_class_method_t *init_method;
    const basl_function_decl_t *init_decl;
    basl_function_decl_t *ctor_decl;
    size_t class_index;
    size_t ctor_index;
    size_t param_index;

    init_method = NULL;
    init_decl = NULL;
    class_index = (size_t)(decl - program->classes);
    if (!basl_class_decl_find_method(decl, "init", 4U, NULL, &init_method)) {
        return BASL_STATUS_OK;
    }

    init_decl = basl_binding_function_table_get(&program->functions, init_method->function_index);
    if (init_decl == NULL) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_INTERNAL,
            "class init declaration is missing"
        );
        return BASL_STATUS_INTERNAL;
    }
    if (
        init_decl->return_count != 1U ||
        (!basl_parser_type_is_void(init_decl->return_type) &&
         !basl_parser_type_is_err(init_decl->return_type))
    ) {
        return basl_compile_report(
            program,
            init_method->name_span,
            "init methods must return void or err"
        );
    }

    status = basl_program_grow_functions(program, program->functions.count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    ctor_index = program->functions.count;
    ctor_decl = &program->functions.functions[ctor_index];
    basl_binding_function_init(ctor_decl);
    ctor_decl->name = decl->name;
    ctor_decl->name_length = decl->name_length;
    ctor_decl->name_span = decl->name_span;
    ctor_decl->is_public = decl->is_public;
    status = basl_binding_function_add_return_type(
        program->registry->runtime,
        ctor_decl,
        basl_binding_type_class(class_index),
        program->error
    );
    if (status != BASL_STATUS_OK) {
        basl_binding_function_free(program->registry->runtime, ctor_decl);
        return status;
    }
    if (basl_parser_type_is_err(init_decl->return_type)) {
        status = basl_binding_function_add_return_type(
            program->registry->runtime,
            ctor_decl,
            basl_binding_type_primitive(BASL_TYPE_ERR),
            program->error
        );
        if (status != BASL_STATUS_OK) {
            basl_binding_function_free(program->registry->runtime, ctor_decl);
            return status;
        }
    }
    ctor_decl->source = program->source;
    ctor_decl->tokens = program->tokens;

    init_decl = basl_binding_function_table_get(&program->functions, init_method->function_index);
    if (init_decl == NULL) {
        basl_binding_function_free(program->registry->runtime, ctor_decl);
        basl_error_set_literal(
            program->error,
            BASL_STATUS_INTERNAL,
            "class init declaration is missing"
        );
        return BASL_STATUS_INTERNAL;
    }

    for (param_index = 1U; param_index < init_decl->param_count; param_index += 1U) {
        status = basl_binding_function_add_param(
            program->registry->runtime,
            ctor_decl,
            init_decl->params[param_index].name,
            init_decl->params[param_index].length,
            init_decl->params[param_index].span,
            init_decl->params[param_index].type,
            program->error
        );
        if (status != BASL_STATUS_OK) {
            basl_binding_function_free(program->registry->runtime, ctor_decl);
            return status;
        }
    }

    decl->constructor_function_index = ctor_index;
    program->functions.count += 1U;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_parse_class_declaration(
    basl_program_state_t *program,
    size_t *cursor,
    int is_public
) {
    basl_status_t status;
    const basl_token_t *class_token;
    const basl_token_t *name_token;
    const basl_token_t *type_token;
    const basl_token_t *field_name_token;
    const basl_token_t *param_name_token;
    const char *name_text;
    size_t name_length;
    size_t class_index;
    size_t interface_index;
    size_t body_depth;
    basl_class_decl_t *decl;
    basl_class_field_t *field;
    basl_class_method_t *method;
    basl_function_decl_t *method_decl;
    basl_parser_type_t field_type;
    int member_is_public;

    class_token = basl_program_cursor_peek(program, *cursor);
    if (class_token == NULL || class_token->kind != BASL_TOKEN_CLASS) {
        return basl_compile_report(
            program,
            class_token == NULL ? basl_program_eof_span(program) : class_token->span,
            "expected 'class'"
        );
    }
    basl_program_cursor_advance(program, cursor);

    name_token = basl_program_cursor_peek(program, *cursor);
    if (name_token == NULL || name_token->kind != BASL_TOKEN_IDENTIFIER) {
        return basl_compile_report(program, class_token->span, "expected class name");
    }
    name_text = basl_program_token_text(program, name_token, &name_length);
    if (basl_program_find_class(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(program, name_token->span, "class is already declared");
    }
    if (basl_program_find_constant(program, name_text, name_length, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "class name conflicts with global constant"
        );
    }
    if (basl_program_find_enum(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(program, name_token->span, "class name conflicts with enum");
    }
    if (basl_program_find_global(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(
            program,
            name_token->span,
            "class name conflicts with global variable"
        );
    }
    if (basl_program_find_top_level_function_name(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(program, name_token->span, "class name conflicts with function");
    }
    if (basl_program_find_interface(program, name_text, name_length, NULL, NULL)) {
        return basl_compile_report(program, name_token->span, "class name conflicts with interface");
    }

    status = basl_program_grow_classes(program, program->class_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    class_index = program->class_count;
    decl = &program->classes[program->class_count];
    memset(decl, 0, sizeof(*decl));
    decl->constructor_function_index = (size_t)-1;
    decl->source_id = program->source->id;
    decl->name = name_text;
    decl->name_length = name_length;
    decl->name_span = name_token->span;
    decl->is_public = is_public;
    program->class_count += 1U;
    basl_program_cursor_advance(program, cursor);

    type_token = basl_program_cursor_peek(program, *cursor);
    if (basl_program_token_is_identifier_text(program, type_token, "implements", 10U)) {
        basl_program_cursor_advance(program, cursor);
        while (1) {
            status = basl_program_parse_type_reference(
                program,
                cursor,
                "unknown interface",
                &field_type
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (!basl_parser_type_is_interface(field_type)) {
                type_token = basl_program_cursor_peek(program, *cursor);
                return basl_compile_report(
                    program,
                    type_token == NULL ? name_token->span : type_token->span,
                    "unknown interface"
                );
            }
            interface_index = field_type.object_index;
            if (basl_class_decl_implements_interface(decl, interface_index)) {
                return basl_compile_report(
                    program,
                    basl_program_cursor_peek(program, *cursor - 1U)->span,
                    "interface is already implemented"
                );
            }
            status = basl_class_decl_grow_implemented_interfaces(
                program,
                decl,
                decl->implemented_interface_count + 1U
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            decl->implemented_interfaces[decl->implemented_interface_count] = interface_index;
            decl->implemented_interface_count += 1U;

            type_token = basl_program_cursor_peek(program, *cursor);
            if (type_token == NULL || type_token->kind != BASL_TOKEN_COMMA) {
                break;
            }
            basl_program_cursor_advance(program, cursor);
        }
    }

    type_token = basl_program_cursor_peek(program, *cursor);
    if (type_token == NULL || type_token->kind != BASL_TOKEN_LBRACE) {
        return basl_compile_report(program, name_token->span, "expected '{' after class name");
    }
    basl_program_cursor_advance(program, cursor);

    while (1) {
        type_token = basl_program_cursor_peek(program, *cursor);
        if (type_token == NULL) {
            return basl_compile_report(
                program,
                basl_program_eof_span(program),
                "expected '}' after class body"
            );
        }
        if (type_token->kind == BASL_TOKEN_RBRACE) {
            basl_program_cursor_advance(program, cursor);
            break;
        }

        member_is_public = basl_program_parse_optional_pub(program, cursor);
        type_token = basl_program_cursor_peek(program, *cursor);
        if (type_token == NULL) {
            return basl_compile_report(
                program,
                basl_program_eof_span(program),
                "expected class member declaration"
            );
        }

        if (
            type_token->kind == BASL_TOKEN_FN &&
            basl_program_token_at(program, *cursor + 1U) != NULL &&
            basl_program_token_at(program, *cursor + 1U)->kind == BASL_TOKEN_IDENTIFIER &&
            basl_program_token_at(program, *cursor + 2U) != NULL &&
            basl_program_token_at(program, *cursor + 2U)->kind == BASL_TOKEN_LPAREN
        ) {
            basl_program_cursor_advance(program, cursor);

            name_token = basl_program_cursor_peek(program, *cursor);
            if (name_token == NULL || name_token->kind != BASL_TOKEN_IDENTIFIER) {
                return basl_compile_report(program, type_token->span, "expected method name");
            }
            name_text = basl_program_token_text(program, name_token, &name_length);
            if (basl_class_decl_find_method(decl, name_text, name_length, NULL, NULL)) {
                return basl_compile_report(program, name_token->span, "class method is already declared");
            }
            if (basl_class_decl_find_field(decl, name_text, name_length, NULL, NULL)) {
                return basl_compile_report(program, name_token->span, "class method conflicts with field");
            }

            status = basl_program_grow_functions(program, program->functions.count + 1U);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            method_decl = &program->functions.functions[program->functions.count];
            basl_binding_function_init(method_decl);
            method_decl->name = name_text;
            method_decl->name_length = name_length;
            method_decl->name_span = name_token->span;
            method_decl->is_public = member_is_public;
            method_decl->owner_class_index = class_index;
            method_decl->source = program->source;
            method_decl->tokens = program->tokens;
            cursor[0] += 1U;

            status = basl_binding_function_add_param(
                program->registry->runtime,
                method_decl,
                "self",
                4U,
                name_token->span,
                basl_binding_type_class(class_index),
                program->error
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }

            type_token = basl_program_token_at(program, *cursor);
            if (type_token == NULL || type_token->kind != BASL_TOKEN_LPAREN) {
                return basl_compile_report(
                    program,
                    name_token->span,
                    "expected '(' after method name"
                );
            }
            cursor[0] += 1U;

            type_token = basl_program_token_at(program, *cursor);
            if (type_token != NULL && type_token->kind != BASL_TOKEN_RPAREN) {
                while (1) {
                    status = basl_program_parse_type_reference(
                        program,
                        cursor,
                        "unsupported function parameter type",
                        &field_type
                    );
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    status = basl_program_require_non_void_type(
                        program,
                        type_token == NULL ? name_token->span : type_token->span,
                        field_type,
                        "function parameters cannot use type void"
                    );
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }

                    param_name_token = basl_program_token_at(program, *cursor);
                    if (param_name_token == NULL ||
                        param_name_token->kind != BASL_TOKEN_IDENTIFIER) {
                        return basl_compile_report(
                            program,
                            type_token->span,
                            "expected parameter name"
                        );
                    }

                    status = basl_program_add_param(
                        program,
                        method_decl,
                        field_type,
                        param_name_token
                    );
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    cursor[0] += 1U;

                    type_token = basl_program_token_at(program, *cursor);
                    if (type_token != NULL && type_token->kind == BASL_TOKEN_COMMA) {
                        cursor[0] += 1U;
                        type_token = basl_program_token_at(program, *cursor);
                        continue;
                    }
                    break;
                }
            }

            type_token = basl_program_token_at(program, *cursor);
            if (type_token == NULL || type_token->kind != BASL_TOKEN_RPAREN) {
                return basl_compile_report(
                    program,
                    name_token->span,
                    "expected ')' after parameter list"
                );
            }
            cursor[0] += 1U;

            type_token = basl_program_token_at(program, *cursor);
            if (type_token == NULL || type_token->kind != BASL_TOKEN_ARROW) {
                return basl_compile_report(
                    program,
                    name_token->span,
                    "expected '->' after method signature"
                );
            }
            cursor[0] += 1U;

            status = basl_program_parse_function_return_types(
                program,
                cursor,
                "unsupported function return type",
                method_decl
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }

            type_token = basl_program_token_at(program, *cursor);
            if (type_token == NULL || type_token->kind != BASL_TOKEN_LBRACE) {
                return basl_compile_report(
                    program,
                    name_token->span,
                    "expected '{' before method body"
                );
            }
            cursor[0] += 1U;
            method_decl->body_start = *cursor;

            body_depth = 1U;
            while (body_depth > 0U) {
                type_token = basl_program_token_at(program, *cursor);
                if (type_token == NULL || type_token->kind == BASL_TOKEN_EOF) {
                    return basl_compile_report(
                        program,
                        basl_program_eof_span(program),
                        "expected '}' after method body"
                    );
                }

                if (type_token->kind == BASL_TOKEN_LBRACE) {
                    body_depth += 1U;
                } else if (type_token->kind == BASL_TOKEN_RBRACE) {
                    body_depth -= 1U;
                    if (body_depth == 0U) {
                        method_decl->body_end = *cursor;
                        cursor[0] += 1U;
                        break;
                    }
                }
                cursor[0] += 1U;
            }

            status = basl_class_decl_grow_methods(program, decl, decl->method_count + 1U);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            method = &decl->methods[decl->method_count];
            memset(method, 0, sizeof(*method));
            method->name = name_text;
            method->name_length = name_length;
            method->name_span = name_token->span;
            method->is_public = member_is_public;
            method->function_index = program->functions.count;
            decl->method_count += 1U;
            program->functions.count += 1U;
            continue;
        }

        status = basl_program_parse_type_reference(
            program,
            cursor,
            "unsupported class field type",
            &field_type
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_program_require_non_void_type(
            program,
            type_token == NULL ? name_token->span : type_token->span,
            field_type,
            "class fields cannot use type void"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        field_name_token = basl_program_cursor_peek(program, *cursor);
        if (field_name_token == NULL || field_name_token->kind != BASL_TOKEN_IDENTIFIER) {
            return basl_compile_report(program, type_token->span, "expected field name");
        }
        name_text = basl_program_token_text(program, field_name_token, &name_length);
        if (basl_class_decl_find_field(decl, name_text, name_length, NULL, NULL)) {
            return basl_compile_report(
                program,
                field_name_token->span,
                "class field is already declared"
            );
        }
        basl_program_cursor_advance(program, cursor);

        type_token = basl_program_cursor_peek(program, *cursor);
        if (type_token == NULL || type_token->kind != BASL_TOKEN_SEMICOLON) {
            return basl_compile_report(
                program,
                field_name_token->span,
                "expected ';' after class field"
            );
        }
        basl_program_cursor_advance(program, cursor);

        status = basl_class_decl_grow_fields(program, decl, decl->field_count + 1U);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        field = &decl->fields[decl->field_count];
        memset(field, 0, sizeof(*field));
        field->name = name_text;
        field->name_length = name_length;
        field->name_span = field_name_token->span;
        field->is_public = member_is_public;
        field->type = field_type;
        decl->field_count += 1U;
    }

    status = basl_program_validate_class_interface_conformance(program, decl);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_program_synthesize_class_constructor(program, decl);
}

static basl_status_t basl_program_parse_declarations(
    basl_program_state_t *program
) {
    basl_status_t status;
    size_t cursor;
    const basl_token_t *token;
    const basl_token_t *name_token;
    const basl_token_t *type_token;
    const basl_token_t *param_name_token;
    basl_function_decl_t *decl;
    const char *name_text;
    size_t name_length;
    size_t body_depth;
    int is_public;

    cursor = 0U;
    while (1) {
        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind == BASL_TOKEN_EOF) {
            break;
        }
        is_public = basl_program_parse_optional_pub(program, &cursor);
        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind == BASL_TOKEN_EOF) {
            return basl_compile_report(
                program,
                basl_program_eof_span(program),
                "expected declaration after 'pub'"
            );
        }
        if (token->kind == BASL_TOKEN_IMPORT) {
            if (is_public) {
                return basl_compile_report(program, token->span, "imports cannot be declared 'pub'");
            }
            status = basl_program_parse_import(program, &cursor);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (token->kind == BASL_TOKEN_CONST) {
            status = basl_program_parse_constant_declaration(program, &cursor, is_public);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (token->kind == BASL_TOKEN_ENUM) {
            status = basl_program_parse_enum_declaration(program, &cursor, is_public);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (token->kind == BASL_TOKEN_INTERFACE) {
            status = basl_program_parse_interface_declaration(program, &cursor, is_public);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (token->kind == BASL_TOKEN_CLASS) {
            status = basl_program_parse_class_declaration(program, &cursor, is_public);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (basl_program_is_global_variable_declaration_start(program, cursor)) {
            status = basl_program_parse_global_variable_declaration(program, &cursor, is_public);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (token->kind != BASL_TOKEN_FN) {
            return basl_compile_report(
                program,
                token->span,
                "expected top-level 'import', 'const', 'enum', 'interface', 'class', variable declaration, or 'fn'"
            );
        }
        cursor += 1U;

        name_token = basl_program_token_at(program, cursor);
        if (name_token == NULL || name_token->kind != BASL_TOKEN_IDENTIFIER) {
            return basl_compile_report(
                program,
                token->span,
                "expected function name"
            );
        }

        name_text = basl_program_token_text(program, name_token, &name_length);
        if (
            basl_program_find_top_level_function_name(
                program,
                name_text,
                name_length,
                NULL,
                NULL
            )
        ) {
            return basl_compile_report(
                program,
                name_token->span,
                "function is already declared"
            );
        }
        if (basl_program_find_constant(program, name_text, name_length, NULL)) {
            return basl_compile_report(
                program,
                name_token->span,
                "function name conflicts with global constant"
            );
        }
        if (basl_program_find_global(program, name_text, name_length, NULL, NULL)) {
            return basl_compile_report(
                program,
                name_token->span,
                "function name conflicts with global variable"
            );
        }
        if (basl_program_find_class(program, name_text, name_length, NULL, NULL)) {
            return basl_compile_report(
                program,
                name_token->span,
                "function name conflicts with class"
            );
        }
        if (basl_program_find_enum(program, name_text, name_length, NULL, NULL)) {
            return basl_compile_report(
                program,
                name_token->span,
                "function name conflicts with enum"
            );
        }
        if (basl_program_find_interface(program, name_text, name_length, NULL, NULL)) {
            return basl_compile_report(
                program,
                name_token->span,
                "function name conflicts with interface"
            );
        }

        status = basl_program_grow_functions(program, program->functions.count + 1U);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        decl = &program->functions.functions[program->functions.count];
        basl_binding_function_init(decl);
        decl->name = name_text;
        decl->name_length = name_length;
        decl->name_span = name_token->span;
        decl->is_public = is_public;
        decl->source = program->source;
        decl->tokens = program->tokens;
        cursor += 1U;

        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_LPAREN) {
            return basl_program_fail_partial_decl(
                program,
                decl,
                basl_compile_report(
                    program,
                    name_token->span,
                    "expected '(' after function name"
                )
            );
        }
        cursor += 1U;

        token = basl_program_token_at(program, cursor);
        if (token != NULL && token->kind != BASL_TOKEN_RPAREN) {
            while (1) {
                basl_parser_type_t param_type;

                type_token = basl_program_token_at(program, cursor);
                status = basl_program_parse_type_reference(
                    program,
                    &cursor,
                    "unsupported function parameter type",
                    &param_type
                );
                if (status != BASL_STATUS_OK) {
                    return basl_program_fail_partial_decl(program, decl, status);
                }
                status = basl_program_require_non_void_type(
                    program,
                    type_token == NULL ? decl->name_span : type_token->span,
                    param_type,
                    "function parameters cannot use type void"
                );
                if (status != BASL_STATUS_OK) {
                    return basl_program_fail_partial_decl(program, decl, status);
                }

                param_name_token = basl_program_token_at(program, cursor);
                if (param_name_token == NULL ||
                    param_name_token->kind != BASL_TOKEN_IDENTIFIER) {
                    return basl_program_fail_partial_decl(
                        program,
                        decl,
                        basl_compile_report(
                            program,
                            type_token->span,
                            "expected parameter name"
                        )
                    );
                }
                status = basl_program_add_param(
                    program,
                    decl,
                    param_type,
                    param_name_token
                );
                if (status != BASL_STATUS_OK) {
                    return basl_program_fail_partial_decl(program, decl, status);
                }
                cursor += 1U;

                token = basl_program_token_at(program, cursor);
                if (token != NULL && token->kind == BASL_TOKEN_COMMA) {
                    cursor += 1U;
                    continue;
                }
                break;
            }
        }

        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_RPAREN) {
            return basl_program_fail_partial_decl(
                program,
                decl,
                basl_compile_report(
                    program,
                    decl->name_span,
                    "expected ')' after parameter list"
                )
            );
        }
        cursor += 1U;

        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_ARROW) {
            return basl_program_fail_partial_decl(
                program,
                decl,
                basl_compile_report(
                    program,
                    decl->name_span,
                    "expected '->' after function signature"
                )
            );
        }
        cursor += 1U;

        if (basl_program_names_equal(name_text, name_length, "main", 4U)) {
            program->functions.main_index = program->functions.count;
            program->functions.has_main = 1;
            type_token = basl_program_token_at(program, cursor);
            status = basl_program_parse_function_return_types(
                program,
                &cursor,
                "main entrypoint must declare return type i32",
                decl
            );
            if (status != BASL_STATUS_OK) {
                return basl_program_fail_partial_decl(program, decl, status);
            }
            status = basl_program_validate_main_signature(program, decl, type_token);
            if (status != BASL_STATUS_OK) {
                return basl_program_fail_partial_decl(program, decl, status);
            }
        } else {
            status = basl_program_parse_function_return_types(
                program,
                &cursor,
                "unsupported function return type",
                decl
            );
            if (status != BASL_STATUS_OK) {
                return basl_program_fail_partial_decl(program, decl, status);
            }
        }

        token = basl_program_token_at(program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_LBRACE) {
            return basl_program_fail_partial_decl(
                program,
                decl,
                basl_compile_report(
                    program,
                    decl->name_span,
                    "expected '{' before function body"
                )
            );
        }
        cursor += 1U;
        decl->body_start = cursor;

        body_depth = 1U;
        while (body_depth > 0U) {
            token = basl_program_token_at(program, cursor);
            if (token == NULL || token->kind == BASL_TOKEN_EOF) {
                return basl_program_fail_partial_decl(
                    program,
                    decl,
                    basl_compile_report(
                        program,
                        basl_program_eof_span(program),
                        "expected '}' after function body"
                    )
                );
            }

            if (token->kind == BASL_TOKEN_LBRACE) {
                body_depth += 1U;
            } else if (token->kind == BASL_TOKEN_RBRACE) {
                body_depth -= 1U;
                if (body_depth == 0U) {
                    decl->body_end = cursor;
                    cursor += 1U;
                    break;
                }
            }
            cursor += 1U;
        }

        program->functions.count += 1U;
        decl = NULL;
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_program_parse_source(
    basl_program_state_t *program,
    basl_source_id_t source_id
) {
    basl_status_t status;
    const basl_source_file_t *previous_source;
    const basl_token_list_t *previous_tokens;
    const basl_source_file_t *source;
    basl_token_list_t tokens;
    size_t module_index;

    module_index = 0U;
    if (basl_program_module_find(program, source_id, &module_index)) {
        if (program->modules[module_index].state != BASL_MODULE_UNSEEN) {
            return BASL_STATUS_OK;
        }
    }

    source = basl_source_registry_get(program->registry, source_id);
    if (source == NULL) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_INVALID_ARGUMENT,
            "source_id must reference a registered source file"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    basl_token_list_init(&tokens, program->registry->runtime);
    status = basl_lex_source(
        program->registry,
        source_id,
        &tokens,
        program->diagnostics,
        program->error
    );
    if (status != BASL_STATUS_OK) {
        basl_token_list_free(&tokens);
        return status;
    }
    status = basl_program_add_module(
        program,
        source_id,
        source,
        &tokens,
        BASL_MODULE_PARSING,
        &module_index
    );
    if (status != BASL_STATUS_OK) {
        basl_token_list_free(&tokens);
        return status;
    }
    previous_source = program->source;
    previous_tokens = program->tokens;
    basl_program_set_module_context(program, source, program->modules[module_index].tokens);
    status = basl_program_parse_declarations(program);
    basl_program_set_module_context(program, previous_source, previous_tokens);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    program->modules[module_index].state = BASL_MODULE_PARSED;
    return BASL_STATUS_OK;
}

static void basl_program_free(basl_program_state_t *program) {
    size_t i;
    void *memory;

    if (program == NULL) {
        return;
    }

    for (i = 0U; i < program->module_count; i += 1U) {
        size_t import_index;

        for (import_index = 0U; import_index < program->modules[i].import_count; import_index += 1U) {
            memory = program->modules[i].imports[import_index].owned_alias;
            basl_runtime_free(program->registry->runtime, &memory);
        }
        memory = program->modules[i].imports;
        basl_runtime_free(program->registry->runtime, &memory);
        if (program->modules[i].tokens != NULL) {
            memory = program->modules[i].tokens;
            basl_token_list_free(program->modules[i].tokens);
            basl_runtime_free(program->registry->runtime, &memory);
        }
    }
    memory = program->modules;
    basl_runtime_free(program->registry->runtime, &memory);
    program->modules = NULL;
    program->module_count = 0U;
    program->module_capacity = 0U;
    for (i = 0U; i < program->class_count; i += 1U) {
        basl_class_decl_free(program, &program->classes[i]);
    }
    memory = program->classes;
    basl_runtime_free(program->registry->runtime, &memory);
    program->classes = NULL;
    program->class_count = 0U;
    program->class_capacity = 0U;
    for (i = 0U; i < program->interface_count; i += 1U) {
        basl_interface_decl_free(program, &program->interfaces[i]);
    }
    memory = program->interfaces;
    basl_runtime_free(program->registry->runtime, &memory);
    program->interfaces = NULL;
    program->interface_count = 0U;
    program->interface_capacity = 0U;
    for (i = 0U; i < program->enum_count; i += 1U) {
        basl_enum_decl_free(program, &program->enums[i]);
    }
    memory = program->enums;
    basl_runtime_free(program->registry->runtime, &memory);
    program->enums = NULL;
    program->enum_count = 0U;
    program->enum_capacity = 0U;
    memory = program->array_types;
    basl_runtime_free(program->registry->runtime, &memory);
    program->array_types = NULL;
    program->array_type_count = 0U;
    program->array_type_capacity = 0U;
    memory = program->map_types;
    basl_runtime_free(program->registry->runtime, &memory);
    program->map_types = NULL;
    program->map_type_count = 0U;
    program->map_type_capacity = 0U;
    for (i = 0U; i < program->function_type_count; i += 1U) {
        basl_function_type_decl_free(program, &program->function_types[i]);
    }
    memory = program->function_types;
    basl_runtime_free(program->registry->runtime, &memory);
    program->function_types = NULL;
    program->function_type_count = 0U;
    program->function_type_capacity = 0U;
    for (i = 0U; i < program->constant_count; i += 1U) {
        basl_value_release(&program->constants[i].value);
    }
    memory = program->constants;
    basl_runtime_free(program->registry->runtime, &memory);
    program->constants = NULL;
    program->constant_count = 0U;
    program->constant_capacity = 0U;
    memory = program->globals;
    basl_runtime_free(program->registry->runtime, &memory);
    program->globals = NULL;
    program->global_count = 0U;
    program->global_capacity = 0U;
    basl_binding_function_table_free(&program->functions);
}

static const basl_token_t *basl_parser_previous(const basl_parser_state_t *state);

static basl_source_span_t basl_parser_fallback_span(
    const basl_parser_state_t *state
) {
    basl_source_span_t span;
    const basl_token_t *token;

    basl_source_span_clear(&span);
    if (state == NULL || state->program == NULL || state->program->source == NULL) {
        return span;
    }

    span.source_id = state->program->source->id;
    token = basl_parser_previous(state);
    if (token != NULL) {
        return token->span;
    }

    return span;
}

static const basl_token_t *basl_parser_peek(const basl_parser_state_t *state) {
    if (state == NULL || state->current >= state->body_end) {
        return NULL;
    }

    return basl_program_token_at(state->program, state->current);
}

static const basl_token_t *basl_parser_previous(const basl_parser_state_t *state) {
    if (state == NULL || state->current == 0U) {
        return NULL;
    }

    return basl_program_token_at(state->program, state->current - 1U);
}

static int basl_parser_check(
    const basl_parser_state_t *state,
    basl_token_kind_t kind
) {
    const basl_token_t *token;

    token = basl_parser_peek(state);
    return token != NULL && token->kind == kind;
}

static int basl_parser_is_at_end(const basl_parser_state_t *state) {
    return basl_parser_peek(state) == NULL;
}

static const basl_token_t *basl_parser_advance(basl_parser_state_t *state) {
    if (!basl_parser_is_at_end(state)) {
        state->current += 1U;
    }

    return basl_parser_previous(state);
}

static int basl_parser_match(
    basl_parser_state_t *state,
    basl_token_kind_t kind
) {
    if (!basl_parser_check(state, kind)) {
        return 0;
    }

    basl_parser_advance(state);
    return 1;
}

static basl_status_t basl_parser_report(
    basl_parser_state_t *state,
    basl_source_span_t span,
    const char *message
) {
    return basl_compile_report(state->program, span, message);
}

static basl_status_t basl_parser_require_type(
    basl_parser_state_t *state,
    basl_source_span_t span,
    basl_parser_type_t actual_type,
    basl_parser_type_t expected_type,
    const char *message
) {
    if (basl_program_type_is_assignable(state->program, expected_type, actual_type)) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, span, message);
}

static basl_status_t basl_parser_require_bool_type(
    basl_parser_state_t *state,
    basl_source_span_t span,
    basl_parser_type_t actual_type,
    const char *message
) {
    return basl_parser_require_type(
        state,
        span,
        actual_type,
        basl_binding_type_primitive(BASL_TYPE_BOOL),
        message
    );
}

static basl_status_t basl_parser_require_same_type(
    basl_parser_state_t *state,
    basl_source_span_t span,
    basl_parser_type_t left_type,
    basl_parser_type_t right_type,
    const char *message
) {
    if (
        basl_parser_type_supports_binary_operator(
            BASL_BINARY_OPERATOR_EQUAL,
            left_type,
            right_type
        )
    ) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, span, message);
}

static basl_status_t basl_parser_require_i32_operands(
    basl_parser_state_t *state,
    basl_source_span_t span,
    basl_parser_type_t left_type,
    basl_parser_type_t right_type,
    basl_binary_operator_kind_t operator_kind,
    const char *message
) {
    if (basl_parser_type_supports_binary_operator(operator_kind, left_type, right_type)) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, span, message);
}

static basl_status_t basl_parser_require_unary_operator(
    basl_parser_state_t *state,
    basl_source_span_t span,
    basl_unary_operator_kind_t operator_kind,
    basl_parser_type_t operand_type,
    const char *message
) {
    if (basl_parser_type_supports_unary_operator(operator_kind, operand_type)) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, span, message);
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
        return basl_parser_report(
            state,
            basl_program_eof_span(state->program),
            message
        );
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
    return basl_program_token_text(state->program, token, out_length);
}

static basl_status_t basl_parser_parse_int_literal(
    basl_parser_state_t *state,
    const basl_token_t *token,
    basl_value_t *out_value
) {
    const char *text;
    size_t length;
    char buffer[128];
    char *end;
    long long parsed;

    text = basl_parser_token_text(state, token, &length);
    if (text == NULL || length == 0U) {
        return basl_parser_report(state, token->span, "invalid integer literal");
    }
    if (length >= sizeof(buffer)) {
        return basl_parser_report(state, token->span, "integer literal is too long");
    }

    memcpy(buffer, text, length);
    buffer[length] = '\0';
    errno = 0;
    parsed = strtoll(buffer, &end, 0);
    if (errno != 0 || end == buffer || *end != '\0') {
        return basl_parser_report(state, token->span, "invalid integer literal");
    }

    basl_value_init_int(out_value, (int64_t)parsed);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_float_literal(
    basl_parser_state_t *state,
    const basl_token_t *token,
    basl_value_t *out_value
) {
    const char *text;
    size_t length;
    char buffer[128];
    char *end;
    double parsed;

    text = basl_parser_token_text(state, token, &length);
    if (text == NULL || length == 0U) {
        return basl_parser_report(state, token->span, "invalid float literal");
    }
    if (length >= sizeof(buffer)) {
        return basl_parser_report(state, token->span, "float literal is too long");
    }

    memcpy(buffer, text, length);
    buffer[length] = '\0';
    errno = 0;
    parsed = strtod(buffer, &end);
    if (errno != 0 || end == buffer || *end != '\0') {
        return basl_parser_report(state, token->span, "invalid float literal");
    }

    basl_value_init_float(out_value, parsed);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_emit_opcode(
    basl_parser_state_t *state,
    basl_opcode_t opcode,
    basl_source_span_t span
) {
    return basl_chunk_write_opcode(&state->chunk, opcode, span, state->program->error);
}

static basl_status_t basl_parser_emit_u32(
    basl_parser_state_t *state,
    uint32_t value,
    basl_source_span_t span
) {
    return basl_chunk_write_u32(&state->chunk, value, span, state->program->error);
}

static basl_status_t basl_parser_emit_jump(
    basl_parser_state_t *state,
    basl_opcode_t opcode,
    basl_source_span_t span,
    size_t *out_operand_offset
) {
    basl_status_t status;

    status = basl_parser_emit_opcode(state, opcode, span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (out_operand_offset != NULL) {
        *out_operand_offset = basl_chunk_code_size(&state->chunk);
    }

    return basl_parser_emit_u32(state, 0U, span);
}

static basl_status_t basl_parser_patch_u32(
    basl_parser_state_t *state,
    size_t operand_offset,
    uint32_t value
) {
    uint8_t *code;
    size_t code_size;

    code = state->chunk.code.data;
    code_size = basl_chunk_code_size(&state->chunk);
    if (code == NULL || operand_offset + 3U >= code_size) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_INTERNAL,
            "jump patch offset is out of range"
        );
        return BASL_STATUS_INTERNAL;
    }

    code[operand_offset] = (uint8_t)(value & 0xffU);
    code[operand_offset + 1U] = (uint8_t)((value >> 8U) & 0xffU);
    code[operand_offset + 2U] = (uint8_t)((value >> 16U) & 0xffU);
    code[operand_offset + 3U] = (uint8_t)((value >> 24U) & 0xffU);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_patch_jump(
    basl_parser_state_t *state,
    size_t operand_offset
) {
    size_t code_size;
    size_t jump_distance;

    code_size = basl_chunk_code_size(&state->chunk);
    if (code_size < operand_offset + 4U) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_INTERNAL,
            "jump patch target is invalid"
        );
        return BASL_STATUS_INTERNAL;
    }

    jump_distance = code_size - (operand_offset + 4U);
    if (jump_distance > UINT32_MAX) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "jump distance overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    return basl_parser_patch_u32(state, operand_offset, (uint32_t)jump_distance);
}

static basl_status_t basl_parser_emit_loop(
    basl_parser_state_t *state,
    size_t loop_start,
    basl_source_span_t span
) {
    basl_status_t status;
    size_t loop_end;
    size_t distance;

    loop_end = basl_chunk_code_size(&state->chunk) + 5U;
    if (loop_end < loop_start) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_INTERNAL,
            "loop target is invalid"
        );
        return BASL_STATUS_INTERNAL;
    }

    distance = loop_end - loop_start;
    if (distance > UINT32_MAX) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "loop distance overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    status = basl_parser_emit_opcode(state, BASL_OPCODE_LOOP, span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_parser_emit_u32(state, (uint32_t)distance, span);
}

static basl_status_t basl_parser_grow_loops(
    basl_parser_state_t *state,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= state->loop_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = state->loop_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*state->loops)) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "loop context allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = state->loops;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            state->program->registry->runtime,
            next_capacity * sizeof(*state->loops),
            &memory,
            state->program->error
        );
    } else {
        status = basl_runtime_realloc(
            state->program->registry->runtime,
            &memory,
            next_capacity * sizeof(*state->loops),
            state->program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_loop_context_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*state->loops)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    state->loops = (basl_loop_context_t *)memory;
    state->loop_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_loop_context_grow_breaks(
    basl_parser_state_t *state,
    basl_loop_context_t *loop,
    size_t minimum_capacity
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= loop->break_capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = loop->break_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(*loop->break_jumps)) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "loop break table allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = loop->break_jumps;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            state->program->registry->runtime,
            next_capacity * sizeof(*loop->break_jumps),
            &memory,
            state->program->error
        );
    } else {
        status = basl_runtime_realloc(
            state->program->registry->runtime,
            &memory,
            next_capacity * sizeof(*loop->break_jumps),
            state->program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_loop_jump_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*loop->break_jumps)
            );
        }
    }

    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop->break_jumps = (basl_loop_jump_t *)memory;
    loop->break_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_loop_context_t *basl_parser_current_loop(
    basl_parser_state_t *state
) {
    if (state == NULL || state->loop_count == 0U) {
        return NULL;
    }

    return &state->loops[state->loop_count - 1U];
}

static basl_status_t basl_parser_grow_jump_offsets(
    basl_parser_state_t *state,
    size_t **offsets,
    size_t *capacity,
    size_t minimum_capacity,
    const char *overflow_message
) {
    basl_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= *capacity) {
        return BASL_STATUS_OK;
    }

    old_capacity = *capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }

    if (next_capacity > SIZE_MAX / sizeof(**offsets)) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            overflow_message
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = *offsets;
    if (memory == NULL) {
        status = basl_runtime_alloc(
            state->program->registry->runtime,
            next_capacity * sizeof(**offsets),
            &memory,
            state->program->error
        );
    } else {
        status = basl_runtime_realloc(
            state->program->registry->runtime,
            &memory,
            next_capacity * sizeof(**offsets),
            state->program->error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (size_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(**offsets)
            );
        }
    }
    if (status != BASL_STATUS_OK) {
        return status;
    }

    *offsets = (size_t *)memory;
    *capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_push_loop(
    basl_parser_state_t *state,
    size_t loop_start
) {
    basl_status_t status;
    basl_loop_context_t *loop;

    status = basl_parser_grow_loops(state, state->loop_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop = &state->loops[state->loop_count];
    memset(loop, 0, sizeof(*loop));
    loop->loop_start = loop_start;
    loop->scope_depth = basl_binding_scope_stack_depth(&state->locals);
    state->loop_count += 1U;
    return BASL_STATUS_OK;
}

static void basl_parser_pop_loop(
    basl_parser_state_t *state
) {
    basl_loop_context_t *loop;
    void *memory;

    if (state == NULL || state->loop_count == 0U) {
        return;
    }

    loop = &state->loops[state->loop_count - 1U];
    memory = loop->break_jumps;
    basl_runtime_free(state->program->registry->runtime, &memory);
    memset(loop, 0, sizeof(*loop));
    state->loop_count -= 1U;
}

static basl_status_t basl_parser_emit_scope_cleanup_to_depth(
    basl_parser_state_t *state,
    size_t target_depth,
    basl_source_span_t span
) {
    basl_status_t status;
    size_t count;
    size_t index;

    count = basl_binding_scope_stack_count_above_depth(&state->locals, target_depth);
    for (index = 0U; index < count; index += 1U) {
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static int basl_parser_local_matches_token(
    const basl_parser_state_t *state,
    const basl_token_t *left,
    const basl_binding_local_t *right
) {
    size_t left_length;
    const char *left_text;

    left_text = basl_parser_token_text(state, left, &left_length);
    return right != NULL &&
           left_text != NULL &&
           left_length == right->length &&
           memcmp(left_text, right->name, left_length) == 0;
}

static int basl_parser_find_local_symbol(
    const basl_parser_state_t *state,
    const basl_token_t *name_token,
    size_t *out_index
) {
    size_t i;

    for (i = basl_binding_scope_stack_count(&state->locals); i > 0U; --i) {
        if (
            basl_parser_local_matches_token(
                state,
                name_token,
                basl_binding_scope_stack_local_at(&state->locals, i - 1U)
            )
        ) {
            if (out_index != NULL) {
                *out_index = i - 1U;
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_parser_resolve_local_symbol(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    size_t *out_index,
    basl_parser_type_t *out_type,
    int *out_is_capture,
    size_t *out_capture_index,
    int *out_found
) {
    basl_function_decl_t *decl;
    size_t local_index;
    size_t capture_index;
    size_t parent_local_index;
    basl_parser_type_t parent_type;
    int parent_is_capture;
    int found;
    const char *name_text;
    size_t name_length;
    basl_status_t status;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_type != NULL) {
        *out_type = basl_binding_type_invalid();
    }
    if (out_is_capture != NULL) {
        *out_is_capture = 0;
    }
    if (out_capture_index != NULL) {
        *out_capture_index = 0U;
    }
    if (out_found != NULL) {
        *out_found = 0;
    }
    if (state == NULL || name_token == NULL) {
        return BASL_STATUS_OK;
    }

    if (basl_parser_find_local_symbol(state, name_token, &local_index)) {
        if (out_index != NULL) {
            *out_index = local_index;
        }
        if (out_type != NULL) {
            *out_type = basl_binding_scope_stack_local_at(&state->locals, local_index)->type;
        }
        if (out_found != NULL) {
            *out_found = 1;
        }
        return BASL_STATUS_OK;
    }

    if (state->parent == NULL) {
        return BASL_STATUS_OK;
    }

    status = basl_parser_resolve_local_symbol(
        state->parent,
        name_token,
        &parent_local_index,
        &parent_type,
        &parent_is_capture,
        NULL,
        &found
    );
    if (status != BASL_STATUS_OK || !found) {
        return status;
    }

    name_text = basl_parser_token_text(state, name_token, &name_length);
    decl = basl_binding_function_table_get_mutable(
        (basl_binding_function_table_t *)&state->program->functions,
        state->function_index
    );
    if (decl == NULL) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_INTERNAL,
            "nested function declaration is missing"
        );
        return BASL_STATUS_INTERNAL;
    }
    if (basl_binding_function_find_capture(decl, name_text, name_length, &capture_index)) {
        if (out_index != NULL) {
            *out_index = capture_index;
        }
        if (out_type != NULL) {
            *out_type = decl->captures[capture_index].type;
        }
        if (out_is_capture != NULL) {
            *out_is_capture = 1;
        }
        if (out_capture_index != NULL) {
            *out_capture_index = capture_index;
        }
        if (out_found != NULL) {
            *out_found = 1;
        }
        return BASL_STATUS_OK;
    }

    status = basl_binding_function_grow_captures(
        (basl_program_state_t *)state->program,
        decl,
        decl->capture_count + 1U
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    capture_index = decl->capture_count;
    decl->captures[capture_index].name = name_text;
    decl->captures[capture_index].name_length = name_length;
    decl->captures[capture_index].type = parent_type;
    decl->captures[capture_index].source_local_index = parent_local_index;
    decl->captures[capture_index].source_is_capture = parent_is_capture;
    decl->capture_count += 1U;

    if (out_index != NULL) {
        *out_index = capture_index;
    }
    if (out_type != NULL) {
        *out_type = parent_type;
    }
    if (out_is_capture != NULL) {
        *out_is_capture = 1;
    }
    if (out_capture_index != NULL) {
        *out_capture_index = capture_index;
    }
    if (out_found != NULL) {
        *out_found = 1;
    }
    return BASL_STATUS_OK;
}

static int basl_program_find_top_level_function_name_in_source(
    const basl_program_state_t *program,
    basl_source_id_t source_id,
    const char *name_text,
    size_t name_length,
    size_t *out_index,
    const basl_function_decl_t **out_decl
) {
    size_t i;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_decl != NULL) {
        *out_decl = NULL;
    }
    if (program == NULL || source_id == 0U || name_text == NULL) {
        return 0;
    }

    for (i = 0U; i < program->functions.count; i += 1U) {
        if (program->functions.functions[i].owner_class_index != BASL_BINDING_INVALID_CLASS_INDEX) {
            continue;
        }
        if (
            program->functions.functions[i].source == NULL ||
            program->functions.functions[i].source->id != source_id
        ) {
            continue;
        }
        if (
            basl_program_names_equal(
                program->functions.functions[i].name,
                program->functions.functions[i].name_length,
                name_text,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_decl != NULL) {
                *out_decl = &program->functions.functions[i];
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_parser_declare_local_symbol(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_parser_type_t type,
    int is_const,
    size_t *out_index
) {
    basl_status_t status;
    const char *name;
    size_t name_length;

    name = basl_parser_token_text(state, name_token, &name_length);
    status = basl_binding_scope_stack_declare_local(
        &state->locals,
        name,
        name_length,
        type,
        is_const,
        out_index,
        state->program->error
    );
    if (status == BASL_STATUS_INVALID_ARGUMENT) {
        return basl_parser_report(
            state,
            name_token->span,
            "local variable is already declared in this scope"
        );
    }

    return status;
}

static int basl_parser_token_is_discard_identifier(
    const basl_parser_state_t *state,
    const basl_token_t *token
) {
    size_t length;
    const char *text;

    text = basl_parser_token_text(state, token, &length);
    return text != NULL && length == 1U && text[0] == '_';
}

static basl_status_t basl_parser_parse_binding_target_list(
    basl_parser_state_t *state,
    const char *unsupported_type_message,
    const char *non_void_message,
    const char *name_message,
    basl_binding_target_list_t *targets
) {
    basl_status_t status;
    basl_parser_type_t declared_type;
    const basl_token_t *name_token;
    const basl_token_t *type_token;

    if (state == NULL || targets == NULL) {
        basl_error_set_literal(
            state == NULL ? NULL : state->program->error,
            BASL_STATUS_INVALID_ARGUMENT,
            "binding target list arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    do {
        status = basl_program_parse_type_reference(
            state->program,
            &state->current,
            unsupported_type_message,
            &declared_type
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        type_token = basl_parser_previous(state);
        status = basl_program_require_non_void_type(
            state->program,
            type_token == NULL ? basl_parser_fallback_span(state) : type_token->span,
            declared_type,
            non_void_message
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_expect(
            state,
            BASL_TOKEN_IDENTIFIER,
            name_message,
            &name_token
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_binding_target_list_append(
            (basl_program_state_t *)state->program,
            targets,
            declared_type,
            name_token,
            basl_parser_token_is_discard_identifier(state, name_token)
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } while (basl_parser_match(state, BASL_TOKEN_COMMA));

    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_require_binding_initializer_shape(
    basl_parser_state_t *state,
    basl_source_span_t span,
    const basl_binding_target_list_t *targets,
    const basl_expression_result_t *initializer_result,
    const char *count_message,
    const char *type_message
) {
    basl_status_t status;
    size_t index;

    if (targets == NULL || initializer_result == NULL) {
        basl_error_set_literal(
            state == NULL ? NULL : state->program->error,
            BASL_STATUS_INVALID_ARGUMENT,
            "binding initializer arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (initializer_result->type_count != targets->count) {
        return basl_parser_report(state, span, count_message);
    }

    for (index = 0U; index < targets->count; index += 1U) {
        status = basl_parser_require_type(
            state,
            span,
            basl_expression_result_type_at(initializer_result, index),
            targets->items[index].type,
            type_message
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_bind_targets(
    basl_parser_state_t *state,
    const basl_binding_target_list_t *targets,
    int is_const,
    size_t *out_last_slot
) {
    basl_status_t status;
    size_t index;
    size_t slot_index;

    if (out_last_slot != NULL) {
        *out_last_slot = 0U;
    }
    if (state == NULL || targets == NULL) {
        basl_error_set_literal(
            state == NULL ? NULL : state->program->error,
            BASL_STATUS_INVALID_ARGUMENT,
            "binding target arguments are invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < targets->count; index += 1U) {
        slot_index = 0U;
        if (targets->items[index].is_discard) {
            status = basl_binding_scope_stack_declare_hidden_local(
                &state->locals,
                targets->items[index].type,
                is_const,
                &slot_index,
                state->program->error
            );
        } else {
            status = basl_parser_declare_local_symbol(
                state,
                targets->items[index].name_token,
                targets->items[index].type,
                is_const,
                &slot_index
            );
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (out_last_slot != NULL) {
            *out_last_slot = slot_index;
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
);

static int basl_builtin_error_kind_by_name(
    const char *name,
    size_t length,
    int64_t *out_kind
) {
    struct kind_entry {
        const char *name;
        size_t length;
        int64_t kind;
    };
    static const struct kind_entry kinds[] = {
        {"not_found", 9U, 1},
        {"permission", 10U, 2},
        {"exists", 6U, 3},
        {"eof", 3U, 4},
        {"io", 2U, 5},
        {"parse", 5U, 6},
        {"bounds", 6U, 7},
        {"type", 4U, 8},
        {"arg", 3U, 9},
        {"timeout", 7U, 10},
        {"closed", 6U, 11},
        {"state", 5U, 12}
    };
    size_t index;

    if (out_kind != NULL) {
        *out_kind = 0;
    }
    if (name == NULL) {
        return 0;
    }

    for (index = 0U; index < sizeof(kinds) / sizeof(kinds[0]); index += 1U) {
        if (basl_program_names_equal(name, length, kinds[index].name, kinds[index].length)) {
            if (out_kind != NULL) {
                *out_kind = kinds[index].kind;
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_parser_emit_ok_constant(
    basl_parser_state_t *state,
    basl_source_span_t span
) {
    basl_status_t status;
    basl_value_t value;
    basl_object_t *object;

    basl_value_init_nil(&value);
    object = NULL;
    status = basl_error_object_new_cstr(
        state->program->registry->runtime,
        "",
        0,
        &object,
        state->program->error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    basl_value_init_object(&value, &object);
    status = basl_chunk_write_constant(
        &state->chunk,
        &value,
        span,
        NULL,
        state->program->error
    );
    basl_value_release(&value);
    return status;
}

static basl_status_t basl_parser_parse_builtin_error_constructor(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t message_result;
    basl_expression_result_t kind_result;

    basl_expression_result_clear(&message_result);
    basl_expression_result_clear(&kind_result);

    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after err",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &message_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_type(
        state,
        name_token->span,
        message_result.type,
        basl_binding_type_primitive(BASL_TYPE_STRING),
        "err(...) message must be a string"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_COMMA,
        "expected ',' after error message",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &kind_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_type(
        state,
        name_token->span,
        kind_result.type,
        basl_binding_type_primitive(BASL_TYPE_I32),
        "err(...) kind must be an i32"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after err arguments",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_opcode(state, BASL_OPCODE_NEW_ERROR, name_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    basl_expression_result_set_type(out_result, basl_binding_type_primitive(BASL_TYPE_ERR));
    return BASL_STATUS_OK;
}

static int basl_parser_resolve_builtin_conversion_kind(
    const basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_type_kind_t *out_kind
) {
    const char *name_text;
    size_t name_length;
    basl_type_kind_t kind;

    if (out_kind != NULL) {
        *out_kind = BASL_TYPE_INVALID;
    }
    if (state == NULL || name_token == NULL) {
        return 0;
    }

    name_text = basl_parser_token_text(state, name_token, &name_length);
    kind = basl_type_kind_from_name(name_text, name_length);
    if (
        kind != BASL_TYPE_I32 &&
        kind != BASL_TYPE_F64 &&
        kind != BASL_TYPE_STRING &&
        kind != BASL_TYPE_BOOL
    ) {
        return 0;
    }

    if (out_kind != NULL) {
        *out_kind = kind;
    }
    return 1;
}

static basl_status_t basl_parser_parse_builtin_conversion(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_type_kind_t target_kind,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t argument_result;
    basl_opcode_t opcode;
    int needs_opcode;

    basl_expression_result_clear(&argument_result);
    opcode = BASL_OPCODE_TO_STRING;
    needs_opcode = 0;

    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after conversion name",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &argument_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (basl_parser_match(state, BASL_TOKEN_COMMA)) {
        return basl_parser_report(
            state,
            name_token->span,
            "built-in conversions accept exactly one argument"
        );
    }
    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after conversion argument",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    switch (target_kind) {
        case BASL_TYPE_I32:
            if (basl_parser_type_is_i32(argument_result.type)) {
                needs_opcode = 0;
            } else if (basl_parser_type_is_f64(argument_result.type)) {
                opcode = BASL_OPCODE_TO_I32;
                needs_opcode = 1;
            } else {
                return basl_parser_report(
                    state,
                    name_token->span,
                    "i32(...) requires an i32 or f64 argument"
                );
            }
            break;
        case BASL_TYPE_F64:
            if (basl_parser_type_is_f64(argument_result.type)) {
                needs_opcode = 0;
            } else if (basl_parser_type_is_i32(argument_result.type)) {
                opcode = BASL_OPCODE_TO_F64;
                needs_opcode = 1;
            } else {
                return basl_parser_report(
                    state,
                    name_token->span,
                    "f64(...) requires an i32 or f64 argument"
                );
            }
            break;
        case BASL_TYPE_STRING:
            if (basl_parser_type_is_string(argument_result.type)) {
                needs_opcode = 0;
            } else if (
                basl_parser_type_is_i32(argument_result.type) ||
                basl_parser_type_is_f64(argument_result.type) ||
                basl_parser_type_is_bool(argument_result.type)
            ) {
                opcode = BASL_OPCODE_TO_STRING;
                needs_opcode = 1;
            } else {
                return basl_parser_report(
                    state,
                    name_token->span,
                    "string(...) requires a string, i32, f64, or bool argument"
                );
            }
            break;
        case BASL_TYPE_BOOL:
            if (!basl_parser_type_is_bool(argument_result.type)) {
                return basl_parser_report(
                    state,
                    name_token->span,
                    "bool(...) requires a bool argument"
                );
            }
            needs_opcode = 0;
            break;
        default:
            return basl_parser_report(
                state,
                name_token->span,
                "unsupported built-in conversion"
            );
    }

    if (needs_opcode) {
        status = basl_parser_emit_opcode(state, opcode, name_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    basl_expression_result_set_type(
        out_result,
        basl_binding_type_primitive(target_kind)
    );
    return BASL_STATUS_OK;
}

static int basl_program_find_function_symbol(
    const basl_program_state_t *program,
    const basl_token_t *name_token,
    size_t *out_index,
    const basl_function_decl_t **out_decl
) {
    const char *name_text;
    size_t name_length;

    name_text = basl_program_token_text(program, name_token, &name_length);
    return basl_program_find_top_level_function_name(
        program,
        name_text,
        name_length,
        out_index,
        out_decl
    );
}

static int basl_program_find_top_level_function_name(
    const basl_program_state_t *program,
    const char *name_text,
    size_t name_length,
    size_t *out_index,
    const basl_function_decl_t **out_decl
) {
    size_t i;

    for (i = 0U; i < program->functions.count; i += 1U) {
        if (program->functions.functions[i].owner_class_index != BASL_BINDING_INVALID_CLASS_INDEX) {
            continue;
        }
        if (
            basl_program_names_equal(
                program->functions.functions[i].name,
                program->functions.functions[i].name_length,
                name_text,
                name_length
            )
        ) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_decl != NULL) {
                *out_decl = &program->functions.functions[i];
            }
            return 1;
        }
    }

    return 0;
}

static basl_status_t basl_parser_lookup_function_symbol(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    size_t *out_index,
    const basl_function_decl_t **out_decl
) {
    if (basl_program_find_function_symbol(
            state->program,
            name_token,
            out_index,
            out_decl
        )) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, name_token->span, "unknown function");
}

static int basl_program_find_class_symbol(
    const basl_program_state_t *program,
    const basl_token_t *name_token,
    size_t *out_index,
    const basl_class_decl_t **out_decl
) {
    const char *name_text;
    size_t name_length;

    name_text = basl_program_token_text(program, name_token, &name_length);
    return basl_program_find_class(program, name_text, name_length, out_index, out_decl);
}

static basl_status_t basl_parser_lookup_class_symbol(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    size_t *out_index,
    const basl_class_decl_t **out_decl
) {
    if (basl_program_find_class_symbol(state->program, name_token, out_index, out_decl)) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, name_token->span, "unknown class");
}

static basl_status_t basl_parser_lookup_field(
    basl_parser_state_t *state,
    basl_parser_type_t receiver_type,
    const basl_token_t *field_token,
    size_t *out_index,
    const basl_class_field_t **out_field
) {
    const basl_class_decl_t *class_decl;
    const char *field_name;
    size_t field_length;

    if (!basl_parser_type_is_class(receiver_type)) {
        return basl_parser_report(
            state,
            field_token->span,
            "field access requires a class instance"
        );
    }

    class_decl = &state->program->classes[receiver_type.object_index];
    field_name = basl_parser_token_text(state, field_token, &field_length);
    if (basl_class_decl_find_field(class_decl, field_name, field_length, out_index, out_field)) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, field_token->span, "unknown class field");
}

static int basl_parser_find_field_by_name(
    const basl_parser_state_t *state,
    basl_parser_type_t receiver_type,
    const basl_token_t *field_token,
    size_t *out_index,
    const basl_class_field_t **out_field
) {
    const basl_class_decl_t *class_decl;
    const char *field_name;
    size_t field_length;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_field != NULL) {
        *out_field = NULL;
    }
    if (state == NULL || !basl_parser_type_is_class(receiver_type)) {
        return 0;
    }

    class_decl = &state->program->classes[receiver_type.object_index];
    field_name = basl_parser_token_text(state, field_token, &field_length);
    return basl_class_decl_find_field(class_decl, field_name, field_length, out_index, out_field);
}

static int basl_parser_find_method_by_name(
    const basl_parser_state_t *state,
    basl_parser_type_t receiver_type,
    const basl_token_t *method_token,
    size_t *out_index,
    const basl_class_method_t **out_method
) {
    const basl_class_decl_t *class_decl;
    const char *method_name;
    size_t method_length;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_method != NULL) {
        *out_method = NULL;
    }
    if (state == NULL || !basl_parser_type_is_class(receiver_type)) {
        return 0;
    }

    class_decl = &state->program->classes[receiver_type.object_index];
    method_name = basl_parser_token_text(state, method_token, &method_length);
    return basl_class_decl_find_method(class_decl, method_name, method_length, out_index, out_method);
}

static int basl_parser_find_interface_method_by_name(
    const basl_parser_state_t *state,
    basl_parser_type_t receiver_type,
    const basl_token_t *method_token,
    size_t *out_index,
    const basl_interface_method_t **out_method
) {
    const basl_interface_decl_t *interface_decl;
    const char *method_name;
    size_t method_length;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_method != NULL) {
        *out_method = NULL;
    }
    if (state == NULL || !basl_parser_type_is_interface(receiver_type)) {
        return 0;
    }

    interface_decl = &state->program->interfaces[receiver_type.object_index];
    method_name = basl_parser_token_text(state, method_token, &method_length);
    return basl_interface_decl_find_method(
        interface_decl,
        method_name,
        method_length,
        out_index,
        out_method
    );
}

static void basl_parser_begin_scope(basl_parser_state_t *state) {
    basl_binding_scope_stack_begin_scope(&state->locals);
}

static basl_status_t basl_parser_end_scope(basl_parser_state_t *state) {
    basl_status_t status;
    basl_source_span_t span;
    size_t popped_count;
    size_t index;

    if (basl_binding_scope_stack_depth(&state->locals) == 0U) {
        return BASL_STATUS_OK;
    }

    basl_binding_scope_stack_end_scope(&state->locals, &popped_count);
    for (index = 0U; index < popped_count; index += 1U) {
        span = basl_parser_fallback_span(state);
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
);
static basl_status_t basl_parser_parse_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
);

static basl_status_t basl_parser_parse_call(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_expression_result_t *out_result
) ;
static basl_status_t basl_parser_parse_value_call(
    basl_parser_state_t *state,
    basl_source_span_t call_span,
    basl_parser_type_t callee_type,
    basl_expression_result_t *out_result
);
static basl_status_t basl_parser_parse_call_resolved(
    basl_parser_state_t *state,
    basl_source_span_t call_span,
    size_t function_index,
    const basl_function_decl_t *decl,
    basl_expression_result_t *out_result
);
static basl_status_t basl_parser_parse_constructor(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_expression_result_t *out_result
) ;
static basl_status_t basl_parser_parse_constructor_resolved(
    basl_parser_state_t *state,
    basl_source_span_t call_span,
    size_t class_index,
    const basl_class_decl_t *decl,
    basl_expression_result_t *out_result
);

static basl_status_t basl_parser_parse_call(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    size_t function_index;
    const basl_function_decl_t *decl;

    function_index = 0U;
    decl = NULL;

    status = basl_parser_lookup_function_symbol(state, name_token, &function_index, &decl);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_parser_parse_call_resolved(
        state,
        name_token->span,
        function_index,
        decl,
        out_result
    );
}

static basl_status_t basl_parser_parse_value_call(
    basl_parser_state_t *state,
    basl_source_span_t call_span,
    basl_parser_type_t callee_type,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t arg_result;
    const basl_function_type_decl_t *function_type;
    size_t arg_count;
    int defer_call;

    basl_expression_result_clear(&arg_result);
    function_type = basl_program_function_type_decl(state->program, callee_type);
    if (function_type == NULL) {
        return basl_parser_report(state, call_span, "call target is not callable");
    }
    if (function_type->is_any) {
        return basl_parser_report(
            state,
            call_span,
            "indirect calls require a concrete function signature"
        );
    }

    defer_call = state->defer_mode;
    state->defer_mode = 0;
    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after callable value",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    arg_count = 0U;
    if (!basl_parser_check(state, BASL_TOKEN_RPAREN)) {
        while (1) {
            status = basl_parser_parse_expression(state, &arg_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_scalar_expression(
                state,
                call_span,
                &arg_result,
                "call arguments must be single values"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (arg_count >= function_type->param_count) {
                return basl_parser_report(
                    state,
                    call_span,
                    "call argument count does not match function signature"
                );
            }
            status = basl_parser_require_type(
                state,
                call_span,
                arg_result.type,
                function_type->param_types[arg_count],
                "call argument type does not match parameter type"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            arg_count += 1U;

            if (!basl_parser_match(state, BASL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after argument list",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (arg_count != function_type->param_count || arg_count > UINT32_MAX) {
        return basl_parser_report(
            state,
            call_span,
            "call argument count does not match function signature"
        );
    }

    status = basl_parser_emit_opcode(
        state,
        defer_call ? BASL_OPCODE_DEFER_CALL_VALUE : BASL_OPCODE_CALL_VALUE,
        call_span
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)arg_count, call_span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (defer_call) {
        state->defer_emitted = 1;
        basl_expression_result_set_type(
            out_result,
            basl_binding_type_primitive(BASL_TYPE_VOID)
        );
    } else {
        basl_expression_result_set_return_types(
            out_result,
            function_type->return_type,
            function_type->return_types,
            function_type->return_count
        );
    }
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_call_resolved(
    basl_parser_state_t *state,
    basl_source_span_t call_span,
    size_t function_index,
    const basl_function_decl_t *decl,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t arg_result;
    size_t arg_count;
    int defer_call;

    basl_expression_result_clear(&arg_result);
    defer_call = state->defer_mode;
    state->defer_mode = 0;

    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after function name",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    arg_count = 0U;
    if (!basl_parser_check(state, BASL_TOKEN_RPAREN)) {
        while (1) {
            status = basl_parser_parse_expression(state, &arg_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_scalar_expression(
                state,
                call_span,
                &arg_result,
                "call arguments must be single values"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (arg_count >= decl->param_count) {
                return basl_parser_report(
                    state,
                    call_span,
                    "call argument count does not match function signature"
                );
            }
            status = basl_parser_require_type(
                state,
                call_span,
                arg_result.type,
                decl->params[arg_count].type,
                "call argument type does not match parameter type"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            arg_count += 1U;

            if (!basl_parser_match(state, BASL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after argument list",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (arg_count != decl->param_count) {
        return basl_parser_report(
            state,
            call_span,
            "call argument count does not match function signature"
        );
    }
    if (function_index > UINT32_MAX || arg_count > UINT32_MAX) {
        basl_error_set_literal(
            state->program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "call operand overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    status = basl_parser_emit_opcode(
        state,
        defer_call ? BASL_OPCODE_DEFER_CALL : BASL_OPCODE_CALL,
        call_span
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)function_index, call_span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)arg_count, call_span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (defer_call) {
        state->defer_emitted = 1;
        basl_expression_result_set_type(
            out_result,
            basl_binding_type_primitive(BASL_TYPE_VOID)
        );
    } else {
        basl_expression_result_set_return_types(
            out_result,
            decl->return_type,
            basl_function_return_types(decl),
            decl->return_count
        );
    }
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_constructor(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    size_t class_index;
    const basl_class_decl_t *decl;

    class_index = 0U;
    decl = NULL;

    status = basl_parser_lookup_class_symbol(state, name_token, &class_index, &decl);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_parser_parse_constructor_resolved(
        state,
        name_token->span,
        class_index,
        decl,
        out_result
    );
}

static basl_status_t basl_parser_parse_constructor_resolved(
    basl_parser_state_t *state,
    basl_source_span_t call_span,
    size_t class_index,
    const basl_class_decl_t *decl,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t arg_result;
    const basl_function_decl_t *ctor_decl;
    size_t arg_count;
    size_t expected_arg_count;
    int use_constructor_function;
    int defer_call;

    basl_expression_result_clear(&arg_result);
    ctor_decl = NULL;
    defer_call = state->defer_mode;
    state->defer_mode = 0;
    use_constructor_function = decl->constructor_function_index != (size_t)-1;
    if (use_constructor_function) {
        ctor_decl = basl_binding_function_table_get(
            &state->program->functions,
            decl->constructor_function_index
        );
        if (ctor_decl == NULL) {
            return basl_parser_report(state, call_span, "unknown constructor");
        }
        expected_arg_count = ctor_decl->param_count;
    } else {
        expected_arg_count = decl->field_count;
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after class name",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    arg_count = 0U;
    if (!basl_parser_check(state, BASL_TOKEN_RPAREN)) {
        while (1) {
            status = basl_parser_parse_expression(state, &arg_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_scalar_expression(
                state,
                call_span,
                &arg_result,
                "call arguments must be single values"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (arg_count >= expected_arg_count) {
                return basl_parser_report(
                    state,
                    call_span,
                    "constructor argument count does not match class signature"
                );
            }
            status = basl_parser_require_type(
                state,
                call_span,
                arg_result.type,
                use_constructor_function
                    ? ctor_decl->params[arg_count].type
                    : decl->fields[arg_count].type,
                use_constructor_function
                    ? "constructor argument type does not match parameter type"
                    : "constructor argument type does not match field type"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            arg_count += 1U;

            if (!basl_parser_match(state, BASL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after constructor arguments",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (arg_count != expected_arg_count || arg_count > UINT32_MAX || class_index > UINT32_MAX) {
        return basl_parser_report(
            state,
            call_span,
            "constructor argument count does not match class signature"
        );
    }

    if (use_constructor_function) {
        if (decl->constructor_function_index > UINT32_MAX) {
            return basl_parser_report(state, call_span, "constructor index overflow");
        }
        status = basl_parser_emit_opcode(
            state,
            defer_call ? BASL_OPCODE_DEFER_CALL : BASL_OPCODE_CALL,
            call_span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(
            state,
            (uint32_t)decl->constructor_function_index,
            call_span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(state, (uint32_t)arg_count, call_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        status = basl_parser_emit_opcode(
            state,
            defer_call ? BASL_OPCODE_DEFER_NEW_INSTANCE : BASL_OPCODE_NEW_INSTANCE,
            call_span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(state, (uint32_t)class_index, call_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(state, (uint32_t)arg_count, call_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    if (defer_call) {
        state->defer_emitted = 1;
        basl_expression_result_set_type(
            out_result,
            basl_binding_type_primitive(BASL_TYPE_VOID)
        );
    } else {
        if (use_constructor_function) {
            basl_expression_result_set_return_types(
                out_result,
                ctor_decl->return_type,
                basl_function_return_types(ctor_decl),
                ctor_decl->return_count
            );
        } else {
            basl_expression_result_set_type(out_result, basl_binding_type_class(class_index));
        }
    }
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_qualified_symbol(
    basl_parser_state_t *state,
    const basl_token_t *module_token,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_value_t value;
    const basl_token_t *member_token;
    const basl_token_t *enum_member_token;
    const basl_global_constant_t *constant;
    const basl_function_decl_t *function_decl;
    const basl_class_decl_t *class_decl;
    const basl_enum_member_t *enum_member;
    const char *module_name;
    const char *member_name;
    const char *enum_member_name;
    size_t module_name_length;
    size_t member_name_length;
    size_t enum_member_name_length;
    size_t function_index;
    size_t class_index;
    size_t enum_index;
    basl_source_id_t source_id;

    constant = NULL;
    function_decl = NULL;
    class_decl = NULL;
    enum_member = NULL;
    function_index = 0U;
    class_index = 0U;
    enum_index = 0U;
    source_id = 0U;
    basl_value_init_nil(&value);

    module_name = basl_parser_token_text(state, module_token, &module_name_length);
    if (
        !basl_program_resolve_import_alias(
            state->program,
            module_name,
            module_name_length,
            &source_id
        )
    ) {
        return basl_parser_report(state, module_token->span, "unknown local variable");
    }

    status = basl_parser_expect(state, BASL_TOKEN_DOT, "expected '.' after module name", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_expect(
        state,
        BASL_TOKEN_IDENTIFIER,
        "expected module member name after '.'",
        &member_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    member_name = basl_parser_token_text(state, member_token, &member_name_length);
    if (basl_parser_match(state, BASL_TOKEN_DOT)) {
        enum_member_token = basl_parser_peek(state);
        if (enum_member_token == NULL || enum_member_token->kind != BASL_TOKEN_IDENTIFIER) {
            return basl_parser_report(state, member_token->span, "unknown enum member");
        }
        basl_parser_advance(state);
        enum_member_name = basl_parser_token_text(
            state,
            enum_member_token,
            &enum_member_name_length
        );
        if (
            !basl_program_lookup_enum_member_in_source(
                state->program,
                source_id,
                member_name,
                member_name_length,
                enum_member_name,
                enum_member_name_length,
                &enum_index,
                &enum_member
            )
        ) {
            return basl_parser_report(state, enum_member_token->span, "unknown enum member");
        }
        if (!basl_program_is_enum_public(&state->program->enums[enum_index])) {
            return basl_parser_report(state, member_token->span, "module member is not public");
        }

        basl_value_init_int(&value, enum_member->value);
        status = basl_chunk_write_constant(
            &state->chunk,
            &value,
            enum_member_token->span,
            NULL,
            state->program->error
        );
        basl_value_release(&value);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, basl_binding_type_enum(enum_index));
        return BASL_STATUS_OK;
    }

    if (basl_parser_check(state, BASL_TOKEN_LPAREN)) {
        if (
            basl_program_find_top_level_function_name_in_source(
                state->program,
                source_id,
                member_name,
                member_name_length,
                &function_index,
                &function_decl
            )
        ) {
            if (!basl_program_is_function_public(function_decl)) {
                return basl_parser_report(state, member_token->span, "module member is not public");
            }
            return basl_parser_parse_call_resolved(
                state,
                member_token->span,
                function_index,
                function_decl,
                out_result
            );
        }
        if (
            basl_program_find_class_in_source(
                state->program,
                source_id,
                member_name,
                member_name_length,
                &class_index,
                &class_decl
            )
        ) {
            if (!basl_program_is_class_public(class_decl)) {
                return basl_parser_report(state, member_token->span, "module member is not public");
            }
            return basl_parser_parse_constructor_resolved(
                state,
                member_token->span,
                class_index,
                class_decl,
                out_result
            );
        }
        return basl_parser_report(state, member_token->span, "unknown function");
    }

    if (
        basl_program_find_top_level_function_name_in_source(
            state->program,
            source_id,
            member_name,
            member_name_length,
            &function_index,
            &function_decl
        )
    ) {
        basl_parser_type_t function_type;

        if (!basl_program_is_function_public(function_decl)) {
            return basl_parser_report(state, member_token->span, "module member is not public");
        }
        function_type = basl_binding_type_invalid();
        status = basl_program_intern_function_type_from_decl(
            (basl_program_state_t *)state->program,
            function_decl,
            &function_type
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, function_type);
        status = basl_parser_emit_opcode(
            state,
            BASL_OPCODE_GET_FUNCTION,
            member_token->span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        return basl_parser_emit_u32(state, (uint32_t)function_index, member_token->span);
    }

    if (
        basl_program_find_constant_in_source(
            state->program,
            source_id,
            member_name,
            member_name_length,
            &constant
        )
    ) {
        if (!basl_program_is_constant_public(constant)) {
            return basl_parser_report(state, member_token->span, "module member is not public");
        }
        status = basl_chunk_write_constant(
            &state->chunk,
            &constant->value,
            member_token->span,
            NULL,
            state->program->error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, constant->type);
        return BASL_STATUS_OK;
    }

    {
        const basl_global_variable_t *global_decl;
        size_t global_index;

        global_decl = NULL;
        global_index = 0U;
        if (
            basl_program_find_global_in_source(
                state->program,
                source_id,
                member_name,
                member_name_length,
                &global_index,
                &global_decl
            )
        ) {
            if (!basl_program_is_global_public(global_decl)) {
                return basl_parser_report(
                    state,
                    member_token->span,
                    "module member is not public"
                );
            }
            status = basl_parser_emit_opcode(
                state,
                BASL_OPCODE_GET_GLOBAL,
                member_token->span
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_u32(state, (uint32_t)global_index, member_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, global_decl->type);
            return BASL_STATUS_OK;
        }
    }

    return basl_parser_report(state, member_token->span, "unknown module member");
}

static basl_status_t basl_parser_parse_method_call(
    basl_parser_state_t *state,
    const basl_token_t *method_token,
    const basl_class_method_t *method,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    const basl_function_decl_t *decl;
    basl_expression_result_t arg_result;
    size_t arg_index;
    int defer_call;

    decl = basl_binding_function_table_get(&state->program->functions, method->function_index);
    if (decl == NULL) {
        return basl_parser_report(state, method_token->span, "unknown class method");
    }

    basl_expression_result_clear(&arg_result);
    defer_call = state->defer_mode;
    state->defer_mode = 0;
    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after method name",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    arg_index = 1U;
    if (!basl_parser_check(state, BASL_TOKEN_RPAREN)) {
        while (1) {
            status = basl_parser_parse_expression(state, &arg_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_scalar_expression(
                state,
                method_token->span,
                &arg_result,
                "call arguments must be single values"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (arg_index >= decl->param_count) {
                return basl_parser_report(
                    state,
                    method_token->span,
                    "call argument count does not match function signature"
                );
            }
            status = basl_parser_require_type(
                state,
                method_token->span,
                arg_result.type,
                decl->params[arg_index].type,
                "call argument type does not match parameter type"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            arg_index += 1U;

            if (!basl_parser_match(state, BASL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after argument list",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (arg_index != decl->param_count || method->function_index > UINT32_MAX) {
        return basl_parser_report(
            state,
            method_token->span,
            "call argument count does not match function signature"
        );
    }

    status = basl_parser_emit_opcode(
        state,
        defer_call ? BASL_OPCODE_DEFER_CALL : BASL_OPCODE_CALL,
        method_token->span
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)method->function_index, method_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)decl->param_count, method_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (defer_call) {
        state->defer_emitted = 1;
        basl_expression_result_set_type(
            out_result,
            basl_binding_type_primitive(BASL_TYPE_VOID)
        );
    } else {
        basl_expression_result_set_return_types(
            out_result,
            decl->return_type,
            basl_function_return_types(decl),
            decl->return_count
        );
    }
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_interface_method_call(
    basl_parser_state_t *state,
    basl_parser_type_t receiver_type,
    size_t method_index,
    const basl_token_t *method_token,
    const basl_interface_method_t *method,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t arg_result;
    size_t arg_count;
    int defer_call;

    basl_expression_result_clear(&arg_result);
    defer_call = state->defer_mode;
    state->defer_mode = 0;
    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after method name",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    arg_count = 0U;
    if (!basl_parser_check(state, BASL_TOKEN_RPAREN)) {
        while (1) {
            status = basl_parser_parse_expression(state, &arg_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_scalar_expression(
                state,
                method_token->span,
                &arg_result,
                "call arguments must be single values"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (arg_count >= method->param_count) {
                return basl_parser_report(
                    state,
                    method_token->span,
                    "call argument count does not match function signature"
                );
            }
            status = basl_parser_require_type(
                state,
                method_token->span,
                arg_result.type,
                method->param_types[arg_count],
                "call argument type does not match parameter type"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            arg_count += 1U;

            if (!basl_parser_match(state, BASL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after argument list",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (
        arg_count != method->param_count ||
        receiver_type.object_index > UINT32_MAX ||
        method_index > UINT32_MAX ||
        arg_count > UINT32_MAX
    ) {
        return basl_parser_report(
            state,
            method_token->span,
            "call argument count does not match function signature"
        );
    }

    status = basl_parser_emit_opcode(
        state,
        defer_call ? BASL_OPCODE_DEFER_CALL_INTERFACE : BASL_OPCODE_CALL_INTERFACE,
        method_token->span
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)receiver_type.object_index, method_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)method_index, method_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)arg_count, method_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (defer_call) {
        state->defer_emitted = 1;
        basl_expression_result_set_type(
            out_result,
            basl_binding_type_primitive(BASL_TYPE_VOID)
        );
    } else {
        basl_expression_result_set_return_types(
            out_result,
            method->return_type,
            basl_interface_method_return_types(method),
            method->return_count
        );
    }
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_postfix_suffixes(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *field_token;
    const basl_class_field_t *field;
    const basl_class_method_t *class_method;
    const basl_interface_method_t *interface_method;
    basl_expression_result_t index_result;
    basl_parser_type_t indexed_type;
    size_t field_index;
    size_t method_index;

    basl_expression_result_clear(&index_result);
    while (1) {
        if (basl_parser_check(state, BASL_TOKEN_LPAREN)) {
            status = basl_parser_require_scalar_expression(
                state,
                basl_parser_previous(state) == NULL
                    ? basl_parser_fallback_span(state)
                    : basl_parser_previous(state)->span,
                out_result,
                "multi-value expressions do not support calls"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (!basl_parser_type_is_function(out_result->type)) {
                break;
            }
            status = basl_parser_parse_value_call(
                state,
                basl_parser_previous(state) == NULL
                    ? basl_parser_fallback_span(state)
                    : basl_parser_previous(state)->span,
                out_result->type,
                out_result
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }

        if (basl_parser_match(state, BASL_TOKEN_DOT)) {
            status = basl_parser_require_scalar_expression(
                state,
                basl_parser_previous(state)->span,
                out_result,
                "multi-value expressions do not support member access"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_expect(
                state,
                BASL_TOKEN_IDENTIFIER,
                "expected field name after '.'",
                &field_token
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }

            class_method = NULL;
            interface_method = NULL;
            field = NULL;
            field_index = 0U;
            method_index = 0U;
            if (basl_parser_find_field_by_name(state, out_result->type, field_token, &field_index, &field)) {
                if (field_index > UINT32_MAX) {
                    basl_error_set_literal(
                        state->program->error,
                        BASL_STATUS_OUT_OF_MEMORY,
                        "field operand overflow"
                    );
                    return BASL_STATUS_OUT_OF_MEMORY;
                }

                status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_FIELD, field_token->span);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_parser_emit_u32(state, (uint32_t)field_index, field_token->span);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                basl_expression_result_set_type(out_result, field->type);
                continue;
            }
            if (
                basl_parser_check(state, BASL_TOKEN_LPAREN) &&
                basl_parser_find_method_by_name(
                    state,
                    out_result->type,
                    field_token,
                    &method_index,
                    &class_method
                )
            ) {
                status = basl_parser_parse_method_call(state, field_token, class_method, out_result);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                continue;
            }
            if (
                basl_parser_check(state, BASL_TOKEN_LPAREN) &&
                basl_parser_find_interface_method_by_name(
                    state,
                    out_result->type,
                    field_token,
                    &method_index,
                    &interface_method
                )
            ) {
                status = basl_parser_parse_interface_method_call(
                    state,
                    out_result->type,
                    method_index,
                    field_token,
                    interface_method,
                    out_result
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                continue;
            }

            if (
                basl_parser_check(state, BASL_TOKEN_LPAREN) &&
                basl_parser_type_is_err(out_result->type)
            ) {
                const char *error_method_name;
                size_t error_method_length;

                error_method_name = basl_parser_token_text(state, field_token, &error_method_length);
                status = basl_parser_expect(
                    state,
                    BASL_TOKEN_LPAREN,
                    "expected '(' after error method name",
                    NULL
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_parser_expect(
                    state,
                    BASL_TOKEN_RPAREN,
                    "error methods do not accept arguments",
                    NULL
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                if (basl_program_names_equal(error_method_name, error_method_length, "kind", 4U)) {
                    status = basl_parser_emit_opcode(
                        state,
                        BASL_OPCODE_GET_ERROR_KIND,
                        field_token->span
                    );
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    basl_expression_result_set_type(
                        out_result,
                        basl_binding_type_primitive(BASL_TYPE_I32)
                    );
                    continue;
                }
                if (basl_program_names_equal(error_method_name, error_method_length, "message", 7U)) {
                    status = basl_parser_emit_opcode(
                        state,
                        BASL_OPCODE_GET_ERROR_MESSAGE,
                        field_token->span
                    );
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    basl_expression_result_set_type(
                        out_result,
                        basl_binding_type_primitive(BASL_TYPE_STRING)
                    );
                    continue;
                }
                return basl_parser_report(state, field_token->span, "unknown error method");
            }
            if (
                basl_parser_check(state, BASL_TOKEN_LPAREN) &&
                basl_parser_type_is_class(out_result->type)
            ) {
                return basl_parser_report(state, field_token->span, "unknown class method");
            }
            if (
                basl_parser_check(state, BASL_TOKEN_LPAREN) &&
                basl_parser_type_is_interface(out_result->type)
            ) {
                return basl_parser_report(state, field_token->span, "unknown interface method");
            }

            field = NULL;
            field_index = 0U;
            status = basl_parser_lookup_field(
                state,
                out_result->type,
                field_token,
                &field_index,
                &field
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (field_index > UINT32_MAX) {
                basl_error_set_literal(
                    state->program->error,
                    BASL_STATUS_OUT_OF_MEMORY,
                    "field operand overflow"
                );
                return BASL_STATUS_OUT_OF_MEMORY;
            }

            status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_FIELD, field_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_u32(state, (uint32_t)field_index, field_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, field->type);
            continue;
        }

        if (basl_parser_match(state, BASL_TOKEN_LBRACKET)) {
            basl_expression_result_clear(&index_result);
            status = basl_parser_require_scalar_expression(
                state,
                basl_parser_previous(state)->span,
                out_result,
                "multi-value expressions do not support indexing"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_parse_expression(state, &index_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_scalar_expression(
                state,
                basl_parser_previous(state) == NULL
                    ? basl_parser_fallback_span(state)
                    : basl_parser_previous(state)->span,
                &index_result,
                "index expressions must evaluate to a single value"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_expect(
                state,
                BASL_TOKEN_RBRACKET,
                "expected ']' after index expression",
                NULL
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }

            indexed_type = basl_binding_type_invalid();
            if (basl_parser_type_is_array(out_result->type)) {
                status = basl_parser_require_type(
                    state,
                    basl_parser_previous(state)->span,
                    index_result.type,
                    basl_binding_type_primitive(BASL_TYPE_I32),
                    "array index must be i32"
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                indexed_type = basl_program_array_type_element(state->program, out_result->type);
            } else if (basl_parser_type_is_map(out_result->type)) {
                status = basl_parser_require_type(
                    state,
                    basl_parser_previous(state)->span,
                    index_result.type,
                    basl_program_map_type_key(state->program, out_result->type),
                    "map index must match map key type"
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                indexed_type = basl_program_map_type_value(state->program, out_result->type);
            } else {
                return basl_parser_report(
                    state,
                    basl_parser_previous(state)->span,
                    "index access requires an array or map"
                );
            }

            status = basl_parser_emit_opcode(
                state,
                BASL_OPCODE_GET_INDEX,
                basl_parser_previous(state)->span
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, indexed_type);
            continue;
        }

        break;
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_nested_function_value(
    basl_parser_state_t *state,
    int named_declaration,
    const basl_token_t *decl_name_token,
    basl_parser_type_t *out_type,
    size_t *out_function_index
) {
    basl_status_t status;
    const basl_token_t *fn_token;
    const basl_token_t *name_token;
    const basl_token_t *token;
    const basl_token_t *type_token;
    const basl_token_t *param_name_token;
    basl_function_decl_t *decl;
    basl_parser_type_t param_type;
    basl_parser_type_t function_type;
    size_t function_index;
    size_t body_depth;
    size_t capture_index;

    if (out_type != NULL) {
        *out_type = basl_binding_type_invalid();
    }
    if (out_function_index != NULL) {
        *out_function_index = 0U;
    }

    status = basl_parser_expect(state, BASL_TOKEN_FN, "expected 'fn'", &fn_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    name_token = decl_name_token;
    if (named_declaration) {
        status = basl_parser_expect(
            state,
            BASL_TOKEN_IDENTIFIER,
            "expected local function name",
            &name_token
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    status = basl_program_grow_functions(
        (basl_program_state_t *)state->program,
        state->program->functions.count + 1U
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    function_index = state->program->functions.count;
    decl = &((basl_program_state_t *)state->program)->functions.functions[function_index];
    basl_binding_function_init(decl);
    decl->name = named_declaration ? basl_parser_token_text(state, name_token, &decl->name_length) : "<anon>";
    decl->name_length = named_declaration ? decl->name_length : 6U;
    decl->name_span = named_declaration ? name_token->span : fn_token->span;
    decl->source = state->program->source;
    decl->tokens = state->program->tokens;
    decl->is_local = 1;

    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after function name",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (!basl_parser_check(state, BASL_TOKEN_RPAREN)) {
        while (1) {
            type_token = basl_parser_peek(state);
            param_type = basl_binding_type_invalid();
            status = basl_program_parse_type_reference(
                state->program,
                &state->current,
                "unsupported local function parameter type",
                &param_type
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_program_require_non_void_type(
                state->program,
                type_token == NULL ? fn_token->span : type_token->span,
                param_type,
                "function parameters cannot use type void"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }

            status = basl_parser_expect(
                state,
                BASL_TOKEN_IDENTIFIER,
                "expected parameter name",
                &param_name_token
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_program_add_param(
                (basl_program_state_t *)state->program,
                decl,
                param_type,
                param_name_token
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }

            if (!basl_parser_match(state, BASL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after parameter list",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_expect(
        state,
        BASL_TOKEN_ARROW,
        "expected '->' after function signature",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_program_parse_function_return_types(
        (basl_program_state_t *)state->program,
        &state->current,
        "unsupported local function return type",
        decl
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_expect(
        state,
        BASL_TOKEN_LBRACE,
        "expected '{' before function body",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    decl->body_start = state->current;
    body_depth = 1U;
    while (body_depth > 0U) {
        token = basl_program_token_at(state->program, state->current);
        if (token == NULL || token->kind == BASL_TOKEN_EOF) {
            return basl_parser_report(
                state,
                basl_program_eof_span(state->program),
                "expected '}' after function body"
            );
        }
        if (token->kind == BASL_TOKEN_LBRACE) {
            body_depth += 1U;
        } else if (token->kind == BASL_TOKEN_RBRACE) {
            body_depth -= 1U;
            if (body_depth == 0U) {
                decl->body_end = state->current;
                state->current += 1U;
                break;
            }
        }
        state->current += 1U;
    }

    ((basl_program_state_t *)state->program)->functions.count += 1U;
    status = basl_compile_function_with_parent(
        (basl_program_state_t *)state->program,
        function_index,
        state
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_program_intern_function_type_from_decl(
        (basl_program_state_t *)state->program,
        decl,
        &function_type
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    for (capture_index = 0U; capture_index < decl->capture_count; ++capture_index) {
        status = basl_parser_emit_opcode(
            state,
            decl->captures[capture_index].source_is_capture
                ? BASL_OPCODE_GET_CAPTURE
                : BASL_OPCODE_GET_LOCAL,
            decl->name_span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(
            state,
            (uint32_t)decl->captures[capture_index].source_local_index,
            decl->name_span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    status = basl_parser_emit_opcode(state, BASL_OPCODE_NEW_CLOSURE, decl->name_span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)function_index, decl->name_span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)decl->capture_count, decl->name_span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (out_type != NULL) {
        *out_type = function_type;
    }
    if (out_function_index != NULL) {
        *out_function_index = function_index;
    }
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_local_function_declaration(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    basl_parser_type_t function_type;
    const basl_token_t *name_token;

    function_type = basl_binding_type_invalid();
    name_token = basl_program_token_at(state->program, state->current + 1U);
    status = basl_parser_parse_nested_function_value(
        state,
        1,
        name_token,
        &function_type,
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_declare_local_symbol(state, name_token, function_type, 1, NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_primary_base(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *token;
    const basl_global_constant_t *constant;
    basl_value_t value;
    size_t local_index;
    size_t capture_index;
    size_t global_index;
    basl_parser_type_t local_type;
    const basl_global_variable_t *global_decl;
    const basl_enum_member_t *enum_member;
    const char *name_text;
    const char *member_text;
    size_t name_length;
    size_t member_length;
    basl_source_id_t source_id;
    size_t enum_index;
    int local_found;
    int local_is_capture;

    constant = NULL;
    local_index = 0U;
    capture_index = 0U;
    global_index = 0U;
    local_type = basl_binding_type_invalid();
    global_decl = NULL;
    enum_member = NULL;
    name_text = NULL;
    name_length = 0U;
    member_text = NULL;
    member_length = 0U;
    source_id = 0U;
    enum_index = 0U;
    local_found = 0;
    local_is_capture = 0;
    token = basl_parser_peek(state);
    if (token == NULL) {
        return basl_parser_report(
            state,
            basl_parser_fallback_span(state),
            "expected expression"
        );
    }

    switch (token->kind) {
        case BASL_TOKEN_INT_LITERAL:
            basl_parser_advance(state);
            status = basl_parser_parse_int_literal(state, token, &value);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_chunk_write_constant(
                &state->chunk,
                &value,
                token->span,
                NULL,
                state->program->error
            );
            basl_value_release(&value);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, basl_binding_type_primitive(BASL_TYPE_I32));
            return BASL_STATUS_OK;
        case BASL_TOKEN_TRUE:
            basl_parser_advance(state);
            basl_expression_result_set_type(out_result, basl_binding_type_primitive(BASL_TYPE_BOOL));
            return basl_parser_emit_opcode(state, BASL_OPCODE_TRUE, token->span);
        case BASL_TOKEN_FALSE:
            basl_parser_advance(state);
            basl_expression_result_set_type(out_result, basl_binding_type_primitive(BASL_TYPE_BOOL));
            return basl_parser_emit_opcode(state, BASL_OPCODE_FALSE, token->span);
        case BASL_TOKEN_NIL:
            basl_parser_advance(state);
            basl_expression_result_set_type(out_result, basl_binding_type_primitive(BASL_TYPE_NIL));
            return basl_parser_emit_opcode(state, BASL_OPCODE_NIL, token->span);
        case BASL_TOKEN_FN:
            status = basl_parser_parse_nested_function_value(
                state,
                0,
                NULL,
                &local_type,
                NULL
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, local_type);
            return BASL_STATUS_OK;
        case BASL_TOKEN_IDENTIFIER:
            basl_parser_advance(state);
            name_text = basl_parser_token_text(state, token, &name_length);
            status = basl_parser_resolve_local_symbol(
                state,
                token,
                &local_index,
                &local_type,
                &local_is_capture,
                &capture_index,
                &local_found
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (
                basl_parser_check(state, BASL_TOKEN_DOT) &&
                !local_found &&
                basl_program_resolve_import_alias(
                    state->program,
                    name_text,
                    name_length,
                    &source_id
                )
            ) {
                return basl_parser_parse_qualified_symbol(state, token, out_result);
            }
            (void)basl_program_find_global(
                state->program,
                name_text,
                name_length,
                &global_index,
                &global_decl
            );
            if (basl_parser_check(state, BASL_TOKEN_LPAREN)) {
                basl_type_kind_t conversion_kind;

                if (
                    basl_parser_resolve_builtin_conversion_kind(
                        state,
                        token,
                        &conversion_kind
                    )
                ) {
                    return basl_parser_parse_builtin_conversion(
                        state,
                        token,
                        conversion_kind,
                        out_result
                    );
                }
                if (
                    !local_found &&
                    basl_program_names_equal(name_text, name_length, "err", 3U)
                ) {
                    return basl_parser_parse_builtin_error_constructor(state, token, out_result);
                }
                if (basl_binding_type_is_valid(local_type) && basl_parser_type_is_function(local_type)) {
                    basl_expression_result_set_type(out_result, local_type);
                    status = basl_parser_emit_opcode(
                        state,
                        local_is_capture ? BASL_OPCODE_GET_CAPTURE : BASL_OPCODE_GET_LOCAL,
                        token->span
                    );
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    status = basl_parser_emit_u32(
                        state,
                        (uint32_t)(local_is_capture ? capture_index : local_index),
                        token->span
                    );
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    return basl_parser_parse_value_call(state, token->span, local_type, out_result);
                }
                if (global_decl != NULL && basl_parser_type_is_function(global_decl->type)) {
                    basl_expression_result_set_type(out_result, global_decl->type);
                    status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_GLOBAL, token->span);
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    status = basl_parser_emit_u32(state, (uint32_t)global_index, token->span);
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    return basl_parser_parse_value_call(
                        state,
                        token->span,
                        global_decl->type,
                        out_result
                    );
                }
                if (basl_program_find_function_symbol(state->program, token, NULL, NULL)) {
                    return basl_parser_parse_call(state, token, out_result);
                }
                if (basl_program_find_class_symbol(state->program, token, NULL, NULL)) {
                    return basl_parser_parse_constructor(state, token, out_result);
                }
                return basl_parser_report(state, token->span, "unknown function");
            }
            if (
                basl_parser_check(state, BASL_TOKEN_DOT) &&
                !local_found
            ) {
                basl_parser_advance(state);
                {
                    const basl_token_t *member_token = basl_parser_peek(state);
                    int64_t error_kind;

                    if (member_token == NULL || member_token->kind != BASL_TOKEN_IDENTIFIER) {
                        return basl_parser_report(state, token->span, "unknown enum member");
                    }
                    basl_parser_advance(state);
                    member_text = basl_parser_token_text(state, member_token, &member_length);
                    error_kind = 0;
                    if (
                        basl_program_names_equal(name_text, name_length, "err", 3U) &&
                        basl_builtin_error_kind_by_name(member_text, member_length, &error_kind)
                    ) {
                        basl_value_init_int(&value, error_kind);
                        status = basl_chunk_write_constant(
                            &state->chunk,
                            &value,
                            member_token->span,
                            NULL,
                            state->program->error
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
                    if (
                        !basl_program_lookup_enum_member(
                            state->program,
                            name_text,
                            name_length,
                            member_text,
                            member_length,
                            &enum_index,
                            &enum_member
                        )
                    ) {
                        return basl_parser_report(state, member_token->span, "unknown enum member");
                    }

                    basl_value_init_int(&value, enum_member->value);
                    status = basl_chunk_write_constant(
                        &state->chunk,
                        &value,
                        member_token->span,
                        NULL,
                        state->program->error
                    );
                    basl_value_release(&value);
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    basl_expression_result_set_type(
                        out_result,
                        basl_binding_type_enum(enum_index)
                    );
                    return BASL_STATUS_OK;
                }
            }

            if (local_found && basl_binding_type_is_valid(local_type)) {
                basl_expression_result_set_type(out_result, local_type);
                status = basl_parser_emit_opcode(
                    state,
                    local_is_capture ? BASL_OPCODE_GET_CAPTURE : BASL_OPCODE_GET_LOCAL,
                    token->span
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                return basl_parser_emit_u32(
                    state,
                    (uint32_t)(local_is_capture ? capture_index : local_index),
                    token->span
                );
            }

            if (global_decl != NULL) {
                basl_expression_result_set_type(out_result, global_decl->type);
                status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_GLOBAL, token->span);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                return basl_parser_emit_u32(state, (uint32_t)global_index, token->span);
            }

            if (basl_program_find_function_symbol(state->program, token, &global_index, NULL)) {
                basl_parser_type_t function_type;

                function_type = basl_binding_type_invalid();
                status = basl_program_intern_function_type_from_decl(
                    (basl_program_state_t *)state->program,
                    basl_binding_function_table_get(&state->program->functions, global_index),
                    &function_type
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                basl_expression_result_set_type(out_result, function_type);
                status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_FUNCTION, token->span);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                return basl_parser_emit_u32(state, (uint32_t)global_index, token->span);
            }

            constant = NULL;
            if (!basl_program_find_constant(state->program, name_text, name_length, &constant)) {
                if (basl_program_names_equal(name_text, name_length, "ok", 2U)) {
                    status = basl_parser_emit_ok_constant(state, token->span);
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    basl_expression_result_set_type(
                        out_result,
                        basl_binding_type_primitive(BASL_TYPE_ERR)
                    );
                    return BASL_STATUS_OK;
                }
                return basl_parser_report(state, token->span, "unknown local variable");
            }
            status = basl_chunk_write_constant(
                &state->chunk,
                &constant->value,
                token->span,
                NULL,
                state->program->error
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, constant->type);
            return BASL_STATUS_OK;
        case BASL_TOKEN_LBRACKET:
            basl_parser_advance(state);
            {
                basl_expression_result_t item_result;
                basl_parser_type_t element_type;
                basl_parser_type_t array_type;
                size_t item_count;

                basl_expression_result_clear(&item_result);
                element_type = basl_binding_type_invalid();
                array_type = basl_binding_type_invalid();
                item_count = 0U;

                if (basl_parser_match(state, BASL_TOKEN_RBRACKET)) {
                    return basl_parser_report(
                        state,
                        token->span,
                        "array literals require at least one element"
                    );
                }

                while (1) {
                    basl_expression_result_clear(&item_result);
                    status = basl_parser_parse_expression(state, &item_result);
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    status = basl_parser_require_scalar_expression(
                        state,
                        basl_parser_previous(state) == NULL
                            ? token->span
                            : basl_parser_previous(state)->span,
                        &item_result,
                        "array literal elements must be single values"
                    );
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    if (item_count == 0U) {
                        element_type = item_result.type;
                    } else {
                        status = basl_parser_require_type(
                            state,
                            basl_parser_previous(state) == NULL
                                ? token->span
                                : basl_parser_previous(state)->span,
                            item_result.type,
                            element_type,
                            "array literal elements must have matching types"
                        );
                        if (status != BASL_STATUS_OK) {
                            return status;
                        }
                    }
                    item_count += 1U;
                    if (!basl_parser_match(state, BASL_TOKEN_COMMA)) {
                        break;
                    }
                }

                status = basl_parser_expect(
                    state,
                    BASL_TOKEN_RBRACKET,
                    "expected ']' after array literal",
                    NULL
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_program_intern_array_type(
                    (basl_program_state_t *)state->program,
                    element_type,
                    &array_type
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                if (array_type.object_index > UINT32_MAX || item_count > UINT32_MAX) {
                    basl_error_set_literal(
                        state->program->error,
                        BASL_STATUS_OUT_OF_MEMORY,
                        "array literal operand overflow"
                    );
                    return BASL_STATUS_OUT_OF_MEMORY;
                }
                status = basl_parser_emit_opcode(state, BASL_OPCODE_NEW_ARRAY, token->span);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_parser_emit_u32(
                    state,
                    (uint32_t)array_type.object_index,
                    token->span
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_parser_emit_u32(state, (uint32_t)item_count, token->span);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                basl_expression_result_set_type(out_result, array_type);
                return BASL_STATUS_OK;
            }
        case BASL_TOKEN_LBRACE:
            basl_parser_advance(state);
            {
                basl_expression_result_t key_result;
                basl_expression_result_t value_result;
                basl_parser_type_t key_type;
                basl_parser_type_t map_type;
                basl_parser_type_t value_type;
                size_t pair_count;

                basl_expression_result_clear(&key_result);
                basl_expression_result_clear(&value_result);
                key_type = basl_binding_type_invalid();
                map_type = basl_binding_type_invalid();
                value_type = basl_binding_type_invalid();
                pair_count = 0U;

                if (basl_parser_match(state, BASL_TOKEN_RBRACE)) {
                    return basl_parser_report(
                        state,
                        token->span,
                        "map literals require at least one entry"
                    );
                }

                while (1) {
                    basl_expression_result_clear(&key_result);
                    basl_expression_result_clear(&value_result);
                    status = basl_parser_parse_expression(state, &key_result);
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    status = basl_parser_require_scalar_expression(
                        state,
                        basl_parser_previous(state) == NULL
                            ? token->span
                            : basl_parser_previous(state)->span,
                        &key_result,
                        "map literal keys must be single values"
                    );
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    if (pair_count == 0U) {
                        if (!basl_parser_type_supports_map_key(key_result.type)) {
                            return basl_parser_report(
                                state,
                                basl_parser_previous(state) == NULL
                                    ? token->span
                                    : basl_parser_previous(state)->span,
                                "map literal keys must use type i32, bool, string, or enum"
                            );
                        }
                        key_type = key_result.type;
                    } else {
                        status = basl_parser_require_type(
                            state,
                            basl_parser_previous(state) == NULL
                                ? token->span
                                : basl_parser_previous(state)->span,
                            key_result.type,
                            key_type,
                            "map literal keys must have matching types"
                        );
                        if (status != BASL_STATUS_OK) {
                            return status;
                        }
                    }
                    status = basl_parser_expect(
                        state,
                        BASL_TOKEN_COLON,
                        "expected ':' after map key",
                        NULL
                    );
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    status = basl_parser_parse_expression(state, &value_result);
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    status = basl_parser_require_scalar_expression(
                        state,
                        basl_parser_previous(state) == NULL
                            ? token->span
                            : basl_parser_previous(state)->span,
                        &value_result,
                        "map literal values must be single values"
                    );
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    if (pair_count == 0U) {
                        value_type = value_result.type;
                    } else {
                        status = basl_parser_require_type(
                            state,
                            basl_parser_previous(state) == NULL
                                ? token->span
                                : basl_parser_previous(state)->span,
                            value_result.type,
                            value_type,
                            "map literal values must have matching types"
                        );
                        if (status != BASL_STATUS_OK) {
                            return status;
                        }
                    }
                    pair_count += 1U;
                    if (!basl_parser_match(state, BASL_TOKEN_COMMA)) {
                        break;
                    }
                }

                status = basl_parser_expect(
                    state,
                    BASL_TOKEN_RBRACE,
                    "expected '}' after map literal",
                    NULL
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_program_intern_map_type(
                    (basl_program_state_t *)state->program,
                    key_type,
                    value_type,
                    &map_type
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                if (map_type.object_index > UINT32_MAX || pair_count > UINT32_MAX) {
                    basl_error_set_literal(
                        state->program->error,
                        BASL_STATUS_OUT_OF_MEMORY,
                        "map literal operand overflow"
                    );
                    return BASL_STATUS_OUT_OF_MEMORY;
                }
                status = basl_parser_emit_opcode(state, BASL_OPCODE_NEW_MAP, token->span);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_parser_emit_u32(
                    state,
                    (uint32_t)map_type.object_index,
                    token->span
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_parser_emit_u32(state, (uint32_t)pair_count, token->span);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                basl_expression_result_set_type(out_result, map_type);
                return BASL_STATUS_OK;
            }
        case BASL_TOKEN_LPAREN:
            basl_parser_advance(state);
            status = basl_parser_parse_expression(state, out_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            return basl_parser_expect(
                state,
                BASL_TOKEN_RPAREN,
                "expected ')' after expression",
                NULL
            );
        case BASL_TOKEN_STRING_LITERAL:
        case BASL_TOKEN_RAW_STRING_LITERAL:
        case BASL_TOKEN_CHAR_LITERAL:
            basl_parser_advance(state);
            {
                basl_value_t string_value;

                basl_value_init_nil(&string_value);
                status = basl_program_parse_string_literal_value(
                    state->program,
                    token,
                    &string_value
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_chunk_write_constant(
                    &state->chunk,
                    &string_value,
                    token->span,
                    NULL,
                    state->program->error
                );
                basl_value_release(&string_value);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_STRING)
            );
            return BASL_STATUS_OK;
        case BASL_TOKEN_FLOAT_LITERAL:
            basl_parser_advance(state);
            {
                basl_value_t float_value;

                basl_value_init_nil(&float_value);
                status = basl_parser_parse_float_literal(state, token, &float_value);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_chunk_write_constant(
                    &state->chunk,
                    &float_value,
                    token->span,
                    NULL,
                    state->program->error
                );
                basl_value_release(&float_value);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_F64)
            );
            return BASL_STATUS_OK;
        case BASL_TOKEN_FSTRING_LITERAL:
            basl_parser_advance(state);
            return basl_parser_parse_fstring_literal(state, token, out_result);
        default:
            return basl_parser_report(state, token->span, "expected expression");
    }
}

static basl_status_t basl_parser_parse_primary(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;

    status = basl_parser_parse_primary_base(state, out_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_parser_parse_postfix_suffixes(state, out_result);
}

static basl_status_t basl_parser_parse_unary(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *operator_token;
    basl_expression_result_t operand_result;

    basl_expression_result_clear(&operand_result);

    operator_token = basl_parser_peek(state);
    if (operator_token != NULL &&
        (operator_token->kind == BASL_TOKEN_MINUS ||
         operator_token->kind == BASL_TOKEN_BANG ||
         operator_token->kind == BASL_TOKEN_TILDE)) {
        basl_parser_advance(state);
        status = basl_parser_parse_unary(state, &operand_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_token->span,
            &operand_result,
            "multi-value expressions cannot be used with unary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        if (operator_token->kind == BASL_TOKEN_MINUS) {
            status = basl_parser_require_unary_operator(
                state,
                operator_token->span,
                BASL_UNARY_OPERATOR_NEGATE,
                operand_result.type,
                "unary '-' requires an i32 or f64 operand"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, operand_result.type);
            return basl_parser_emit_opcode(state, BASL_OPCODE_NEGATE, operator_token->span);
        }

        if (operator_token->kind == BASL_TOKEN_BANG) {
            status = basl_parser_require_unary_operator(
                state,
                operator_token->span,
                BASL_UNARY_OPERATOR_LOGICAL_NOT,
                operand_result.type,
                "logical '!' requires a bool operand"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(
                out_result,
                basl_binding_type_primitive(BASL_TYPE_BOOL)
            );
            return basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_token->span);
        }

        status = basl_parser_require_unary_operator(
            state,
            operator_token->span,
            BASL_UNARY_OPERATOR_BITWISE_NOT,
            operand_result.type,
            "bitwise '~' requires an i32 operand"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, operand_result.type);
        return basl_parser_emit_opcode(state, BASL_OPCODE_BITWISE_NOT, operator_token->span);
    }

    return basl_parser_parse_primary(state, out_result);
}

static basl_status_t basl_parser_parse_factor(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_unary(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (basl_parser_check(state, BASL_TOKEN_STAR)) {
            operator_kind = BASL_TOKEN_STAR;
        } else if (basl_parser_check(state, BASL_TOKEN_SLASH)) {
            operator_kind = BASL_TOKEN_SLASH;
        } else if (basl_parser_check(state, BASL_TOKEN_PERCENT)) {
            operator_kind = BASL_TOKEN_PERCENT;
        } else {
            break;
        }

        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_unary(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (operator_kind == BASL_TOKEN_PERCENT) {
            status = basl_parser_require_i32_operands(
                state,
                operator_span,
                left_result.type,
                right_result.type,
                BASL_BINARY_OPERATOR_MODULO,
                "modulo requires i32 operands"
            );
        } else if (
            !basl_parser_type_supports_binary_operator(
                operator_kind == BASL_TOKEN_STAR
                    ? BASL_BINARY_OPERATOR_MULTIPLY
                    : BASL_BINARY_OPERATOR_DIVIDE,
                left_result.type,
                right_result.type
            )
        ) {
            status = basl_parser_report(
                state,
                operator_span,
                "arithmetic operators require matching i32 or f64 operands"
            );
        } else {
            status = BASL_STATUS_OK;
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }

        if (operator_kind == BASL_TOKEN_STAR) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_MULTIPLY, operator_span);
        } else if (operator_kind == BASL_TOKEN_SLASH) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_DIVIDE, operator_span);
        } else {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_MODULO, operator_span);
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = operator_kind == BASL_TOKEN_PERCENT
            ? basl_binding_type_primitive(BASL_TYPE_I32)
            : left_result.type;
    }

    basl_expression_result_copy(out_result, &left_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_term(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_factor(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (basl_parser_check(state, BASL_TOKEN_PLUS)) {
            operator_kind = BASL_TOKEN_PLUS;
        } else if (basl_parser_check(state, BASL_TOKEN_MINUS)) {
            operator_kind = BASL_TOKEN_MINUS;
        } else {
            break;
        }

        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_factor(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (
            !basl_parser_type_supports_binary_operator(
                operator_kind == BASL_TOKEN_PLUS
                    ? BASL_BINARY_OPERATOR_ADD
                    : BASL_BINARY_OPERATOR_SUBTRACT,
                left_result.type,
                right_result.type
            )
        ) {
            return basl_parser_report(
                state,
                operator_span,
                operator_kind == BASL_TOKEN_PLUS
                    ? "'+' requires matching i32, f64, or string operands"
                    : "arithmetic operators require matching i32 or f64 operands"
            );
        }

        status = basl_parser_emit_opcode(
            state,
            operator_kind == BASL_TOKEN_PLUS ? BASL_OPCODE_ADD : BASL_OPCODE_SUBTRACT,
            operator_span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (
            operator_kind == BASL_TOKEN_PLUS &&
            basl_parser_type_equal(
                left_result.type,
                basl_binding_type_primitive(BASL_TYPE_STRING)
            )
        ) {
            left_result.type = basl_binding_type_primitive(BASL_TYPE_STRING);
        }
    }

    basl_expression_result_copy(out_result, &left_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_shift(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_term(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (basl_parser_check(state, BASL_TOKEN_SHIFT_LEFT)) {
            operator_kind = BASL_TOKEN_SHIFT_LEFT;
        } else if (basl_parser_check(state, BASL_TOKEN_SHIFT_RIGHT)) {
            operator_kind = BASL_TOKEN_SHIFT_RIGHT;
        } else {
            break;
        }

        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_term(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            operator_kind == BASL_TOKEN_SHIFT_LEFT
                ? BASL_BINARY_OPERATOR_SHIFT_LEFT
                : BASL_BINARY_OPERATOR_SHIFT_RIGHT,
            "shift operators require i32 operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_opcode(
            state,
            operator_kind == BASL_TOKEN_SHIFT_LEFT
                ? BASL_OPCODE_SHIFT_LEFT
                : BASL_OPCODE_SHIFT_RIGHT,
            operator_span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = basl_binding_type_primitive(BASL_TYPE_I32);
    }

    basl_expression_result_copy(out_result, &left_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_comparison(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_shift(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (basl_parser_check(state, BASL_TOKEN_GREATER)) {
            operator_kind = BASL_TOKEN_GREATER;
        } else if (basl_parser_check(state, BASL_TOKEN_GREATER_EQUAL)) {
            operator_kind = BASL_TOKEN_GREATER_EQUAL;
        } else if (basl_parser_check(state, BASL_TOKEN_LESS)) {
            operator_kind = BASL_TOKEN_LESS;
        } else if (basl_parser_check(state, BASL_TOKEN_LESS_EQUAL)) {
            operator_kind = BASL_TOKEN_LESS_EQUAL;
        } else {
            break;
        }

        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_shift(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (
            !basl_parser_type_supports_binary_operator(
                operator_kind == BASL_TOKEN_GREATER
                    ? BASL_BINARY_OPERATOR_GREATER
                    : (operator_kind == BASL_TOKEN_GREATER_EQUAL
                           ? BASL_BINARY_OPERATOR_GREATER_EQUAL
                           : (operator_kind == BASL_TOKEN_LESS
                                  ? BASL_BINARY_OPERATOR_LESS
                                  : BASL_BINARY_OPERATOR_LESS_EQUAL)),
                left_result.type,
                right_result.type
            )
        ) {
            status = basl_parser_report(
                state,
                operator_span,
                "comparison operators require matching i32 or f64 operands"
            );
        } else {
            status = BASL_STATUS_OK;
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }

        switch (operator_kind) {
            case BASL_TOKEN_GREATER:
                status = basl_parser_emit_opcode(state, BASL_OPCODE_GREATER, operator_span);
                break;
            case BASL_TOKEN_LESS:
                status = basl_parser_emit_opcode(state, BASL_OPCODE_LESS, operator_span);
                break;
            case BASL_TOKEN_GREATER_EQUAL:
                status = basl_parser_emit_opcode(state, BASL_OPCODE_LESS, operator_span);
                if (status == BASL_STATUS_OK) {
                    status = basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_span);
                }
                break;
            case BASL_TOKEN_LESS_EQUAL:
                status = basl_parser_emit_opcode(state, BASL_OPCODE_GREATER, operator_span);
                if (status == BASL_STATUS_OK) {
                    status = basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_span);
                }
                break;
            default:
                status = BASL_STATUS_INTERNAL;
                break;
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = basl_binding_type_primitive(BASL_TYPE_BOOL);
    }

    basl_expression_result_copy(out_result, &left_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_equality(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_token_kind_t operator_kind;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_comparison(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (basl_parser_check(state, BASL_TOKEN_EQUAL_EQUAL)) {
            operator_kind = BASL_TOKEN_EQUAL_EQUAL;
        } else if (basl_parser_check(state, BASL_TOKEN_BANG_EQUAL)) {
            operator_kind = BASL_TOKEN_BANG_EQUAL;
        } else {
            break;
        }

        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_comparison(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with equality operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with equality operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_same_type(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            "equality operators require operands of the same type"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_opcode(state, BASL_OPCODE_EQUAL, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (operator_kind == BASL_TOKEN_BANG_EQUAL) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
        }
        left_result.type = basl_binding_type_primitive(BASL_TYPE_BOOL);
    }

    basl_expression_result_copy(out_result, &left_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_bitwise_and(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_equality(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (basl_parser_check(state, BASL_TOKEN_AMPERSAND)) {
        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_equality(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            BASL_BINARY_OPERATOR_BITWISE_AND,
            "bitwise operators require i32 operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_opcode(state, BASL_OPCODE_BITWISE_AND, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = basl_binding_type_primitive(BASL_TYPE_I32);
    }

    basl_expression_result_copy(out_result, &left_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_bitwise_xor(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_bitwise_and(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (basl_parser_check(state, BASL_TOKEN_CARET)) {
        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_bitwise_and(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            BASL_BINARY_OPERATOR_BITWISE_XOR,
            "bitwise operators require i32 operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_opcode(state, BASL_OPCODE_BITWISE_XOR, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = basl_binding_type_primitive(BASL_TYPE_I32);
    }

    basl_expression_result_copy(out_result, &left_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_bitwise_or(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_source_span_t operator_span;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_bitwise_xor(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (basl_parser_check(state, BASL_TOKEN_PIPE)) {
        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_parse_bitwise_xor(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            BASL_BINARY_OPERATOR_BITWISE_OR,
            "bitwise operators require i32 operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_opcode(state, BASL_OPCODE_BITWISE_OR, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = basl_binding_type_primitive(BASL_TYPE_I32);
    }

    basl_expression_result_copy(out_result, &left_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_logical_and(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_source_span_t operator_span;
    size_t false_jump_offset;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_bitwise_or(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (basl_parser_check(state, BASL_TOKEN_AMPERSAND_AMPERSAND)) {
        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "logical '&&' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_bool_type(
            state,
            operator_span,
            left_result.type,
            "logical '&&' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_jump(
            state,
            BASL_OPCODE_JUMP_IF_FALSE,
            operator_span,
            &false_jump_offset
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_parse_bitwise_or(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "logical '&&' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_bool_type(
            state,
            operator_span,
            right_result.type,
            "logical '&&' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_patch_jump(state, false_jump_offset);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = basl_binding_type_primitive(BASL_TYPE_BOOL);
    }

    basl_expression_result_copy(out_result, &left_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_logical_or(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t left_result;
    basl_expression_result_t right_result;
    basl_source_span_t operator_span;
    size_t false_jump_offset;
    size_t end_jump_offset;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    status = basl_parser_parse_logical_and(state, &left_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (basl_parser_check(state, BASL_TOKEN_PIPE_PIPE)) {
        operator_span = basl_parser_advance(state)->span;
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "logical '||' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_bool_type(
            state,
            operator_span,
            left_result.type,
            "logical '||' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_jump(
            state,
            BASL_OPCODE_JUMP_IF_FALSE,
            operator_span,
            &false_jump_offset
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_jump(
            state,
            BASL_OPCODE_JUMP,
            operator_span,
            &end_jump_offset
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_patch_jump(state, false_jump_offset);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_parse_logical_and(state, &right_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "logical '||' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_bool_type(
            state,
            operator_span,
            right_result.type,
            "logical '||' requires bool operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_patch_jump(state, end_jump_offset);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = basl_binding_type_primitive(BASL_TYPE_BOOL);
    }

    basl_expression_result_copy(out_result, &left_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_ternary(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t condition_result;
    basl_expression_result_t then_result;
    basl_expression_result_t else_result;
    const basl_token_t *question_token;
    const basl_token_t *colon_token;
    size_t false_jump_offset;
    size_t end_jump_offset;

    basl_expression_result_clear(&condition_result);
    basl_expression_result_clear(&then_result);
    basl_expression_result_clear(&else_result);

    status = basl_parser_parse_logical_or(state, &condition_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (!basl_parser_check(state, BASL_TOKEN_QUESTION)) {
        basl_expression_result_copy(out_result, &condition_result);
        return BASL_STATUS_OK;
    }

    question_token = basl_parser_advance(state);
    status = basl_parser_require_scalar_expression(
        state,
        question_token->span,
        &condition_result,
        "ternary condition must be a single bool value"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_bool_type(
        state,
        question_token->span,
        condition_result.type,
        "ternary condition must be bool"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP_IF_FALSE,
        question_token->span,
        &false_jump_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, question_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &then_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_scalar_expression(
        state,
        question_token->span,
        &then_result,
        "ternary branches must be single values"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP,
        question_token->span,
        &end_jump_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_patch_jump(state, false_jump_offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, question_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_COLON,
        "expected ':' in ternary expression",
        &colon_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_ternary(state, &else_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_scalar_expression(
        state,
        colon_token->span,
        &else_result,
        "ternary branches must be single values"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_same_type(
        state,
        colon_token->span,
        then_result.type,
        else_result.type,
        "ternary branches must have the same type"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_patch_jump(state, end_jump_offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_expression_result_copy(out_result, &then_result);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    return basl_parser_parse_ternary(state, out_result);
}

static basl_status_t basl_parser_parse_block_contents(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
);

static basl_status_t basl_parser_parse_block_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    basl_statement_result_t block_result;

    basl_statement_result_clear(&block_result);

    status = basl_parser_expect(state, BASL_TOKEN_LBRACE, "expected '{'", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_parser_begin_scope(state);
    status = basl_parser_parse_block_contents(state, &block_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_RBRACE, "expected '}' after block", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_end_scope(state);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_statement_result_set_guaranteed_return(
        out_result,
        block_result.guaranteed_return
    );
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_return_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *return_token;
    const basl_token_t *next_token;
    basl_expression_result_t return_result;
    size_t return_index;

    basl_expression_result_clear(&return_result);

    status = basl_parser_expect(
        state,
        BASL_TOKEN_RETURN,
        "expected return statement",
        &return_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    next_token = basl_parser_peek(state);
    if (
        state->expected_return_count == 1U &&
        basl_parser_type_is_void(state->expected_return_type)
    ) {
        if (next_token != NULL && next_token->kind != BASL_TOKEN_SEMICOLON) {
            return basl_parser_report(
                state,
                return_token->span,
                "void functions cannot return a value"
            );
        }
        status = basl_parser_expect(
            state,
            BASL_TOKEN_SEMICOLON,
            "expected ';' after return",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_RETURN, return_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(state, 0U, return_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        basl_statement_result_set_guaranteed_return(out_result, 1);
        return BASL_STATUS_OK;
    }
    if (next_token != NULL && next_token->kind == BASL_TOKEN_SEMICOLON) {
        return basl_parser_report(
            state,
            return_token->span,
            state->function_index == state->program->functions.main_index
                ? "main entrypoint must return an i32 expression"
                : "return statement requires a value"
        );
    }

    if (state->expected_return_count > 1U) {
        status = basl_parser_expect(
            state,
            BASL_TOKEN_LPAREN,
            "expected '(' after return for multi-value function",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        for (return_index = 0U; return_index < state->expected_return_count; return_index += 1U) {
            status = basl_parser_parse_expression(state, &return_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_scalar_expression(
                state,
                return_token->span,
                &return_result,
                "return values must be single expressions"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_type(
                state,
                return_token->span,
                return_result.type,
                state->expected_return_types[return_index],
                "return expression type does not match function return type"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (return_index + 1U < state->expected_return_count) {
                status = basl_parser_expect(
                    state,
                    BASL_TOKEN_COMMA,
                    "expected ',' between return values",
                    NULL
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
            }
        }

        status = basl_parser_expect(
            state,
            BASL_TOKEN_RPAREN,
            "expected ')' after return values",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        status = basl_parser_parse_expression(state, &return_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            return_token->span,
            &return_result,
            "return statement requires the function's declared number of values"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_type(
            state,
            return_token->span,
            return_result.type,
            state->expected_return_type,
            state->function_index == state->program->functions.main_index
                ? "main entrypoint must return an i32 expression"
                : "return expression type does not match function return type"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
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

    status = basl_parser_emit_opcode(state, BASL_OPCODE_RETURN, return_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)state->expected_return_count, return_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_statement_result_set_guaranteed_return(out_result, 1);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_defer_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *defer_token;
    basl_expression_result_t expression_result;

    basl_expression_result_clear(&expression_result);
    status = basl_parser_expect(
        state,
        BASL_TOKEN_DEFER,
        "expected 'defer'",
        &defer_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    state->defer_mode = 1;
    state->defer_emitted = 0;
    status = basl_parser_parse_primary(state, &expression_result);
    state->defer_mode = 0;
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (!state->defer_emitted) {
        return basl_parser_report(
            state,
            defer_token->span,
            "defer requires a call expression"
        );
    }
    state->defer_emitted = 0;

    status = basl_parser_expect(
        state,
        BASL_TOKEN_SEMICOLON,
        "expected ';' after defer call",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_guard_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *guard_token;
    basl_binding_target_list_t targets;
    basl_expression_result_t initializer_result;
    size_t error_slot;
    size_t body_jump_offset;
    size_t end_jump_offset;

    guard_token = NULL;
    error_slot = 0U;
    body_jump_offset = 0U;
    end_jump_offset = 0U;
    basl_binding_target_list_init(&targets);
    basl_expression_result_clear(&initializer_result);

    status = basl_parser_expect(state, BASL_TOKEN_GUARD, "expected 'guard'", &guard_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_binding_target_list(
        state,
        "unsupported guard binding type",
        "guard bindings cannot use type void",
        "expected guard binding name",
        &targets
    );
    if (status != BASL_STATUS_OK) {
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }

    if (
        targets.count == 0U ||
        !basl_binding_type_equal(
            targets.items[targets.count - 1U].type,
            basl_binding_type_primitive(BASL_TYPE_ERR)
        )
    ) {
        status = basl_parser_report(
            state,
            guard_token->span,
            "guard must end with an err binding"
        );
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }
    if (targets.items[targets.count - 1U].is_discard) {
        status = basl_parser_report(
            state,
            targets.items[targets.count - 1U].name_token->span,
            "guard error binding must be named"
        );
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_ASSIGN,
        "expected '=' after guard bindings",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }

    status = basl_parser_parse_expression(state, &initializer_result);
    if (status != BASL_STATUS_OK) {
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }
    status = basl_parser_require_binding_initializer_shape(
        state,
        guard_token->span,
        &targets,
        &initializer_result,
        "guard binding count does not match expression result count",
        "guard binding type does not match expression result type"
    );
    if (status != BASL_STATUS_OK) {
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }

    status = basl_parser_bind_targets(state, &targets, 0, &error_slot);
    basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, guard_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)error_slot, guard_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_ok_constant(state, guard_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_EQUAL, guard_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP_IF_FALSE,
        guard_token->span,
        &body_jump_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, guard_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP,
        guard_token->span,
        &end_jump_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_patch_jump(state, body_jump_offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, guard_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_parse_block_statement(state, out_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_patch_jump(state, end_jump_offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_if_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *if_token;
    basl_expression_result_t condition_result;
    size_t false_jump_offset;
    size_t end_jump_offset;
    basl_statement_result_t then_result;
    basl_statement_result_t else_result;
    int has_else_branch;

    basl_expression_result_clear(&condition_result);
    basl_statement_result_clear(&then_result);
    basl_statement_result_clear(&else_result);
    has_else_branch = 0;

    status = basl_parser_expect(state, BASL_TOKEN_IF, "expected 'if'", &if_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_LPAREN, "expected '(' after 'if'", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &condition_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_scalar_expression(
        state,
        if_token->span,
        &condition_result,
        "if condition must be a single bool value"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_bool_type(
        state,
        if_token->span,
        condition_result.type,
        "if condition must be bool"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_RPAREN, "expected ')' after if condition", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP_IF_FALSE,
        if_token->span,
        &false_jump_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, if_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_statement(state, &then_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (basl_parser_match(state, BASL_TOKEN_ELSE)) {
        has_else_branch = 1;
    }

    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP,
        if_token->span,
        &end_jump_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_patch_jump(state, false_jump_offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, if_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (has_else_branch) {
        status = basl_parser_parse_statement(state, &else_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    status = basl_parser_patch_jump(state, end_jump_offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    basl_statement_result_set_guaranteed_return(
        out_result,
        has_else_branch &&
            then_result.guaranteed_return &&
            else_result.guaranteed_return
    );
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_while_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *while_token;
    basl_expression_result_t condition_result;
    size_t loop_start;
    size_t exit_jump_offset;
    basl_loop_context_t *loop;
    size_t i;

    while_token = NULL;
    basl_expression_result_clear(&condition_result);

    status = basl_parser_expect(state, BASL_TOKEN_WHILE, "expected 'while'", &while_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop_start = basl_chunk_code_size(&state->chunk);
    status = basl_parser_expect(state, BASL_TOKEN_LPAREN, "expected '(' after 'while'", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_expression(state, &condition_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_scalar_expression(
        state,
        while_token->span,
        &condition_result,
        "while condition must be a single bool value"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_bool_type(
        state,
        while_token->span,
        condition_result.type,
        "while condition must be bool"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_RPAREN, "expected ')' after while condition", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP_IF_FALSE,
        while_token->span,
        &exit_jump_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, while_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_push_loop(state, loop_start);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_parse_statement(state, NULL);
    if (status != BASL_STATUS_OK) {
        goto cleanup_loop;
    }

    status = basl_parser_emit_loop(state, loop_start, while_token->span);
    if (status != BASL_STATUS_OK) {
        goto cleanup_loop;
    }
    status = basl_parser_patch_jump(state, exit_jump_offset);
    if (status != BASL_STATUS_OK) {
        goto cleanup_loop;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, while_token->span);
    if (status != BASL_STATUS_OK) {
        goto cleanup_loop;
    }

    loop = basl_parser_current_loop(state);
    if (loop != NULL) {
        for (i = 0U; i < loop->break_count; ++i) {
            status = basl_parser_patch_jump(
                state,
                loop->break_jumps[i].operand_offset
            );
            if (status != BASL_STATUS_OK) {
                goto cleanup_loop;
            }
        }
    }

cleanup_loop:
    basl_parser_pop_loop(state);
    if (status == BASL_STATUS_OK) {
        basl_statement_result_set_guaranteed_return(out_result, 0);
    }
    return status;
}

static basl_status_t basl_parser_parse_c_for_statement(
    basl_parser_state_t *state,
    const basl_token_t *for_token,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    basl_expression_result_t condition_result;
    size_t condition_start;
    size_t loop_start;
    size_t exit_jump_offset;
    size_t body_jump_offset;
    size_t increment_start;
    basl_loop_context_t *loop;
    size_t i;
    int has_condition;
    int has_increment;
    int loop_pushed;

    basl_expression_result_clear(&condition_result);
    body_jump_offset = 0U;
    increment_start = 0U;
    has_condition = 0;
    has_increment = 0;
    loop_pushed = 0;

    status = basl_parser_expect(state, BASL_TOKEN_LPAREN, "expected '(' after 'for'", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_parser_begin_scope(state);
    if (basl_parser_match(state, BASL_TOKEN_SEMICOLON)) {
    } else if (basl_parser_is_variable_declaration_start(state)) {
        status = basl_parser_parse_variable_declaration(state, NULL);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else if (basl_parser_is_assignment_start(state)) {
        status = basl_parser_parse_assignment_statement_internal(state, NULL, 1);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        status = basl_parser_parse_expression_statement_internal(state, NULL, 1);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    condition_start = basl_chunk_code_size(&state->chunk);
    if (!basl_parser_check(state, BASL_TOKEN_SEMICOLON)) {
        has_condition = 1;
        status = basl_parser_parse_expression(state, &condition_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            for_token->span,
            &condition_result,
            "for condition must be a single bool value"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_bool_type(
            state,
            for_token->span,
            condition_result.type,
            "for condition must be bool"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }
    status = basl_parser_expect(state, BASL_TOKEN_SEMICOLON, "expected ';' after for condition", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    exit_jump_offset = 0U;
    if (has_condition) {
        status = basl_parser_emit_jump(
            state,
            BASL_OPCODE_JUMP_IF_FALSE,
            for_token->span,
            &exit_jump_offset
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, for_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    loop_start = condition_start;
    if (!basl_parser_check(state, BASL_TOKEN_RPAREN)) {
        has_increment = 1;
        status = basl_parser_emit_jump(
            state,
            BASL_OPCODE_JUMP,
            for_token->span,
            &body_jump_offset
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        increment_start = basl_chunk_code_size(&state->chunk);
        if (basl_parser_is_assignment_start(state)) {
            status = basl_parser_parse_assignment_statement_internal(state, NULL, 0);
        } else {
            status = basl_parser_parse_expression_statement_internal(state, NULL, 0);
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_loop(state, condition_start, for_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        loop_start = increment_start;
    }

    status = basl_parser_expect(state, BASL_TOKEN_RPAREN, "expected ')' after for clauses", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (has_increment) {
        status = basl_parser_patch_jump(state, body_jump_offset);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    status = basl_parser_push_loop(state, loop_start);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    loop_pushed = 1;

    status = basl_parser_parse_statement(state, NULL);
    if (status != BASL_STATUS_OK) {
        goto cleanup_loop;
    }

    status = basl_parser_emit_loop(state, loop_start, for_token->span);
    if (status != BASL_STATUS_OK) {
        goto cleanup_loop;
    }
    if (has_condition) {
        status = basl_parser_patch_jump(state, exit_jump_offset);
        if (status != BASL_STATUS_OK) {
            goto cleanup_loop;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup_loop;
        }
    }

    loop = basl_parser_current_loop(state);
    if (loop != NULL) {
        for (i = 0U; i < loop->break_count; ++i) {
            status = basl_parser_patch_jump(
                state,
                loop->break_jumps[i].operand_offset
            );
            if (status != BASL_STATUS_OK) {
                goto cleanup_loop;
            }
        }
    }

cleanup_loop:
    if (loop_pushed) {
        basl_parser_pop_loop(state);
    }
    if (status == BASL_STATUS_OK) {
        status = basl_parser_end_scope(state);
        if (status == BASL_STATUS_OK) {
            basl_statement_result_set_guaranteed_return(out_result, 0);
        }
    }
    return status;
}

static basl_status_t basl_parser_bind_inferred_target(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    basl_parser_type_t type
) {
    if (basl_parser_token_is_discard_identifier(state, name_token)) {
        return basl_binding_scope_stack_declare_hidden_local(
            &state->locals,
            type,
            0,
            NULL,
            state->program->error
        );
    }

    return basl_parser_declare_local_symbol(state, name_token, type, 0, NULL);
}

static basl_status_t basl_parser_parse_for_in_statement(
    basl_parser_state_t *state,
    const basl_token_t *for_token,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *first_name;
    const basl_token_t *second_name;
    basl_expression_result_t iterable_result;
    basl_parser_type_t iterable_type;
    basl_parser_type_t element_type;
    basl_parser_type_t key_type;
    basl_parser_type_t value_type;
    size_t collection_slot;
    size_t index_slot;
    size_t condition_start;
    size_t exit_jump_offset;
    size_t body_jump_offset;
    size_t increment_start;
    basl_loop_context_t *loop;
    size_t i;
    int loop_pushed;
    int iteration_scope_begun;

    first_name = NULL;
    second_name = NULL;
    collection_slot = 0U;
    index_slot = 0U;
    exit_jump_offset = 0U;
    body_jump_offset = 0U;
    increment_start = 0U;
    loop_pushed = 0;
    iteration_scope_begun = 0;
    basl_expression_result_clear(&iterable_result);
    iterable_type = basl_binding_type_invalid();
    element_type = basl_binding_type_invalid();
    key_type = basl_binding_type_invalid();
    value_type = basl_binding_type_invalid();

    status = basl_parser_expect(
        state,
        BASL_TOKEN_IDENTIFIER,
        "expected loop binding name after 'for'",
        &first_name
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (basl_parser_match(state, BASL_TOKEN_COMMA)) {
        status = basl_parser_expect(
            state,
            BASL_TOKEN_IDENTIFIER,
            "expected second loop binding name after ','",
            &second_name
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }
    status = basl_parser_expect(state, BASL_TOKEN_IN, "expected 'in' after loop bindings", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_parse_expression(state, &iterable_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_scalar_expression(
        state,
        for_token->span,
        &iterable_result,
        "for-in iterable must be a single array or map value"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    iterable_type = iterable_result.type;
    if (basl_parser_type_is_array(iterable_type)) {
        if (second_name != NULL) {
            return basl_parser_report(
                state,
                second_name->span,
                "for-in over arrays requires a single loop binding"
            );
        }
        element_type = basl_program_array_type_element(state->program, iterable_type);
    } else if (basl_parser_type_is_map(iterable_type)) {
        if (second_name == NULL) {
            return basl_parser_report(
                state,
                first_name->span,
                "for-in over maps requires key and value bindings"
            );
        }
        key_type = basl_program_map_type_key(state->program, iterable_type);
        value_type = basl_program_map_type_value(state->program, iterable_type);
    } else {
        return basl_parser_report(
            state,
            for_token->span,
            "for-in requires an array or map iterable"
        );
    }

    basl_parser_begin_scope(state);

    status = basl_binding_scope_stack_declare_hidden_local(
        &state->locals,
        iterable_type,
        0,
        &collection_slot,
        state->program->error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_i32_constant(state, 0, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_binding_scope_stack_declare_hidden_local(
        &state->locals,
        basl_binding_type_primitive(BASL_TYPE_I32),
        0,
        &index_slot,
        state->program->error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    condition_start = basl_chunk_code_size(&state->chunk);
    status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)collection_slot, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_COLLECTION_SIZE, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_LESS, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP_IF_FALSE,
        for_token->span,
        &exit_jump_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_jump(state, BASL_OPCODE_JUMP, for_token->span, &body_jump_offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    increment_start = basl_chunk_code_size(&state->chunk);
    status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_i32_constant(state, 1, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_ADD, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_SET_LOCAL, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_emit_loop(state, condition_start, for_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_patch_jump(state, body_jump_offset);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_push_loop(state, increment_start);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    loop_pushed = 1;

    basl_parser_begin_scope(state);
    iteration_scope_begun = 1;
    if (basl_parser_type_is_array(iterable_type)) {
        status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_u32(state, (uint32_t)collection_slot, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_INDEX, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_bind_inferred_target(state, first_name, element_type);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
    } else {
        status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_u32(state, (uint32_t)collection_slot, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_MAP_KEY_AT, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_bind_inferred_target(state, first_name, key_type);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }

        status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_u32(state, (uint32_t)collection_slot, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_MAP_VALUE_AT, for_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_bind_inferred_target(state, second_name, value_type);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
    }

    status = basl_parser_parse_statement(state, NULL);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }

    status = basl_parser_end_scope(state);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    iteration_scope_begun = 0;

    status = basl_parser_emit_loop(state, increment_start, for_token->span);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_patch_jump(state, exit_jump_offset);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, for_token->span);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }

    loop = basl_parser_current_loop(state);
    if (loop != NULL) {
        for (i = 0U; i < loop->break_count; ++i) {
            status = basl_parser_patch_jump(state, loop->break_jumps[i].operand_offset);
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
        }
    }

cleanup:
    if (iteration_scope_begun) {
        (void)basl_parser_end_scope(state);
    }
    if (loop_pushed) {
        basl_parser_pop_loop(state);
    }
    if (status == BASL_STATUS_OK) {
        status = basl_parser_end_scope(state);
        if (status == BASL_STATUS_OK) {
            basl_statement_result_set_guaranteed_return(out_result, 0);
        }
    }
    return status;
}

static basl_status_t basl_parser_parse_for_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *for_token;

    for_token = NULL;
    status = basl_parser_expect(state, BASL_TOKEN_FOR, "expected 'for'", &for_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (basl_parser_check(state, BASL_TOKEN_LPAREN)) {
        return basl_parser_parse_c_for_statement(state, for_token, out_result);
    }

    return basl_parser_parse_for_in_statement(state, for_token, out_result);
}

static basl_status_t basl_parser_parse_switch_case_contents(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    basl_statement_result_t declaration_result;
    basl_statement_result_t block_result;
    const basl_token_t *token;

    basl_statement_result_clear(&declaration_result);
    basl_statement_result_clear(&block_result);

    while (!basl_parser_is_at_end(state)) {
        token = basl_parser_peek(state);
        if (
            token == NULL ||
            token->kind == BASL_TOKEN_RBRACE ||
            token->kind == BASL_TOKEN_CASE ||
            token->kind == BASL_TOKEN_DEFAULT
        ) {
            break;
        }

        status = basl_parser_parse_declaration(state, &declaration_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (declaration_result.guaranteed_return) {
            block_result.guaranteed_return = 1;
        }
    }

    basl_statement_result_set_guaranteed_return(
        out_result,
        block_result.guaranteed_return
    );
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_switch_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *switch_token;
    const basl_token_t *token;
    basl_expression_result_t switch_result;
    basl_expression_result_t case_result;
    basl_statement_result_t case_body_result;
    size_t *end_jumps;
    size_t end_jump_count;
    size_t end_jump_capacity;
    size_t *body_jumps;
    size_t body_jump_count;
    size_t body_jump_capacity;
    size_t jump_offset;
    size_t false_jump_offset;
    int has_default;
    int all_branches_return;

    basl_expression_result_clear(&switch_result);
    basl_expression_result_clear(&case_result);
    basl_statement_result_clear(&case_body_result);
    end_jumps = NULL;
    end_jump_count = 0U;
    end_jump_capacity = 0U;
    body_jumps = NULL;
    body_jump_count = 0U;
    body_jump_capacity = 0U;
    has_default = 0;
    all_branches_return = 1;

    status = basl_parser_expect(
        state,
        BASL_TOKEN_SWITCH,
        "expected 'switch'",
        &switch_token
    );
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_expect(
        state,
        BASL_TOKEN_LPAREN,
        "expected '(' after 'switch'",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_parse_expression(state, &switch_result);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    if (
        !basl_parser_type_equal(
            switch_result.type,
            basl_binding_type_primitive(BASL_TYPE_I32)
        ) &&
        !basl_parser_type_equal(
            switch_result.type,
            basl_binding_type_primitive(BASL_TYPE_BOOL)
        ) &&
        !basl_parser_type_is_enum(switch_result.type)
    ) {
        status = basl_parser_report(
            state,
            switch_token->span,
            "switch expression must be i32, bool, or enum"
        );
        goto cleanup;
    }
    status = basl_parser_expect(
        state,
        BASL_TOKEN_RPAREN,
        "expected ')' after switch expression",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_expect(
        state,
        BASL_TOKEN_LBRACE,
        "expected '{' after switch expression",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }

    while (!basl_parser_is_at_end(state)) {
        token = basl_parser_peek(state);
        if (token == NULL) {
            status = basl_parser_report(
                state,
                basl_parser_fallback_span(state),
                "expected '}' after switch body"
            );
            goto cleanup;
        }
        if (token->kind == BASL_TOKEN_RBRACE) {
            basl_parser_advance(state);
            break;
        }

        if (token->kind == BASL_TOKEN_DEFAULT) {
            if (has_default) {
                status = basl_parser_report(
                    state,
                    token->span,
                    "switch already has a default case"
                );
                goto cleanup;
            }
            has_default = 1;
            basl_parser_advance(state);
            status = basl_parser_expect(
                state,
                BASL_TOKEN_COLON,
                "expected ':' after default",
                NULL
            );
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, token->span);
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            basl_statement_result_clear(&case_body_result);
            status = basl_parser_parse_switch_case_contents(state, &case_body_result);
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            all_branches_return =
                all_branches_return && case_body_result.guaranteed_return;
            status = basl_parser_grow_jump_offsets(
                state,
                &end_jumps,
                &end_jump_capacity,
                end_jump_count + 1U,
                "switch jump table allocation overflow"
            );
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            status = basl_parser_emit_jump(
                state,
                BASL_OPCODE_JUMP,
                token->span,
                &end_jumps[end_jump_count]
            );
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            end_jump_count += 1U;
            continue;
        }

        if (token->kind != BASL_TOKEN_CASE) {
            status = basl_parser_report(
                state,
                token->span,
                "expected 'case', 'default', or '}' in switch body"
            );
            goto cleanup;
        }
        basl_parser_advance(state);

        body_jump_count = 0U;
        while (1) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_DUP, token->span);
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            basl_expression_result_clear(&case_result);
            status = basl_parser_parse_expression(state, &case_result);
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            status = basl_parser_require_same_type(
                state,
                token->span,
                case_result.type,
                switch_result.type,
                "switch case value type does not match switch expression"
            );
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_EQUAL, token->span);
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            status = basl_parser_emit_jump(
                state,
                BASL_OPCODE_JUMP_IF_FALSE,
                token->span,
                &false_jump_offset
            );
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, token->span);
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            status = basl_parser_grow_jump_offsets(
                state,
                &body_jumps,
                &body_jump_capacity,
                body_jump_count + 1U,
                "switch jump table allocation overflow"
            );
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            status = basl_parser_emit_jump(
                state,
                BASL_OPCODE_JUMP,
                token->span,
                &body_jumps[body_jump_count]
            );
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            body_jump_count += 1U;
            status = basl_parser_patch_jump(state, false_jump_offset);
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, token->span);
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }

            if (!basl_parser_match(state, BASL_TOKEN_COMMA)) {
                break;
            }
        }

        status = basl_parser_expect(
            state,
            BASL_TOKEN_COLON,
            "expected ':' after case value",
            NULL
        );
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_jump(
            state,
            BASL_OPCODE_JUMP,
            token->span,
            &jump_offset
        );
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        for (false_jump_offset = 0U; false_jump_offset < body_jump_count; false_jump_offset += 1U) {
            status = basl_parser_patch_jump(state, body_jumps[false_jump_offset]);
            if (status != BASL_STATUS_OK) {
                goto cleanup;
            }
        }

        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        basl_statement_result_clear(&case_body_result);
        status = basl_parser_parse_switch_case_contents(state, &case_body_result);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        all_branches_return = all_branches_return && case_body_result.guaranteed_return;
        status = basl_parser_grow_jump_offsets(
            state,
            &end_jumps,
            &end_jump_capacity,
            end_jump_count + 1U,
            "switch jump table allocation overflow"
        );
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_jump(
            state,
            BASL_OPCODE_JUMP,
            token->span,
            &end_jumps[end_jump_count]
        );
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        end_jump_count += 1U;
        status = basl_parser_patch_jump(state, jump_offset);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
    }

    if (!has_default) {
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, switch_token->span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
    }
    for (jump_offset = 0U; jump_offset < end_jump_count; jump_offset += 1U) {
        status = basl_parser_patch_jump(state, end_jumps[jump_offset]);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
    }

    basl_statement_result_set_guaranteed_return(
        out_result,
        has_default && all_branches_return
    );
    status = BASL_STATUS_OK;

cleanup:
    if (end_jumps != NULL) {
        void *memory = end_jumps;
        basl_runtime_free(state->program->registry->runtime, &memory);
    }
    if (body_jumps != NULL) {
        void *memory = body_jumps;
        basl_runtime_free(state->program->registry->runtime, &memory);
    }
    return status;
}

static basl_status_t basl_parser_parse_break_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *break_token;
    basl_loop_context_t *loop;
    size_t operand_offset;

    status = basl_parser_expect(state, BASL_TOKEN_BREAK, "expected 'break'", &break_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop = basl_parser_current_loop(state);
    if (loop == NULL) {
        return basl_parser_report(state, break_token->span, "'break' is only valid inside a loop");
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_SEMICOLON,
        "expected ';' after break",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_scope_cleanup_to_depth(
        state,
        loop->scope_depth,
        break_token->span
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_loop_context_grow_breaks(state, loop, loop->break_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_jump(
        state,
        BASL_OPCODE_JUMP,
        break_token->span,
        &operand_offset
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop = basl_parser_current_loop(state);
    loop->break_jumps[loop->break_count].operand_offset = operand_offset;
    loop->break_jumps[loop->break_count].span = break_token->span;
    loop->break_count += 1U;
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_continue_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *continue_token;
    basl_loop_context_t *loop;

    status = basl_parser_expect(
        state,
        BASL_TOKEN_CONTINUE,
        "expected 'continue'",
        &continue_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    loop = basl_parser_current_loop(state);
    if (loop == NULL) {
        return basl_parser_report(
            state,
            continue_token->span,
            "'continue' is only valid inside a loop"
        );
    }

    status = basl_parser_expect(
        state,
        BASL_TOKEN_SEMICOLON,
        "expected ';' after continue",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_scope_cleanup_to_depth(
        state,
        loop->scope_depth,
        continue_token->span
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_emit_loop(state, loop->loop_start, continue_token->span);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static int basl_parser_is_assignment_operator(
    basl_token_kind_t kind
) {
    return kind == BASL_TOKEN_ASSIGN ||
           kind == BASL_TOKEN_PLUS_ASSIGN ||
           kind == BASL_TOKEN_MINUS_ASSIGN ||
           kind == BASL_TOKEN_STAR_ASSIGN ||
           kind == BASL_TOKEN_SLASH_ASSIGN ||
           kind == BASL_TOKEN_PERCENT_ASSIGN ||
           kind == BASL_TOKEN_PLUS_PLUS ||
           kind == BASL_TOKEN_MINUS_MINUS;
}

static basl_status_t basl_parser_emit_i32_constant(
    basl_parser_state_t *state,
    int64_t value,
    basl_source_span_t span
) {
    basl_status_t status;
    basl_value_t constant;

    basl_value_init_int(&constant, value);
    status = basl_chunk_write_constant(
        &state->chunk,
        &constant,
        span,
        NULL,
        state->program->error
    );
    basl_value_release(&constant);
    return status;
}

static basl_status_t basl_parser_emit_f64_constant(
    basl_parser_state_t *state,
    double value,
    basl_source_span_t span
) {
    basl_status_t status;
    basl_value_t constant;

    basl_value_init_float(&constant, value);
    status = basl_chunk_write_constant(
        &state->chunk,
        &constant,
        span,
        NULL,
        state->program->error
    );
    basl_value_release(&constant);
    return status;
}

static int basl_parser_skip_bracketed_suffix(
    const basl_parser_state_t *state,
    size_t *cursor
) {
    const basl_token_t *token;
    size_t paren_depth;
    size_t bracket_depth;
    size_t brace_depth;

    if (cursor == NULL) {
        return 0;
    }

    token = basl_program_token_at(state->program, *cursor);
    if (token == NULL || token->kind != BASL_TOKEN_LBRACKET) {
        return 0;
    }

    paren_depth = 0U;
    bracket_depth = 1U;
    brace_depth = 0U;
    *cursor += 1U;
    while ((token = basl_program_token_at(state->program, *cursor)) != NULL) {
        switch (token->kind) {
            case BASL_TOKEN_LPAREN:
                paren_depth += 1U;
                break;
            case BASL_TOKEN_RPAREN:
                if (paren_depth == 0U) {
                    return 0;
                }
                paren_depth -= 1U;
                break;
            case BASL_TOKEN_LBRACE:
                brace_depth += 1U;
                break;
            case BASL_TOKEN_RBRACE:
                if (brace_depth == 0U) {
                    return 0;
                }
                brace_depth -= 1U;
                break;
            case BASL_TOKEN_LBRACKET:
                bracket_depth += 1U;
                break;
            case BASL_TOKEN_RBRACKET:
                if (bracket_depth == 0U) {
                    return 0;
                }
                bracket_depth -= 1U;
                *cursor += 1U;
                if (bracket_depth == 0U) {
                    return 1;
                }
                continue;
            default:
                break;
        }
        *cursor += 1U;
    }

    return 0;
}

static int basl_parser_skip_type_reference_tokens(
    const basl_program_state_t *program,
    size_t *cursor
) {
    const basl_token_t *token;
    const basl_token_t *next_token;
    const char *name_text;
    size_t name_length;

    if (program == NULL || cursor == NULL) {
        return 0;
    }

    token = basl_program_token_at(program, *cursor);
    if (token == NULL) {
        return 0;
    }

    if (token->kind == BASL_TOKEN_FN) {
        *cursor += 1U;
        token = basl_program_token_at(program, *cursor);
        if (token == NULL || token->kind != BASL_TOKEN_LPAREN) {
            return 1;
        }

        *cursor += 1U;
        token = basl_program_token_at(program, *cursor);
        if (token != NULL && token->kind != BASL_TOKEN_RPAREN) {
            while (1) {
                if (!basl_parser_skip_type_reference_tokens(program, cursor)) {
                    return 0;
                }
                token = basl_program_token_at(program, *cursor);
                if (token != NULL && token->kind == BASL_TOKEN_COMMA) {
                    *cursor += 1U;
                    continue;
                }
                break;
            }
        }

        token = basl_program_token_at(program, *cursor);
        if (token == NULL || token->kind != BASL_TOKEN_RPAREN) {
            return 0;
        }
        *cursor += 1U;

        token = basl_program_token_at(program, *cursor);
        if (token != NULL && token->kind == BASL_TOKEN_ARROW) {
            *cursor += 1U;
            token = basl_program_token_at(program, *cursor);
            if (token != NULL && token->kind == BASL_TOKEN_LPAREN) {
                *cursor += 1U;
                while (1) {
                    if (!basl_parser_skip_type_reference_tokens(program, cursor)) {
                        return 0;
                    }
                    token = basl_program_token_at(program, *cursor);
                    if (token != NULL && token->kind == BASL_TOKEN_COMMA) {
                        *cursor += 1U;
                        continue;
                    }
                    if (token != NULL && token->kind == BASL_TOKEN_RPAREN) {
                        *cursor += 1U;
                        break;
                    }
                    return 0;
                }
            } else if (!basl_parser_skip_type_reference_tokens(program, cursor)) {
                return 0;
            }
        }
        return 1;
    }

    if (token->kind != BASL_TOKEN_IDENTIFIER) {
        return 0;
    }

    name_text = basl_program_token_text(program, token, &name_length);
    next_token = basl_program_token_at(program, *cursor + 1U);
    if (
        next_token != NULL &&
        next_token->kind == BASL_TOKEN_LESS &&
        basl_program_names_equal(name_text, name_length, "array", 5U)
    ) {
        *cursor += 2U;
        if (!basl_parser_skip_type_reference_tokens(program, cursor)) {
            return 0;
        }
        token = basl_program_token_at(program, *cursor);
        if (token == NULL || token->kind != BASL_TOKEN_GREATER) {
            return 0;
        }
        *cursor += 1U;
        return 1;
    }
    if (
        next_token != NULL &&
        next_token->kind == BASL_TOKEN_LESS &&
        basl_program_names_equal(name_text, name_length, "map", 3U)
    ) {
        *cursor += 2U;
        if (!basl_parser_skip_type_reference_tokens(program, cursor)) {
            return 0;
        }
        token = basl_program_token_at(program, *cursor);
        if (token == NULL || token->kind != BASL_TOKEN_COMMA) {
            return 0;
        }
        *cursor += 1U;
        if (!basl_parser_skip_type_reference_tokens(program, cursor)) {
            return 0;
        }
        token = basl_program_token_at(program, *cursor);
        if (token == NULL || token->kind != BASL_TOKEN_GREATER) {
            return 0;
        }
        *cursor += 1U;
        return 1;
    }

    *cursor += 1U;
    token = basl_program_token_at(program, *cursor);
    if (token != NULL && token->kind == BASL_TOKEN_DOT) {
        *cursor += 1U;
        token = basl_program_token_at(program, *cursor);
        if (token == NULL || token->kind != BASL_TOKEN_IDENTIFIER) {
            return 0;
        }
        *cursor += 1U;
    }

    return 1;
}

static int basl_parser_is_assignment_start(
    const basl_parser_state_t *state
) {
    size_t cursor;
    const basl_token_t *token;

    token = basl_parser_peek(state);
    if (token == NULL || token->kind != BASL_TOKEN_IDENTIFIER) {
        return 0;
    }

    cursor = state->current + 1U;
    token = basl_program_token_at(state->program, cursor);
    while (token != NULL) {
        if (token->kind == BASL_TOKEN_DOT) {
            cursor += 1U;
            token = basl_program_token_at(state->program, cursor);
            if (token == NULL || token->kind != BASL_TOKEN_IDENTIFIER) {
                return 0;
            }
            cursor += 1U;
        } else if (token->kind == BASL_TOKEN_LBRACKET) {
            if (!basl_parser_skip_bracketed_suffix(state, &cursor)) {
                return 0;
            }
        } else {
            break;
        }
        token = basl_program_token_at(state->program, cursor);
    }

    return token != NULL && basl_parser_is_assignment_operator(token->kind);
}

static basl_status_t basl_parser_parse_assignment_statement_internal(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result,
    int expect_semicolon
) {
    basl_status_t status;
    const basl_token_t *name_token;
    const basl_token_t *field_token;
    const basl_token_t *operator_token;
    size_t local_index;
    size_t capture_index;
    size_t global_index;
    size_t field_index;
    basl_parser_type_t local_type;
    basl_parser_type_t target_type;
    basl_expression_result_t value_result;
    basl_expression_result_t index_result;
    const basl_class_field_t *field;
    const basl_binding_local_t *local_decl;
    const basl_global_variable_t *global_decl;
    int is_field_assignment;
    int is_index_assignment;
    int is_global_assignment;
    int is_const_local;
    int emitted_target_base;
    int is_capture_local;
    int local_found;

    local_index = 0U;
    capture_index = 0U;
    global_index = 0U;
    field_index = 0U;
    local_type = basl_binding_type_invalid();
    target_type = basl_binding_type_invalid();
    field = NULL;
    local_decl = NULL;
    global_decl = NULL;
    is_field_assignment = 0;
    is_index_assignment = 0;
    is_global_assignment = 0;
    is_const_local = 0;
    emitted_target_base = 0;
    is_capture_local = 0;
    local_found = 0;
    basl_expression_result_clear(&value_result);
    basl_expression_result_clear(&index_result);

    status = basl_parser_expect(
        state,
        BASL_TOKEN_IDENTIFIER,
        "expected local variable name",
        &name_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_resolve_local_symbol(
        state,
        name_token,
        &local_index,
        &local_type,
        &is_capture_local,
        &capture_index,
        &local_found
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (local_found) {
        if (!is_capture_local) {
            local_decl = basl_binding_scope_stack_local_at(&state->locals, local_index);
            local_type = local_decl->type;
            is_const_local = local_decl->is_const;
        } else {
            is_const_local = 0;
        }
    } else {
        const char *name_text;
        size_t name_length;

        name_text = basl_parser_token_text(state, name_token, &name_length);
        if (!basl_program_find_global(
                state->program,
                name_text,
                name_length,
                &global_index,
                &global_decl
            )) {
            return basl_parser_report(state, name_token->span, "unknown local variable");
        }
        is_global_assignment = 1;
        local_type = global_decl->type;
    }

    target_type = local_type;
    while (
        basl_parser_check(state, BASL_TOKEN_DOT) ||
        basl_parser_check(state, BASL_TOKEN_LBRACKET)
    ) {
        if (!emitted_target_base) {
            status = basl_parser_emit_opcode(
                state,
                is_global_assignment
                    ? BASL_OPCODE_GET_GLOBAL
                    : (is_capture_local ? BASL_OPCODE_GET_CAPTURE : BASL_OPCODE_GET_LOCAL),
                name_token->span
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_u32(
                state,
                (uint32_t)(is_global_assignment
                               ? global_index
                               : (is_capture_local ? capture_index : local_index)),
                name_token->span
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            emitted_target_base = 1;
        }

        if (basl_parser_match(state, BASL_TOKEN_DOT)) {
            is_field_assignment = 1;
            is_index_assignment = 0;
            status = basl_parser_expect(
                state,
                BASL_TOKEN_IDENTIFIER,
                "expected field name after '.'",
                &field_token
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }

            field = NULL;
            field_index = 0U;
            status = basl_parser_lookup_field(
                state,
                target_type,
                field_token,
                &field_index,
                &field
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            target_type = field->type;

            operator_token = basl_parser_peek(state);
            if (
                operator_token != NULL &&
                basl_parser_is_assignment_operator(operator_token->kind)
            ) {
                break;
            }

            status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_FIELD, field_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_u32(state, (uint32_t)field_index, field_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }

        if (basl_parser_match(state, BASL_TOKEN_LBRACKET)) {
            basl_parser_type_t indexed_type;

            basl_expression_result_clear(&index_result);
            indexed_type = basl_binding_type_invalid();
            status = basl_parser_parse_expression(state, &index_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_scalar_expression(
                state,
                basl_parser_previous(state) == NULL
                    ? name_token->span
                    : basl_parser_previous(state)->span,
                &index_result,
                "index expressions must evaluate to a single value"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_expect(
                state,
                BASL_TOKEN_RBRACKET,
                "expected ']' after index expression",
                NULL
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }

            if (basl_parser_type_is_array(target_type)) {
                status = basl_parser_require_type(
                    state,
                    name_token->span,
                    index_result.type,
                    basl_binding_type_primitive(BASL_TYPE_I32),
                    "array index must be i32"
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                indexed_type = basl_program_array_type_element(state->program, target_type);
            } else if (basl_parser_type_is_map(target_type)) {
                status = basl_parser_require_type(
                    state,
                    name_token->span,
                    index_result.type,
                    basl_program_map_type_key(state->program, target_type),
                    "map index must match map key type"
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                indexed_type = basl_program_map_type_value(state->program, target_type);
            } else {
                return basl_parser_report(
                    state,
                    name_token->span,
                    "index assignment requires an array or map"
                );
            }
            target_type = indexed_type;
            is_field_assignment = 0;
            is_index_assignment = 1;

            operator_token = basl_parser_peek(state);
            if (
                operator_token != NULL &&
                basl_parser_is_assignment_operator(operator_token->kind)
            ) {
                break;
            }

            status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_INDEX, name_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            continue;
        }
    }

    if (is_const_local && !is_field_assignment && !is_index_assignment) {
        return basl_parser_report(
            state,
            name_token->span,
            "cannot assign to const local variable"
        );
    }

    operator_token = basl_parser_peek(state);
    if (operator_token == NULL || !basl_parser_is_assignment_operator(operator_token->kind)) {
        return basl_parser_report(state, name_token->span, "expected assignment operator");
    }
    basl_parser_advance(state);

    if (operator_token->kind == BASL_TOKEN_ASSIGN) {
        status = basl_parser_parse_expression(state, &value_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_scalar_expression(
            state,
            operator_token->span,
            &value_result,
            "assigned expression must be a single value"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_require_type(
            state,
            name_token->span,
            value_result.type,
            target_type,
            is_index_assignment
                ? "assigned expression type does not match indexed value type"
                : (is_field_assignment
                       ? "assigned expression type does not match field type"
                       : (is_global_assignment
                              ? "assigned expression type does not match global variable type"
                              : "assigned expression type does not match local variable type"))
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        basl_opcode_t opcode;
        basl_binary_operator_kind_t operator_kind;

        if (is_index_assignment) {
            return basl_parser_report(
                state,
                operator_token->span,
                "compound indexed assignment is not yet supported"
            );
        }

        opcode = BASL_OPCODE_ADD;
        operator_kind = BASL_BINARY_OPERATOR_ADD;

        if (is_field_assignment) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_DUP, operator_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_FIELD, operator_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_u32(state, (uint32_t)field_index, operator_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
        } else {
            if (!emitted_target_base) {
                status = basl_parser_emit_opcode(
                    state,
                    is_global_assignment
                        ? BASL_OPCODE_GET_GLOBAL
                        : (is_capture_local ? BASL_OPCODE_GET_CAPTURE : BASL_OPCODE_GET_LOCAL),
                    operator_token->span
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                status = basl_parser_emit_u32(
                    state,
                    (uint32_t)(is_global_assignment
                                   ? global_index
                                   : (is_capture_local ? capture_index : local_index)),
                    operator_token->span
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
            }
        }

        if (
            operator_token->kind == BASL_TOKEN_PLUS_PLUS ||
            operator_token->kind == BASL_TOKEN_MINUS_MINUS
        ) {
            if (basl_parser_type_is_i32(target_type)) {
                status = basl_parser_emit_i32_constant(state, 1, operator_token->span);
            } else if (basl_parser_type_is_f64(target_type)) {
                status = basl_parser_emit_f64_constant(state, 1.0, operator_token->span);
            } else {
                status = basl_parser_report(
                    state,
                    operator_token->span,
                    "increment and decrement require an i32 or f64 target"
                );
            }
            if (status != BASL_STATUS_OK) {
                return status;
            }
            opcode = operator_token->kind == BASL_TOKEN_PLUS_PLUS
                ? BASL_OPCODE_ADD
                : BASL_OPCODE_SUBTRACT;
        } else {
            status = basl_parser_parse_expression(state, &value_result);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_require_scalar_expression(
                state,
                operator_token->span,
                &value_result,
                "assigned expression must be a single value"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }

            switch (operator_token->kind) {
                case BASL_TOKEN_PLUS_ASSIGN:
                    operator_kind = BASL_BINARY_OPERATOR_ADD;
                    opcode = BASL_OPCODE_ADD;
                    break;
                case BASL_TOKEN_MINUS_ASSIGN:
                    operator_kind = BASL_BINARY_OPERATOR_SUBTRACT;
                    opcode = BASL_OPCODE_SUBTRACT;
                    break;
                case BASL_TOKEN_STAR_ASSIGN:
                    operator_kind = BASL_BINARY_OPERATOR_MULTIPLY;
                    opcode = BASL_OPCODE_MULTIPLY;
                    break;
                case BASL_TOKEN_SLASH_ASSIGN:
                    operator_kind = BASL_BINARY_OPERATOR_DIVIDE;
                    opcode = BASL_OPCODE_DIVIDE;
                    break;
                case BASL_TOKEN_PERCENT_ASSIGN:
                    operator_kind = BASL_BINARY_OPERATOR_MODULO;
                    opcode = BASL_OPCODE_MODULO;
                    break;
                default:
                    return basl_parser_report(
                        state,
                        operator_token->span,
                        "unsupported assignment operator"
                    );
            }

            if (
                !basl_parser_type_supports_binary_operator(
                    operator_kind,
                    target_type,
                    value_result.type
                )
            ) {
                status = basl_parser_report(
                    state,
                    operator_token->span,
                    operator_kind == BASL_BINARY_OPERATOR_ADD
                        ? "compound assignment requires matching i32, f64, or string operands"
                        : (operator_kind == BASL_BINARY_OPERATOR_MODULO
                               ? "compound assignment modulo requires i32 operands"
                               : "compound assignment requires matching i32 or f64 operands")
                );
            } else {
                status = BASL_STATUS_OK;
            }
            if (status != BASL_STATUS_OK) {
                return status;
            }
        }

        status = basl_parser_emit_opcode(state, opcode, operator_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    if (expect_semicolon) {
        status = basl_parser_expect(state, BASL_TOKEN_SEMICOLON, "expected ';' after assignment", NULL);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    if (is_field_assignment) {
        status = basl_parser_emit_opcode(state, BASL_OPCODE_SET_FIELD, name_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(state, (uint32_t)field_index, name_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else if (is_index_assignment) {
        status = basl_parser_emit_opcode(state, BASL_OPCODE_SET_INDEX, name_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        status = basl_parser_emit_opcode(
            state,
            is_global_assignment
                ? BASL_OPCODE_SET_GLOBAL
                : (is_capture_local ? BASL_OPCODE_SET_CAPTURE : BASL_OPCODE_SET_LOCAL),
            name_token->span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(
            state,
            (uint32_t)(is_global_assignment
                           ? global_index
                           : (is_capture_local ? capture_index : local_index)),
            name_token->span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, name_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_assignment_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    return basl_parser_parse_assignment_statement_internal(state, out_result, 1);
}

static basl_status_t basl_parser_parse_expression_statement_internal(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result,
    int expect_semicolon
) {
    basl_status_t status;
    basl_expression_result_t expression_result;
    const basl_token_t *last_token;

    basl_expression_result_clear(&expression_result);

    status = basl_parser_parse_expression(state, &expression_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (expression_result.type_count > 1U) {
        return basl_parser_report(
            state,
            basl_parser_previous(state) == NULL
                ? basl_parser_fallback_span(state)
                : basl_parser_previous(state)->span,
            "multi-value expressions must be bound explicitly"
        );
    }

    last_token = basl_parser_previous(state);
    if (expect_semicolon) {
        status = basl_parser_expect(state, BASL_TOKEN_SEMICOLON, "expected ';' after expression", NULL);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    if (!basl_parser_type_is_void(expression_result.type)) {
        status = basl_parser_emit_opcode(
            state,
            BASL_OPCODE_POP,
            last_token == NULL ? basl_parser_fallback_span(state) : last_token->span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_expression_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    return basl_parser_parse_expression_statement_internal(state, out_result, 1);
}

static basl_status_t basl_parser_parse_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    if (basl_parser_check(state, BASL_TOKEN_RETURN)) {
        return basl_parser_parse_return_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_DEFER)) {
        return basl_parser_parse_defer_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_GUARD)) {
        return basl_parser_parse_guard_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_IF)) {
        return basl_parser_parse_if_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_SWITCH)) {
        return basl_parser_parse_switch_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_FOR)) {
        return basl_parser_parse_for_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_WHILE)) {
        return basl_parser_parse_while_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_BREAK)) {
        return basl_parser_parse_break_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_CONTINUE)) {
        return basl_parser_parse_continue_statement(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_LBRACE)) {
        return basl_parser_parse_block_statement(state, out_result);
    }
    if (basl_parser_is_assignment_start(state)) {
        return basl_parser_parse_assignment_statement(state, out_result);
    }

    return basl_parser_parse_expression_statement(state, out_result);
}

static basl_status_t basl_parser_parse_variable_declaration(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    basl_binding_target_list_t targets;
    basl_expression_result_t initializer_result;

    basl_binding_target_list_init(&targets);
    basl_expression_result_clear(&initializer_result);

    status = basl_parser_parse_binding_target_list(
        state,
        "unsupported local variable type",
        "local variables cannot use type void",
        "expected local variable name",
        &targets
    );
    if (status != BASL_STATUS_OK) {
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }

    if (!basl_parser_match(state, BASL_TOKEN_ASSIGN)) {
        status = basl_parser_report(
            state,
            targets.items[0].name_token->span,
            "variables must be initialized at declaration"
        );
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }

    status = basl_parser_parse_expression(state, &initializer_result);
    if (status != BASL_STATUS_OK) {
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }
    status = basl_parser_require_binding_initializer_shape(
        state,
        targets.items[0].name_token->span,
        &targets,
        &initializer_result,
        targets.count == 1U
            ? "initializer must be a single value"
            : "initializer return shape does not match declaration",
        targets.count == 1U
            ? "initializer type does not match local variable type"
            : "initializer type does not match local binding type"
    );
    if (status != BASL_STATUS_OK) {
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_SEMICOLON, "expected ';' after local declaration", NULL);
    if (status != BASL_STATUS_OK) {
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }

    status = basl_parser_bind_targets(state, &targets, 0, NULL);
    if (status != BASL_STATUS_OK) {
        basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
        return status;
    }
    basl_binding_target_list_free((basl_program_state_t *)state->program, &targets);
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_const_declaration(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *const_token;
    const basl_token_t *name_token;
    basl_parser_type_t declared_type;
    basl_expression_result_t initializer_result;

    basl_expression_result_clear(&initializer_result);
    status = basl_parser_expect(state, BASL_TOKEN_CONST, "expected 'const'", &const_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_program_parse_type_reference(
        state->program,
        &state->current,
        "unsupported local constant type",
        &declared_type
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_program_require_non_void_type(
        state->program,
        basl_parser_previous(state) == NULL
            ? const_token->span
            : basl_parser_previous(state)->span,
        declared_type,
        "local constants cannot use type void"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_expect(
        state,
        BASL_TOKEN_IDENTIFIER,
        "expected local constant name",
        &name_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_expect(
        state,
        BASL_TOKEN_ASSIGN,
        "constants must be initialized at declaration",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_parse_expression(state, &initializer_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_scalar_expression(
        state,
        name_token->span,
        &initializer_result,
        "initializer must be a single value"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_type(
        state,
        name_token->span,
        initializer_result.type,
        declared_type,
        "initializer type does not match local constant type"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_expect(
        state,
        BASL_TOKEN_SEMICOLON,
        "expected ';' after local constant declaration",
        NULL
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_declare_local_symbol(state, name_token, declared_type, 1, NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    basl_statement_result_set_guaranteed_return(out_result, 0);
    return BASL_STATUS_OK;
}

static int basl_parser_is_variable_declaration_start(
    const basl_parser_state_t *state
) {
    const basl_token_t *token;
    size_t cursor;

    cursor = state->current;
    while (1) {
        if (!basl_parser_skip_type_reference_tokens(state->program, &cursor)) {
            return 0;
        }

        token = basl_program_token_at(state->program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_IDENTIFIER) {
            return 0;
        }
        cursor += 1U;

        token = basl_program_token_at(state->program, cursor);
        if (token == NULL) {
            return 0;
        }
        if (token->kind == BASL_TOKEN_ASSIGN) {
            return 1;
        }
        if (token->kind == BASL_TOKEN_SEMICOLON) {
            return 1;
        }
        if (token->kind != BASL_TOKEN_COMMA) {
            return 0;
        }
        cursor += 1U;
    }
}

static basl_status_t basl_parser_parse_declaration(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    if (
        basl_parser_check(state, BASL_TOKEN_FN) &&
        basl_program_token_at(state->program, state->current + 1U) != NULL &&
        basl_program_token_at(state->program, state->current + 1U)->kind == BASL_TOKEN_IDENTIFIER &&
        basl_program_token_at(state->program, state->current + 2U) != NULL &&
        basl_program_token_at(state->program, state->current + 2U)->kind == BASL_TOKEN_LPAREN
    ) {
        return basl_parser_parse_local_function_declaration(state, out_result);
    }
    if (basl_parser_check(state, BASL_TOKEN_CONST)) {
        return basl_parser_parse_const_declaration(state, out_result);
    }
    if (basl_parser_is_variable_declaration_start(state)) {
        return basl_parser_parse_variable_declaration(state, out_result);
    }

    return basl_parser_parse_statement(state, out_result);
}

static basl_status_t basl_parser_parse_block_contents(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    basl_statement_result_t declaration_result;
    basl_statement_result_t block_result;

    basl_statement_result_clear(&declaration_result);
    basl_statement_result_clear(&block_result);

    while (!basl_parser_is_at_end(state) && !basl_parser_check(state, BASL_TOKEN_RBRACE)) {
        status = basl_parser_parse_declaration(state, &declaration_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        if (declaration_result.guaranteed_return) {
            block_result.guaranteed_return = 1;
        }
    }

    basl_statement_result_set_guaranteed_return(
        out_result,
        block_result.guaranteed_return
    );
    return BASL_STATUS_OK;
}

static basl_status_t basl_compile_seed_parameter_symbols(
    basl_parser_state_t *state,
    const basl_function_decl_t *decl
) {
    basl_status_t status;
    size_t i;

    for (i = 0U; i < decl->param_count; ++i) {
        if (
            decl->owner_class_index != BASL_BINDING_INVALID_CLASS_INDEX &&
            i == 0U &&
            decl->params[i].length == 4U &&
            memcmp(decl->params[i].name, "self", 4U) == 0
        ) {
            status = basl_binding_scope_stack_declare_local(
                &state->locals,
                "self",
                4U,
                decl->params[i].type,
                0,
                NULL,
                state->program->error
            );
        } else {
            const basl_token_t fake_name = {
                BASL_TOKEN_IDENTIFIER,
                decl->params[i].span
            };

            status = basl_parser_declare_local_symbol(
                state,
                &fake_name,
                decl->params[i].type,
                0,
                NULL
            );
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_compile_emit_global_initializers(
    basl_program_state_t *program,
    basl_parser_state_t *state
) {
    basl_status_t status;
    size_t i;
    const basl_source_file_t *previous_source;
    const basl_token_list_t *previous_tokens;
    size_t previous_current;
    size_t previous_body_end;

    previous_source = program->source;
    previous_tokens = program->tokens;
    previous_current = state->current;
    previous_body_end = state->body_end;

    for (i = 0U; i < program->global_count; i += 1U) {
        basl_expression_result_t initializer_result;

        basl_expression_result_clear(&initializer_result);
        basl_program_set_module_context(program, program->globals[i].source, program->globals[i].tokens);
        state->current = program->globals[i].initializer_start;
        state->body_end = program->globals[i].initializer_end;

        status = basl_parser_parse_expression(state, &initializer_result);
        if (status != BASL_STATUS_OK) {
            goto restore;
        }
        if (state->current != program->globals[i].initializer_end) {
            status = basl_compile_report(
                program,
                program->globals[i].name_span,
                "invalid global initializer expression"
            );
            goto restore;
        }
        status = basl_parser_require_type(
            state,
            program->globals[i].name_span,
            initializer_result.type,
            program->globals[i].type,
            "initializer type does not match global variable type"
        );
        if (status != BASL_STATUS_OK) {
            goto restore;
        }

        status = basl_parser_emit_opcode(
            state,
            BASL_OPCODE_SET_GLOBAL,
            program->globals[i].name_span
        );
        if (status != BASL_STATUS_OK) {
            goto restore;
        }
        status = basl_parser_emit_u32(state, (uint32_t)i, program->globals[i].name_span);
        if (status != BASL_STATUS_OK) {
            goto restore;
        }
        status = basl_parser_emit_opcode(
            state,
            BASL_OPCODE_POP,
            program->globals[i].name_span
        );
        if (status != BASL_STATUS_OK) {
            goto restore;
        }
    }

    status = BASL_STATUS_OK;

restore:
    basl_program_set_module_context(program, previous_source, previous_tokens);
    state->current = previous_current;
    state->body_end = previous_body_end;
    return status;
}

static basl_status_t basl_compile_synthetic_constructor(
    basl_program_state_t *program,
    size_t function_index,
    size_t class_index,
    size_t init_function_index
) {
    basl_status_t status;
    basl_parser_state_t state;
    basl_function_decl_t *decl;
    const basl_class_decl_t *class_decl;
    basl_object_t *object;
    size_t field_index;
    size_t param_index;
    uint32_t init_arg_count;

    decl = &program->functions.functions[function_index];
    if (class_index >= program->class_count || init_function_index >= program->functions.count) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_INTERNAL,
            "synthetic constructor metadata is invalid"
        );
        return BASL_STATUS_INTERNAL;
    }

    class_decl = &program->classes[class_index];
    if (decl->param_count > UINT32_MAX - 1U) {
        basl_error_set_literal(
            program->error,
            BASL_STATUS_OUT_OF_MEMORY,
            "constructor arity overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }
    init_arg_count = (uint32_t)(decl->param_count + 1U);

    memset(&state, 0, sizeof(state));
    state.program = program;
    state.function_index = function_index;
    state.expected_return_type = decl->return_type;
    basl_chunk_init(&state.chunk, program->registry->runtime);
    basl_binding_scope_stack_init(&state.locals, program->registry->runtime);

    for (field_index = 0U; field_index < class_decl->field_count; field_index += 1U) {
        status = basl_parser_emit_opcode(&state, BASL_OPCODE_NIL, decl->name_span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
    }
    status = basl_parser_emit_opcode(&state, BASL_OPCODE_NEW_INSTANCE, decl->name_span);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_emit_u32(&state, (uint32_t)class_index, decl->name_span);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_emit_u32(&state, (uint32_t)class_decl->field_count, decl->name_span);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_emit_opcode(&state, BASL_OPCODE_DUP, decl->name_span);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }

    for (param_index = 0U; param_index < decl->param_count; param_index += 1U) {
        status = basl_parser_emit_opcode(&state, BASL_OPCODE_GET_LOCAL, decl->name_span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
        status = basl_parser_emit_u32(&state, (uint32_t)param_index, decl->name_span);
        if (status != BASL_STATUS_OK) {
            goto cleanup;
        }
    }

    status = basl_parser_emit_opcode(&state, BASL_OPCODE_CALL, decl->name_span);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_emit_u32(
        &state,
        (uint32_t)init_function_index,
        decl->name_span
    );
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_emit_u32(&state, init_arg_count, decl->name_span);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_emit_opcode(&state, BASL_OPCODE_RETURN, decl->name_span);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }
    status = basl_parser_emit_u32(&state, (uint32_t)decl->return_count, decl->name_span);
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }

    object = NULL;
    status = basl_function_object_new(
        program->registry->runtime,
        decl->name,
        decl->name_length,
        decl->param_count,
        decl->return_count,
        &state.chunk,
        &object,
        program->error
    );
    if (status != BASL_STATUS_OK) {
        goto cleanup;
    }

    basl_parser_state_free(&state);
    decl->object = object;
    return BASL_STATUS_OK;

cleanup:
    basl_chunk_free(&state.chunk);
    basl_parser_state_free(&state);
    return status;
}

static basl_status_t basl_compile_require_function_returns(
    basl_program_state_t *program,
    const basl_function_decl_t *decl,
    size_t function_index,
    int guaranteed_return
) {
    if (decl->return_count == 1U && basl_parser_type_is_void(decl->return_type)) {
        return BASL_STATUS_OK;
    }
    if (guaranteed_return) {
        return BASL_STATUS_OK;
    }

    return basl_compile_report(
        program,
        decl->name_span,
        function_index == program->functions.main_index
            ? "main entrypoint must return an i32 value on all paths"
            : "function must return a value on all paths"
    );
}

static basl_status_t basl_compile_function_with_parent(
    basl_program_state_t *program,
    size_t function_index,
    const basl_parser_state_t *parent_state
) {
    basl_status_t status;
    basl_parser_state_t state;
    basl_function_decl_t *decl;
    basl_object_t *object;
    basl_statement_result_t body_result;
    size_t class_index;

    decl = &program->functions.functions[function_index];
    if (decl->object != NULL) {
        return BASL_STATUS_OK;
    }
    for (class_index = 0U; class_index < program->class_count; class_index += 1U) {
        const basl_class_decl_t *class_decl;
        const basl_class_method_t *init_method;

        class_decl = &program->classes[class_index];
        if (class_decl->constructor_function_index != function_index) {
            continue;
        }
        init_method = NULL;
        if (!basl_class_decl_find_method(class_decl, "init", 4U, NULL, &init_method) ||
            init_method == NULL) {
            basl_error_set_literal(
                program->error,
                BASL_STATUS_INTERNAL,
                "class init declaration is missing"
            );
            return BASL_STATUS_INTERNAL;
        }
        return basl_compile_synthetic_constructor(
            program,
            function_index,
            class_index,
            init_method->function_index
        );
    }
    memset(&state, 0, sizeof(state));
    basl_program_set_module_context(program, decl->source, decl->tokens);
    state.program = program;
    state.parent = (basl_parser_state_t *)parent_state;
    state.current = decl->body_start;
    state.body_end = decl->body_end;
    state.function_index = function_index;
    state.expected_return_type = decl->return_type;
    state.expected_return_types = basl_function_return_types(decl);
    state.expected_return_count = decl->return_count;
    basl_chunk_init(&state.chunk, program->registry->runtime);
    basl_binding_scope_stack_init(&state.locals, program->registry->runtime);
    basl_binding_scope_stack_begin_scope(&state.locals);
    basl_statement_result_clear(&body_result);

    status = basl_compile_seed_parameter_symbols(&state, decl);
    if (status != BASL_STATUS_OK) {
        basl_chunk_free(&state.chunk);
        basl_parser_state_free(&state);
        return status;
    }

    if (function_index == program->functions.main_index) {
        status = basl_compile_emit_global_initializers(program, &state);
        if (status != BASL_STATUS_OK) {
            basl_chunk_free(&state.chunk);
            basl_parser_state_free(&state);
            return status;
        }
    }

    status = basl_parser_parse_block_contents(&state, &body_result);
    if (status != BASL_STATUS_OK) {
        basl_chunk_free(&state.chunk);
        basl_parser_state_free(&state);
        return status;
    }
    if (
        !body_result.guaranteed_return &&
        decl->return_count == 1U &&
        basl_parser_type_is_void(decl->return_type)
    ) {
        status = basl_parser_emit_opcode(&state, BASL_OPCODE_RETURN, decl->name_span);
        if (status != BASL_STATUS_OK) {
            basl_chunk_free(&state.chunk);
            basl_parser_state_free(&state);
            return status;
        }
        status = basl_parser_emit_u32(&state, 0U, decl->name_span);
        if (status != BASL_STATUS_OK) {
            basl_chunk_free(&state.chunk);
            basl_parser_state_free(&state);
            return status;
        }
        body_result.guaranteed_return = 1;
    }

    status = basl_compile_require_function_returns(
        program,
        decl,
        function_index,
        body_result.guaranteed_return
    );
    if (status != BASL_STATUS_OK) {
        basl_chunk_free(&state.chunk);
        basl_parser_state_free(&state);
        return status;
    }

    object = NULL;
    status = basl_function_object_new(
        program->registry->runtime,
        decl->name,
        decl->name_length,
        decl->param_count,
        decl->return_count,
        &state.chunk,
        &object,
        program->error
    );
    basl_parser_state_free(&state);
    if (status != BASL_STATUS_OK) {
        basl_chunk_free(&state.chunk);
        return status;
    }

    decl->object = object;
    return BASL_STATUS_OK;
}

static basl_status_t basl_compile_function(
    basl_program_state_t *program,
    size_t function_index
) {
    return basl_compile_function_with_parent(program, function_index, NULL);
}

static basl_status_t basl_compile_validate_inputs(
    const basl_source_registry_t *registry,
    basl_diagnostic_list_t *diagnostics,
    basl_object_t **out_function,
    basl_compile_mode_t mode,
    basl_error_t *error
) {
    if (registry == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "source registry must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (diagnostics == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "diagnostic list must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (mode == BASL_COMPILE_MODE_BUILD_ENTRYPOINT && out_function == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "out_function must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_compile_all_functions(
    basl_program_state_t *program
) {
    basl_status_t status;
    size_t i;

    for (i = 0U; i < program->functions.count; ++i) {
        status = basl_compile_function(program, i);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_compile_attach_entrypoint(
    basl_program_state_t *program,
    basl_object_t **out_function
) {
    basl_status_t status;
    basl_object_t **function_table;
    basl_value_t *initial_globals;
    basl_runtime_class_init_t *class_inits;
    size_t i;
    void *memory;

    memory = NULL;
    status = basl_runtime_alloc(
        program->registry->runtime,
        program->functions.count * sizeof(*function_table),
        &memory,
        program->error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    function_table = (basl_object_t **)memory;
    for (i = 0U; i < program->functions.count; ++i) {
        function_table[i] = program->functions.functions[i].object;
    }

    initial_globals = NULL;
    if (program->global_count != 0U) {
        memory = NULL;
        status = basl_runtime_alloc(
            program->registry->runtime,
            program->global_count * sizeof(*initial_globals),
            &memory,
            program->error
        );
        if (status != BASL_STATUS_OK) {
            memory = function_table;
            basl_runtime_free(program->registry->runtime, &memory);
            return status;
        }

        initial_globals = (basl_value_t *)memory;
        for (i = 0U; i < program->global_count; ++i) {
            basl_value_init_nil(&initial_globals[i]);
        }
    }

    class_inits = NULL;
    if (program->class_count != 0U) {
        memory = NULL;
        status = basl_runtime_alloc(
            program->registry->runtime,
            program->class_count * sizeof(*class_inits),
            &memory,
            program->error
        );
        if (status != BASL_STATUS_OK) {
            if (initial_globals != NULL) {
                memory = initial_globals;
                basl_runtime_free(program->registry->runtime, &memory);
            }
            memory = function_table;
            basl_runtime_free(program->registry->runtime, &memory);
            return status;
        }

        class_inits = (basl_runtime_class_init_t *)memory;
        memset(class_inits, 0, program->class_count * sizeof(*class_inits));
        for (i = 0U; i < program->class_count; ++i) {
            const basl_class_decl_t *decl = &program->classes[i];

            class_inits[i].interface_impl_count = decl->interface_impl_count;
            if (decl->interface_impl_count == 0U) {
                continue;
            }

            memory = NULL;
            status = basl_runtime_alloc(
                program->registry->runtime,
                decl->interface_impl_count * sizeof(*class_inits[i].interface_impls),
                &memory,
                program->error
            );
            if (status != BASL_STATUS_OK) {
                size_t class_index;

                for (class_index = 0U; class_index < i; ++class_index) {
                    memory = (void *)class_inits[class_index].interface_impls;
                    basl_runtime_free(program->registry->runtime, &memory);
                }
                memory = class_inits;
                basl_runtime_free(program->registry->runtime, &memory);
                if (initial_globals != NULL) {
                    memory = initial_globals;
                    basl_runtime_free(program->registry->runtime, &memory);
                }
                memory = function_table;
                basl_runtime_free(program->registry->runtime, &memory);
                return status;
            }

            class_inits[i].interface_impls = (const basl_runtime_interface_impl_init_t *)memory;
            memset(
                (void *)class_inits[i].interface_impls,
                0,
                decl->interface_impl_count * sizeof(*class_inits[i].interface_impls)
            );

            {
                size_t impl_index;
                basl_runtime_interface_impl_init_t *impls =
                    (basl_runtime_interface_impl_init_t *)class_inits[i].interface_impls;

                for (impl_index = 0U; impl_index < decl->interface_impl_count; ++impl_index) {
                    impls[impl_index].interface_index = decl->interface_impls[impl_index].interface_index;
                    impls[impl_index].function_indices =
                        decl->interface_impls[impl_index].function_indices;
                    impls[impl_index].function_count =
                        decl->interface_impls[impl_index].function_count;
                }
            }
        }
    }

    status = basl_function_object_attach_siblings(
        program->functions.functions[program->functions.main_index].object,
        function_table,
        program->functions.count,
        program->functions.main_index,
        initial_globals,
        program->global_count,
        class_inits,
        program->class_count,
        program->error
    );
    if (initial_globals != NULL) {
        memory = initial_globals;
        basl_runtime_free(program->registry->runtime, &memory);
    }
    if (class_inits != NULL) {
        for (i = 0U; i < program->class_count; ++i) {
            memory = (void *)class_inits[i].interface_impls;
            basl_runtime_free(program->registry->runtime, &memory);
        }
        memory = class_inits;
        basl_runtime_free(program->registry->runtime, &memory);
    }
    if (status != BASL_STATUS_OK) {
        memory = function_table;
        basl_runtime_free(program->registry->runtime, &memory);
        return status;
    }

    *out_function = program->functions.functions[program->functions.main_index].object;
    for (i = 0U; i < program->functions.count; ++i) {
        program->functions.functions[i].object = NULL;
    }

    return BASL_STATUS_OK;
}

basl_status_t basl_compile_source_internal(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_compile_mode_t mode,
    basl_object_t **out_function,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
) {
    basl_status_t status;
    basl_program_state_t program;
    const basl_source_file_t *source;

    basl_error_clear(error);
    if (out_function != NULL) {
        *out_function = NULL;
    }

    status = basl_compile_validate_inputs(
        registry,
        diagnostics,
        out_function,
        mode,
        error
    );
    if (status != BASL_STATUS_OK) {
        return status;
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

    memset(&program, 0, sizeof(program));
    program.registry = registry;
    program.diagnostics = diagnostics;
    program.error = error;
    basl_binding_function_table_init(&program.functions, registry->runtime);
    basl_program_set_module_context(&program, source, NULL);

    status = basl_program_parse_source(&program, source_id);
    if (status != BASL_STATUS_OK) {
        basl_program_free(&program);
        return status;
    }

    basl_program_set_module_context(&program, source, NULL);
    if (!program.functions.has_main) {
        status = basl_compile_report(
            &program,
            basl_program_eof_span(&program),
            "expected top-level function 'main'"
        );
        basl_program_free(&program);
        return status;
    }

    status = basl_compile_all_functions(&program);
    if (status != BASL_STATUS_OK) {
        basl_program_free(&program);
        return status;
    }

    if (mode == BASL_COMPILE_MODE_BUILD_ENTRYPOINT) {
        status = basl_compile_attach_entrypoint(&program, out_function);
        if (status != BASL_STATUS_OK) {
            basl_program_free(&program);
            return status;
        }
    }

    basl_program_free(&program);
    return BASL_STATUS_OK;
}

basl_status_t basl_compile_source(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_object_t **out_function,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
) {
    return basl_compile_source_internal(
        registry,
        source_id,
        BASL_COMPILE_MODE_BUILD_ENTRYPOINT,
        out_function,
        diagnostics,
        error
    );
}
