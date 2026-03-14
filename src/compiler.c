#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basl/chunk.h"
#include "basl/lexer.h"
#include "basl/native_module.h"
#include "basl/string.h"
#include "basl/token.h"
#include "basl/type.h"
#include "internal/basl_binding.h"
#include "internal/basl_compiler_internal.h"
#include "internal/basl_internal.h"
#include "internal/basl_compiler_types.h"
#include "internal/basl_nanbox.h"

static int basl_parser_is_assignment_start(
    const basl_parser_state_t *state
);
basl_status_t basl_parser_report(
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
basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
);
static basl_status_t basl_parser_parse_expression_with_expected_type(
    basl_parser_state_t *state,
    basl_parser_type_t expected_type,
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
basl_status_t basl_program_require_non_void_type(
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
basl_status_t basl_parser_emit_f64_constant(
    basl_parser_state_t *state,
    double value,
    basl_source_span_t span
);
basl_status_t basl_parser_emit_string_constant_text(
    basl_parser_state_t *state,
    basl_source_span_t span,
    const char *text,
    size_t length
);
basl_status_t basl_parser_emit_ok_constant(
    basl_parser_state_t *state,
    basl_source_span_t span
);
static basl_status_t basl_parser_emit_integer_cast(
    basl_parser_state_t *state,
    basl_parser_type_t target_type,
    basl_source_span_t span
);
basl_status_t basl_parser_emit_integer_constant(
    basl_parser_state_t *state,
    basl_parser_type_t target_type,
    int64_t value,
    basl_source_span_t span
);
static basl_status_t basl_compile_function_with_parent(
    basl_program_state_t *program,
    size_t function_index,
    const basl_parser_state_t *parent_state
);
const basl_token_t *basl_parser_peek(const basl_parser_state_t *state);
int basl_parser_check(
    const basl_parser_state_t *state,
    basl_token_kind_t kind
);
const char *basl_parser_token_text(
    const basl_parser_state_t *state,
    const basl_token_t *token,
    size_t *out_length
);
basl_status_t basl_parser_emit_opcode(
    basl_parser_state_t *state,
    basl_opcode_t opcode,
    basl_source_span_t span
);
basl_status_t basl_parser_emit_u32(
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

void basl_expression_result_clear(
    basl_expression_result_t *result
) {
    if (result == NULL) {
        return;
    }

    result->type = basl_binding_type_invalid();
    result->types = NULL;
    result->type_count = 0U;
    result->owned_types[0] = basl_binding_type_invalid();
    result->owned_types[1] = basl_binding_type_invalid();
}

void basl_expression_result_set_type(
    basl_expression_result_t *result,
    basl_parser_type_t type
) {
    if (result == NULL) {
        return;
    }

    result->type = type;
    result->types = NULL;
    result->type_count = basl_binding_type_is_valid(type) ? 1U : 0U;
    result->owned_types[0] = basl_binding_type_invalid();
    result->owned_types[1] = basl_binding_type_invalid();
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
    result->owned_types[0] = basl_binding_type_invalid();
    result->owned_types[1] = basl_binding_type_invalid();
}

void basl_expression_result_set_pair(
    basl_expression_result_t *result,
    basl_parser_type_t first_type,
    basl_parser_type_t second_type
) {
    if (result == NULL) {
        return;
    }

    result->type = first_type;
    result->owned_types[0] = first_type;
    result->owned_types[1] = second_type;
    result->types = result->owned_types;
    result->type_count = 2U;
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
    if (source->types == source->owned_types && source->type_count == 2U) {
        basl_expression_result_set_pair(
            result,
            source->owned_types[0],
            source->owned_types[1]
        );
    }
}

basl_status_t basl_parser_require_scalar_expression(
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

void basl_constant_result_clear(
    basl_constant_result_t *result
) {
    if (result == NULL) {
        return;
    }

    result->type = basl_binding_type_invalid();
    basl_value_init_nil(&result->value);
}

void basl_constant_result_release(
    basl_constant_result_t *result
) {
    if (result == NULL) {
        return;
    }

    basl_value_release(&result->value);
    result->type = basl_binding_type_invalid();
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

basl_status_t basl_compile_report(
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

const basl_token_t *basl_program_token_at(
    const basl_program_state_t *program,
    size_t index
) {
    return basl_token_list_get(program->tokens, index);
}

int basl_program_names_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
);

const char *basl_program_token_text(
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


basl_source_span_t basl_program_eof_span(const basl_program_state_t *program) {
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

int basl_program_names_equal(
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

int basl_program_resolve_import_alias(
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

int basl_program_find_class_in_source(
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

int basl_program_find_interface_in_source(
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

int basl_program_find_enum_in_source(
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

int basl_enum_decl_find_member(
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

basl_parser_type_t basl_program_array_type_element(
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

basl_parser_type_t basl_program_map_type_key(
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

basl_parser_type_t basl_program_map_type_value(
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

int basl_class_decl_find_field(
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

int basl_class_decl_find_method(
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



int basl_interface_decl_find_method(
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

int basl_program_find_constant_in_source(
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

int basl_program_find_global_in_source(
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

static basl_status_t basl_program_checked_uadd(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (UINT64_MAX - left < right) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left + right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_usubtract(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (left < right) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left - right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_umultiply(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (left != 0U && right > UINT64_MAX / left) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left * right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_udivide(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (right == 0U) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left / right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_umodulo(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (right == 0U) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left % right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_ushift_left(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (right >= 64U) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left << (uint32_t)right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_checked_ushift_right(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (right >= 64U) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left >> (uint32_t)right;
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
    basl_value_kind_t lk;
    basl_value_kind_t rk;

    if (left == NULL || right == NULL) {
        return 0;
    }

    lk = basl_value_kind(left);
    rk = basl_value_kind(right);
    if (lk != rk) {
        return 0;
    }

    switch (lk) {
        case BASL_VALUE_NIL:
            return 1;
        case BASL_VALUE_BOOL:
            return basl_value_as_bool(left) == basl_value_as_bool(right);
        case BASL_VALUE_INT:
            return basl_value_as_int(left) == basl_value_as_int(right);
        case BASL_VALUE_UINT:
            return basl_value_as_uint(left) == basl_value_as_uint(right);
        case BASL_VALUE_FLOAT:
            return basl_value_as_float(left) == basl_value_as_float(right);
        case BASL_VALUE_OBJECT:
            left_object = basl_value_as_object(left);
            right_object = basl_value_as_object(right);
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

basl_status_t basl_parser_emit_string_constant_text(
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


int basl_program_is_class_public(const basl_class_decl_t *decl);
int basl_program_is_interface_public(const basl_interface_decl_t *decl);
int basl_program_is_enum_public(const basl_enum_decl_t *decl);
int basl_program_is_function_public(const basl_function_decl_t *decl);
int basl_program_is_constant_public(const basl_global_constant_t *decl);
int basl_program_is_global_public(const basl_global_variable_t *decl);


basl_status_t basl_program_require_non_void_type(
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

basl_status_t basl_program_parse_function_return_types(
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


int basl_program_is_class_public(
    const basl_class_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

int basl_program_is_interface_public(
    const basl_interface_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

int basl_program_is_enum_public(
    const basl_enum_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

int basl_program_is_function_public(
    const basl_function_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

int basl_program_is_constant_public(
    const basl_global_constant_t *decl
) {
    return decl != NULL && decl->is_public;
}

int basl_program_is_global_public(
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

basl_status_t basl_program_add_param(
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
const basl_token_t *basl_program_cursor_peek(
    const basl_program_state_t *program,
    size_t cursor
);
const basl_token_t *basl_program_cursor_advance(
    const basl_program_state_t *program,
    size_t *cursor
);
int basl_program_find_top_level_function_name_in_source(
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

const basl_token_t *basl_program_cursor_peek(
    const basl_program_state_t *program,
    size_t cursor
) {
    return basl_program_token_at(program, cursor);
}

const basl_token_t *basl_program_cursor_advance(
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

int basl_program_parse_optional_pub(
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
    const basl_token_t *name_token;
    const basl_token_t *assign_token;
    size_t lookahead;

    lookahead = cursor;
    if (!basl_program_skip_type_reference_syntax(program, &lookahead)) {
        return 0;
    }

    name_token = basl_program_token_at(program, lookahead);
    assign_token = basl_program_token_at(program, lookahead + 1U);
    return name_token != NULL &&
           assign_token != NULL &&
           name_token->kind == BASL_TOKEN_IDENTIFIER &&
           assign_token->kind == BASL_TOKEN_ASSIGN;
}

basl_status_t basl_program_parse_constant_expression(
    basl_program_state_t *program,
    size_t *cursor,
    basl_constant_result_t *out_result
);

static int basl_parse_integer_literal_text(
    const char *text,
    size_t length,
    unsigned long long *out_value
) {
    char buffer[128];
    char *digits;
    char *end;
    int base;
    unsigned long long parsed;

    if (text == NULL || out_value == NULL || length == 0U || length >= sizeof(buffer)) {
        return 0;
    }

    memcpy(buffer, text, length);
    buffer[length] = '\0';

    digits = buffer;
    base = 0;
    if (length > 2U && buffer[0] == '0') {
        if (buffer[1] == 'b' || buffer[1] == 'B') {
            digits = buffer + 2;
            base = 2;
        } else if (buffer[1] == 'o' || buffer[1] == 'O') {
            digits = buffer + 2;
            base = 8;
        }
    }
    if (*digits == '\0') {
        return 0;
    }

    errno = 0;
    parsed = strtoull(digits, &end, base);
    if (errno != 0 || end == digits || *end != '\0') {
        return 0;
    }

    *out_value = parsed;
    return 1;
}

static basl_status_t basl_program_parse_constant_int(
    basl_program_state_t *program,
    const basl_token_t *token,
    basl_value_t *out_value
) {
    const char *text;
    size_t length;
    unsigned long long parsed;

    text = basl_program_token_text(program, token, &length);
    if (text == NULL || length == 0U) {
        return basl_compile_report(program, token->span, "invalid integer literal");
    }
    if (!basl_parse_integer_literal_text(text, length, &parsed)) {
        return basl_compile_report(program, token->span, "invalid integer literal");
    }

    if (parsed <= (unsigned long long)INT64_MAX) {
        basl_value_init_int(out_value, (int64_t)parsed);
    } else {
        basl_value_init_uint(out_value, (uint64_t)parsed);
    }
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

static int basl_program_integer_type_signed_bounds(
    basl_type_kind_t kind,
    int64_t *out_minimum,
    int64_t *out_maximum
) {
    int64_t minimum_value;
    int64_t maximum_value;

    switch (kind) {
        case BASL_TYPE_I32:
            minimum_value = (int64_t)INT32_MIN;
            maximum_value = (int64_t)INT32_MAX;
            break;
        case BASL_TYPE_I64:
            minimum_value = INT64_MIN;
            maximum_value = INT64_MAX;
            break;
        default:
            return 0;
    }

    if (out_minimum != NULL) {
        *out_minimum = minimum_value;
    }
    if (out_maximum != NULL) {
        *out_maximum = maximum_value;
    }
    return 1;
}

static int basl_program_integer_type_unsigned_max(
    basl_type_kind_t kind,
    uint64_t *out_maximum
) {
    uint64_t maximum_value;

    switch (kind) {
        case BASL_TYPE_U8:
            maximum_value = (uint64_t)UINT8_MAX;
            break;
        case BASL_TYPE_U32:
            maximum_value = (uint64_t)UINT32_MAX;
            break;
        case BASL_TYPE_U64:
            maximum_value = UINT64_MAX;
            break;
        default:
            return 0;
    }

    if (out_maximum != NULL) {
        *out_maximum = maximum_value;
    }
    return 1;
}

static basl_parser_type_t basl_program_integer_literal_type(
    const basl_value_t *value
) {
    if (value == NULL) {
        return basl_binding_type_invalid();
    }
    if (basl_value_kind(value) == BASL_VALUE_UINT) {
        return basl_binding_type_primitive(BASL_TYPE_U64);
    }
    if (
        basl_value_kind(value) == BASL_VALUE_INT &&
        basl_value_as_int(value) > (int64_t)INT32_MAX
    ) {
        return basl_binding_type_primitive(BASL_TYPE_I64);
    }
    return basl_binding_type_primitive(BASL_TYPE_I32);
}

static basl_status_t basl_program_validate_integer_value_for_type(
    basl_program_state_t *program,
    basl_source_span_t span,
    basl_parser_type_t target_type,
    const basl_value_t *value
) {
    int64_t minimum_value;
    int64_t maximum_value;
    uint64_t maximum_unsigned;

    if (!basl_parser_type_is_integer(target_type) || value == NULL) {
        return BASL_STATUS_OK;
    }

    if (basl_parser_type_is_signed_integer(target_type)) {
        if (basl_value_kind(value) == BASL_VALUE_UINT) {
            return basl_compile_report(
                program,
                span,
                "integer arithmetic overflow or invalid operation"
            );
        }
        if (
            !basl_program_integer_type_signed_bounds(
                target_type.kind,
                &minimum_value,
                &maximum_value
            )
        ) {
            return BASL_STATUS_INVALID_ARGUMENT;
        }
        if (
            basl_value_as_int(value) < minimum_value ||
            basl_value_as_int(value) > maximum_value
        ) {
            return basl_compile_report(
                program,
                span,
                "integer arithmetic overflow or invalid operation"
            );
        }
        return BASL_STATUS_OK;
    }

    if (
        !basl_program_integer_type_unsigned_max(
            target_type.kind,
            &maximum_unsigned
        )
    ) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (basl_value_kind(value) == BASL_VALUE_INT) {
        if (basl_value_as_int(value) < 0) {
            return basl_compile_report(
                program,
                span,
                "integer arithmetic overflow or invalid operation"
            );
        }
        if ((uint64_t)basl_value_as_int(value) > maximum_unsigned) {
            return basl_compile_report(
                program,
                span,
                "integer arithmetic overflow or invalid operation"
            );
        }
        return BASL_STATUS_OK;
    }
    if (basl_value_kind(value) != BASL_VALUE_UINT ||
        basl_value_as_uint(value) > maximum_unsigned) {
        return basl_compile_report(
            program,
            span,
            "integer arithmetic overflow or invalid operation"
        );
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_program_convert_constant_to_integer(
    basl_program_state_t *program,
    basl_source_span_t span,
    basl_type_kind_t target_kind,
    const basl_constant_result_t *argument,
    basl_constant_result_t *out_result
) {
    int64_t minimum_value;
    int64_t maximum_value;
    uint64_t maximum_unsigned;
    double float_value;

    if (
        program == NULL ||
        argument == NULL ||
        out_result == NULL
    ) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (basl_parser_type_equal(argument->type, basl_binding_type_primitive(target_kind))) {
        out_result->type = basl_binding_type_primitive(target_kind);
        out_result->value = basl_value_copy(&argument->value);
        return BASL_STATUS_OK;
    }

    if (basl_parser_type_is_integer(argument->type)) {
        out_result->type = basl_binding_type_primitive(target_kind);
        if (basl_parser_type_is_signed_integer(out_result->type)) {
            if (
                !basl_program_integer_type_signed_bounds(
                    target_kind,
                    &minimum_value,
                    &maximum_value
                )
            ) {
                return BASL_STATUS_INVALID_ARGUMENT;
            }
            if (basl_value_kind(&argument->value) == BASL_VALUE_UINT) {
                if (basl_value_as_uint(&argument->value) > (uint64_t)maximum_value) {
                    return basl_compile_report(
                        program,
                        span,
                        "integer conversion overflow or invalid value"
                    );
                }
                basl_value_init_int(
                    &out_result->value,
                    (int64_t)basl_value_as_uint(&argument->value)
                );
            } else {
                if (basl_value_as_int(&argument->value) < minimum_value ||
                    basl_value_as_int(&argument->value) > maximum_value) {
                    return basl_compile_report(
                        program,
                        span,
                        "integer conversion overflow or invalid value"
                    );
                }
                basl_value_init_int(&out_result->value, basl_value_as_int(&argument->value));
            }
        } else {
            if (
                !basl_program_integer_type_unsigned_max(
                    target_kind,
                    &maximum_unsigned
                )
            ) {
                return BASL_STATUS_INVALID_ARGUMENT;
            }
            if (basl_value_kind(&argument->value) == BASL_VALUE_INT) {
                if (basl_value_as_int(&argument->value) < 0 ||
                    (uint64_t)basl_value_as_int(&argument->value) > maximum_unsigned) {
                    return basl_compile_report(
                        program,
                        span,
                        "integer conversion overflow or invalid value"
                    );
                }
                basl_value_init_uint(
                    &out_result->value,
                    (uint64_t)basl_value_as_int(&argument->value)
                );
            } else {
                if (basl_value_as_uint(&argument->value) > maximum_unsigned) {
                    return basl_compile_report(
                        program,
                        span,
                        "integer conversion overflow or invalid value"
                    );
                }
                basl_value_init_uint(&out_result->value, basl_value_as_uint(&argument->value));
            }
        }
        return BASL_STATUS_OK;
    }

    if (!basl_parser_type_is_f64(argument->type)) {
        return basl_compile_report(
            program,
            span,
            "integer conversions require an integer or f64 argument"
        );
    }

    float_value = basl_value_as_float(&argument->value);
    out_result->type = basl_binding_type_primitive(target_kind);
    if (basl_parser_type_is_signed_integer(out_result->type)) {
        int64_t integer_value;

        if (
            !basl_program_integer_type_signed_bounds(
                target_kind,
                &minimum_value,
                &maximum_value
            )
        ) {
            return BASL_STATUS_INVALID_ARGUMENT;
        }
        if (!isfinite(float_value) ||
            float_value < (double)INT64_MIN ||
            float_value > (double)INT64_MAX) {
            return basl_compile_report(
                program,
                span,
                "integer conversion overflow or invalid value"
            );
        }

        integer_value = (int64_t)float_value;
        if (integer_value < minimum_value || integer_value > maximum_value) {
            return basl_compile_report(
                program,
                span,
                "integer conversion overflow or invalid value"
            );
        }
        basl_value_init_int(&out_result->value, integer_value);
    } else {
        uint64_t integer_value;

        if (
            !basl_program_integer_type_unsigned_max(
                target_kind,
                &maximum_unsigned
            )
        ) {
            return BASL_STATUS_INVALID_ARGUMENT;
        }
        if (!isfinite(float_value) || float_value < 0.0 || float_value > (double)UINT64_MAX) {
            return basl_compile_report(
                program,
                span,
                "integer conversion overflow or invalid value"
            );
        }

        integer_value = (uint64_t)float_value;
        if (integer_value > maximum_unsigned) {
            return basl_compile_report(
                program,
                span,
                "integer conversion overflow or invalid value"
            );
        }
        basl_value_init_uint(&out_result->value, integer_value);
    }
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_convert_constant_to_string(
    basl_program_state_t *program,
    basl_source_span_t span,
    const basl_constant_result_t *argument,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_object_t *object;
    const char *text;
    char buffer[128];
    int written;

    if (program == NULL || argument == NULL || out_result == NULL) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (basl_parser_type_is_string(argument->type)) {
        out_result->type = basl_binding_type_primitive(BASL_TYPE_STRING);
        out_result->value = basl_value_copy(&argument->value);
        return BASL_STATUS_OK;
    }

    object = NULL;
    if (basl_parser_type_is_bool(argument->type)) {
        text = basl_value_as_bool(&argument->value) ? "true" : "false";
        status = basl_string_object_new_cstr(
            program->registry->runtime,
            text,
            &object,
            program->error
        );
    } else if (basl_parser_type_is_integer(argument->type)) {
        if (basl_value_kind(&argument->value) == BASL_VALUE_UINT) {
            written = snprintf(
                buffer,
                sizeof(buffer),
                "%llu",
                (unsigned long long)basl_value_as_uint(&argument->value)
            );
        } else {
            written = snprintf(
                buffer,
                sizeof(buffer),
                "%lld",
                (long long)basl_value_as_int(&argument->value)
            );
        }
        if (written < 0 || (size_t)written >= sizeof(buffer)) {
            return basl_compile_report(program, span, "failed to format integer constant");
        }
        status = basl_string_object_new(
            program->registry->runtime,
            buffer,
            (size_t)written,
            &object,
            program->error
        );
    } else if (basl_parser_type_is_f64(argument->type)) {
        written = snprintf(buffer, sizeof(buffer), "%.17g", basl_value_as_float(&argument->value));
        if (written < 0 || (size_t)written >= sizeof(buffer)) {
            return basl_compile_report(program, span, "failed to format float constant");
        }
        status = basl_string_object_new(
            program->registry->runtime,
            buffer,
            (size_t)written,
            &object,
            program->error
        );
    } else {
        return basl_compile_report(
            program,
            span,
            "string(...) requires a string, integer, f64, or bool argument"
        );
    }
    if (status != BASL_STATUS_OK) {
        return status;
    }

    basl_value_init_object(&out_result->value, &object);
    basl_object_release(&object);
    out_result->type = basl_binding_type_primitive(BASL_TYPE_STRING);
    return BASL_STATUS_OK;
}

static basl_status_t basl_program_parse_constant_builtin_conversion(
    basl_program_state_t *program,
    size_t *cursor,
    const basl_token_t *name_token,
    basl_type_kind_t target_kind,
    basl_constant_result_t *out_result
) {
    basl_status_t status;
    basl_constant_result_t argument;
    const basl_token_t *token;

    basl_constant_result_clear(&argument);
    token = basl_program_cursor_peek(program, *cursor);
    if (token == NULL || token->kind != BASL_TOKEN_LPAREN) {
        return basl_compile_report(program, name_token->span, "expected '(' after conversion name");
    }
    basl_program_cursor_advance(program, cursor);

    status = basl_program_parse_constant_expression(program, cursor, &argument);
    if (status != BASL_STATUS_OK) {
        return status;
    }
    if (
        basl_program_cursor_peek(program, *cursor) != NULL &&
        basl_program_cursor_peek(program, *cursor)->kind == BASL_TOKEN_COMMA
    ) {
        basl_constant_result_release(&argument);
        return basl_compile_report(
            program,
            name_token->span,
            "built-in conversions accept exactly one argument"
        );
    }
    token = basl_program_cursor_peek(program, *cursor);
    if (token == NULL || token->kind != BASL_TOKEN_RPAREN) {
        basl_constant_result_release(&argument);
        return basl_compile_report(
            program,
            name_token->span,
            "expected ')' after conversion argument"
        );
    }
    basl_program_cursor_advance(program, cursor);

    switch (target_kind) {
        case BASL_TYPE_I32:
        case BASL_TYPE_I64:
        case BASL_TYPE_U8:
        case BASL_TYPE_U32:
        case BASL_TYPE_U64:
            status = basl_program_convert_constant_to_integer(
                program,
                name_token->span,
                target_kind,
                &argument,
                out_result
            );
            break;
        case BASL_TYPE_F64:
            if (basl_parser_type_is_f64(argument.type)) {
                out_result->type = basl_binding_type_primitive(BASL_TYPE_F64);
                out_result->value = basl_value_copy(&argument.value);
                status = BASL_STATUS_OK;
            } else if (basl_parser_type_is_integer(argument.type)) {
                if (basl_value_kind(&argument.value) == BASL_VALUE_UINT) {
                    basl_value_init_float(
                        &out_result->value,
                        (double)basl_value_as_uint(&argument.value)
                    );
                } else {
                    basl_value_init_float(
                        &out_result->value,
                        (double)basl_value_as_int(&argument.value)
                    );
                }
                out_result->type = basl_binding_type_primitive(BASL_TYPE_F64);
                status = BASL_STATUS_OK;
            } else {
                status = basl_compile_report(
                    program,
                    name_token->span,
                    "f64(...) requires an integer or f64 argument"
                );
            }
            break;
        case BASL_TYPE_STRING:
            status = basl_program_convert_constant_to_string(
                program,
                name_token->span,
                &argument,
                out_result
            );
            break;
        case BASL_TYPE_BOOL:
            if (!basl_parser_type_is_bool(argument.type)) {
                status = basl_compile_report(
                    program,
                    name_token->span,
                    "bool(...) requires a bool argument"
                );
            } else {
                out_result->type = basl_binding_type_primitive(BASL_TYPE_BOOL);
                out_result->value = basl_value_copy(&argument.value);
                status = BASL_STATUS_OK;
            }
            break;
        default:
            status = basl_compile_report(
                program,
                name_token->span,
                "unsupported built-in conversion"
            );
            break;
    }

    basl_constant_result_release(&argument);
    return status;
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
            out_result->type = basl_program_integer_literal_type(&out_result->value);
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
            if (
                basl_program_cursor_peek(program, *cursor) != NULL &&
                basl_program_cursor_peek(program, *cursor)->kind == BASL_TOKEN_LPAREN
            ) {
                basl_type_kind_t conversion_kind;

                conversion_kind = basl_type_kind_from_name(name_text, name_length);
                if (
                    conversion_kind == BASL_TYPE_I32 ||
                    conversion_kind == BASL_TYPE_I64 ||
                    conversion_kind == BASL_TYPE_U8 ||
                    conversion_kind == BASL_TYPE_U32 ||
                    conversion_kind == BASL_TYPE_U64 ||
                    conversion_kind == BASL_TYPE_F64 ||
                    conversion_kind == BASL_TYPE_STRING ||
                    conversion_kind == BASL_TYPE_BOOL
                ) {
                    return basl_program_parse_constant_builtin_conversion(
                        program,
                        cursor,
                        token,
                        conversion_kind,
                        out_result
                    );
                }
            }
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
            if (basl_parser_type_is_signed_integer(operand.type)) {
                basl_parser_type_t integer_type;

                integer_type = operand.type;
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
                status = basl_program_validate_integer_value_for_type(
                    program,
                    token->span,
                    integer_type,
                    &(basl_value_t){ basl_nanbox_encode_int(integer_result ) }
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                basl_value_init_int(&out_result->value, integer_result);
                out_result->type = integer_type;
                return BASL_STATUS_OK;
            }
            if (!basl_parser_type_is_f64(operand.type)) {
                basl_constant_result_release(&operand);
                return basl_compile_report(
                    program,
                    token->span,
                    "unary '-' requires a signed integer or f64 operand"
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
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;
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
        if (basl_parser_type_is_integer(left.type) &&
            basl_parser_type_equal(left.type, right.type)) {
            if (basl_parser_type_is_unsigned_integer(left.type)) {
                switch (token->kind) {
                    case BASL_TOKEN_STAR:
                        status = basl_program_checked_umultiply(
                            basl_value_as_uint(&left.value),
                            basl_value_as_uint(&right.value),
                            &uinteger_result
                        );
                        break;
                    case BASL_TOKEN_SLASH:
                        status = basl_program_checked_udivide(
                            basl_value_as_uint(&left.value),
                            basl_value_as_uint(&right.value),
                            &uinteger_result
                        );
                        break;
                    default:
                        status = basl_program_checked_umodulo(
                            basl_value_as_uint(&left.value),
                            basl_value_as_uint(&right.value),
                            &uinteger_result
                        );
                        break;
                }
            } else {
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
            status = basl_program_validate_integer_value_for_type(
                program,
                token->span,
                left.type,
                basl_parser_type_is_unsigned_integer(left.type)
                    ? &(basl_value_t){ basl_nanbox_encode_uint(uinteger_result ) }
                    : &(basl_value_t){ basl_nanbox_encode_int(integer_result ) }
            );
            if (status != BASL_STATUS_OK) {
                basl_constant_result_release(&left);
                basl_constant_result_release(&right);
                return status;
            }

            {
                basl_parser_type_t integer_type = left.type;

                basl_constant_result_release(&left);
                if (basl_parser_type_is_unsigned_integer(integer_type)) {
                    basl_value_init_uint(&left.value, uinteger_result);
                } else {
                    basl_value_init_int(&left.value, integer_result);
                }
                left.type = integer_type;
            }
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
                    ? "modulo requires matching integer operands"
                    : "arithmetic operators require matching integer or f64 operands"
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
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;
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
        if (basl_parser_type_is_integer(left.type) &&
            basl_parser_type_equal(left.type, right.type)) {
            if (basl_parser_type_is_unsigned_integer(left.type)) {
                if (token->kind == BASL_TOKEN_PLUS) {
                    status = basl_program_checked_uadd(
                        basl_value_as_uint(&left.value),
                        basl_value_as_uint(&right.value),
                        &uinteger_result
                    );
                } else {
                    status = basl_program_checked_usubtract(
                        basl_value_as_uint(&left.value),
                        basl_value_as_uint(&right.value),
                        &uinteger_result
                    );
                }
            } else {
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
            status = basl_program_validate_integer_value_for_type(
                program,
                token->span,
                left.type,
                basl_parser_type_is_unsigned_integer(left.type)
                    ? &(basl_value_t){ basl_nanbox_encode_uint(uinteger_result ) }
                    : &(basl_value_t){ basl_nanbox_encode_int(integer_result ) }
            );
            if (status != BASL_STATUS_OK) {
                basl_constant_result_release(&left);
                basl_constant_result_release(&right);
                return status;
            }

            {
                basl_parser_type_t integer_type = left.type;

                basl_constant_result_release(&left);
                if (basl_parser_type_is_unsigned_integer(integer_type)) {
                    basl_value_init_uint(&left.value, uinteger_result);
                } else {
                    basl_value_init_int(&left.value, integer_result);
                }
                left.type = integer_type;
            }
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
                    ? "'+' requires matching integer, f64, or string operands"
                    : "arithmetic operators require matching integer or f64 operands"
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
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

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
        if (!basl_parser_type_is_integer(left.type) ||
            !basl_parser_type_equal(left.type, right.type)) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "shift operators require matching integer operands"
            );
        }

        if (basl_parser_type_is_unsigned_integer(left.type)) {
            if (token->kind == BASL_TOKEN_SHIFT_LEFT) {
                status = basl_program_checked_ushift_left(
                    basl_value_as_uint(&left.value),
                    basl_value_as_uint(&right.value),
                    &uinteger_result
                );
            } else {
                status = basl_program_checked_ushift_right(
                    basl_value_as_uint(&left.value),
                    basl_value_as_uint(&right.value),
                    &uinteger_result
                );
            }
        } else {
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
        status = basl_program_validate_integer_value_for_type(
            program,
            token->span,
            left.type,
            basl_parser_type_is_unsigned_integer(left.type)
                ? &(basl_value_t){ basl_nanbox_encode_uint(uinteger_result ) }
                : &(basl_value_t){ basl_nanbox_encode_int(integer_result ) }
        );
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return status;
        }

        {
            basl_parser_type_t integer_type = left.type;

            basl_constant_result_release(&left);
            if (basl_parser_type_is_unsigned_integer(integer_type)) {
                basl_value_init_uint(&left.value, uinteger_result);
            } else {
                basl_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
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
            (!basl_parser_type_is_integer(left.type) ||
             !basl_parser_type_equal(left.type, right.type)) &&
            (!basl_parser_type_is_f64(left.type) || !basl_parser_type_is_f64(right.type))
        ) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "comparison operators require matching integer or f64 operands"
            );
        }

        if (basl_parser_type_is_integer(left.type)) {
            if (basl_parser_type_is_unsigned_integer(left.type)) {
                switch (token->kind) {
                    case BASL_TOKEN_GREATER:
                        comparison_result =
                            basl_value_as_uint(&left.value) > basl_value_as_uint(&right.value);
                        break;
                    case BASL_TOKEN_GREATER_EQUAL:
                        comparison_result =
                            basl_value_as_uint(&left.value) >= basl_value_as_uint(&right.value);
                        break;
                    case BASL_TOKEN_LESS:
                        comparison_result =
                            basl_value_as_uint(&left.value) < basl_value_as_uint(&right.value);
                        break;
                    default:
                        comparison_result =
                            basl_value_as_uint(&left.value) <= basl_value_as_uint(&right.value);
                        break;
                }
            } else {
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
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

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
        if (!basl_parser_type_is_integer(left.type) ||
            !basl_parser_type_equal(left.type, right.type)) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "bitwise operators require matching integer operands"
            );
        }

        if (basl_parser_type_is_unsigned_integer(left.type)) {
            uinteger_result = basl_value_as_uint(&left.value) & basl_value_as_uint(&right.value);
        } else {
            integer_result = basl_value_as_int(&left.value) & basl_value_as_int(&right.value);
        }
        status = basl_program_validate_integer_value_for_type(
            program,
            token->span,
            left.type,
            basl_parser_type_is_unsigned_integer(left.type)
                ? &(basl_value_t){ basl_nanbox_encode_uint(uinteger_result ) }
                : &(basl_value_t){ basl_nanbox_encode_int(integer_result ) }
        );
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return status;
        }
        {
            basl_parser_type_t integer_type = left.type;

            basl_constant_result_release(&left);
            if (basl_parser_type_is_unsigned_integer(integer_type)) {
                basl_value_init_uint(&left.value, uinteger_result);
            } else {
                basl_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
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
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

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
        if (!basl_parser_type_is_integer(left.type) ||
            !basl_parser_type_equal(left.type, right.type)) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "bitwise operators require matching integer operands"
            );
        }

        if (basl_parser_type_is_unsigned_integer(left.type)) {
            uinteger_result = basl_value_as_uint(&left.value) ^ basl_value_as_uint(&right.value);
        } else {
            integer_result = basl_value_as_int(&left.value) ^ basl_value_as_int(&right.value);
        }
        status = basl_program_validate_integer_value_for_type(
            program,
            token->span,
            left.type,
            basl_parser_type_is_unsigned_integer(left.type)
                ? &(basl_value_t){ basl_nanbox_encode_uint(uinteger_result ) }
                : &(basl_value_t){ basl_nanbox_encode_int(integer_result ) }
        );
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return status;
        }
        {
            basl_parser_type_t integer_type = left.type;

            basl_constant_result_release(&left);
            if (basl_parser_type_is_unsigned_integer(integer_type)) {
                basl_value_init_uint(&left.value, uinteger_result);
            } else {
                basl_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
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
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

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
        if (!basl_parser_type_is_integer(left.type) ||
            !basl_parser_type_equal(left.type, right.type)) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return basl_compile_report(
                program,
                token->span,
                "bitwise operators require matching integer operands"
            );
        }

        if (basl_parser_type_is_unsigned_integer(left.type)) {
            uinteger_result = basl_value_as_uint(&left.value) | basl_value_as_uint(&right.value);
        } else {
            integer_result = basl_value_as_int(&left.value) | basl_value_as_int(&right.value);
        }
        status = basl_program_validate_integer_value_for_type(
            program,
            token->span,
            left.type,
            basl_parser_type_is_unsigned_integer(left.type)
                ? &(basl_value_t){ basl_nanbox_encode_uint(uinteger_result ) }
                : &(basl_value_t){ basl_nanbox_encode_int(integer_result ) }
        );
        if (status != BASL_STATUS_OK) {
            basl_constant_result_release(&left);
            basl_constant_result_release(&right);
            return status;
        }
        {
            basl_parser_type_t integer_type = left.type;

            basl_constant_result_release(&left);
            if (basl_parser_type_is_unsigned_integer(integer_type)) {
                basl_value_init_uint(&left.value, uinteger_result);
            } else {
                basl_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
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

basl_status_t basl_program_parse_constant_expression(
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
            basl_program_find_top_level_function_name_in_source(
                program,
                program->source->id,
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
        if (
            basl_program_find_constant_in_source(
                program,
                program->source->id,
                name_text,
                name_length,
                NULL
            )
        ) {
            return basl_compile_report(
                program,
                name_token->span,
                "function name conflicts with global constant"
            );
        }
        if (
            basl_program_find_global_in_source(
                program,
                program->source->id,
                name_text,
                name_length,
                NULL,
                NULL
            )
        ) {
            return basl_compile_report(
                program,
                name_token->span,
                "function name conflicts with global variable"
            );
        }
        if (
            basl_program_find_class_in_source(
                program,
                program->source->id,
                name_text,
                name_length,
                NULL,
                NULL
            )
        ) {
            return basl_compile_report(
                program,
                name_token->span,
                "function name conflicts with class"
            );
        }
        if (
            basl_program_find_enum_in_source(
                program,
                program->source->id,
                name_text,
                name_length,
                NULL,
                NULL
            )
        ) {
            return basl_compile_report(
                program,
                name_token->span,
                "function name conflicts with enum"
            );
        }
        if (
            basl_program_find_interface_in_source(
                program,
                program->source->id,
                name_text,
                name_length,
                NULL,
                NULL
            )
        ) {
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

const basl_token_t *basl_parser_peek(const basl_parser_state_t *state) {
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

int basl_parser_check(
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

basl_status_t basl_parser_report(
    basl_parser_state_t *state,
    basl_source_span_t span,
    const char *message
) {
    return basl_compile_report(state->program, span, message);
}

basl_status_t basl_parser_require_type(
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

basl_status_t basl_parser_expect(
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

const char *basl_parser_token_text(
    const basl_parser_state_t *state,
    const basl_token_t *token,
    size_t *out_length
) {
    return basl_program_token_text(state->program, token, out_length);
}

static basl_status_t basl_parser_parse_int_literal(
    basl_parser_state_t *state,
    const basl_token_t *token,
    basl_value_t *out_value,
    basl_parser_type_t *out_type
) {
    const char *text;
    size_t length;
    unsigned long long parsed;

    text = basl_parser_token_text(state, token, &length);
    if (text == NULL || length == 0U) {
        return basl_parser_report(state, token->span, "invalid integer literal");
    }
    if (!basl_parse_integer_literal_text(text, length, &parsed)) {
        return basl_parser_report(state, token->span, "invalid integer literal");
    }
    if (parsed <= (unsigned long long)INT64_MAX) {
        basl_value_init_int(out_value, (int64_t)parsed);
    } else {
        basl_value_init_uint(out_value, (uint64_t)parsed);
    }
    if (out_type != NULL) {
        *out_type = basl_program_integer_literal_type(out_value);
    }
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

/*
 * Peephole: try to fuse GET_LOCAL + GET_LOCAL + <i64_op> into a single
 * LOCALS_<op>_I64 superinstruction.  Called just before emitting an i64
 * binary opcode.  If the last 10 emitted bytes are two GET_LOCAL
 * instructions, rewind the chunk and emit the fused opcode with the
 * two local indices as operands.  Returns the fused opcode, or the
 * original opcode if fusion is not possible.
 */
static basl_opcode_t basl_parser_try_fuse_locals_i64(
    basl_parser_state_t *state,
    basl_opcode_t opcode,
    size_t pre_left_size
) {
    uint8_t *code;
    size_t len;
    basl_opcode_t fused;

    len = state->chunk.code.length;
    /* Only fuse if exactly 10 bytes were emitted for the two operands
       (5 bytes each: 1 opcode + 4 u32).  This ensures we don't match
       stale GET_LOCALs from earlier code. */
    if (len < 10U || len - pre_left_size != 10U) {
        return opcode;
    }
    code = state->chunk.code.data;
    if (code[len - 10U] != BASL_OPCODE_GET_LOCAL ||
        code[len - 5U] != BASL_OPCODE_GET_LOCAL) {
        return opcode;
    }

    switch (opcode) {
        case BASL_OPCODE_ADD_I64:           fused = BASL_OPCODE_LOCALS_ADD_I64; break;
        case BASL_OPCODE_SUBTRACT_I64:      fused = BASL_OPCODE_LOCALS_SUBTRACT_I64; break;
        case BASL_OPCODE_MULTIPLY_I64:      fused = BASL_OPCODE_LOCALS_MULTIPLY_I64; break;
        case BASL_OPCODE_MODULO_I64:        fused = BASL_OPCODE_LOCALS_MODULO_I64; break;
        case BASL_OPCODE_LESS_I64:          fused = BASL_OPCODE_LOCALS_LESS_I64; break;
        case BASL_OPCODE_LESS_EQUAL_I64:    fused = BASL_OPCODE_LOCALS_LESS_EQUAL_I64; break;
        case BASL_OPCODE_GREATER_I64:       fused = BASL_OPCODE_LOCALS_GREATER_I64; break;
        case BASL_OPCODE_GREATER_EQUAL_I64: fused = BASL_OPCODE_LOCALS_GREATER_EQUAL_I64; break;
        case BASL_OPCODE_EQUAL_I64:         fused = BASL_OPCODE_LOCALS_EQUAL_I64; break;
        case BASL_OPCODE_NOT_EQUAL_I64:     fused = BASL_OPCODE_LOCALS_NOT_EQUAL_I64; break;
        default: return opcode;
    }

    /* Rewind: remove the two GET_LOCAL opcodes (keep their u32 operands).
       The fused opcode will be emitted by the caller, followed by the
       two u32 operands that are already in the byte stream.
       Layout before: [GET_LOCAL][u32_a][GET_LOCAL][u32_b]  (10 bytes)
       Layout after:  [LOCALS_<op>_I64][u32_a][u32_b]       (9 bytes)
       We need to shift u32_b left by 1 byte (removing the second GET_LOCAL). */
    /* Overwrite first GET_LOCAL with fused opcode */
    code[len - 10U] = (uint8_t)fused;
    /* Shift u32_b left by 1 to remove the second GET_LOCAL byte */
    code[len - 5U] = code[len - 4U];
    code[len - 4U] = code[len - 3U];
    code[len - 3U] = code[len - 2U];
    code[len - 2U] = code[len - 1U];
    /* Shrink chunk by 1 byte (removed second GET_LOCAL opcode) */
    state->chunk.code.length -= 1U;
    /* Also fix the span array length to match */
    if (state->chunk.span_count > 0U) {
        state->chunk.span_count -= 1U;
    }

    /* Return a sentinel so the caller knows NOT to emit the opcode */
    return (basl_opcode_t)255;
}

basl_status_t basl_parser_emit_opcode(
    basl_parser_state_t *state,
    basl_opcode_t opcode,
    basl_source_span_t span
) {
    return basl_chunk_write_opcode(&state->chunk, opcode, span, state->program->error);
}

/* Emit an i64 binary opcode, attempting superinstruction fusion first.
   If the preceding bytecode is GET_LOCAL + GET_LOCAL, rewrites to a
   single LOCALS_<op>_I64 superinstruction and returns OK without
   emitting a separate opcode byte. */
static basl_status_t basl_parser_emit_i64_binop(
    basl_parser_state_t *state,
    basl_opcode_t opcode,
    basl_source_span_t span,
    size_t pre_left_size
) {
    basl_opcode_t result = basl_parser_try_fuse_locals_i64(state, opcode, pre_left_size);
    if (result == (basl_opcode_t)255) {
        return BASL_STATUS_OK; /* fused — opcode already rewritten in place */
    }
    return basl_parser_emit_opcode(state, result, span);
}

/*
 * Map an i64 opcode to its i32 equivalent.
 */
static basl_opcode_t basl_parser_i64_to_i32(basl_opcode_t op) {
    switch (op) {
        case BASL_OPCODE_ADD_I64:           return BASL_OPCODE_ADD_I32;
        case BASL_OPCODE_SUBTRACT_I64:      return BASL_OPCODE_SUBTRACT_I32;
        case BASL_OPCODE_MULTIPLY_I64:      return BASL_OPCODE_MULTIPLY_I32;
        case BASL_OPCODE_DIVIDE_I64:        return BASL_OPCODE_DIVIDE_I32;
        case BASL_OPCODE_MODULO_I64:        return BASL_OPCODE_MODULO_I32;
        case BASL_OPCODE_LESS_I64:          return BASL_OPCODE_LESS_I32;
        case BASL_OPCODE_LESS_EQUAL_I64:    return BASL_OPCODE_LESS_EQUAL_I32;
        case BASL_OPCODE_GREATER_I64:       return BASL_OPCODE_GREATER_I32;
        case BASL_OPCODE_GREATER_EQUAL_I64: return BASL_OPCODE_GREATER_EQUAL_I32;
        case BASL_OPCODE_EQUAL_I64:         return BASL_OPCODE_EQUAL_I32;
        case BASL_OPCODE_NOT_EQUAL_I64:     return BASL_OPCODE_NOT_EQUAL_I32;
        default:                            return op;
    }
}

/*
 * Emit an i32 binary opcode.  Tries LOCALS fusion first (reusing the
 * i64 fusion which avoids push/pop).  If no fusion, emits the i32
 * stack opcode which skips i64 overflow checks.
 */
static basl_status_t basl_parser_emit_i32_binop(
    basl_parser_state_t *state,
    basl_opcode_t i64_opcode,
    basl_source_span_t span,
    size_t pre_left_size
) {
    basl_opcode_t fused = basl_parser_try_fuse_locals_i64(state, i64_opcode, pre_left_size);
    if (fused == (basl_opcode_t)255) {
        return BASL_STATUS_OK; /* LOCALS_*_I64 fusion succeeded */
    }
    /* No fusion — emit the i32 stack opcode. */
    return basl_parser_emit_opcode(state, basl_parser_i64_to_i32(i64_opcode), span);
}

basl_status_t basl_parser_emit_u32(
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

int basl_program_find_top_level_function_name_in_source(
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

basl_status_t basl_parser_parse_expression(
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

basl_status_t basl_parser_emit_ok_constant(
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
        kind != BASL_TYPE_I64 &&
        kind != BASL_TYPE_U8 &&
        kind != BASL_TYPE_U32 &&
        kind != BASL_TYPE_U64 &&
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
            opcode = BASL_OPCODE_TO_I32;
            goto integer_conversion;
        case BASL_TYPE_I64:
            opcode = BASL_OPCODE_TO_I64;
            goto integer_conversion;
        case BASL_TYPE_U8:
            opcode = BASL_OPCODE_TO_U8;
            goto integer_conversion;
        case BASL_TYPE_U32:
            opcode = BASL_OPCODE_TO_U32;
            goto integer_conversion;
        case BASL_TYPE_U64:
            opcode = BASL_OPCODE_TO_U64;
            goto integer_conversion;
        case BASL_TYPE_F64:
            if (basl_parser_type_is_f64(argument_result.type)) {
                needs_opcode = 0;
            } else if (basl_parser_type_is_integer(argument_result.type)) {
                opcode = BASL_OPCODE_TO_F64;
                needs_opcode = 1;
            } else {
                return basl_parser_report(
                    state,
                    name_token->span,
                    "f64(...) requires an integer or f64 argument"
                );
            }
            break;
        case BASL_TYPE_STRING:
            if (basl_parser_type_is_string(argument_result.type)) {
                needs_opcode = 0;
            } else if (
                basl_parser_type_is_integer(argument_result.type) ||
                basl_parser_type_is_f64(argument_result.type) ||
                basl_parser_type_is_bool(argument_result.type)
            ) {
                opcode = BASL_OPCODE_TO_STRING;
                needs_opcode = 1;
            } else {
                return basl_parser_report(
                    state,
                    name_token->span,
                    "string(...) requires a string, integer, f64, or bool argument"
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

integer_conversion:
    if (
        target_kind == BASL_TYPE_I32 ||
        target_kind == BASL_TYPE_I64 ||
        target_kind == BASL_TYPE_U8 ||
        target_kind == BASL_TYPE_U32 ||
        target_kind == BASL_TYPE_U64
    ) {
        if (
            basl_parser_type_equal(
                argument_result.type,
                basl_binding_type_primitive(target_kind)
            )
        ) {
            needs_opcode = 0;
        } else if (
            basl_parser_type_is_integer(argument_result.type) ||
            basl_parser_type_is_f64(argument_result.type)
        ) {
            needs_opcode = 1;
        } else {
            return basl_parser_report(
                state,
                name_token->span,
                "integer conversions require an integer or f64 argument"
            );
        }
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

static int basl_program_find_function_symbol_in_source(
    const basl_program_state_t *program,
    basl_source_id_t source_id,
    const basl_token_t *name_token,
    size_t *out_index,
    const basl_function_decl_t **out_decl
) {
    const char *name_text;
    size_t name_length;

    name_text = basl_program_token_text(program, name_token, &name_length);
    return basl_program_find_top_level_function_name_in_source(
        program,
        source_id,
        name_text,
        name_length,
        out_index,
        out_decl
    );
}

static basl_status_t basl_parser_lookup_function_symbol(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    size_t *out_index,
    const basl_function_decl_t **out_decl
) {
    if (basl_program_find_function_symbol_in_source(
            state->program,
            state->program->source == NULL ? 0U : state->program->source->id,
            name_token,
            out_index,
            out_decl
        )) {
        return BASL_STATUS_OK;
    }

    return basl_parser_report(state, name_token->span, "unknown function");
}

static int basl_program_find_class_symbol_in_source(
    const basl_program_state_t *program,
    basl_source_id_t source_id,
    const basl_token_t *name_token,
    size_t *out_index,
    const basl_class_decl_t **out_decl
) {
    const char *name_text;
    size_t name_length;

    name_text = basl_program_token_text(program, name_token, &name_length);
    return basl_program_find_class_in_source(
        program,
        source_id,
        name_text,
        name_length,
        out_index,
        out_decl
    );
}

static basl_status_t basl_parser_lookup_class_symbol(
    basl_parser_state_t *state,
    const basl_token_t *name_token,
    size_t *out_index,
    const basl_class_decl_t **out_decl
) {
    if (
        basl_program_find_class_symbol_in_source(
            state->program,
            state->program->source == NULL ? 0U : state->program->source->id,
            name_token,
            out_index,
            out_decl
        )
    ) {
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
    const basl_class_field_t *field;
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
    field = NULL;
    if (basl_class_decl_find_field(class_decl, field_name, field_length, out_index, &field)) {
        if (
            !field->is_public &&
            class_decl->source_id != state->program->source->id
        ) {
            return basl_parser_report(state, field_token->span, "class field is not public");
        }
        if (out_field != NULL) {
            *out_field = field;
        }
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
    if (!basl_class_decl_find_field(class_decl, field_name, field_length, out_index, out_field)) {
        return 0;
    }
    if (
        out_field != NULL &&
        *out_field != NULL &&
        !(*out_field)->is_public &&
        class_decl->source_id != state->program->source->id
    ) {
        if (out_index != NULL) {
            *out_index = 0U;
        }
        if (out_field != NULL) {
            *out_field = NULL;
        }
        return 0;
    }
    return 1;
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

basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
);
static basl_status_t basl_parser_parse_expression_with_expected_type(
    basl_parser_state_t *state,
    basl_parser_type_t expected_type,
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
    if (
        basl_parser_check(state, BASL_TOKEN_DOT) &&
        basl_program_find_enum_in_source(
            state->program,
            source_id,
            member_name,
            member_name_length,
            &enum_index,
            NULL
        )
    ) {
        basl_parser_advance(state);
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
                basl_parser_type_is_string(out_result->type)
            ) {
                status = basl_parser_parse_string_method_call(state, field_token, out_result);
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                continue;
            }
            if (
                basl_parser_check(state, BASL_TOKEN_LPAREN) &&
                basl_parser_type_is_array(out_result->type)
            ) {
                status = basl_parser_parse_array_method_call(
                    state,
                    out_result->type,
                    field_token,
                    out_result
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                continue;
            }
            if (
                basl_parser_check(state, BASL_TOKEN_LPAREN) &&
                basl_parser_type_is_map(out_result->type)
            ) {
                status = basl_parser_parse_map_method_call(
                    state,
                    out_result->type,
                    field_token,
                    out_result
                );
                if (status != BASL_STATUS_OK) {
                    return status;
                }
                continue;
            }
            if (
                basl_parser_check(state, BASL_TOKEN_LPAREN) &&
                basl_parser_type_is_class(out_result->type)
            ) {
                const basl_class_decl_t *class_decl;

                class_method = NULL;
                if (basl_parser_find_method_by_name(
                        state,
                        out_result->type,
                        field_token,
                        &method_index,
                        &class_method
                    )) {
                    class_decl = &state->program->classes[out_result->type.object_index];
                    if (
                        class_method != NULL &&
                        !class_method->is_public &&
                        class_decl->source_id != state->program->source->id
                    ) {
                        return basl_parser_report(
                            state,
                            field_token->span,
                            "class method is not public"
                        );
                    }
                    status = basl_parser_parse_method_call(state, field_token, class_method, out_result);
                    if (status != BASL_STATUS_OK) {
                        return status;
                    }
                    continue;
                }
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
            status = basl_parser_parse_int_literal(state, token, &value, &local_type);
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
            basl_expression_result_set_type(out_result, local_type);
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
            (void)basl_program_find_global_in_source(
                state->program,
                state->program->source == NULL ? 0U : state->program->source->id,
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
                if (
                    basl_program_find_function_symbol_in_source(
                        state->program,
                        state->program->source == NULL ? 0U : state->program->source->id,
                        token,
                        NULL,
                        NULL
                    )
                ) {
                    return basl_parser_parse_call(state, token, out_result);
                }
                if (
                    basl_program_find_class_symbol_in_source(
                        state->program,
                        state->program->source == NULL ? 0U : state->program->source->id,
                        token,
                        NULL,
                        NULL
                    )
                ) {
                    return basl_parser_parse_constructor(state, token, out_result);
                }
                return basl_parser_report(state, token->span, "unknown function");
            }
            if (
                basl_parser_check(state, BASL_TOKEN_DOT) &&
                !local_found &&
                (
                    basl_program_names_equal(name_text, name_length, "err", 3U) ||
                    basl_program_find_enum_in_source(
                        state->program,
                        state->program->source == NULL ? 0U : state->program->source->id,
                        name_text,
                        name_length,
                        &enum_index,
                        NULL
                    )
                )
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
                        !basl_program_lookup_enum_member_in_source(
                            state->program,
                            state->program->source == NULL ? 0U : state->program->source->id,
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

            if (
                basl_program_find_function_symbol_in_source(
                    state->program,
                    state->program->source == NULL ? 0U : state->program->source->id,
                    token,
                    &global_index,
                    NULL
                )
            ) {
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
            if (
                !basl_program_find_constant_in_source(
                    state->program,
                    state->program->source == NULL ? 0U : state->program->source->id,
                    name_text,
                    name_length,
                    &constant
                )
            ) {
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
                                "map literal keys must use an integer, bool, string, or enum type"
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
                "unary '-' requires a signed integer or f64 operand"
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            basl_expression_result_set_type(out_result, operand_result.type);
            status = basl_parser_emit_opcode(state, BASL_OPCODE_NEGATE, operator_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            return basl_parser_emit_integer_cast(state, operand_result.type, operator_token->span);
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
            "bitwise '~' requires a signed integer operand"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, operand_result.type);
        status = basl_parser_emit_opcode(state, BASL_OPCODE_BITWISE_NOT, operator_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        return basl_parser_emit_integer_cast(state, operand_result.type, operator_token->span);
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
    size_t pre_left_size;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    pre_left_size = state->chunk.code.length;
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
                "modulo requires matching integer operands"
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
                "arithmetic operators require matching integer or f64 operands"
            );
        } else {
            status = BASL_STATUS_OK;
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }

        if (operator_kind == BASL_TOKEN_STAR) {
            int both_i32 = basl_parser_type_is_i32(left_result.type) &&
                           basl_parser_type_is_i32(right_result.type);
            int both_si = !both_i32 &&
                          basl_parser_type_is_signed_integer(left_result.type) &&
                          basl_parser_type_is_signed_integer(right_result.type);
            status = both_i32
                ? basl_parser_emit_i32_binop(state, BASL_OPCODE_MULTIPLY_I64, operator_span, pre_left_size)
                : both_si
                ? basl_parser_emit_i64_binop(state, BASL_OPCODE_MULTIPLY_I64, operator_span, pre_left_size)
                : basl_parser_emit_opcode(state, BASL_OPCODE_MULTIPLY, operator_span);
        } else if (operator_kind == BASL_TOKEN_SLASH) {
            int both_i32 = basl_parser_type_is_i32(left_result.type) &&
                           basl_parser_type_is_i32(right_result.type);
            int both_si = !both_i32 &&
                          basl_parser_type_is_signed_integer(left_result.type) &&
                          basl_parser_type_is_signed_integer(right_result.type);
            status = both_i32
                ? basl_parser_emit_i32_binop(state, BASL_OPCODE_DIVIDE_I64, operator_span, pre_left_size)
                : both_si
                ? basl_parser_emit_i64_binop(state, BASL_OPCODE_DIVIDE_I64, operator_span, pre_left_size)
                : basl_parser_emit_opcode(state, BASL_OPCODE_DIVIDE, operator_span);
        } else {
            int both_i32 = basl_parser_type_is_i32(left_result.type) &&
                           basl_parser_type_is_i32(right_result.type);
            int both_si = !both_i32 &&
                          basl_parser_type_is_signed_integer(left_result.type) &&
                          basl_parser_type_is_signed_integer(right_result.type);
            status = both_i32
                ? basl_parser_emit_i32_binop(state, BASL_OPCODE_MODULO_I64, operator_span, pre_left_size)
                : both_si
                ? basl_parser_emit_i64_binop(state, BASL_OPCODE_MODULO_I64, operator_span, pre_left_size)
                : basl_parser_emit_opcode(state, BASL_OPCODE_MODULO, operator_span);
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }
        /* i32 opcodes already produce i32 results — skip the cast. */
        if (!basl_parser_type_is_i32(left_result.type) ||
            !basl_parser_type_is_i32(right_result.type)) {
            status = basl_parser_emit_integer_cast(state, left_result.type, operator_span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
        }
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
    size_t pre_left_size;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    pre_left_size = state->chunk.code.length;
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
                    ? "'+' requires matching integer, f64, or string operands"
                    : "arithmetic operators require matching integer or f64 operands"
            );
        }

        {
            int both_i32 = basl_parser_type_is_i32(left_result.type) &&
                           basl_parser_type_is_i32(right_result.type);
            int both_si = !both_i32 &&
                          basl_parser_type_is_signed_integer(left_result.type) &&
                          basl_parser_type_is_signed_integer(right_result.type);
            basl_opcode_t op = operator_kind == BASL_TOKEN_PLUS
                ? ((both_si || both_i32) ? BASL_OPCODE_ADD_I64 : BASL_OPCODE_ADD)
                : ((both_si || both_i32) ? BASL_OPCODE_SUBTRACT_I64 : BASL_OPCODE_SUBTRACT);
            status = both_i32
                ? basl_parser_emit_i32_binop(state, op, operator_span, pre_left_size)
                : (both_si
                    ? basl_parser_emit_i64_binop(state, op, operator_span, pre_left_size)
                    : basl_parser_emit_opcode(state, op, operator_span));
        }
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
        } else if (!(basl_parser_type_is_i32(left_result.type) &&
                     basl_parser_type_is_i32(right_result.type))) {
            status = basl_parser_emit_integer_cast(state, left_result.type, operator_span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
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
            "shift operators require matching integer operands"
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
        status = basl_parser_emit_integer_cast(state, left_result.type, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
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
    size_t pre_left_size;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    pre_left_size = state->chunk.code.length;
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
                "comparison operators require matching integer or f64 operands"
            );
        } else {
            status = BASL_STATUS_OK;
        }
        if (status != BASL_STATUS_OK) {
            return status;
        }

        {
            int both_i32 = basl_parser_type_is_i32(left_result.type) &&
                           basl_parser_type_is_i32(right_result.type);
            int both_signed = !both_i32 &&
                              basl_parser_type_is_signed_integer(left_result.type) &&
                              basl_parser_type_is_signed_integer(right_result.type);
            switch (operator_kind) {
                case BASL_TOKEN_GREATER:
                    status = both_i32
                        ? basl_parser_emit_i32_binop(state, BASL_OPCODE_GREATER_I64, operator_span, pre_left_size)
                        : both_signed
                        ? basl_parser_emit_i64_binop(state, BASL_OPCODE_GREATER_I64, operator_span, pre_left_size)
                        : basl_parser_emit_opcode(state, BASL_OPCODE_GREATER, operator_span);
                    break;
                case BASL_TOKEN_LESS:
                    status = both_i32
                        ? basl_parser_emit_i32_binop(state, BASL_OPCODE_LESS_I64, operator_span, pre_left_size)
                        : both_signed
                        ? basl_parser_emit_i64_binop(state, BASL_OPCODE_LESS_I64, operator_span, pre_left_size)
                        : basl_parser_emit_opcode(state, BASL_OPCODE_LESS, operator_span);
                    break;
                case BASL_TOKEN_GREATER_EQUAL:
                    if (both_i32) {
                        status = basl_parser_emit_i32_binop(state,
                            BASL_OPCODE_GREATER_EQUAL_I64, operator_span, pre_left_size);
                    } else if (both_signed) {
                        status = basl_parser_emit_i64_binop(state,
                            BASL_OPCODE_GREATER_EQUAL_I64, operator_span, pre_left_size);
                    } else {
                        status = basl_parser_emit_opcode(state, BASL_OPCODE_LESS, operator_span);
                        if (status == BASL_STATUS_OK) {
                            status = basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_span);
                        }
                    }
                    break;
                case BASL_TOKEN_LESS_EQUAL:
                    if (both_i32) {
                        status = basl_parser_emit_i32_binop(state,
                            BASL_OPCODE_LESS_EQUAL_I64, operator_span, pre_left_size);
                    } else if (both_signed) {
                        status = basl_parser_emit_i64_binop(state,
                            BASL_OPCODE_LESS_EQUAL_I64, operator_span, pre_left_size);
                    } else {
                        status = basl_parser_emit_opcode(state, BASL_OPCODE_GREATER, operator_span);
                        if (status == BASL_STATUS_OK) {
                            status = basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_span);
                        }
                    }
                    break;
                default:
                    status = BASL_STATUS_INTERNAL;
                    break;
            }
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
    size_t pre_left_size;

    basl_expression_result_clear(&left_result);
    basl_expression_result_clear(&right_result);

    pre_left_size = state->chunk.code.length;
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

        if (basl_parser_type_is_i32(left_result.type) &&
            basl_parser_type_is_i32(right_result.type)) {
            basl_opcode_t eq_op = operator_kind == BASL_TOKEN_BANG_EQUAL
                ? BASL_OPCODE_NOT_EQUAL_I64 : BASL_OPCODE_EQUAL_I64;
            status = basl_parser_emit_i32_binop(state, eq_op, operator_span, pre_left_size);
        } else if (basl_parser_type_is_signed_integer(left_result.type) &&
            basl_parser_type_is_signed_integer(right_result.type)) {
            basl_opcode_t eq_op = operator_kind == BASL_TOKEN_BANG_EQUAL
                ? BASL_OPCODE_NOT_EQUAL_I64 : BASL_OPCODE_EQUAL_I64;
            status = basl_parser_emit_i64_binop(state, eq_op, operator_span, pre_left_size);
        } else {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_EQUAL, operator_span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (operator_kind == BASL_TOKEN_BANG_EQUAL) {
                status = basl_parser_emit_opcode(state, BASL_OPCODE_NOT, operator_span);
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
            "bitwise operators require matching integer operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_opcode(state, BASL_OPCODE_BITWISE_AND, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_integer_cast(state, left_result.type, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
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
            "bitwise operators require matching integer operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_opcode(state, BASL_OPCODE_BITWISE_XOR, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_integer_cast(state, left_result.type, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
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
            "bitwise operators require matching integer operands"
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        status = basl_parser_emit_opcode(state, BASL_OPCODE_BITWISE_OR, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_integer_cast(state, left_result.type, operator_span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
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

basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result
) {
    return basl_parser_parse_ternary(state, out_result);
}

static basl_status_t basl_parser_parse_expression_with_expected_type(
    basl_parser_state_t *state,
    basl_parser_type_t expected_type,
    basl_expression_result_t *out_result
) {
    basl_status_t status;
    const basl_token_t *token;
    const basl_token_t *next_token;

    token = basl_parser_peek(state);
    next_token = basl_program_token_at(state->program, state->current + 1U);
    if (
        token != NULL &&
        token->kind == BASL_TOKEN_LBRACKET &&
        next_token != NULL &&
        next_token->kind == BASL_TOKEN_RBRACKET &&
        basl_parser_type_is_array(expected_type)
    ) {
        if (expected_type.object_index > UINT32_MAX) {
            basl_error_set_literal(
                state->program->error,
                BASL_STATUS_OUT_OF_MEMORY,
                "array literal operand overflow"
            );
            return BASL_STATUS_OUT_OF_MEMORY;
        }
        basl_parser_advance(state);
        basl_parser_advance(state);
        status = basl_parser_emit_opcode(state, BASL_OPCODE_NEW_ARRAY, token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(state, (uint32_t)expected_type.object_index, token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(state, 0U, token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, expected_type);
        return BASL_STATUS_OK;
    }
    if (
        token != NULL &&
        token->kind == BASL_TOKEN_LBRACE &&
        next_token != NULL &&
        next_token->kind == BASL_TOKEN_RBRACE &&
        basl_parser_type_is_map(expected_type)
    ) {
        if (expected_type.object_index > UINT32_MAX) {
            basl_error_set_literal(
                state->program->error,
                BASL_STATUS_OUT_OF_MEMORY,
                "map literal operand overflow"
            );
            return BASL_STATUS_OUT_OF_MEMORY;
        }
        basl_parser_advance(state);
        basl_parser_advance(state);
        status = basl_parser_emit_opcode(state, BASL_OPCODE_NEW_MAP, token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(state, (uint32_t)expected_type.object_index, token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(state, 0U, token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        basl_expression_result_set_type(out_result, expected_type);
        return BASL_STATUS_OK;
    }

    return basl_parser_parse_expression(state, out_result);
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
            status = basl_parser_parse_expression_with_expected_type(
                state,
                state->expected_return_types[return_index],
                &return_result
            );
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
        status = basl_parser_parse_expression_with_expected_type(
            state,
            state->expected_return_type,
            &return_result
        );
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

    /* Peephole: CALL + RETURN 1 → TAIL_CALL when safe.
       Pattern: [CALL(1)][u32 func][u32 argc][RETURN(1)][u32 1] = 14 bytes
       Rewrite: [TAIL_CALL(1)][u32 func][u32 argc] = 9 bytes
       Safe only when: single return value, no defers emitted. */
    if (state->expected_return_count == 1U && !state->defer_emitted) {
        uint8_t *c = state->chunk.code.data;
        size_t len = state->chunk.code.length;
        if (len >= 14U &&
            c[len - 14U] == BASL_OPCODE_CALL &&
            c[len - 5U] == BASL_OPCODE_RETURN) {
            /* Verify RETURN operand is 1. */
            uint32_t ret_count = (uint32_t)c[len - 4U]
                | ((uint32_t)c[len - 3U] << 8U)
                | ((uint32_t)c[len - 2U] << 16U)
                | ((uint32_t)c[len - 1U] << 24U);
            if (ret_count == 1U) {
                c[len - 14U] = BASL_OPCODE_TAIL_CALL;
                state->chunk.code.length = len - 5U;
                if (state->chunk.span_count > len - 5U) {
                    state->chunk.span_count = len - 5U;
                }
            }
        }
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

    status = basl_parser_parse_expression_with_expected_type(
        state,
        targets.count == 1U ? targets.items[0].type : basl_binding_type_invalid(),
        &initializer_result
    );
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

    /* Peephole: rewrite INCREMENT_LOCAL_I32 + LOOP → FORLOOP_I32
       when the condition at loop_start is GET_LOCAL + CONSTANT + <cmp_I32>
       + JUMP_IF_FALSE + POP.  The FORLOOP does increment + compare + branch
       in a single dispatch, jumping back to the body start. */
    {
        uint8_t *c = state->chunk.code.data;
        size_t len = state->chunk.code.length;
        /* Last 11 bytes: INCREMENT_LOCAL_I32(6) + LOOP(5) */
        if (len >= 11U &&
            c[len - 11U] == BASL_OPCODE_INCREMENT_LOCAL_I32 &&
            c[len - 5U] == BASL_OPCODE_LOOP) {
            /* Condition at loop_start: GET_LOCAL(5) + CONSTANT(5) + cmp(1)
               + JUMP_IF_FALSE(5) + POP(1) = 17 bytes */
            size_t cs = loop_start;
            if (cs + 17U <= len &&
                c[cs] == BASL_OPCODE_GET_LOCAL &&
                c[cs + 5U] == BASL_OPCODE_CONSTANT) {
                uint8_t cmp_op = c[cs + 10U];
                uint8_t cmp_type = 255;
                switch ((basl_opcode_t)cmp_op) {
                    case BASL_OPCODE_LESS_I32:          cmp_type = 0; break;
                    case BASL_OPCODE_LESS_EQUAL_I32:    cmp_type = 1; break;
                    case BASL_OPCODE_GREATER_I32:       cmp_type = 2; break;
                    case BASL_OPCODE_GREATER_EQUAL_I32: cmp_type = 3; break;
                    case BASL_OPCODE_NOT_EQUAL_I32:     cmp_type = 4; break;
                    default: break;
                }
                if (cmp_type != 255 &&
                    c[cs + 11U] == BASL_OPCODE_JUMP_IF_FALSE &&
                    c[cs + 16U] == BASL_OPCODE_POP) {
                    /* Verify the GET_LOCAL index matches INCREMENT local. */
                    uint32_t cond_idx = (uint32_t)c[cs + 1U]
                        | ((uint32_t)c[cs + 2U] << 8U)
                        | ((uint32_t)c[cs + 3U] << 16U)
                        | ((uint32_t)c[cs + 4U] << 24U);
                    uint32_t inc_idx = (uint32_t)c[len - 10U]
                        | ((uint32_t)c[len - 9U] << 8U)
                        | ((uint32_t)c[len - 8U] << 16U)
                        | ((uint32_t)c[len - 7U] << 24U);
                    if (cond_idx == inc_idx) {
                        /* Extract constant index and delta. */
                        uint8_t const_idx[4];
                        int8_t delta = (int8_t)c[len - 6U];
                        size_t body_start = cs + 17U;
                        size_t forloop_pos = len - 11U;
                        /* back_offset: from end of FORLOOP to body_start */
                        size_t forloop_end = forloop_pos + 15U;
                        uint32_t back_off = (uint32_t)(forloop_end - body_start);

                        memcpy(const_idx, &c[cs + 6U], 4);

                        /* Write FORLOOP_I32 over INCREMENT+LOOP. */
                        c[forloop_pos] = BASL_OPCODE_FORLOOP_I32;
                        /* local idx (reuse from increment) */
                        /* c[forloop_pos+1..4] already has inc_idx */
                        memcpy(&c[forloop_pos + 1U], &c[len - 10U], 4);
                        c[forloop_pos + 5U] = (uint8_t)delta;
                        memcpy(&c[forloop_pos + 6U], const_idx, 4);
                        c[forloop_pos + 10U] = cmp_type;
                        c[forloop_pos + 11U] = (uint8_t)(back_off & 0xFF);
                        c[forloop_pos + 12U] = (uint8_t)((back_off >> 8U) & 0xFF);
                        c[forloop_pos + 13U] = (uint8_t)((back_off >> 16U) & 0xFF);
                        c[forloop_pos + 14U] = (uint8_t)((back_off >> 24U) & 0xFF);
                        /* New length: forloop_pos + 15 (grew by 4 bytes). */
                        state->chunk.code.length = forloop_pos + 15U;
                    }
                }
            }
        }
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
        !basl_parser_type_is_integer(switch_result.type) &&
        !basl_parser_type_equal(
            switch_result.type,
            basl_binding_type_primitive(BASL_TYPE_BOOL)
        ) &&
        !basl_parser_type_is_enum(switch_result.type)
    ) {
        status = basl_parser_report(
            state,
            switch_token->span,
            "switch expression must be an integer, bool, or enum"
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

basl_status_t basl_parser_emit_f64_constant(
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

static basl_status_t basl_parser_emit_integer_cast(
    basl_parser_state_t *state,
    basl_parser_type_t target_type,
    basl_source_span_t span
) {
    basl_opcode_t opcode;

    if (!basl_parser_type_is_integer(target_type)) {
        return BASL_STATUS_OK;
    }

    if (basl_parser_type_is_i32(target_type)) {
        opcode = BASL_OPCODE_TO_I32;
    } else if (basl_parser_type_is_i64(target_type)) {
        opcode = BASL_OPCODE_TO_I64;
    } else if (basl_parser_type_is_u8(target_type)) {
        opcode = BASL_OPCODE_TO_U8;
    } else if (basl_parser_type_is_u32(target_type)) {
        opcode = BASL_OPCODE_TO_U32;
    } else {
        opcode = BASL_OPCODE_TO_U64;
    }

    return basl_parser_emit_opcode(state, opcode, span);
}

basl_status_t basl_parser_emit_integer_constant(
    basl_parser_state_t *state,
    basl_parser_type_t target_type,
    int64_t value,
    basl_source_span_t span
) {
    basl_status_t status;

    status = basl_parser_emit_i32_constant(state, value, span);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_parser_emit_integer_cast(state, target_type, span);
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
        if (!basl_program_consume_type_close(program, cursor)) {
            return 0;
        }
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
        if (!basl_program_consume_type_close(program, cursor)) {
            return 0;
        }
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
    basl_source_id_t import_source_id;
    basl_parser_type_t local_type;
    basl_parser_type_t target_type;
    basl_expression_result_t value_result;
    basl_expression_result_t index_result;
    const basl_class_field_t *field;
    const basl_binding_local_t *local_decl;
    const basl_global_variable_t *global_decl;
    const basl_token_t *target_token;
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
    import_source_id = 0U;
    local_type = basl_binding_type_invalid();
    target_type = basl_binding_type_invalid();
    field = NULL;
    local_decl = NULL;
    global_decl = NULL;
    target_token = NULL;
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
    target_token = name_token;

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
        if (
            basl_parser_check(state, BASL_TOKEN_DOT) &&
            basl_program_resolve_import_alias(
                state->program,
                name_text,
                name_length,
                &import_source_id
            )
        ) {
            const basl_token_t *member_token;
            const basl_global_constant_t *constant_decl;
            const basl_function_decl_t *function_decl;
            const basl_class_decl_t *class_decl;
            const basl_interface_decl_t *interface_decl;
            const basl_enum_decl_t *enum_decl;
            const char *member_text;
            size_t member_length;
            size_t object_index;
            size_t function_index;

            constant_decl = NULL;
            function_decl = NULL;
            class_decl = NULL;
            interface_decl = NULL;
            enum_decl = NULL;
            object_index = 0U;
            function_index = 0U;

            basl_parser_advance(state);
            status = basl_parser_expect(
                state,
                BASL_TOKEN_IDENTIFIER,
                "expected module member name after '.'",
                &member_token
            );
            if (status != BASL_STATUS_OK) {
                return status;
            }
            target_token = member_token;
            member_text = basl_parser_token_text(state, member_token, &member_length);

            if (
                basl_program_find_global_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
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
                is_global_assignment = 1;
                local_type = global_decl->type;
            } else if (
                basl_program_find_constant_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
                    &constant_decl
                )
            ) {
                if (!basl_program_is_constant_public(constant_decl)) {
                    return basl_parser_report(
                        state,
                        member_token->span,
                        "module member is not public"
                    );
                }
                return basl_parser_report(
                    state,
                    member_token->span,
                    "cannot assign to module constant"
                );
            } else if (
                basl_program_find_top_level_function_name_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
                    &function_index,
                    &function_decl
                )
            ) {
                if (!basl_program_is_function_public(function_decl)) {
                    return basl_parser_report(
                        state,
                        member_token->span,
                        "module member is not public"
                    );
                }
                return basl_parser_report(
                    state,
                    member_token->span,
                    "module member is not assignable"
                );
            } else if (
                basl_program_find_class_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
                    &object_index,
                    &class_decl
                )
            ) {
                if (!basl_program_is_class_public(class_decl)) {
                    return basl_parser_report(
                        state,
                        member_token->span,
                        "module member is not public"
                    );
                }
                return basl_parser_report(
                    state,
                    member_token->span,
                    "module member is not assignable"
                );
            } else if (
                basl_program_find_interface_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
                    &object_index,
                    &interface_decl
                )
            ) {
                if (!basl_program_is_interface_public(interface_decl)) {
                    return basl_parser_report(
                        state,
                        member_token->span,
                        "module member is not public"
                    );
                }
                return basl_parser_report(
                    state,
                    member_token->span,
                    "module member is not assignable"
                );
            } else if (
                basl_program_find_enum_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
                    &object_index,
                    &enum_decl
                )
            ) {
                if (!basl_program_is_enum_public(enum_decl)) {
                    return basl_parser_report(
                        state,
                        member_token->span,
                        "module member is not public"
                    );
                }
                return basl_parser_report(
                    state,
                    member_token->span,
                    "module member is not assignable"
                );
            } else {
                return basl_parser_report(
                    state,
                    member_token->span,
                    "unknown module member"
                );
            }
        } else if (
            !basl_program_find_global_in_source(
                state->program,
                state->program->source == NULL ? 0U : state->program->source->id,
                name_text,
                name_length,
                &global_index,
                &global_decl
            )
        ) {
            return basl_parser_report(state, name_token->span, "unknown local variable");
        }
        if (!is_global_assignment) {
            is_global_assignment = 1;
            local_type = global_decl->type;
        }
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
            target_token->span,
            "cannot assign to const local variable"
        );
    }

    operator_token = basl_parser_peek(state);
    if (operator_token == NULL || !basl_parser_is_assignment_operator(operator_token->kind)) {
        return basl_parser_report(state, target_token->span, "expected assignment operator");
    }
    basl_parser_advance(state);

    if (operator_token->kind == BASL_TOKEN_ASSIGN) {
        status = basl_parser_parse_expression_with_expected_type(
            state,
            target_type,
            &value_result
        );
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
            target_token->span,
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
        } else if (is_index_assignment) {
            status = basl_parser_emit_opcode(state, BASL_OPCODE_DUP_TWO, operator_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            status = basl_parser_emit_opcode(state, BASL_OPCODE_GET_INDEX, operator_token->span);
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
            if (basl_parser_type_is_integer(target_type)) {
                status = basl_parser_emit_integer_constant(
                    state,
                    target_type,
                    1,
                    operator_token->span
                );
            } else if (basl_parser_type_is_f64(target_type)) {
                status = basl_parser_emit_f64_constant(state, 1.0, operator_token->span);
            } else {
                status = basl_parser_report(
                    state,
                    operator_token->span,
                    "increment and decrement require an integer or f64 target"
                );
            }
            if (status != BASL_STATUS_OK) {
                return status;
            }
            opcode = operator_token->kind == BASL_TOKEN_PLUS_PLUS
                ? BASL_OPCODE_ADD
                : BASL_OPCODE_SUBTRACT;
            value_result.type = target_type;
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
                        ? "compound assignment requires matching integer, f64, or string operands"
                        : (operator_kind == BASL_BINARY_OPERATOR_MODULO
                               ? "compound assignment modulo requires matching integer operands"
                               : "compound assignment requires matching integer or f64 operands")
                );
            } else {
                status = BASL_STATUS_OK;
            }
            if (status != BASL_STATUS_OK) {
                return status;
            }
        }

        /* Specialize to i32/i64 opcodes when both operands are signed integers. */
        if (basl_parser_type_is_i32(target_type) &&
            basl_parser_type_is_i32(value_result.type)) {
            switch (opcode) {
                case BASL_OPCODE_ADD:      opcode = BASL_OPCODE_ADD_I32;      break;
                case BASL_OPCODE_SUBTRACT: opcode = BASL_OPCODE_SUBTRACT_I32; break;
                case BASL_OPCODE_MULTIPLY: opcode = BASL_OPCODE_MULTIPLY_I32; break;
                case BASL_OPCODE_DIVIDE:   opcode = BASL_OPCODE_DIVIDE_I32;   break;
                case BASL_OPCODE_MODULO:   opcode = BASL_OPCODE_MODULO_I32;   break;
                default: break;
            }
        } else if (basl_parser_type_is_signed_integer(target_type) &&
            basl_parser_type_is_signed_integer(value_result.type)) {
            switch (opcode) {
                case BASL_OPCODE_ADD:      opcode = BASL_OPCODE_ADD_I64;      break;
                case BASL_OPCODE_SUBTRACT: opcode = BASL_OPCODE_SUBTRACT_I64; break;
                case BASL_OPCODE_MULTIPLY: opcode = BASL_OPCODE_MULTIPLY_I64; break;
                case BASL_OPCODE_DIVIDE:   opcode = BASL_OPCODE_DIVIDE_I64;   break;
                case BASL_OPCODE_MODULO:   opcode = BASL_OPCODE_MODULO_I64;   break;
                default: break;
            }
        }

        status = basl_parser_emit_opcode(state, opcode, operator_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }
        /* i32 opcodes already produce i32 results — skip the cast. */
        if (!(basl_parser_type_is_i32(target_type) &&
              basl_parser_type_is_i32(value_result.type))) {
            status = basl_parser_emit_integer_cast(state, target_type, operator_token->span);
            if (status != BASL_STATUS_OK) {
                return status;
            }
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
            target_token->span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_u32(
            state,
            (uint32_t)(is_global_assignment
                           ? global_index
                           : (is_capture_local ? capture_index : local_index)),
            target_token->span
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
        status = basl_parser_emit_opcode(state, BASL_OPCODE_POP, target_token->span);
        if (status != BASL_STATUS_OK) {
            return status;
        }

        /* Peephole: rewrite GET_LOCAL + CONSTANT + ADD_I32/SUBTRACT_I32
           + SET_LOCAL + POP → INCREMENT_LOCAL_I32 when the constant is
           a small integer and both locals are the same slot. */
        if (!is_global_assignment && !is_capture_local) {
            uint8_t *code = state->chunk.code.data;
            size_t len = state->chunk.code.length;
            /* Pattern: [GET_LOCAL(5)][CONSTANT(5)][ADD_I32|SUB_I32(1)][SET_LOCAL(5)][POP(1)] = 17 */
            if (len >= 17U) {
                size_t base = len - 17U;
                if (code[base] == BASL_OPCODE_GET_LOCAL &&
                    code[base + 5U] == BASL_OPCODE_CONSTANT &&
                    (code[base + 10U] == BASL_OPCODE_ADD_I32 ||
                     code[base + 10U] == BASL_OPCODE_SUBTRACT_I32) &&
                    code[base + 11U] == BASL_OPCODE_SET_LOCAL &&
                    code[base + 16U] == BASL_OPCODE_POP) {
                    /* Read both local indices. */
                    uint32_t get_idx = (uint32_t)code[base + 1U]
                        | ((uint32_t)code[base + 2U] << 8U)
                        | ((uint32_t)code[base + 3U] << 16U)
                        | ((uint32_t)code[base + 4U] << 24U);
                    uint32_t set_idx = (uint32_t)code[base + 12U]
                        | ((uint32_t)code[base + 13U] << 8U)
                        | ((uint32_t)code[base + 14U] << 16U)
                        | ((uint32_t)code[base + 15U] << 24U);
                    if (get_idx == set_idx) {
                        /* Read constant index. */
                        uint32_t ci = (uint32_t)code[base + 6U]
                            | ((uint32_t)code[base + 7U] << 8U)
                            | ((uint32_t)code[base + 8U] << 16U)
                            | ((uint32_t)code[base + 9U] << 24U);
                        const basl_value_t *cv = (ci < state->chunk.constant_count)
                            ? &state->chunk.constants[ci] : NULL;
                        if (cv != NULL && basl_value_kind(cv) == BASL_VALUE_INT) {
                            int64_t val = basl_value_as_int(cv);
                            int is_sub = (code[base + 10U] == BASL_OPCODE_SUBTRACT_I32);
                            if (is_sub) val = -val;
                            if (val >= -128 && val <= 127) {
                                /* Rewrite to INCREMENT_LOCAL_I32. */
                                code[base] = BASL_OPCODE_INCREMENT_LOCAL_I32;
                                /* idx already at base+1..base+4 */
                                code[base + 5U] = (uint8_t)(int8_t)val;
                                state->chunk.code.length = base + 6U;
                                if (state->chunk.span_count > base + 6U) {
                                    state->chunk.span_count = base + 6U;
                                }
                            }
                        }
                    }
                }
            }
        }

        /* Peephole: rewrite LOCALS_*_I64 + SET_LOCAL + POP →
           LOCALS_*_I32_STORE when target type is i32.
           Pattern: [LOCALS_op(1)][u32 a][u32 b][SET_LOCAL(1)][u32 dst][POP(1)] = 15 bytes
           Rewrite: [LOCALS_op_I32_STORE(1)][u32 dst][u32 a][u32 b] = 13 bytes */
        if (!is_global_assignment && !is_capture_local &&
            basl_parser_type_is_i32(target_type)) {
            uint8_t *code = state->chunk.code.data;
            size_t len = state->chunk.code.length;
            if (len >= 15U) {
                size_t base = len - 15U;
                uint8_t op = code[base];
                basl_opcode_t store_op = (basl_opcode_t)0;
                switch ((basl_opcode_t)op) {
                    case BASL_OPCODE_LOCALS_ADD_I64:       store_op = BASL_OPCODE_LOCALS_ADD_I32_STORE; break;
                    case BASL_OPCODE_LOCALS_SUBTRACT_I64:  store_op = BASL_OPCODE_LOCALS_SUBTRACT_I32_STORE; break;
                    case BASL_OPCODE_LOCALS_MULTIPLY_I64:  store_op = BASL_OPCODE_LOCALS_MULTIPLY_I32_STORE; break;
                    case BASL_OPCODE_LOCALS_MODULO_I64:    store_op = BASL_OPCODE_LOCALS_MODULO_I32_STORE; break;
                    case BASL_OPCODE_LOCALS_LESS_I64:      store_op = BASL_OPCODE_LOCALS_LESS_I32_STORE; break;
                    case BASL_OPCODE_LOCALS_LESS_EQUAL_I64: store_op = BASL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE; break;
                    case BASL_OPCODE_LOCALS_GREATER_I64:   store_op = BASL_OPCODE_LOCALS_GREATER_I32_STORE; break;
                    case BASL_OPCODE_LOCALS_GREATER_EQUAL_I64: store_op = BASL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE; break;
                    case BASL_OPCODE_LOCALS_EQUAL_I64:     store_op = BASL_OPCODE_LOCALS_EQUAL_I32_STORE; break;
                    case BASL_OPCODE_LOCALS_NOT_EQUAL_I64: store_op = BASL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE; break;
                    default: break;
                }
                if (store_op != (basl_opcode_t)0 &&
                    code[base + 9U] == BASL_OPCODE_SET_LOCAL &&
                    code[base + 14U] == BASL_OPCODE_POP) {
                    /* Extract operands. */
                    uint8_t a[4], b[4], dst[4];
                    memcpy(a, &code[base + 1U], 4);
                    memcpy(b, &code[base + 5U], 4);
                    memcpy(dst, &code[base + 10U], 4);
                    /* Rewrite: [store_op][dst][a][b] */
                    code[base] = (uint8_t)store_op;
                    memcpy(&code[base + 1U], dst, 4);
                    memcpy(&code[base + 5U], a, 4);
                    memcpy(&code[base + 9U], b, 4);
                    state->chunk.code.length = base + 13U;
                    if (state->chunk.span_count > base + 13U) {
                        state->chunk.span_count = base + 13U;
                    }
                }
            }
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

    status = basl_parser_parse_expression_with_expected_type(
        state,
        targets.count == 1U ? targets.items[0].type : basl_binding_type_invalid(),
        &initializer_result
    );
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
    status = basl_parser_parse_expression_with_expected_type(
        state,
        declared_type,
        &initializer_result
    );
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

        status = basl_parser_parse_expression_with_expected_type(
            state,
            program->globals[i].type,
            &initializer_result
        );
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

    /* Re-fetch: the function table may have been reallocated while
       parsing the body (e.g. local/anonymous function declarations
       call basl_program_grow_functions). */
    decl = &program->functions.functions[function_index];

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
    const basl_native_registry_t *natives,
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
    program.natives = natives;
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
        NULL,
        out_function,
        diagnostics,
        error
    );
}

basl_status_t basl_compile_source_with_natives(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    const basl_native_registry_t *natives,
    basl_object_t **out_function,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
) {
    return basl_compile_source_internal(
        registry,
        source_id,
        BASL_COMPILE_MODE_BUILD_ENTRYPOINT,
        natives,
        out_function,
        diagnostics,
        error
    );
}
