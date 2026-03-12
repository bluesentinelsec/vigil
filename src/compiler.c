#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "basl/chunk.h"
#include "basl/lexer.h"
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
    basl_token_list_t tokens;
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
    basl_global_constant_t *constants;
    size_t constant_count;
    size_t constant_capacity;
    basl_global_variable_t *globals;
    size_t global_count;
    size_t global_capacity;
} basl_program_state_t;

typedef struct basl_parser_state {
    const basl_program_state_t *program;
    size_t current;
    size_t body_end;
    size_t function_index;
    basl_parser_type_t expected_return_type;
    basl_chunk_t chunk;
    basl_binding_scope_stack_t locals;
    basl_loop_context_t *loops;
    size_t loop_count;
    size_t loop_capacity;
} basl_parser_state_t;

typedef struct basl_expression_result {
    basl_parser_type_t type;
} basl_expression_result_t;

typedef struct basl_constant_result {
    basl_parser_type_t type;
    basl_value_t value;
} basl_constant_result_t;

typedef struct basl_statement_result {
    int guaranteed_return;
} basl_statement_result_t;

static int basl_parser_is_assignment_start(
    const basl_parser_state_t *state
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
static int basl_parser_is_variable_declaration_start(
    const basl_parser_state_t *state
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
}

static void basl_expression_result_set_type(
    basl_expression_result_t *result,
    basl_parser_type_t type
) {
    if (result == NULL) {
        return;
    }

    result->type = type;
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
    return type.kind != BASL_TYPE_INVALID && type.kind != BASL_TYPE_OBJECT;
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

    if (
        basl_parser_type_is_class(target_type) ||
        basl_parser_type_is_class(source_type) ||
        basl_parser_type_is_interface(target_type) ||
        basl_parser_type_is_interface(source_type)
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

    status = basl_program_grow_modules(program, program->module_count + 1U);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    module = &program->modules[program->module_count];
    memset(module, 0, sizeof(*module));
    module->source_id = source_id;
    module->source = source;
    module->tokens = *tokens;
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
        default:
            return 0;
    }
}

static int basl_program_is_class_public(const basl_class_decl_t *decl);
static int basl_program_is_interface_public(const basl_interface_decl_t *decl);
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
    if (type_kind == BASL_TYPE_I32 || type_kind == BASL_TYPE_BOOL) {
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

    return basl_compile_report(program, token->span, unsupported_message);
}

static basl_status_t basl_program_parse_type_reference(
    const basl_program_state_t *program,
    size_t *cursor,
    const char *unsupported_message,
    basl_parser_type_t *out_type
) {
    const basl_token_t *token;
    const basl_token_t *next_token;
    const basl_token_t *member_token;
    const char *name_text;
    const char *member_text;
    size_t name_length;
    size_t member_length;
    size_t object_index;
    basl_source_id_t source_id;

    token = basl_program_token_at(program, *cursor);
    if (token == NULL || token->kind != BASL_TOKEN_IDENTIFIER) {
        return basl_compile_report(
            program,
            token == NULL ? basl_program_eof_span(program) : token->span,
            unsupported_message
        );
    }

    next_token = basl_program_token_at(program, *cursor + 1U);
    if (next_token == NULL || next_token->kind != BASL_TOKEN_DOT) {
        basl_status_t status;

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

    name_text = basl_program_token_text(program, token, &name_length);
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
    if (out_type->kind == BASL_TYPE_I32 || out_type->kind == BASL_TYPE_BOOL) {
        return BASL_STATUS_OK;
    }

    return basl_compile_report(
        program,
        type_token == NULL ? basl_program_eof_span(program) : type_token->span,
        unsupported_message
    );
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

    if (!basl_parser_type_equal(decl->return_type, basl_binding_type_primitive(BASL_TYPE_I32))) {
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

static basl_status_t basl_program_parse_constant_primary(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *token;
    const basl_global_constant_t *constant;
    const basl_token_t *member_token;
    basl_source_id_t source_id;
    const char *name_text;
    const char *member_text;
    size_t name_length;
    size_t member_length;

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
        case BASL_TOKEN_IDENTIFIER:
            basl_program_cursor_advance(program, cursor);
            name_text = basl_program_token_text(program, token, &name_length);
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
            if (!basl_parser_type_equal(operand.type, basl_binding_type_primitive(BASL_TYPE_I32))) {
                basl_constant_result_release(&operand);
                return basl_compile_report(
                    program,
                    token->span,
                    "unary '-' requires an i32 operand"
                );
            }
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
        if (
            !basl_parser_type_equal(left.type, basl_binding_type_primitive(BASL_TYPE_I32)) ||
            !basl_parser_type_equal(right.type, basl_binding_type_primitive(BASL_TYPE_I32))
        ) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "integer operands are required"
            );
        }

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
        if (
            !basl_parser_type_equal(left.type, basl_binding_type_primitive(BASL_TYPE_I32)) ||
            !basl_parser_type_equal(right.type, basl_binding_type_primitive(BASL_TYPE_I32))
        ) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(program, token->span, "integer operands are required");
        }

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
            !basl_parser_type_equal(left.type, basl_binding_type_primitive(BASL_TYPE_I32)) ||
            !basl_parser_type_equal(right.type, basl_binding_type_primitive(BASL_TYPE_I32))
        ) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(program, token->span, "integer operands are required");
        }

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

    status = basl_program_parse_type_reference(
        program,
        cursor,
        "unsupported global variable type",
        &declared_type
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    type_token = basl_program_cursor_peek(program, *cursor);
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

        status = basl_program_parse_type_reference(
            program,
            cursor,
            "unsupported function return type",
            &method->return_type
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
                !basl_parser_type_equal(method_decl->return_type, interface_method->return_type) ||
                method_decl->param_count != interface_method->param_count + 1U
            ) {
                return basl_compile_report(
                    program,
                    class_method->name_span,
                    "class method signature does not match interface"
                );
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

        if (type_token->kind == BASL_TOKEN_FN) {
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

            status = basl_program_parse_type_reference(
                program,
                cursor,
                "unsupported function return type",
                &method_decl->return_type
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

    return basl_program_validate_class_interface_conformance(program, decl);
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
                "expected top-level 'import', 'const', 'interface', 'class', variable declaration, or 'fn'"
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
                status = basl_program_parse_type_reference(
                    program,
                    &cursor,
                    "unsupported function parameter type",
                    &decl->return_type
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
                    decl->return_type,
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
            status = basl_program_parse_type_reference(
                program,
                &cursor,
                "main entrypoint must declare return type i32",
                &decl->return_type
            );
            if (status != BASL_STATUS_OK) {
                return basl_program_fail_partial_decl(program, decl, status);
            }
            status = basl_program_validate_main_signature(program, decl, type_token);
            if (status != BASL_STATUS_OK) {
                return basl_program_fail_partial_decl(program, decl, status);
            }
        } else {
            status = basl_program_parse_type_reference(
                program,
                &cursor,
                "unsupported function return type",
                &decl->return_type
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
    basl_program_set_module_context(program, source, &program->modules[module_index].tokens);
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
        basl_token_list_free(&program->modules[i].tokens);
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

static const basl_token_t *basl_parser_peek_next(const basl_parser_state_t *state) {
    if (state == NULL || state->current + 1U >= state->body_end) {
        return NULL;
    }

    return basl_program_token_at(state->program, state->current + 1U);
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

static basl_status_t basl_parser_lookup_global_constant(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    const basl_global_constant_t **out_constant
) {
    const char *name_text;
    size_t name_length;

    name_text = basl_parser_token_text(state, name_token, &name_length);
    if (basl_program_find_constant(state->program, name_text, name_length, out_constant)) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, name_token->span, "unknown local variable");
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

    basl_expression_result_clear(&arg_result);

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

    status = basl_parser_emit_opcode(state, BASL_OPCODE_CALL, call_span);
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

    basl_expression_result_set_type(out_result, decl->return_type);
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
    size_t arg_count;

    basl_expression_result_clear(&arg_result);

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
            if (arg_count >= decl->field_count) {
                return basl_parser_report(
                    state,
                    call_span,
                    "constructor argument count does not match class fields"
                );
            }
            status = basl_parser_require_type(
                state,
                call_span,
                arg_result.type,
                decl->fields[arg_count].type,
                "constructor argument type does not match field type"
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

    if (
        arg_count != decl->field_count ||
        arg_count > UINT32_MAX ||
        class_index > UINT32_MAX
    ) {
        return basl_parser_report(
            state,
            call_span,
            "constructor argument count does not match class fields"
        );
    }

    status = basl_parser_emit_opcode(state, BASL_OPCODE_NEW_INSTANCE, call_span);
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

    basl_expression_result_set_type(out_result, basl_binding_type_class(class_index));
    return BASL_STATUS_OK;
}

static basl_status_t basl_parser_parse_qualified_symbol(
    basl_parser_state_t *state,
    const basl_token_t *module_token,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *member_token;
    const basl_global_constant_t *constant;
    const basl_function_decl_t *function_decl;
    const basl_class_decl_t *class_decl;
    const char *module_name;
    const char *member_name;
    size_t module_name_length;
    size_t member_name_length;
    size_t function_index;
    size_t class_index;
    basl_source_id_t source_id;

    constant = NULL;
    function_decl = NULL;
    class_decl = NULL;
    function_index = 0U;
    class_index = 0U;
    source_id = 0U;

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

    decl = basl_binding_function_table_get(&state->program->functions, method->function_index);
    if (decl == NULL) {
        return basl_parser_report(state, method_token->span, "unknown class method");
    }

    basl_expression_result_clear(&arg_result);
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

    status = basl_parser_emit_opcode(state, BASL_OPCODE_CALL, method_token->span);
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

    basl_expression_result_set_type(out_result, decl->return_type);
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

    basl_expression_result_clear(&arg_result);
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

    status = basl_parser_emit_opcode(state, BASL_OPCODE_CALL_INTERFACE, method_token->span);
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

    basl_expression_result_set_type(out_result, method->return_type);
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
    size_t field_index;
    size_t method_index;

    while (basl_parser_match(state, BASL_TOKEN_DOT)) {
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
        method_index = 0U;
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
        if (basl_parser_check(state, BASL_TOKEN_LPAREN) && basl_parser_type_is_class(out_result->type)) {
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
    }

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
    size_t global_index;
    basl_parser_type_t local_type;
    const basl_global_variable_t *global_decl;
    const char *name_text;
    size_t name_length;
    basl_source_id_t source_id;

    constant = NULL;
    local_index = 0U;
    global_index = 0U;
    local_type = basl_binding_type_invalid();
    global_decl = NULL;
    name_text = NULL;
    name_length = 0U;
    source_id = 0U;
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
        case BASL_TOKEN_IDENTIFIER:
            basl_parser_advance(state);
            name_text = basl_parser_token_text(state, token, &name_length);
            if (
                basl_parser_check(state, BASL_TOKEN_DOT) &&
                !basl_parser_find_local_symbol(state, token, &local_index) &&
                basl_program_resolve_import_alias(
                    state->program,
                    name_text,
                    name_length,
                    &source_id
                )
            ) {
                return basl_parser_parse_qualified_symbol(state, token, out_result);
            }
            if (basl_parser_check(state, BASL_TOKEN_LPAREN)) {
                if (basl_program_find_function_symbol(state->program, token, NULL, NULL)) {
                    return basl_parser_parse_call(state, token, out_result);
                }
                if (basl_program_find_class_symbol(state->program, token, NULL, NULL)) {
                    return basl_parser_parse_constructor(state, token, out_result);
                }
                return basl_parser_report(state, token->span, "unknown function");
            }

            if (basl_parser_find_local_symbol(state, token, &local_index)) {
                local_type =
                    basl_binding_scope_stack_local_at(&state->locals, local_index)->type;
                basl_expression_result_set_type(out_result, local_type);
                status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_LOCAL, token->span);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                return basl_parser_emit_u32(state, (uint32_t)local_index, token->span);
            }

            if (basl_program_find_global(state->program, name_text, name_length, &global_index, &global_decl)) {
                basl_expression_result_set_type(out_result, global_decl->type);
                status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_GLOBAL, token->span);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                return basl_parser_emit_u32(state, (uint32_t)global_index, token->span);
            }

            status = basl_parser_lookup_global_constant(state, token, &constant);
            if (status != BASL_STATUS_OK || constant == NULL) {
                return status;
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
            return basl_parser_report(state, token->span, "string expressions are not yet supported");
        case BASL_TOKEN_FLOAT_LITERAL:
            return basl_parser_report(state, token->span, "float expressions are not yet supported");
        case BASL_TOKEN_FSTRING_LITERAL:
            return basl_parser_report(state, token->span, "f-strings are not yet supported");
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
         operator_token->kind == BASL_TOKEN_BANG)) {
        basl_parser_advance(state);
        status = basl_parser_parse_unary(state, &operand_result);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        if (operator_token->kind == BASL_TOKEN_MINUS) {
            status = basl_parser_require_unary_operator(
                state,
                operator_token->span,
                BASL_UNARY_OPERATOR_NEGATE,
                operand_result.type,
                "unary '-' requires an i32 operand"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, basl_binding_type_primitive(BASL_TYPE_I32));
            return basl_parser_emit_opcode(state, BASL_OPCODE_NEGATE, operator_token->span);
        }

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
        basl_expression_result_set_type(out_result, basl_binding_type_primitive(BASL_TYPE_BOOL));
        return basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_token->span);
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
        status = basl_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            operator_kind == BASL_TOKEN_STAR
                ? BASL_BINARY_OPERATOR_MULTIPLY
                : (operator_kind == BASL_TOKEN_SLASH
                       ? BASL_BINARY_OPERATOR_DIVIDE
                       : BASL_BINARY_OPERATOR_MODULO),
            "arithmetic operators require i32 operands"
        );
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
        left_result.type = basl_binding_type_primitive(BASL_TYPE_I32);
    }

    basl_expression_result_set_type(out_result, left_result.type);
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
        status = basl_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            operator_kind == BASL_TOKEN_PLUS
                ? BASL_BINARY_OPERATOR_ADD
                : BASL_BINARY_OPERATOR_SUBTRACT,
            "arithmetic operators require i32 operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_opcode(
            state,
            operator_kind == BASL_TOKEN_PLUS ? BASL_OPCODE_ADD : BASL_OPCODE_SUBTRACT,
            operator_span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        left_result.type = basl_binding_type_primitive(BASL_TYPE_I32);
    }

    basl_expression_result_set_type(out_result, left_result.type);
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

    basl_expression_result_set_type(out_result, left_result.type);
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
        status = basl_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            operator_kind == BASL_TOKEN_GREATER
                ? BASL_BINARY_OPERATOR_GREATER
                : (operator_kind == BASL_TOKEN_GREATER_EQUAL
                       ? BASL_BINARY_OPERATOR_GREATER_EQUAL
                       : (operator_kind == BASL_TOKEN_LESS
                              ? BASL_BINARY_OPERATOR_LESS
                              : BASL_BINARY_OPERATOR_LESS_EQUAL)),
            "comparison operators require i32 operands"
        );
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

    basl_expression_result_set_type(out_result, left_result.type);
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

    basl_expression_result_set_type(out_result, left_result.type);
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

    basl_expression_result_set_type(out_result, left_result.type);
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

    basl_expression_result_set_type(out_result, left_result.type);
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

    basl_expression_result_set_type(out_result, left_result.type);
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

    basl_expression_result_set_type(out_result, left_result.type);
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

    basl_expression_result_set_type(out_result, left_result.type);
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
        basl_expression_result_set_type(out_result, condition_result.type);
        return BASL_STATUS_OK;
    }

    question_token = basl_parser_advance(state);
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

    basl_expression_result_set_type(out_result, then_result.type);
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
    basl_expression_result_t return_result;

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

    status = basl_parser_parse_expression(state, &return_result);
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

    basl_statement_result_set_guaranteed_return(out_result, 1);
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

static basl_status_t basl_parser_parse_for_statement(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *for_token;
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

    status = basl_parser_expect(state, BASL_TOKEN_FOR, "expected 'for'", &for_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }
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
    while (token != NULL && token->kind == BASL_TOKEN_DOT) {
        cursor += 1U;
        token = basl_program_token_at(state->program, cursor);
        if (token == NULL || token->kind != BASL_TOKEN_IDENTIFIER) {
            return 0;
        }
        cursor += 1U;
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
    size_t global_index;
    size_t field_index;
    basl_parser_type_t local_type;
    basl_parser_type_t target_type;
    basl_expression_result_t value_result;
    const basl_class_field_t *field;
    const basl_global_variable_t *global_decl;
    int is_field_assignment;
    int is_global_assignment;

    local_index = 0U;
    global_index = 0U;
    field_index = 0U;
    local_type = basl_binding_type_invalid();
    target_type = basl_binding_type_invalid();
    field = NULL;
    global_decl = NULL;
    is_field_assignment = 0;
    is_global_assignment = 0;
    basl_expression_result_clear(&value_result);

    status = basl_parser_expect(
        state,
        BASL_TOKEN_IDENTIFIER,
        "expected local variable name",
        &name_token
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (basl_parser_find_local_symbol(state, name_token, &local_index)) {
        local_type = basl_binding_scope_stack_local_at(&state->locals, local_index)->type;
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
    if (basl_parser_match(state, BASL_TOKEN_DOT)) {
        is_field_assignment = 1;
        status = basl_parser_emit_opcode(
            state,
            is_global_assignment ? BASL_OPCODE_GET_GLOBAL : BASL_OPCODE_GET_LOCAL,
            name_token->span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(
            state,
            (uint32_t)(is_global_assignment ? global_index : local_index),
            name_token->span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        while (1) {
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
            if (operator_token != NULL && basl_parser_is_assignment_operator(operator_token->kind)) {
                break;
            }
            if (!basl_parser_match(state, BASL_TOKEN_DOT)) {
                return basl_parser_report(state, field_token->span, "expected '=' in assignment");
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_FIELD, field_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_u32(state, (uint32_t)field_index, field_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
        }
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
        status = basl_parser_require_type(
            state,
            name_token->span,
            value_result.type,
            target_type,
            is_field_assignment
                ? "assigned expression type does not match field type"
                : (is_global_assignment
                       ? "assigned expression type does not match global variable type"
                       : "assigned expression type does not match local variable type")
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        basl_opcode_t opcode;
        basl_binary_operator_kind_t operator_kind;

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
            status = basl_parser_emit_opcode(
                state,
                is_global_assignment ? BASL_OPCODE_GET_GLOBAL : BASL_OPCODE_GET_LOCAL,
                operator_token->span
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_u32(
                state,
                (uint32_t)(is_global_assignment ? global_index : local_index),
                operator_token->span
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
        }

        if (
            operator_token->kind == BASL_TOKEN_PLUS_PLUS ||
            operator_token->kind == BASL_TOKEN_MINUS_MINUS
        ) {
            status = basl_parser_require_i32_operands(
                state,
                operator_token->span,
                target_type,
                basl_binding_type_primitive(BASL_TYPE_I32),
                BASL_BINARY_OPERATOR_ADD,
                "increment and decrement require an i32 target"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_i32_constant(state, 1, operator_token->span);
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

            status = basl_parser_require_i32_operands(
                state,
                operator_token->span,
                target_type,
                value_result.type,
                operator_kind,
                "compound assignment requires i32 operands"
            );
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
    } else {
        status = basl_parser_emit_opcode(
            state,
            is_global_assignment ? BASL_OPCODE_SET_GLOBAL : BASL_OPCODE_SET_LOCAL,
            name_token->span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(
            state,
            (uint32_t)(is_global_assignment ? global_index : local_index),
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

    last_token = basl_parser_previous(state);
    if (expect_semicolon) {
        status = basl_parser_expect(state, BASL_TOKEN_SEMICOLON, "expected ';' after expression", NULL);
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    status = basl_parser_emit_opcode(
        state,
        BASL_OPCODE_POP,
        last_token == NULL ? basl_parser_fallback_span(state) : last_token->span
    );
    if (status != BASL_STATUS_OK) {
        return status;
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
    if (basl_parser_check(state, BASL_TOKEN_IF)) {
        return basl_parser_parse_if_statement(state, out_result);
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
    const basl_token_t *name_token;
    basl_parser_type_t declared_type;
    basl_expression_result_t initializer_result;

    basl_expression_result_clear(&initializer_result);

    status = basl_program_parse_type_reference(
        state->program,
        &state->current,
        "unsupported local variable type",
        &declared_type
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_expect(state, BASL_TOKEN_IDENTIFIER, "expected local variable name", &name_token);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (!basl_parser_match(state, BASL_TOKEN_ASSIGN)) {
        return basl_parser_report(
            state,
            name_token->span,
            "variables must be initialized at declaration"
        );
    }

    status = basl_parser_parse_expression(state, &initializer_result);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    status = basl_parser_require_type(
        state,
        name_token->span,
        initializer_result.type,
        declared_type,
        "initializer type does not match local variable type"
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_expect(state, BASL_TOKEN_SEMICOLON, "expected ';' after local declaration", NULL);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    status = basl_parser_declare_local_symbol(state, name_token, declared_type, NULL);
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
    const basl_token_t *next_token;
    const basl_token_t *third_token;
    const basl_token_t *fourth_token;

    token = basl_parser_peek(state);
    next_token = basl_parser_peek_next(state);
    if (
        token != NULL &&
        next_token != NULL &&
        token->kind == BASL_TOKEN_IDENTIFIER &&
        next_token->kind == BASL_TOKEN_IDENTIFIER
    ) {
        return 1;
    }

    third_token = basl_program_token_at(state->program, state->current + 2U);
    fourth_token = basl_program_token_at(state->program, state->current + 3U);
    return token != NULL &&
           next_token != NULL &&
           third_token != NULL &&
           fourth_token != NULL &&
           token->kind == BASL_TOKEN_IDENTIFIER &&
           next_token->kind == BASL_TOKEN_DOT &&
           third_token->kind == BASL_TOKEN_IDENTIFIER &&
           fourth_token->kind == BASL_TOKEN_IDENTIFIER;
}

static basl_status_t basl_parser_parse_declaration(
    basl_parser_state_t *state,
    basl_statement_result_t *out_result
) {
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

static basl_status_t basl_compile_require_function_returns(
    basl_program_state_t *program,
    const basl_function_decl_t *decl,
    size_t function_index,
    int guaranteed_return
) {
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

static basl_status_t basl_compile_function(
    basl_program_state_t *program,
    size_t function_index
) {
    basl_status_t status;
    basl_parser_state_t state;
    basl_function_decl_t *decl;
    basl_object_t *object;
    basl_statement_result_t body_result;

    decl = &program->functions.functions[function_index];
    memset(&state, 0, sizeof(state));
    basl_program_set_module_context(program, decl->source, decl->tokens);
    state.program = program;
    state.current = decl->body_start;
    state.body_end = decl->body_end;
    state.function_index = function_index;
    state.expected_return_type = decl->return_type;
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
