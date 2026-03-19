#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/chunk.h"
#include "vigil/lexer.h"
#include "vigil/native_module.h"
#include "vigil/stdlib.h"
#include "vigil/string.h"
#include "vigil/token.h"
#include "vigil/type.h"
#include "internal/vigil_binding.h"
#include "internal/vigil_compiler_internal.h"
#include "internal/vigil_internal.h"
#include "internal/vigil_compiler_types.h"
#include "internal/vigil_nanbox.h"

static int vigil_parser_is_assignment_start(
    const vigil_parser_state_t *state
);
vigil_status_t vigil_parser_report(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    const char *message
);
static vigil_status_t vigil_parser_parse_assignment_statement_internal(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result,
    int expect_semicolon
);
static vigil_status_t vigil_parser_parse_expression_statement_internal(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result,
    int expect_semicolon
);
static vigil_status_t vigil_parser_parse_variable_declaration(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
);
vigil_status_t vigil_parser_parse_expression(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
);
static vigil_status_t vigil_parser_parse_expression_with_expected_type(
    vigil_parser_state_t *state,
    vigil_parser_type_t expected_type,
    vigil_expression_result_t *out_result
);
static int vigil_parser_is_variable_declaration_start(
    const vigil_parser_state_t *state
);
static vigil_status_t vigil_parser_parse_declaration(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
);
static vigil_status_t vigil_parser_parse_switch_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
);
vigil_status_t vigil_program_require_non_void_type(
    const vigil_program_state_t *program,
    vigil_source_span_t span,
    vigil_parser_type_t type,
    const char *message
);
static vigil_status_t vigil_parser_emit_i32_constant(
    vigil_parser_state_t *state,
    int64_t value,
    vigil_source_span_t span
);
vigil_status_t vigil_parser_emit_f64_constant(
    vigil_parser_state_t *state,
    double value,
    vigil_source_span_t span
);
vigil_status_t vigil_parser_emit_string_constant_text(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    const char *text,
    size_t length
);
vigil_status_t vigil_parser_emit_ok_constant(
    vigil_parser_state_t *state,
    vigil_source_span_t span
);
static vigil_status_t vigil_parser_emit_integer_cast(
    vigil_parser_state_t *state,
    vigil_parser_type_t target_type,
    vigil_source_span_t span
);
vigil_status_t vigil_parser_emit_integer_constant(
    vigil_parser_state_t *state,
    vigil_parser_type_t target_type,
    int64_t value,
    vigil_source_span_t span
);
static vigil_status_t vigil_compile_function_with_parent(
    vigil_program_state_t *program,
    size_t function_index,
    const vigil_parser_state_t *parent_state
);
static vigil_status_t vigil_compile_extern_fn(
    vigil_program_state_t *program,
    size_t function_index,
    const vigil_extern_fn_decl_t *ext
);
static vigil_status_t vigil_program_parse_extern_fn(
    vigil_program_state_t *program,
    size_t *cursor,
    int is_public
);
const vigil_token_t *vigil_parser_peek(const vigil_parser_state_t *state);
int vigil_parser_check(
    const vigil_parser_state_t *state,
    vigil_token_kind_t kind
);
const char *vigil_parser_token_text(
    const vigil_parser_state_t *state,
    const vigil_token_t *token,
    size_t *out_length
);
vigil_status_t vigil_parser_emit_opcode(
    vigil_parser_state_t *state,
    vigil_opcode_t opcode,
    vigil_source_span_t span
);
vigil_status_t vigil_parser_emit_u32(
    vigil_parser_state_t *state,
    uint32_t value,
    vigil_source_span_t span
);

static void vigil_parser_state_free(
    vigil_parser_state_t *state
) {
    size_t i;
    void *memory;

    if (state == NULL || state->program == NULL) {
        return;
    }

    for (i = 0U; i < state->loop_count; ++i) {
        memory = state->loops[i].break_jumps;
        vigil_runtime_free(state->program->registry->runtime, &memory);
    }

    memory = state->loops;
    vigil_runtime_free(state->program->registry->runtime, &memory);
    vigil_binding_scope_stack_free(&state->locals);

    state->loops = NULL;
    state->loop_count = 0U;
    state->loop_capacity = 0U;
}

void vigil_expression_result_clear(
    vigil_expression_result_t *result
) {
    if (result == NULL) {
        return;
    }

    result->type = vigil_binding_type_invalid();
    result->types = NULL;
    result->type_count = 0U;
    result->owned_types[0] = vigil_binding_type_invalid();
    result->owned_types[1] = vigil_binding_type_invalid();
}

void vigil_expression_result_set_type(
    vigil_expression_result_t *result,
    vigil_parser_type_t type
) {
    if (result == NULL) {
        return;
    }

    result->type = type;
    result->types = NULL;
    result->type_count = vigil_binding_type_is_valid(type) ? 1U : 0U;
    result->owned_types[0] = vigil_binding_type_invalid();
    result->owned_types[1] = vigil_binding_type_invalid();
    result->owned_types[2] = vigil_binding_type_invalid();
}

static void vigil_expression_result_set_return_types(
    vigil_expression_result_t *result,
    vigil_parser_type_t first_type,
    const vigil_parser_type_t *types,
    size_t type_count
) {
    if (result == NULL) {
        return;
    }

    result->type = first_type;
    result->types = types;
    result->type_count = type_count;
    result->owned_types[0] = vigil_binding_type_invalid();
    result->owned_types[1] = vigil_binding_type_invalid();
}

void vigil_expression_result_set_pair(
    vigil_expression_result_t *result,
    vigil_parser_type_t first_type,
    vigil_parser_type_t second_type
) {
    if (result == NULL) {
        return;
    }

    result->type = first_type;
    result->owned_types[0] = first_type;
    result->owned_types[1] = second_type;
    result->owned_types[2] = vigil_binding_type_invalid();
    result->types = result->owned_types;
    result->type_count = 2U;
}

void vigil_expression_result_set_triple(
    vigil_expression_result_t *result,
    vigil_parser_type_t first_type,
    vigil_parser_type_t second_type,
    vigil_parser_type_t third_type
) {
    if (result == NULL) {
        return;
    }

    result->type = first_type;
    result->owned_types[0] = first_type;
    result->owned_types[1] = second_type;
    result->owned_types[2] = third_type;
    result->types = result->owned_types;
    result->type_count = 3U;
}

static void vigil_expression_result_copy(
    vigil_expression_result_t *result,
    const vigil_expression_result_t *source
) {
    if (result == NULL || source == NULL) {
        return;
    }

    vigil_expression_result_set_return_types(
        result,
        source->type,
        source->types,
        source->type_count
    );
    if (source->types == source->owned_types && source->type_count == 2U) {
        vigil_expression_result_set_pair(
            result,
            source->owned_types[0],
            source->owned_types[1]
        );
    } else if (source->types == source->owned_types && source->type_count == 3U) {
        vigil_expression_result_set_triple(
            result,
            source->owned_types[0],
            source->owned_types[1],
            source->owned_types[2]
        );
    }
}

vigil_status_t vigil_parser_require_scalar_expression(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    const vigil_expression_result_t *result,
    const char *message
) {
    if (result == NULL || result->type_count == 1U) {
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_report(state, span, message);
}

void vigil_constant_result_clear(
    vigil_constant_result_t *result
) {
    if (result == NULL) {
        return;
    }

    result->type = vigil_binding_type_invalid();
    vigil_value_init_nil(&result->value);
}

void vigil_constant_result_release(
    vigil_constant_result_t *result
) {
    if (result == NULL) {
        return;
    }

    vigil_value_release(&result->value);
    result->type = vigil_binding_type_invalid();
}

static void vigil_binding_target_list_init(
    vigil_binding_target_list_t *list
) {
    if (list == NULL) {
        return;
    }

    memset(list, 0, sizeof(*list));
}

static void vigil_binding_target_list_free(
    vigil_program_state_t *program,
    vigil_binding_target_list_t *list
) {
    void *memory;

    if (program == NULL || list == NULL) {
        return;
    }

    memory = list->items;
    vigil_runtime_free(program->registry->runtime, &memory);
    memset(list, 0, sizeof(*list));
}

static vigil_status_t vigil_binding_target_list_grow(
    vigil_program_state_t *program,
    vigil_binding_target_list_t *list,
    size_t minimum_capacity
) {
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= list->capacity) {
        return VIGIL_STATUS_OK;
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
        vigil_error_set_literal(
            program->error,
            VIGIL_STATUS_OUT_OF_MEMORY,
            "binding target allocation overflow"
        );
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = list->items;
    if (memory == NULL) {
        status = vigil_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*list->items),
            &memory,
            program->error
        );
    } else {
        status = vigil_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*list->items),
            program->error
        );
        if (status == VIGIL_STATUS_OK) {
            memset(
                (vigil_binding_target_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*list->items)
            );
        }
    }
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    list->items = (vigil_binding_target_t *)memory;
    list->capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_binding_target_list_append(
    vigil_program_state_t *program,
    vigil_binding_target_list_t *list,
    vigil_parser_type_t type,
    const vigil_token_t *name_token,
    int is_discard
) {
    vigil_status_t status;
    vigil_binding_target_t *target;

    status = vigil_binding_target_list_grow(program, list, list->count + 1U);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    target = &list->items[list->count];
    target->type = type;
    target->name_token = name_token;
    target->is_discard = is_discard != 0;
    list->count += 1U;
    return VIGIL_STATUS_OK;
}

static vigil_parser_type_t vigil_expression_result_type_at(
    const vigil_expression_result_t *result,
    size_t index
) {
    if (result == NULL || index >= result->type_count) {
        return vigil_binding_type_invalid();
    }
    if (index == 0U) {
        return result->type;
    }
    if (result->types == NULL) {
        return vigil_binding_type_invalid();
    }
    return result->types[index];
}

vigil_status_t vigil_compile_report(
    const vigil_program_state_t *program,
    vigil_source_span_t span,
    const char *message
) {
    vigil_status_t status;
    vigil_source_location_t location;

    status = vigil_diagnostic_list_append_cstr(
        program->diagnostics,
        VIGIL_DIAGNOSTIC_ERROR,
        span,
        message,
        program->error
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    vigil_error_set_literal(program->error, VIGIL_STATUS_SYNTAX_ERROR, message);
    if (program->error != NULL) {
        vigil_source_location_clear(&location);
        location.source_id = span.source_id;
        location.offset = span.start_offset;
        if (vigil_source_registry_resolve_location(program->registry, &location, NULL) == VIGIL_STATUS_OK) {
            program->error->location = location;
        } else {
            program->error->location.source_id = span.source_id;
            program->error->location.offset = span.start_offset;
        }
    }
    return VIGIL_STATUS_SYNTAX_ERROR;
}

const vigil_token_t *vigil_program_token_at(
    const vigil_program_state_t *program,
    size_t index
) {
    return vigil_token_list_get(program->tokens, index);
}

int vigil_program_names_equal(
    const char *left,
    size_t left_length,
    const char *right,
    size_t right_length
);

const char *vigil_program_token_text(
    const vigil_program_state_t *program,
    const vigil_token_t *token,
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

    return vigil_string_c_str(&program->source->text) + token->span.start_offset;
}


vigil_source_span_t vigil_program_eof_span(const vigil_program_state_t *program) {
    vigil_source_span_t span;

    vigil_source_span_clear(&span);
    if (program == NULL || program->source == NULL) {
        return span;
    }

    span.source_id = program->source->id;
    span.start_offset = vigil_string_length(&program->source->text);
    span.end_offset = span.start_offset;
    return span;
}

int vigil_program_names_equal(
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

static int vigil_program_module_find(
    const vigil_program_state_t *program,
    vigil_source_id_t source_id,
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

static vigil_program_module_t *vigil_program_current_module(
    vigil_program_state_t *program
) {
    size_t module_index;

    module_index = 0U;
    if (
        program == NULL ||
        program->source == NULL ||
        !vigil_program_module_find(program, program->source->id, &module_index)
    ) {
        return NULL;
    }

    return &program->modules[module_index];
}

static const vigil_program_module_t *vigil_program_current_module_const(
    const vigil_program_state_t *program
) {
    size_t module_index;

    module_index = 0U;
    if (
        program == NULL ||
        program->source == NULL ||
        !vigil_program_module_find(program, program->source->id, &module_index)
    ) {
        return NULL;
    }

    return &program->modules[module_index];
}

static vigil_status_t vigil_program_add_module(
    vigil_program_state_t *program,
    vigil_source_id_t source_id,
    const vigil_source_file_t *source,
    const vigil_token_list_t *tokens,
    vigil_module_compile_state_t state,
    size_t *out_index
) {
    vigil_status_t status;
    vigil_program_module_t *module;
    void *memory;

    status = vigil_program_grow_modules(program, program->module_count + 1U);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    module = &program->modules[program->module_count];
    memset(module, 0, sizeof(*module));
    module->source_id = source_id;
    module->source = source;
    memory = NULL;
    status = vigil_runtime_alloc(
        program->registry->runtime,
        sizeof(*module->tokens),
        &memory,
        program->error
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    module->tokens = (vigil_token_list_t *)memory;
    *module->tokens = *tokens;
    module->state = state;
    if (out_index != NULL) {
        *out_index = program->module_count;
    }
    program->module_count += 1U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_module_grow_imports(
    vigil_program_state_t *program,
    vigil_program_module_t *module,
    size_t minimum_capacity
) {
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= module->import_capacity) {
        return VIGIL_STATUS_OK;
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
        vigil_error_set_literal(
            program->error,
            VIGIL_STATUS_OUT_OF_MEMORY,
            "module import table allocation overflow"
        );
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = module->imports;
    if (memory == NULL) {
        status = vigil_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*module->imports),
            &memory,
            program->error
        );
    } else {
        status = vigil_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*module->imports),
            program->error
        );
        if (status == VIGIL_STATUS_OK) {
            memset(
                (vigil_module_import_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*module->imports)
            );
        }
    }

    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    module->imports = (vigil_module_import_t *)memory;
    module->import_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static int vigil_program_module_find_import(
    const vigil_program_module_t *module,
    const char *alias,
    size_t alias_length,
    vigil_source_id_t *out_source_id
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
            vigil_program_names_equal(
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

static vigil_status_t vigil_program_add_module_import(
    vigil_program_state_t *program,
    vigil_program_module_t *module,
    const char *alias,
    size_t alias_length,
    vigil_source_span_t alias_span,
    vigil_source_id_t source_id
) {
    vigil_status_t status;
    vigil_module_import_t *import_decl;
    void *memory;

    if (program->compile_mode != VIGIL_COMPILE_MODE_REPL) {
    if (vigil_program_module_find_import(module, alias, alias_length, NULL)) {
        return vigil_compile_report(program, alias_span, "import alias is already declared");
    }
    } /* end REPL redefinition guard */

    status = vigil_program_module_grow_imports(program, module, module->import_count + 1U);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    import_decl = &module->imports[module->import_count];
    memset(import_decl, 0, sizeof(*import_decl));
    memory = NULL;
    status = vigil_runtime_alloc(
        program->registry->runtime,
        alias_length + 1U,
        &memory,
        program->error
    );
    if (status != VIGIL_STATUS_OK) {
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
    return VIGIL_STATUS_OK;
}

int vigil_program_resolve_import_alias(
    const vigil_program_state_t *program,
    const char *alias,
    size_t alias_length,
    vigil_source_id_t *out_source_id
) {
    const vigil_program_module_t *module;

    if (out_source_id != NULL) {
        *out_source_id = 0U;
    }
    if (program == NULL || alias == NULL) {
        return 0;
    }

    module = vigil_program_current_module_const(program);
    if (module == NULL) {
        return 0;
    }

    return vigil_program_module_find_import(module, alias, alias_length, out_source_id);
}

static int vigil_program_path_has_vigil_extension(
    const char *path,
    size_t length
) {
    return path != NULL &&
           length >= 6U &&
           memcmp(path + length - 6U, ".vigil", 6U) == 0;
}

static int vigil_program_path_is_absolute(
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

static vigil_status_t vigil_program_resolve_import_path(
    const vigil_program_state_t *program,
    const char *import_text,
    size_t import_length,
    vigil_string_t *out_path
) {
    vigil_status_t status;
    const char *base_path;
    size_t base_length;
    size_t prefix_length;

    vigil_string_clear(out_path);
    if (program == NULL || program->source == NULL || import_text == NULL) {
        if (program != NULL) {
            vigil_error_set_literal(
                program->error,
                VIGIL_STATUS_INVALID_ARGUMENT,
                "import path inputs must not be null"
            );
        }
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_program_path_is_absolute(import_text, import_length)) {
        status = vigil_string_assign(out_path, import_text, import_length, program->error);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    } else {
        base_path = vigil_string_c_str(&program->source->path);
        base_length = vigil_string_length(&program->source->path);
        prefix_length = base_length;
        while (prefix_length > 0U) {
            char current = base_path[prefix_length - 1U];

            if (current == '/' || current == '\\') {
                break;
            }
            prefix_length -= 1U;
        }

        if (prefix_length != 0U) {
            status = vigil_string_assign(out_path, base_path, prefix_length, program->error);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_string_append(out_path, import_text, import_length, program->error);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
        } else {
            status = vigil_string_assign(out_path, import_text, import_length, program->error);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
        }
    }

    if (
        !vigil_program_path_has_vigil_extension(
            vigil_string_c_str(out_path),
            vigil_string_length(out_path)
        )
    ) {
        status = vigil_string_append_cstr(out_path, ".vigil", program->error);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}

static int vigil_program_find_source_by_path(
    const vigil_program_state_t *program,
    const char *path,
    size_t path_length,
    vigil_source_id_t *out_source_id
) {
    size_t i;
    const vigil_source_file_t *source;

    if (out_source_id != NULL) {
        *out_source_id = 0U;
    }
    if (program == NULL || path == NULL) {
        return 0;
    }

    for (i = 1U; i <= vigil_source_registry_count(program->registry); i += 1U) {
        source = vigil_source_registry_get(program->registry, (vigil_source_id_t)i);
        if (source == NULL) {
            continue;
        }
        if (
            vigil_program_names_equal(
                vigil_string_c_str(&source->path),
                vigil_string_length(&source->path),
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

int vigil_program_find_class_in_source(
    const vigil_program_state_t *program,
    vigil_source_id_t source_id,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const vigil_class_decl_t **out_class
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
            vigil_program_names_equal(
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

int vigil_program_find_interface_in_source(
    const vigil_program_state_t *program,
    vigil_source_id_t source_id,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const vigil_interface_decl_t **out_interface
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
            vigil_program_names_equal(
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


static int vigil_program_find_enum(
    const vigil_program_state_t *program,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const vigil_enum_decl_t **out_decl
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
            vigil_program_names_equal(
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

int vigil_program_find_enum_in_source(
    const vigil_program_state_t *program,
    vigil_source_id_t source_id,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const vigil_enum_decl_t **out_decl
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
            vigil_program_names_equal(
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

int vigil_enum_decl_find_member(
    const vigil_enum_decl_t *decl,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const vigil_enum_member_t **out_member
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
            vigil_program_names_equal(
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

vigil_parser_type_t vigil_program_array_type_element(
    const vigil_program_state_t *program,
    vigil_parser_type_t array_type
) {
    if (
        program == NULL ||
        !vigil_parser_type_is_array(array_type) ||
        array_type.object_index >= program->array_type_count
    ) {
        return vigil_binding_type_invalid();
    }

    return program->array_types[array_type.object_index].element_type;
}

vigil_parser_type_t vigil_program_map_type_key(
    const vigil_program_state_t *program,
    vigil_parser_type_t map_type
) {
    if (
        program == NULL ||
        !vigil_parser_type_is_map(map_type) ||
        map_type.object_index >= program->map_type_count
    ) {
        return vigil_binding_type_invalid();
    }

    return program->map_types[map_type.object_index].key_type;
}

vigil_parser_type_t vigil_program_map_type_value(
    const vigil_program_state_t *program,
    vigil_parser_type_t map_type
) {
    if (
        program == NULL ||
        !vigil_parser_type_is_map(map_type) ||
        map_type.object_index >= program->map_type_count
    ) {
        return vigil_binding_type_invalid();
    }

    return program->map_types[map_type.object_index].value_type;
}

static vigil_status_t vigil_binding_function_grow_captures(
    vigil_program_state_t *program,
    vigil_function_decl_t *decl,
    size_t minimum_capacity
) {
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= decl->capture_capacity) {
        return VIGIL_STATUS_OK;
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
        vigil_error_set_literal(
            program->error,
            VIGIL_STATUS_OUT_OF_MEMORY,
            "binding capture table allocation overflow"
        );
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = decl->captures;
    if (memory == NULL) {
        status = vigil_runtime_alloc(
            program->registry->runtime,
            next_capacity * sizeof(*decl->captures),
            &memory,
            program->error
        );
    } else {
        status = vigil_runtime_realloc(
            program->registry->runtime,
            &memory,
            next_capacity * sizeof(*decl->captures),
            program->error
        );
        if (status == VIGIL_STATUS_OK) {
            memset(
                (vigil_binding_capture_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*decl->captures)
            );
        }
    }
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    decl->captures = (vigil_binding_capture_t *)memory;
    decl->capture_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static int vigil_binding_function_find_capture(
    const vigil_function_decl_t *decl,
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
            vigil_program_names_equal(
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

int vigil_class_decl_find_field(
    const vigil_class_decl_t *decl,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const vigil_class_field_t **out_field
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
            vigil_program_names_equal(
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

int vigil_class_decl_find_method(
    const vigil_class_decl_t *decl,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const vigil_class_method_t **out_method
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
            vigil_program_names_equal(
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



int vigil_interface_decl_find_method(
    const vigil_interface_decl_t *decl,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const vigil_interface_method_t **out_method
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
            vigil_program_names_equal(
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

static int vigil_program_find_constant(
    const vigil_program_state_t *program,
    const char *name,
    size_t name_length,
    const vigil_global_constant_t **out_constant
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
            vigil_program_names_equal(
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

int vigil_program_find_constant_in_source(
    const vigil_program_state_t *program,
    vigil_source_id_t source_id,
    const char *name,
    size_t name_length,
    const vigil_global_constant_t **out_constant
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
            vigil_program_names_equal(
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

int vigil_program_find_global_in_source(
    const vigil_program_state_t *program,
    vigil_source_id_t source_id,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const vigil_global_variable_t **out_global
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
            vigil_program_names_equal(
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

static vigil_status_t vigil_program_checked_add(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (
        (right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)
    ) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left + right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_subtract(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (
        (right > 0 && left < INT64_MIN + right) ||
        (right < 0 && left > INT64_MAX + right)
    ) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left - right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_multiply(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (left == 0 || right == 0) {
        *out_result = 0;
        return VIGIL_STATUS_OK;
    }

    if (
        (left == -1 && right == INT64_MIN) ||
        (right == -1 && left == INT64_MIN)
    ) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (left > 0) {
        if (right > 0) {
            if (left > INT64_MAX / right) {
                return VIGIL_STATUS_INVALID_ARGUMENT;
            }
        } else if (right < INT64_MIN / left) {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
    } else if (right > 0) {
        if (left < INT64_MIN / right) {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
    } else if (left != 0 && right < INT64_MAX / left) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left * right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_divide(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (right == 0 || (left == INT64_MIN && right == -1)) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left / right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_modulo(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (right == 0 || (left == INT64_MIN && right == -1)) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left % right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_negate(
    int64_t value,
    int64_t *out_result
) {
    if (value == INT64_MIN) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = -value;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_shift_left(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (right < 0 || right >= 64) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = (int64_t)(((uint64_t)left) << (uint32_t)right);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_shift_right(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    uint64_t shifted;

    if (right < 0 || right >= 64) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (right == 0) {
        *out_result = left;
        return VIGIL_STATUS_OK;
    }

    shifted = ((uint64_t)left) >> (uint32_t)right;
    if (left < 0) {
        shifted |= UINT64_MAX << (64U - (uint32_t)right);
    }

    *out_result = (int64_t)shifted;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_uadd(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (UINT64_MAX - left < right) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left + right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_usubtract(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (left < right) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left - right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_umultiply(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (left != 0U && right > UINT64_MAX / left) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left * right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_udivide(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (right == 0U) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left / right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_umodulo(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (right == 0U) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left % right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_ushift_left(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (right >= 64U) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left << (uint32_t)right;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_checked_ushift_right(
    uint64_t left,
    uint64_t right,
    uint64_t *out_result
) {
    if (right >= 64U) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left >> (uint32_t)right;
    return VIGIL_STATUS_OK;
}

static int vigil_program_values_equal(
    const vigil_value_t *left,
    const vigil_value_t *right
) {
    const vigil_object_t *left_object;
    const vigil_object_t *right_object;
    const char *left_text;
    const char *right_text;
    size_t left_length;
    size_t right_length;
    vigil_value_kind_t lk;
    vigil_value_kind_t rk;

    if (left == NULL || right == NULL) {
        return 0;
    }

    lk = vigil_value_kind(left);
    rk = vigil_value_kind(right);
    if (lk != rk) {
        return 0;
    }

    switch (lk) {
        case VIGIL_VALUE_NIL:
            return 1;
        case VIGIL_VALUE_BOOL:
            return vigil_value_as_bool(left) == vigil_value_as_bool(right);
        case VIGIL_VALUE_INT:
            return vigil_value_as_int(left) == vigil_value_as_int(right);
        case VIGIL_VALUE_UINT:
            return vigil_value_as_uint(left) == vigil_value_as_uint(right);
        case VIGIL_VALUE_FLOAT:
            return vigil_value_as_float(left) == vigil_value_as_float(right);
        case VIGIL_VALUE_OBJECT:
            left_object = vigil_value_as_object(left);
            right_object = vigil_value_as_object(right);
            if (left_object == right_object) {
                return 1;
            }
            if (left_object == NULL || right_object == NULL) {
                return 0;
            }
            if (
                vigil_object_type(left_object) != VIGIL_OBJECT_STRING ||
                vigil_object_type(right_object) != VIGIL_OBJECT_STRING
            ) {
                return 0;
            }
            left_text = vigil_string_object_c_str(left_object);
            right_text = vigil_string_object_c_str(right_object);
            left_length = vigil_string_object_length(left_object);
            right_length = vigil_string_object_length(right_object);
            return left_length == right_length &&
                   left_text != NULL &&
                   right_text != NULL &&
                   memcmp(left_text, right_text, left_length) == 0;
        default:
            return 0;
    }
}

static size_t vigil_program_utf8_codepoint_count(const char *text, size_t length) {
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

static vigil_status_t vigil_program_decode_string_literal(
    const vigil_program_state_t *program,
    const vigil_token_t *token,
    vigil_string_t *out_text
) {
    const char *text;
    size_t length;
    size_t index;
    size_t start;
    size_t end;
    char decoded;
    vigil_status_t status;

    if (program == NULL || token == NULL || out_text == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    text = vigil_program_token_text(program, token, &length);
    if (text == NULL || length < 2U) {
        return vigil_compile_report(program, token->span, "invalid string literal");
    }

    start = token->kind == VIGIL_TOKEN_FSTRING_LITERAL ? 2U : 1U;
    if (length < start + 1U) {
        return vigil_compile_report(program, token->span, "invalid string literal");
    }
    end = length - 1U;

    vigil_string_clear(out_text);
    if (token->kind == VIGIL_TOKEN_RAW_STRING_LITERAL) {
        return vigil_string_assign(
            out_text,
            text + start,
            end - start,
            program->error
        );
    }

    for (index = start; index < end; index += 1U) {
        if (index + 1U < end && text[index] == '{' && text[index + 1U] == '{') {
            status = vigil_string_append(out_text, "{", 1U, program->error);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            index += 1U;
            continue;
        }
        if (index + 1U < end && text[index] == '}' && text[index + 1U] == '}') {
            status = vigil_string_append(out_text, "}", 1U, program->error);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            index += 1U;
            continue;
        }
        if (text[index] != '\\') {
            status = vigil_string_append(out_text, text + index, 1U, program->error);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }

        index += 1U;
        if (index >= end) {
            return vigil_compile_report(program, token->span, "invalid escape sequence");
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
            case 'x': {
                unsigned int hi;
                unsigned int lo;
                if (index + 2U >= end) {
                    return vigil_compile_report(program, token->span,
                        "\\x escape requires two hex digits");
                }
                hi = (unsigned int)text[index + 1U];
                lo = (unsigned int)text[index + 2U];
                if (hi >= '0' && hi <= '9') { hi = hi - '0'; }
                else if (hi >= 'a' && hi <= 'f') { hi = hi - 'a' + 10U; }
                else if (hi >= 'A' && hi <= 'F') { hi = hi - 'A' + 10U; }
                else {
                    return vigil_compile_report(program, token->span,
                        "\\x escape requires two hex digits");
                }
                if (lo >= '0' && lo <= '9') { lo = lo - '0'; }
                else if (lo >= 'a' && lo <= 'f') { lo = lo - 'a' + 10U; }
                else if (lo >= 'A' && lo <= 'F') { lo = lo - 'A' + 10U; }
                else {
                    return vigil_compile_report(program, token->span,
                        "\\x escape requires two hex digits");
                }
                decoded = (char)((hi << 4U) | lo);
                index += 2U;
                break;
            }
            default:
                return vigil_compile_report(program, token->span, "invalid escape sequence");
        }

        status = vigil_string_append(out_text, &decoded, 1U, program->error);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    if (token->kind == VIGIL_TOKEN_CHAR_LITERAL) {
        if (vigil_program_utf8_codepoint_count(vigil_string_c_str(out_text), vigil_string_length(out_text)) != 1U) {
            return vigil_compile_report(
                program,
                token->span,
                "character literals must contain exactly one character"
            );
        }
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_string_literal_value(
    const vigil_program_state_t *program,
    const vigil_token_t *token,
    vigil_value_t *out_value
) {
    vigil_status_t status;
    vigil_string_t decoded;
    vigil_object_t *object;

    if (program == NULL || token == NULL || out_value == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    object = NULL;
    vigil_string_init(&decoded, program->registry->runtime);
    status = vigil_program_decode_string_literal(program, token, &decoded);
    if (status == VIGIL_STATUS_OK) {
        status = vigil_string_object_new(
            program->registry->runtime,
            vigil_string_c_str(&decoded),
            vigil_string_length(&decoded),
            &object,
            program->error
        );
    }
    if (status == VIGIL_STATUS_OK) {
        vigil_value_init_object(out_value, &object);
    }
    vigil_object_release(&object);
    vigil_string_free(&decoded);
    return status;
}

static vigil_status_t vigil_program_concat_string_values(
    const vigil_program_state_t *program,
    const vigil_value_t *left,
    const vigil_value_t *right,
    vigil_value_t *out_value
) {
    vigil_status_t status;
    vigil_string_t text;
    const vigil_object_t *left_object;
    const vigil_object_t *right_object;
    vigil_object_t *object;

    if (program == NULL || left == NULL || right == NULL || out_value == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    left_object = vigil_value_as_object(left);
    right_object = vigil_value_as_object(right);
    if (
        left_object == NULL ||
        right_object == NULL ||
        vigil_object_type(left_object) != VIGIL_OBJECT_STRING ||
        vigil_object_type(right_object) != VIGIL_OBJECT_STRING
    ) {
        return vigil_compile_report(program, vigil_program_eof_span(program), "string operands are required");
    }

    object = NULL;
    vigil_string_init(&text, program->registry->runtime);
    status = vigil_string_append(
        &text,
        vigil_string_object_c_str(left_object),
        vigil_string_object_length(left_object),
        program->error
    );
    if (status == VIGIL_STATUS_OK) {
        status = vigil_string_append(
            &text,
            vigil_string_object_c_str(right_object),
            vigil_string_object_length(right_object),
            program->error
        );
    }
    if (status == VIGIL_STATUS_OK) {
        status = vigil_string_object_new(
            program->registry->runtime,
            vigil_string_c_str(&text),
            vigil_string_length(&text),
            &object,
            program->error
        );
    }
    if (status == VIGIL_STATUS_OK) {
        vigil_value_init_object(out_value, &object);
    }
    vigil_object_release(&object);
    vigil_string_free(&text);
    return status;
}

vigil_status_t vigil_parser_emit_string_constant_text(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    const char *text,
    size_t length
) {
    vigil_status_t status;
    vigil_object_t *object;
    vigil_value_t value;

    if (state == NULL || text == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    object = NULL;
    vigil_value_init_nil(&value);
    status = vigil_string_object_new(
        state->program->registry->runtime,
        text,
        length,
        &object,
        state->program->error
    );
    if (status == VIGIL_STATUS_OK) {
        vigil_value_init_object(&value, &object);
        status = vigil_chunk_write_constant(
            &state->chunk,
            &value,
            span,
            NULL,
            state->program->error
        );
    }
    vigil_value_release(&value);
    vigil_object_release(&object);
    return status;
}


int vigil_program_is_class_public(const vigil_class_decl_t *decl);
int vigil_program_is_interface_public(const vigil_interface_decl_t *decl);
int vigil_program_is_enum_public(const vigil_enum_decl_t *decl);
int vigil_program_is_function_public(const vigil_function_decl_t *decl);
int vigil_program_is_constant_public(const vigil_global_constant_t *decl);
int vigil_program_is_global_public(const vigil_global_variable_t *decl);


vigil_status_t vigil_program_require_non_void_type(
    const vigil_program_state_t *program,
    vigil_source_span_t span,
    vigil_parser_type_t type,
    const char *message
) {
    if (vigil_parser_type_is_void(type)) {
        return vigil_compile_report(program, span, message);
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_program_parse_function_return_types(
    vigil_program_state_t *program,
    size_t *cursor,
    const char *unsupported_message,
    vigil_function_decl_t *decl
) {
    vigil_status_t status;
    const vigil_token_t *token;
    vigil_parser_type_t return_type;

    token = vigil_program_token_at(program, *cursor);
    if (token != NULL && token->kind == VIGIL_TOKEN_LPAREN) {
        *cursor += 1U;
        while (1) {
            status = vigil_program_parse_type_reference(
                program,
                cursor,
                unsupported_message,
                &return_type
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_binding_function_add_return_type(
                program->registry->runtime,
                decl,
                return_type,
                program->error
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }

            token = vigil_program_token_at(program, *cursor);
            if (token != NULL && token->kind == VIGIL_TOKEN_COMMA) {
                *cursor += 1U;
                continue;
            }
            if (token != NULL && token->kind == VIGIL_TOKEN_RPAREN) {
                *cursor += 1U;
                break;
            }
            return vigil_compile_report(
                program,
                token == NULL ? vigil_program_eof_span(program) : token->span,
                unsupported_message
            );
        }
        return VIGIL_STATUS_OK;
    }

    status = vigil_program_parse_type_reference(program, cursor, unsupported_message, &return_type);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    return vigil_binding_function_add_return_type(
        program->registry->runtime,
        decl,
        return_type,
        program->error
    );
}


int vigil_program_is_class_public(
    const vigil_class_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

int vigil_program_is_interface_public(
    const vigil_interface_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

int vigil_program_is_enum_public(
    const vigil_enum_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

int vigil_program_is_function_public(
    const vigil_function_decl_t *decl
) {
    return decl != NULL && decl->is_public;
}

int vigil_program_is_constant_public(
    const vigil_global_constant_t *decl
) {
    return decl != NULL && decl->is_public;
}

int vigil_program_is_global_public(
    const vigil_global_variable_t *decl
) {
    return decl != NULL && decl->is_public;
}

static int vigil_program_lookup_enum_member(
    const vigil_program_state_t *program,
    const char *enum_name,
    size_t enum_name_length,
    const char *member_name,
    size_t member_name_length,
    size_t *out_enum_index,
    const vigil_enum_member_t **out_member
) {
    const vigil_enum_decl_t *decl;
    size_t enum_index;

    decl = NULL;
    enum_index = 0U;
    if (
        !vigil_program_find_enum(
            program,
            enum_name,
            enum_name_length,
            &enum_index,
            &decl
        )
    ) {
        return 0;
    }
    if (!vigil_enum_decl_find_member(decl, member_name, member_name_length, NULL, out_member)) {
        return 0;
    }
    if (out_enum_index != NULL) {
        *out_enum_index = enum_index;
    }
    return 1;
}

static int vigil_program_lookup_enum_member_in_source(
    const vigil_program_state_t *program,
    vigil_source_id_t source_id,
    const char *enum_name,
    size_t enum_name_length,
    const char *member_name,
    size_t member_name_length,
    size_t *out_enum_index,
    const vigil_enum_member_t **out_member
) {
    const vigil_enum_decl_t *decl;
    size_t enum_index;

    decl = NULL;
    enum_index = 0U;
    if (
        !vigil_program_find_enum_in_source(
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
    if (!vigil_enum_decl_find_member(decl, member_name, member_name_length, NULL, out_member)) {
        return 0;
    }
    if (out_enum_index != NULL) {
        *out_enum_index = enum_index;
    }
    return 1;
}

static vigil_status_t vigil_program_validate_main_signature(
    vigil_program_state_t *program,
    vigil_function_decl_t *decl,
    const vigil_token_t *type_token
) {
    if (decl->param_count != 0U) {
        return vigil_compile_report(
            program,
            decl->name_span,
            "main entrypoint must not declare parameters"
        );
    }

    if (
        decl->return_count != 1U ||
        !vigil_parser_type_equal(decl->return_type, vigil_binding_type_primitive(VIGIL_TYPE_I32))
    ) {
        return vigil_compile_report(
            program,
            type_token->span,
            "main entrypoint must declare return type i32"
        );
    }

    return VIGIL_STATUS_OK;
}

static void vigil_function_decl_free(
    vigil_program_state_t *program,
    vigil_function_decl_t *decl
) {
    if (program == NULL || program->registry == NULL || decl == NULL) {
        return;
    }

    vigil_binding_function_free(program->registry->runtime, decl);
}

static vigil_status_t vigil_program_fail_partial_decl(
    vigil_program_state_t *program,
    vigil_function_decl_t *decl,
    vigil_status_t status
) {
    vigil_function_decl_free(program, decl);
    return status;
}

vigil_status_t vigil_program_add_param(
    vigil_program_state_t *program,
    vigil_function_decl_t *decl,
    vigil_parser_type_t type,
    const vigil_token_t *name_token
) {
    vigil_status_t status;
    const char *name;
    size_t name_length;

    name = vigil_program_token_text(program, name_token, &name_length);
    status = vigil_binding_function_add_param(
        program->registry->runtime,
        decl,
        name,
        name_length,
        name_token->span,
        type,
        program->error
    );
    if (status == VIGIL_STATUS_INVALID_ARGUMENT) {
        return vigil_compile_report(
            program,
            name_token->span,
            "function parameter is already declared"
        );
    }

    return status;
}

static vigil_status_t vigil_program_parse_source(
    vigil_program_state_t *program,
    vigil_source_id_t source_id
);
const vigil_token_t *vigil_program_cursor_peek(
    const vigil_program_state_t *program,
    size_t cursor
);
const vigil_token_t *vigil_program_cursor_advance(
    const vigil_program_state_t *program,
    size_t *cursor
);
int vigil_program_find_top_level_function_name_in_source(
    const vigil_program_state_t *program,
    vigil_source_id_t source_id,
    const char *name_text,
    size_t name_length,
    size_t *out_index,
    const vigil_function_decl_t **out_decl
);

static vigil_status_t vigil_program_parse_import_target(
    const vigil_program_state_t *program,
    const vigil_token_t *token,
    vigil_string_t *out_path
) {
    const char *text;
    size_t length;

    if (
        token == NULL ||
        (token->kind != VIGIL_TOKEN_STRING_LITERAL &&
         token->kind != VIGIL_TOKEN_RAW_STRING_LITERAL)
    ) {
        return vigil_compile_report(
            program,
            token == NULL ? vigil_program_eof_span(program) : token->span,
            "expected import path string literal"
        );
    }

    text = vigil_program_token_text(program, token, &length);
    if (text == NULL || length < 2U) {
        return vigil_compile_report(program, token->span, "import path is invalid");
    }

    return vigil_program_resolve_import_path(program, text + 1U, length - 2U, out_path);
}

static vigil_status_t vigil_program_register_native_function_types(
    vigil_program_state_t *program,
    const vigil_native_module_t *mod
) {
    vigil_status_t status;
    size_t fi;

    for (fi = 0U; fi < mod->function_count; fi++) {
        const vigil_native_module_function_t *fn = &mod->functions[fi];
        if (fn->return_type == VIGIL_TYPE_OBJECT && fn->return_element_type != 0) {
            /* Pre-register array type for this function's return type */
            vigil_parser_type_t arr_type;
            vigil_parser_type_t elem_type = vigil_binding_type_primitive(
                (vigil_type_kind_t)fn->return_element_type);
            status = vigil_program_intern_array_type(program, elem_type, &arr_type);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
        }
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_register_native_classes(
    vigil_program_state_t *program,
    const vigil_native_module_t *mod,
    vigil_source_id_t source_id
) {
    vigil_status_t status;
    size_t ci;
    size_t fi;
    size_t mi;

    for (ci = 0U; ci < mod->class_count; ci++) {
        const vigil_native_class_t *nc = &mod->classes[ci];
        vigil_class_decl_t *decl;

        status = vigil_program_grow_classes(program, program->class_count + 1U);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        decl = &program->classes[program->class_count];
        memset(decl, 0, sizeof(*decl));
        decl->constructor_function_index = (size_t)-1;
        decl->source_id = source_id;
        decl->name = nc->name;
        decl->name_length = nc->name_length;
        decl->is_public = 1;
        decl->native_class = nc;

        /* Register fields. */
        if (nc->field_count > 0U) {
            status = vigil_class_decl_grow_fields(
                program, decl, nc->field_count);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            for (fi = 0U; fi < nc->field_count; fi++) {
                vigil_class_field_t *f = &decl->fields[fi];
                f->name = nc->fields[fi].name;
                f->name_length = nc->fields[fi].name_length;
                f->is_public = 1;
                if (nc->fields[fi].object_kind == VIGIL_BINDING_OBJECT_CLASS &&
                    nc->fields[fi].class_name != NULL) {
                    /* Resolve class by name within the same module. */
                    size_t cls_idx = 0U;
                    int found = 0;
                    size_t k;
                    for (k = 0U; k < program->class_count; k++) {
                        if (program->classes[k].name_length == nc->fields[fi].class_name_length &&
                            memcmp(program->classes[k].name, nc->fields[fi].class_name,
                                   nc->fields[fi].class_name_length) == 0) {
                            cls_idx = k;
                            found = 1;
                            break;
                        }
                    }
                    f->type = found
                        ? vigil_binding_type_class(cls_idx)
                        : vigil_binding_type_primitive(VIGIL_TYPE_OBJECT);
                } else if (nc->fields[fi].object_kind == VIGIL_BINDING_OBJECT_ARRAY) {
                    /* Intern array type with the given element type. */
                    vigil_parser_type_t arr_type;
                    status = vigil_program_intern_array_type(
                        program,
                        vigil_binding_type_primitive(
                            (vigil_type_kind_t)nc->fields[fi].element_type),
                        &arr_type);
                    f->type = (status == VIGIL_STATUS_OK)
                        ? arr_type
                        : vigil_binding_type_primitive(VIGIL_TYPE_OBJECT);
                } else {
                    f->type = vigil_binding_type_primitive(
                        (vigil_type_kind_t)nc->fields[fi].type);
                }
            }
            decl->field_count = nc->field_count;
        }

        /* Register method stubs (function_index unused for native). */
        if (nc->method_count > 0U) {
            status = vigil_class_decl_grow_methods(
                program, decl, nc->method_count);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            for (mi = 0U; mi < nc->method_count; mi++) {
                vigil_class_method_t *m = &decl->methods[mi];
                m->name = nc->methods[mi].name;
                m->name_length = nc->methods[mi].name_length;
                m->is_public = 1;
                m->function_index = (size_t)-1;
            }
            decl->method_count = nc->method_count;
        }

        program->class_count += 1U;
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_import(
    vigil_program_state_t *program,
    size_t *cursor
) {
    vigil_status_t status;
    const vigil_token_t *token;
    const vigil_token_t *import_target_token;
    const vigil_token_t *alias_token;
    const char *alias_text;
    size_t alias_length;
    vigil_string_t import_path;
    vigil_source_id_t imported_source_id;
    vigil_program_module_t *module;
    size_t native_idx;
    int native_found;

    alias_token = NULL;
    import_target_token = NULL;
    alias_text = NULL;
    alias_length = 0U;
    imported_source_id = 0U;
    module = NULL;
    native_idx = 0U;
    native_found = 0;
    vigil_string_init(&import_path, program->registry->runtime);

    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_IMPORT) {
        vigil_string_free(&import_path);
        return vigil_compile_report(
            program,
            token == NULL ? vigil_program_eof_span(program) : token->span,
            "expected 'import'"
        );
    }
    *cursor += 1U;

    token = vigil_program_token_at(program, *cursor);
    import_target_token = token;
    {
        const char *raw_import;
        size_t raw_import_len;

        raw_import = vigil_program_token_text(program, token, &raw_import_len);
        /* Strip quotes from the string literal. */
        if (raw_import != NULL && raw_import_len >= 2U) {
            raw_import += 1U;
            raw_import_len -= 2U;
        }
        /* Check native module registry first — skip file resolution. */
        if (
            program->natives != NULL &&
            raw_import != NULL &&
            vigil_native_registry_find_index(
                program->natives,
                raw_import, raw_import_len,
                &native_idx
            )
        ) {
            native_found = 1;
            imported_source_id = VIGIL_NATIVE_SOURCE_ID(native_idx);
        }
    }
    status = vigil_program_parse_import_target(program, token, &import_path);
    if (status != VIGIL_STATUS_OK) {
        vigil_string_free(&import_path);
        return status;
    }
    *cursor += 1U;

    token = vigil_program_token_at(program, *cursor);
    if (token != NULL && token->kind == VIGIL_TOKEN_AS) {
        *cursor += 1U;
        alias_token = vigil_program_token_at(program, *cursor);
        if (alias_token == NULL || alias_token->kind != VIGIL_TOKEN_IDENTIFIER) {
            vigil_string_free(&import_path);
            return vigil_compile_report(
                program,
                token->span,
                "expected import alias name"
            );
        }
        alias_text = vigil_program_token_text(program, alias_token, &alias_length);
        *cursor += 1U;
    }

    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_SEMICOLON) {
        vigil_string_free(&import_path);
        return vigil_compile_report(
            program,
            token == NULL ? vigil_program_eof_span(program) : token->span,
            "expected ';' after import"
        );
    }
    *cursor += 1U;

    if (!native_found && program->natives != NULL && import_target_token != NULL) {
        char message[128];
        const char *raw_import = NULL;
        size_t raw_import_len = 0U;
        int written;

        raw_import = vigil_program_token_text(program, import_target_token, &raw_import_len);
        if (raw_import != NULL && raw_import_len >= 2U) {
            raw_import += 1U;
            raw_import_len -= 2U;
        } else {
            raw_import = "";
            raw_import_len = 0U;
        }

        if (!vigil_stdlib_is_known_module(raw_import, raw_import_len)) {
            goto check_registered_import_source;
        }

        written = snprintf(
            message,
            sizeof(message),
            "stdlib module '%.*s' is not available in this build",
            (int)raw_import_len,
            raw_import
        );
        vigil_string_free(&import_path);
        if (written < 0 || (size_t)written >= sizeof(message)) {
            return vigil_compile_report(
                program,
                import_target_token->span,
                "stdlib module is not available in this build"
            );
        }
        return vigil_compile_report(program, import_target_token->span, message);
    }

check_registered_import_source:
    if (
        !native_found &&
        !vigil_program_find_source_by_path(
            program,
            vigil_string_c_str(&import_path),
            vigil_string_length(&import_path),
            &imported_source_id
        )
    ) {
        vigil_string_free(&import_path);
        return vigil_compile_report(
            program,
            import_target_token == NULL ? token->span : import_target_token->span,
            "imported source is not registered"
        );
    }

    module = vigil_program_current_module(program);
    if (module == NULL) {
        vigil_string_free(&import_path);
        vigil_error_set_literal(
            program->error,
            VIGIL_STATUS_INTERNAL,
            "current module must be available while parsing imports"
        );
        return VIGIL_STATUS_INTERNAL;
    }
    if (alias_text == NULL) {
        if (native_found) {
            alias_text = program->natives->modules[native_idx]->name;
            alias_length = program->natives->modules[native_idx]->name_length;
        } else {
            vigil_program_import_default_alias(
                vigil_string_c_str(&import_path),
                vigil_string_length(&import_path),
                &alias_text,
                &alias_length
            );
        }
    }
    if (alias_token != NULL &&
        program->natives != NULL &&
        vigil_stdlib_is_known_module(alias_text, alias_length)) {
        vigil_string_free(&import_path);
        return vigil_compile_report(
            program,
            alias_token->span,
            "import alias shadows a standard library module"
        );
    }
    status = vigil_program_add_module_import(
        program,
        module,
        alias_text,
        alias_length,
        alias_token == NULL ? token->span : alias_token->span,
        imported_source_id
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_string_free(&import_path);
        return status;
    }

    if (!native_found) {
        status = vigil_program_parse_source(program, imported_source_id);
    } else {
        /* Register array types for native function returns */
        status = vigil_program_register_native_function_types(
            program,
            program->natives->modules[native_idx]
        );
        if (status != VIGIL_STATUS_OK) {
            vigil_string_free(&import_path);
            return status;
        }
        if (program->natives->modules[native_idx]->class_count > 0U) {
            status = vigil_program_register_native_classes(
                program,
                program->natives->modules[native_idx],
                imported_source_id
            );
        }
    }
    vigil_string_free(&import_path);
    return status;
}

const vigil_token_t *vigil_program_cursor_peek(
    const vigil_program_state_t *program,
    size_t cursor
) {
    return vigil_program_token_at(program, cursor);
}

const vigil_token_t *vigil_program_cursor_advance(
    const vigil_program_state_t *program,
    size_t *cursor
) {
    const vigil_token_t *token;

    token = vigil_program_cursor_peek(program, *cursor);
    if (token != NULL && token->kind != VIGIL_TOKEN_EOF) {
        *cursor += 1U;
    }
    return token;
}

int vigil_program_parse_optional_pub(
    const vigil_program_state_t *program,
    size_t *cursor
) {
    const vigil_token_t *token;

    token = vigil_program_cursor_peek(program, *cursor);
    if (token != NULL && token->kind == VIGIL_TOKEN_PUB) {
        *cursor += 1U;
        return 1;
    }

    return 0;
}


static int vigil_program_is_global_variable_declaration_start(
    const vigil_program_state_t *program,
    size_t cursor
) {
    const vigil_token_t *name_token;
    const vigil_token_t *assign_token;
    size_t lookahead;

    lookahead = cursor;
    if (!vigil_program_skip_type_reference_syntax(program, &lookahead)) {
        return 0;
    }

    name_token = vigil_program_token_at(program, lookahead);
    assign_token = vigil_program_token_at(program, lookahead + 1U);
    return name_token != NULL &&
           assign_token != NULL &&
           name_token->kind == VIGIL_TOKEN_IDENTIFIER &&
           assign_token->kind == VIGIL_TOKEN_ASSIGN;
}

vigil_status_t vigil_program_parse_constant_expression(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
);

static int vigil_parse_integer_literal_text(
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

static vigil_status_t vigil_program_parse_constant_int(
    vigil_program_state_t *program,
    const vigil_token_t *token,
    vigil_value_t *out_value
) {
    const char *text;
    size_t length;
    unsigned long long parsed;

    text = vigil_program_token_text(program, token, &length);
    if (text == NULL || length == 0U) {
        return vigil_compile_report(program, token->span, "invalid integer literal");
    }
    if (!vigil_parse_integer_literal_text(text, length, &parsed)) {
        return vigil_compile_report(program, token->span, "invalid integer literal");
    }

    if (parsed <= (unsigned long long)INT64_MAX) {
        vigil_value_init_int(out_value, (int64_t)parsed);
    } else {
        vigil_value_init_uint(out_value, (uint64_t)parsed);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_constant_float(
    vigil_program_state_t *program,
    const vigil_token_t *token,
    vigil_value_t *out_value
) {
    const char *text;
    size_t length;
    char buffer[128];
    char *end;
    double parsed;

    text = vigil_program_token_text(program, token, &length);
    if (text == NULL || length == 0U) {
        return vigil_compile_report(program, token->span, "invalid float literal");
    }
    if (length >= sizeof(buffer)) {
        return vigil_compile_report(program, token->span, "float literal is too long");
    }

    memcpy(buffer, text, length);
    buffer[length] = '\0';
    errno = 0;
    parsed = strtod(buffer, &end);
    if (end == buffer || *end != '\0') {
        return vigil_compile_report(program, token->span, "invalid float literal");
    }
    /* Reject overflow (HUGE_VAL) but allow underflow to zero/denormal. */
    if (errno == ERANGE && (parsed == HUGE_VAL || parsed == -HUGE_VAL)) {
        return vigil_compile_report(program, token->span, "invalid float literal");
    }

    vigil_value_init_float(out_value, parsed);
    return VIGIL_STATUS_OK;
}

static int vigil_program_integer_type_signed_bounds(
    vigil_type_kind_t kind,
    int64_t *out_minimum,
    int64_t *out_maximum
) {
    int64_t minimum_value;
    int64_t maximum_value;

    switch (kind) {
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

    if (out_minimum != NULL) {
        *out_minimum = minimum_value;
    }
    if (out_maximum != NULL) {
        *out_maximum = maximum_value;
    }
    return 1;
}

static int vigil_program_integer_type_unsigned_max(
    vigil_type_kind_t kind,
    uint64_t *out_maximum
) {
    uint64_t maximum_value;

    switch (kind) {
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

    if (out_maximum != NULL) {
        *out_maximum = maximum_value;
    }
    return 1;
}

static vigil_parser_type_t vigil_program_integer_literal_type(
    const vigil_value_t *value
) {
    if (value == NULL) {
        return vigil_binding_type_invalid();
    }
    if (vigil_value_kind(value) == VIGIL_VALUE_UINT) {
        return vigil_binding_type_primitive(VIGIL_TYPE_U64);
    }
    if (
        vigil_value_kind(value) == VIGIL_VALUE_INT &&
        vigil_value_as_int(value) > (int64_t)INT32_MAX
    ) {
        return vigil_binding_type_primitive(VIGIL_TYPE_I64);
    }
    return vigil_binding_type_primitive(VIGIL_TYPE_I32);
}

static vigil_status_t vigil_program_validate_integer_value_for_type(
    vigil_program_state_t *program,
    vigil_source_span_t span,
    vigil_parser_type_t target_type,
    const vigil_value_t *value
) {
    int64_t minimum_value;
    int64_t maximum_value;
    uint64_t maximum_unsigned;

    if (!vigil_parser_type_is_integer(target_type) || value == NULL) {
        return VIGIL_STATUS_OK;
    }

    if (vigil_parser_type_is_signed_integer(target_type)) {
        if (vigil_value_kind(value) == VIGIL_VALUE_UINT) {
            return vigil_compile_report(
                program,
                span,
                "integer arithmetic overflow or invalid operation"
            );
        }
        if (
            !vigil_program_integer_type_signed_bounds(
                target_type.kind,
                &minimum_value,
                &maximum_value
            )
        ) {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        if (
            vigil_value_as_int(value) < minimum_value ||
            vigil_value_as_int(value) > maximum_value
        ) {
            return vigil_compile_report(
                program,
                span,
                "integer arithmetic overflow or invalid operation"
            );
        }
        return VIGIL_STATUS_OK;
    }

    if (
        !vigil_program_integer_type_unsigned_max(
            target_type.kind,
            &maximum_unsigned
        )
    ) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (vigil_value_kind(value) == VIGIL_VALUE_INT) {
        if (vigil_value_as_int(value) < 0) {
            return vigil_compile_report(
                program,
                span,
                "integer arithmetic overflow or invalid operation"
            );
        }
        if ((uint64_t)vigil_value_as_int(value) > maximum_unsigned) {
            return vigil_compile_report(
                program,
                span,
                "integer arithmetic overflow or invalid operation"
            );
        }
        return VIGIL_STATUS_OK;
    }
    if (vigil_value_kind(value) != VIGIL_VALUE_UINT ||
        vigil_value_as_uint(value) > maximum_unsigned) {
        return vigil_compile_report(
            program,
            span,
            "integer arithmetic overflow or invalid operation"
        );
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_convert_constant_to_integer(
    vigil_program_state_t *program,
    vigil_source_span_t span,
    vigil_type_kind_t target_kind,
    const vigil_constant_result_t *argument,
    vigil_constant_result_t *out_result
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
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_parser_type_equal(argument->type, vigil_binding_type_primitive(target_kind))) {
        out_result->type = vigil_binding_type_primitive(target_kind);
        out_result->value = vigil_value_copy(&argument->value);
        return VIGIL_STATUS_OK;
    }

    if (vigil_parser_type_is_integer(argument->type)) {
        out_result->type = vigil_binding_type_primitive(target_kind);
        if (vigil_parser_type_is_signed_integer(out_result->type)) {
            if (
                !vigil_program_integer_type_signed_bounds(
                    target_kind,
                    &minimum_value,
                    &maximum_value
                )
            ) {
                return VIGIL_STATUS_INVALID_ARGUMENT;
            }
            if (vigil_value_kind(&argument->value) == VIGIL_VALUE_UINT) {
                if (vigil_value_as_uint(&argument->value) > (uint64_t)maximum_value) {
                    return vigil_compile_report(
                        program,
                        span,
                        "integer conversion overflow or invalid value"
                    );
                }
                vigil_value_init_int(
                    &out_result->value,
                    (int64_t)vigil_value_as_uint(&argument->value)
                );
            } else {
                if (vigil_value_as_int(&argument->value) < minimum_value ||
                    vigil_value_as_int(&argument->value) > maximum_value) {
                    return vigil_compile_report(
                        program,
                        span,
                        "integer conversion overflow or invalid value"
                    );
                }
                vigil_value_init_int(&out_result->value, vigil_value_as_int(&argument->value));
            }
        } else {
            if (
                !vigil_program_integer_type_unsigned_max(
                    target_kind,
                    &maximum_unsigned
                )
            ) {
                return VIGIL_STATUS_INVALID_ARGUMENT;
            }
            if (vigil_value_kind(&argument->value) == VIGIL_VALUE_INT) {
                if (vigil_value_as_int(&argument->value) < 0 ||
                    (uint64_t)vigil_value_as_int(&argument->value) > maximum_unsigned) {
                    return vigil_compile_report(
                        program,
                        span,
                        "integer conversion overflow or invalid value"
                    );
                }
                vigil_value_init_uint(
                    &out_result->value,
                    (uint64_t)vigil_value_as_int(&argument->value)
                );
            } else {
                if (vigil_value_as_uint(&argument->value) > maximum_unsigned) {
                    return vigil_compile_report(
                        program,
                        span,
                        "integer conversion overflow or invalid value"
                    );
                }
                vigil_value_init_uint(&out_result->value, vigil_value_as_uint(&argument->value));
            }
        }
        return VIGIL_STATUS_OK;
    }

    if (!vigil_parser_type_is_f64(argument->type)) {
        return vigil_compile_report(
            program,
            span,
            "integer conversions require an integer or f64 argument"
        );
    }

    float_value = vigil_value_as_float(&argument->value);
    out_result->type = vigil_binding_type_primitive(target_kind);
    if (vigil_parser_type_is_signed_integer(out_result->type)) {
        int64_t integer_value;

        if (
            !vigil_program_integer_type_signed_bounds(
                target_kind,
                &minimum_value,
                &maximum_value
            )
        ) {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        if (!isfinite(float_value) ||
            float_value < (double)INT64_MIN ||
            float_value > (double)INT64_MAX) {
            return vigil_compile_report(
                program,
                span,
                "integer conversion overflow or invalid value"
            );
        }

        integer_value = (int64_t)float_value;
        if (integer_value < minimum_value || integer_value > maximum_value) {
            return vigil_compile_report(
                program,
                span,
                "integer conversion overflow or invalid value"
            );
        }
        vigil_value_init_int(&out_result->value, integer_value);
    } else {
        uint64_t integer_value;

        if (
            !vigil_program_integer_type_unsigned_max(
                target_kind,
                &maximum_unsigned
            )
        ) {
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }
        if (!isfinite(float_value) || float_value < 0.0 || float_value > (double)UINT64_MAX) {
            return vigil_compile_report(
                program,
                span,
                "integer conversion overflow or invalid value"
            );
        }

        integer_value = (uint64_t)float_value;
        if (integer_value > maximum_unsigned) {
            return vigil_compile_report(
                program,
                span,
                "integer conversion overflow or invalid value"
            );
        }
        vigil_value_init_uint(&out_result->value, integer_value);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_convert_constant_to_string(
    vigil_program_state_t *program,
    vigil_source_span_t span,
    const vigil_constant_result_t *argument,
    vigil_constant_result_t *out_result
) {
    vigil_status_t status;
    vigil_object_t *object;
    const char *text;
    char buffer[128];
    int written;

    if (program == NULL || argument == NULL || out_result == NULL) {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (vigil_parser_type_is_string(argument->type)) {
        out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_STRING);
        out_result->value = vigil_value_copy(&argument->value);
        return VIGIL_STATUS_OK;
    }

    object = NULL;
    if (vigil_parser_type_is_bool(argument->type)) {
        text = vigil_value_as_bool(&argument->value) ? "true" : "false";
        status = vigil_string_object_new_cstr(
            program->registry->runtime,
            text,
            &object,
            program->error
        );
    } else if (vigil_parser_type_is_integer(argument->type)) {
        if (vigil_value_kind(&argument->value) == VIGIL_VALUE_UINT) {
            written = snprintf(
                buffer,
                sizeof(buffer),
                "%llu",
                (unsigned long long)vigil_value_as_uint(&argument->value)
            );
        } else {
            written = snprintf(
                buffer,
                sizeof(buffer),
                "%lld",
                (long long)vigil_value_as_int(&argument->value)
            );
        }
        if (written < 0 || (size_t)written >= sizeof(buffer)) {
            return vigil_compile_report(program, span, "failed to format integer constant");
        }
        status = vigil_string_object_new(
            program->registry->runtime,
            buffer,
            (size_t)written,
            &object,
            program->error
        );
    } else if (vigil_parser_type_is_f64(argument->type)) {
        written = snprintf(buffer, sizeof(buffer), "%.17g", vigil_value_as_float(&argument->value));
        if (written < 0 || (size_t)written >= sizeof(buffer)) {
            return vigil_compile_report(program, span, "failed to format float constant");
        }
        status = vigil_string_object_new(
            program->registry->runtime,
            buffer,
            (size_t)written,
            &object,
            program->error
        );
    } else {
        return vigil_compile_report(
            program,
            span,
            "string(...) requires a string, integer, f64, or bool argument"
        );
    }
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    vigil_value_init_object(&out_result->value, &object);
    vigil_object_release(&object);
    out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_STRING);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_constant_builtin_conversion(
    vigil_program_state_t *program,
    size_t *cursor,
    const vigil_token_t *name_token,
    vigil_type_kind_t target_kind,
    vigil_constant_result_t *out_result
) {
    vigil_status_t status;
    vigil_constant_result_t argument;
    const vigil_token_t *token;

    vigil_constant_result_clear(&argument);
    token = vigil_program_cursor_peek(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_LPAREN) {
        return vigil_compile_report(program, name_token->span, "expected '(' after conversion name");
    }
    vigil_program_cursor_advance(program, cursor);

    status = vigil_program_parse_constant_expression(program, cursor, &argument);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    if (
        vigil_program_cursor_peek(program, *cursor) != NULL &&
        vigil_program_cursor_peek(program, *cursor)->kind == VIGIL_TOKEN_COMMA
    ) {
        vigil_constant_result_release(&argument);
        return vigil_compile_report(
            program,
            name_token->span,
            "built-in conversions accept exactly one argument"
        );
    }
    token = vigil_program_cursor_peek(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_RPAREN) {
        vigil_constant_result_release(&argument);
        return vigil_compile_report(
            program,
            name_token->span,
            "expected ')' after conversion argument"
        );
    }
    vigil_program_cursor_advance(program, cursor);

    switch (target_kind) {
        case VIGIL_TYPE_I32:
        case VIGIL_TYPE_I64:
        case VIGIL_TYPE_U8:
        case VIGIL_TYPE_U32:
        case VIGIL_TYPE_U64:
            status = vigil_program_convert_constant_to_integer(
                program,
                name_token->span,
                target_kind,
                &argument,
                out_result
            );
            break;
        case VIGIL_TYPE_F64:
            if (vigil_parser_type_is_f64(argument.type)) {
                out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_F64);
                out_result->value = vigil_value_copy(&argument.value);
                status = VIGIL_STATUS_OK;
            } else if (vigil_parser_type_is_integer(argument.type)) {
                if (vigil_value_kind(&argument.value) == VIGIL_VALUE_UINT) {
                    vigil_value_init_float(
                        &out_result->value,
                        (double)vigil_value_as_uint(&argument.value)
                    );
                } else {
                    vigil_value_init_float(
                        &out_result->value,
                        (double)vigil_value_as_int(&argument.value)
                    );
                }
                out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_F64);
                status = VIGIL_STATUS_OK;
            } else {
                status = vigil_compile_report(
                    program,
                    name_token->span,
                    "f64(...) requires an integer or f64 argument"
                );
            }
            break;
        case VIGIL_TYPE_STRING:
            status = vigil_program_convert_constant_to_string(
                program,
                name_token->span,
                &argument,
                out_result
            );
            break;
        case VIGIL_TYPE_BOOL:
            if (!vigil_parser_type_is_bool(argument.type)) {
                status = vigil_compile_report(
                    program,
                    name_token->span,
                    "bool(...) requires a bool argument"
                );
            } else {
                out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
                out_result->value = vigil_value_copy(&argument.value);
                status = VIGIL_STATUS_OK;
            }
            break;
        default:
            status = vigil_compile_report(
                program,
                name_token->span,
                "unsupported built-in conversion"
            );
            break;
    }

    vigil_constant_result_release(&argument);
    return status;
}

static vigil_status_t vigil_program_parse_constant_primary(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
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
    if (token == NULL) {
        return vigil_compile_report(
            program,
            vigil_program_eof_span(program),
            "expected constant expression"
        );
    }

    switch (token->kind) {
        case VIGIL_TOKEN_INT_LITERAL:
            vigil_program_cursor_advance(program, cursor);
            status = vigil_program_parse_constant_int(program, token, &out_result->value);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            out_result->type = vigil_program_integer_literal_type(&out_result->value);
            return VIGIL_STATUS_OK;
        case VIGIL_TOKEN_FLOAT_LITERAL:
            vigil_program_cursor_advance(program, cursor);
            status = vigil_program_parse_constant_float(program, token, &out_result->value);
            if (status != VIGIL_STATUS_OK) {
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
            status = vigil_program_parse_string_literal_value(
                program,
                token,
                &out_result->value
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_STRING);
            return VIGIL_STATUS_OK;
        case VIGIL_TOKEN_IDENTIFIER:
            vigil_program_cursor_advance(program, cursor);
            name_text = vigil_program_token_text(program, token, &name_length);
            if (
                vigil_program_cursor_peek(program, *cursor) != NULL &&
                vigil_program_cursor_peek(program, *cursor)->kind == VIGIL_TOKEN_LPAREN
            ) {
                vigil_type_kind_t conversion_kind;

                conversion_kind = vigil_type_kind_from_name(name_text, name_length);
                if (
                    conversion_kind == VIGIL_TYPE_I32 ||
                    conversion_kind == VIGIL_TYPE_I64 ||
                    conversion_kind == VIGIL_TYPE_U8 ||
                    conversion_kind == VIGIL_TYPE_U32 ||
                    conversion_kind == VIGIL_TYPE_U64 ||
                    conversion_kind == VIGIL_TYPE_F64 ||
                    conversion_kind == VIGIL_TYPE_STRING ||
                    conversion_kind == VIGIL_TYPE_BOOL
                ) {
                    return vigil_program_parse_constant_builtin_conversion(
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
                vigil_program_cursor_peek(program, *cursor) != NULL &&
                vigil_program_cursor_peek(program, *cursor)->kind == VIGIL_TOKEN_DOT &&
                vigil_program_resolve_import_alias(program, name_text, name_length, &source_id)
            ) {
                vigil_program_cursor_advance(program, cursor);
                member_token = vigil_program_cursor_peek(program, *cursor);
                if (member_token == NULL || member_token->kind != VIGIL_TOKEN_IDENTIFIER) {
                    return vigil_compile_report(
                        program,
                        token->span,
                        "unknown global constant"
                    );
                }
                vigil_program_cursor_advance(program, cursor);
                member_text = vigil_program_token_text(program, member_token, &member_length);
                if (
                    vigil_program_cursor_peek(program, *cursor) != NULL &&
                    vigil_program_cursor_peek(program, *cursor)->kind == VIGIL_TOKEN_DOT
                ) {
                    vigil_program_cursor_advance(program, cursor);
                    enum_member_token = vigil_program_cursor_peek(program, *cursor);
                    if (
                        enum_member_token == NULL ||
                        enum_member_token->kind != VIGIL_TOKEN_IDENTIFIER
                    ) {
                        return vigil_compile_report(
                            program,
                            member_token->span,
                            "unknown enum member"
                        );
                    }
                    vigil_program_cursor_advance(program, cursor);
                    enum_member_text = vigil_program_token_text(
                        program,
                        enum_member_token,
                        &enum_member_length
                    );
                    if (
                        !vigil_program_lookup_enum_member_in_source(
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
                        return vigil_compile_report(
                            program,
                            enum_member_token->span,
                            "unknown enum member"
                        );
                    }
                    if (!vigil_program_is_enum_public(&program->enums[enum_index])) {
                        return vigil_compile_report(
                            program,
                            member_token->span,
                            "module member is not public"
                        );
                    }
                    vigil_value_init_int(&out_result->value, enum_member->value);
                    out_result->type = vigil_binding_type_enum(enum_index);
                    return VIGIL_STATUS_OK;
                }
                if (
                    !vigil_program_find_constant_in_source(
                        program,
                        source_id,
                        member_text,
                        member_length,
                        &constant
                    )
                ) {
                    return vigil_compile_report(
                        program,
                        member_token->span,
                        "unknown global constant"
                    );
                }
                if (!vigil_program_is_constant_public(constant)) {
                    return vigil_compile_report(
                        program,
                        member_token->span,
                        "module member is not public"
                    );
                }
                out_result->type = constant->type;
                out_result->value = vigil_value_copy(&constant->value);
                return VIGIL_STATUS_OK;
            }
            if (
                vigil_program_cursor_peek(program, *cursor) != NULL &&
                vigil_program_cursor_peek(program, *cursor)->kind == VIGIL_TOKEN_DOT
            ) {
                vigil_program_cursor_advance(program, cursor);
                member_token = vigil_program_cursor_peek(program, *cursor);
                if (member_token == NULL || member_token->kind != VIGIL_TOKEN_IDENTIFIER) {
                    return vigil_compile_report(program, token->span, "unknown enum member");
                }
                vigil_program_cursor_advance(program, cursor);
                member_text = vigil_program_token_text(program, member_token, &member_length);
                if (
                    !vigil_program_lookup_enum_member(
                        program,
                        name_text,
                        name_length,
                        member_text,
                        member_length,
                        &enum_index,
                        &enum_member
                    )
                ) {
                    return vigil_compile_report(program, member_token->span, "unknown enum member");
                }
                vigil_value_init_int(&out_result->value, enum_member->value);
                out_result->type = vigil_binding_type_enum(enum_index);
                return VIGIL_STATUS_OK;
            }
            if (!vigil_program_find_constant(program, name_text, name_length, &constant)) {
                return vigil_compile_report(
                    program,
                    token->span,
                    "unknown global constant"
                );
            }
            out_result->type = constant->type;
            out_result->value = vigil_value_copy(&constant->value);
            return VIGIL_STATUS_OK;
        case VIGIL_TOKEN_LPAREN:
            vigil_program_cursor_advance(program, cursor);
            status = vigil_program_parse_constant_expression(program, cursor, out_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            token = vigil_program_cursor_peek(program, *cursor);
            if (token == NULL || token->kind != VIGIL_TOKEN_RPAREN) {
                return vigil_compile_report(
                    program,
                    token == NULL ? vigil_program_eof_span(program) : token->span,
                    "expected ')' after constant expression"
                );
            }
            vigil_program_cursor_advance(program, cursor);
            return VIGIL_STATUS_OK;
        default:
            return vigil_compile_report(
                program,
                token->span,
                "expected constant expression"
            );
    }
}

static vigil_status_t vigil_program_parse_constant_unary(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *token;
    vigil_constant_result_t operand;
    int64_t integer_result;
    double float_result;

    vigil_constant_result_clear(&operand);
    token = vigil_program_cursor_peek(program, *cursor);
    if (
        token != NULL &&
        (token->kind == VIGIL_TOKEN_MINUS || token->kind == VIGIL_TOKEN_BANG)
    ) {
        vigil_program_cursor_advance(program, cursor);
        status = vigil_program_parse_constant_unary(program, cursor, &operand);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        if (token->kind == VIGIL_TOKEN_MINUS) {
            if (vigil_parser_type_is_signed_integer(operand.type)) {
                vigil_parser_type_t integer_type;

                integer_type = operand.type;
                status = vigil_program_checked_negate(
                    vigil_value_as_int(&operand.value),
                    &integer_result
                );
                vigil_constant_result_release(&operand);
                if (status != VIGIL_STATUS_OK) {
                    return vigil_compile_report(
                        program,
                        token->span,
                        "integer arithmetic overflow or invalid operation"
                    );
                }
                status = vigil_program_validate_integer_value_for_type(
                    program,
                    token->span,
                    integer_type,
                    &(vigil_value_t){ vigil_nanbox_encode_int(integer_result ) }
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                vigil_value_init_int(&out_result->value, integer_result);
                out_result->type = integer_type;
                return VIGIL_STATUS_OK;
            }
            if (!vigil_parser_type_is_f64(operand.type)) {
                vigil_constant_result_release(&operand);
                return vigil_compile_report(
                    program,
                    token->span,
                    "unary '-' requires a signed integer or f64 operand"
                );
            }
            float_result = -vigil_value_as_float(&operand.value);
            vigil_constant_result_release(&operand);
            vigil_value_init_float(&out_result->value, float_result);
            out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_F64);
            return VIGIL_STATUS_OK;
        }

        if (!vigil_parser_type_equal(operand.type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL))) {
            vigil_constant_result_release(&operand);
            return vigil_compile_report(
                program,
                token->span,
                "logical '!' requires a bool operand"
            );
        }
        vigil_value_init_bool(&out_result->value, !vigil_value_as_bool(&operand.value));
        vigil_constant_result_release(&operand);
        out_result->type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        return VIGIL_STATUS_OK;
    }

    return vigil_program_parse_constant_primary(program, cursor, out_result);
}

static vigil_status_t vigil_program_parse_constant_factor(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
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
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = vigil_program_cursor_peek(program, *cursor);
        if (
            token == NULL ||
            (token->kind != VIGIL_TOKEN_STAR &&
             token->kind != VIGIL_TOKEN_SLASH &&
             token->kind != VIGIL_TOKEN_PERCENT)
        ) {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_unary(program, cursor, &right);
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            return status;
        }
        if (vigil_parser_type_is_integer(left.type) &&
            vigil_parser_type_equal(left.type, right.type)) {
            if (vigil_parser_type_is_unsigned_integer(left.type)) {
                switch (token->kind) {
                    case VIGIL_TOKEN_STAR:
                        status = vigil_program_checked_umultiply(
                            vigil_value_as_uint(&left.value),
                            vigil_value_as_uint(&right.value),
                            &uinteger_result
                        );
                        break;
                    case VIGIL_TOKEN_SLASH:
                        status = vigil_program_checked_udivide(
                            vigil_value_as_uint(&left.value),
                            vigil_value_as_uint(&right.value),
                            &uinteger_result
                        );
                        break;
                    default:
                        status = vigil_program_checked_umodulo(
                            vigil_value_as_uint(&left.value),
                            vigil_value_as_uint(&right.value),
                            &uinteger_result
                        );
                        break;
                }
            } else {
                switch (token->kind) {
                    case VIGIL_TOKEN_STAR:
                        status = vigil_program_checked_multiply(
                            vigil_value_as_int(&left.value),
                            vigil_value_as_int(&right.value),
                            &integer_result
                        );
                        break;
                    case VIGIL_TOKEN_SLASH:
                        status = vigil_program_checked_divide(
                            vigil_value_as_int(&left.value),
                            vigil_value_as_int(&right.value),
                            &integer_result
                        );
                        break;
                    default:
                        status = vigil_program_checked_modulo(
                            vigil_value_as_int(&left.value),
                            vigil_value_as_int(&right.value),
                            &integer_result
                        );
                        break;
                }
            }
            if (status != VIGIL_STATUS_OK) {
                vigil_constant_result_release(&left);
                vigil_constant_result_release(&right);
                return vigil_compile_report(
                    program,
                    token->span,
                    "integer arithmetic overflow or invalid operation"
                );
            }
            status = vigil_program_validate_integer_value_for_type(
                program,
                token->span,
                left.type,
                vigil_parser_type_is_unsigned_integer(left.type)
                    ? &(vigil_value_t){ vigil_nanbox_encode_uint(uinteger_result ) }
                    : &(vigil_value_t){ vigil_nanbox_encode_int(integer_result ) }
            );
            if (status != VIGIL_STATUS_OK) {
                vigil_constant_result_release(&left);
                vigil_constant_result_release(&right);
                return status;
            }

            {
                vigil_parser_type_t integer_type = left.type;

                vigil_constant_result_release(&left);
                if (vigil_parser_type_is_unsigned_integer(integer_type)) {
                    vigil_value_init_uint(&left.value, uinteger_result);
                } else {
                    vigil_value_init_int(&left.value, integer_result);
                }
                left.type = integer_type;
            }
            vigil_constant_result_release(&right);
            continue;
        }
        if (
            token->kind != VIGIL_TOKEN_PERCENT &&
            vigil_parser_type_is_f64(left.type) &&
            vigil_parser_type_is_f64(right.type)
        ) {
            switch (token->kind) {
                case VIGIL_TOKEN_STAR:
                    float_result = vigil_value_as_float(&left.value) *
                                   vigil_value_as_float(&right.value);
                    break;
                default:
                    float_result = vigil_value_as_float(&left.value) /
                                   vigil_value_as_float(&right.value);
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
            return vigil_compile_report(
                program,
                token->span,
                token->kind == VIGIL_TOKEN_PERCENT
                    ? "modulo requires matching integer operands"
                    : "arithmetic operators require matching integer or f64 operands"
            );
        }
    }
}

static vigil_status_t vigil_program_parse_constant_term(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
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
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || (token->kind != VIGIL_TOKEN_PLUS && token->kind != VIGIL_TOKEN_MINUS)) {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_factor(program, cursor, &right);
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            return status;
        }
        if (token->kind == VIGIL_TOKEN_PLUS &&
            vigil_parser_type_equal(left.type, vigil_binding_type_primitive(VIGIL_TYPE_STRING)) &&
            vigil_parser_type_equal(right.type, vigil_binding_type_primitive(VIGIL_TYPE_STRING))) {
            vigil_value_t concatenated;

            vigil_value_init_nil(&concatenated);
            status = vigil_program_concat_string_values(
                program,
                &left.value,
                &right.value,
                &concatenated
            );
            if (status != VIGIL_STATUS_OK) {
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
        if (vigil_parser_type_is_integer(left.type) &&
            vigil_parser_type_equal(left.type, right.type)) {
            if (vigil_parser_type_is_unsigned_integer(left.type)) {
                if (token->kind == VIGIL_TOKEN_PLUS) {
                    status = vigil_program_checked_uadd(
                        vigil_value_as_uint(&left.value),
                        vigil_value_as_uint(&right.value),
                        &uinteger_result
                    );
                } else {
                    status = vigil_program_checked_usubtract(
                        vigil_value_as_uint(&left.value),
                        vigil_value_as_uint(&right.value),
                        &uinteger_result
                    );
                }
            } else {
                if (token->kind == VIGIL_TOKEN_PLUS) {
                    status = vigil_program_checked_add(
                        vigil_value_as_int(&left.value),
                        vigil_value_as_int(&right.value),
                        &integer_result
                    );
                } else {
                    status = vigil_program_checked_subtract(
                        vigil_value_as_int(&left.value),
                        vigil_value_as_int(&right.value),
                        &integer_result
                    );
                }
            }
            if (status != VIGIL_STATUS_OK) {
                vigil_constant_result_release(&left);
                vigil_constant_result_release(&right);
                return vigil_compile_report(
                    program,
                    token->span,
                    "integer arithmetic overflow or invalid operation"
                );
            }
            status = vigil_program_validate_integer_value_for_type(
                program,
                token->span,
                left.type,
                vigil_parser_type_is_unsigned_integer(left.type)
                    ? &(vigil_value_t){ vigil_nanbox_encode_uint(uinteger_result ) }
                    : &(vigil_value_t){ vigil_nanbox_encode_int(integer_result ) }
            );
            if (status != VIGIL_STATUS_OK) {
                vigil_constant_result_release(&left);
                vigil_constant_result_release(&right);
                return status;
            }

            {
                vigil_parser_type_t integer_type = left.type;

                vigil_constant_result_release(&left);
                if (vigil_parser_type_is_unsigned_integer(integer_type)) {
                    vigil_value_init_uint(&left.value, uinteger_result);
                } else {
                    vigil_value_init_int(&left.value, integer_result);
                }
                left.type = integer_type;
            }
            vigil_constant_result_release(&right);
            continue;
        }
        if (vigil_parser_type_is_f64(left.type) && vigil_parser_type_is_f64(right.type)) {
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
            return vigil_compile_report(
                program,
                token->span,
                token->kind == VIGIL_TOKEN_PLUS
                    ? "'+' requires matching integer, f64, or string operands"
                    : "arithmetic operators require matching integer or f64 operands"
            );
        }
    }
}

static vigil_status_t vigil_program_parse_constant_shift(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_term(program, cursor, &left);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = vigil_program_cursor_peek(program, *cursor);
        if (
            token == NULL ||
            (token->kind != VIGIL_TOKEN_SHIFT_LEFT &&
             token->kind != VIGIL_TOKEN_SHIFT_RIGHT)
        ) {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_term(program, cursor, &right);
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_is_integer(left.type) ||
            !vigil_parser_type_equal(left.type, right.type)) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(
                program,
                token->span,
                "shift operators require matching integer operands"
            );
        }

        if (vigil_parser_type_is_unsigned_integer(left.type)) {
            if (token->kind == VIGIL_TOKEN_SHIFT_LEFT) {
                status = vigil_program_checked_ushift_left(
                    vigil_value_as_uint(&left.value),
                    vigil_value_as_uint(&right.value),
                    &uinteger_result
                );
            } else {
                status = vigil_program_checked_ushift_right(
                    vigil_value_as_uint(&left.value),
                    vigil_value_as_uint(&right.value),
                    &uinteger_result
                );
            }
        } else {
            if (token->kind == VIGIL_TOKEN_SHIFT_LEFT) {
                status = vigil_program_checked_shift_left(
                    vigil_value_as_int(&left.value),
                    vigil_value_as_int(&right.value),
                    &integer_result
                );
            } else {
                status = vigil_program_checked_shift_right(
                    vigil_value_as_int(&left.value),
                    vigil_value_as_int(&right.value),
                    &integer_result
                );
            }
        }
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(
                program,
                token->span,
                "integer arithmetic overflow or invalid operation"
            );
        }
        status = vigil_program_validate_integer_value_for_type(
            program,
            token->span,
            left.type,
            vigil_parser_type_is_unsigned_integer(left.type)
                ? &(vigil_value_t){ vigil_nanbox_encode_uint(uinteger_result ) }
                : &(vigil_value_t){ vigil_nanbox_encode_int(integer_result ) }
        );
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return status;
        }

        {
            vigil_parser_type_t integer_type = left.type;

            vigil_constant_result_release(&left);
            if (vigil_parser_type_is_unsigned_integer(integer_type)) {
                vigil_value_init_uint(&left.value, uinteger_result);
            } else {
                vigil_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_comparison(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int comparison_result;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_shift(program, cursor, &left);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = vigil_program_cursor_peek(program, *cursor);
        if (
            token == NULL ||
            (token->kind != VIGIL_TOKEN_GREATER &&
             token->kind != VIGIL_TOKEN_GREATER_EQUAL &&
             token->kind != VIGIL_TOKEN_LESS &&
             token->kind != VIGIL_TOKEN_LESS_EQUAL)
        ) {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_shift(program, cursor, &right);
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            return status;
        }
        if (
            (!vigil_parser_type_is_integer(left.type) ||
             !vigil_parser_type_equal(left.type, right.type)) &&
            (!vigil_parser_type_is_f64(left.type) || !vigil_parser_type_is_f64(right.type)) &&
            !(vigil_parser_type_is_string(left.type) && vigil_parser_type_is_string(right.type))
        ) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(
                program,
                token->span,
                "comparison operators require matching integer, f64, or string operands"
            );
        }

        if (vigil_parser_type_is_integer(left.type)) {
            if (vigil_parser_type_is_unsigned_integer(left.type)) {
                switch (token->kind) {
                    case VIGIL_TOKEN_GREATER:
                        comparison_result =
                            vigil_value_as_uint(&left.value) > vigil_value_as_uint(&right.value);
                        break;
                    case VIGIL_TOKEN_GREATER_EQUAL:
                        comparison_result =
                            vigil_value_as_uint(&left.value) >= vigil_value_as_uint(&right.value);
                        break;
                    case VIGIL_TOKEN_LESS:
                        comparison_result =
                            vigil_value_as_uint(&left.value) < vigil_value_as_uint(&right.value);
                        break;
                    default:
                        comparison_result =
                            vigil_value_as_uint(&left.value) <= vigil_value_as_uint(&right.value);
                        break;
                }
            } else {
                switch (token->kind) {
                    case VIGIL_TOKEN_GREATER:
                        comparison_result =
                            vigil_value_as_int(&left.value) > vigil_value_as_int(&right.value);
                        break;
                    case VIGIL_TOKEN_GREATER_EQUAL:
                        comparison_result =
                            vigil_value_as_int(&left.value) >= vigil_value_as_int(&right.value);
                        break;
                    case VIGIL_TOKEN_LESS:
                        comparison_result =
                            vigil_value_as_int(&left.value) < vigil_value_as_int(&right.value);
                        break;
                    default:
                        comparison_result =
                            vigil_value_as_int(&left.value) <= vigil_value_as_int(&right.value);
                        break;
                }
            }
        } else {
            switch (token->kind) {
                case VIGIL_TOKEN_GREATER:
                    comparison_result =
                        vigil_value_as_float(&left.value) > vigil_value_as_float(&right.value);
                    break;
                case VIGIL_TOKEN_GREATER_EQUAL:
                    comparison_result =
                        vigil_value_as_float(&left.value) >= vigil_value_as_float(&right.value);
                    break;
                case VIGIL_TOKEN_LESS:
                    comparison_result =
                        vigil_value_as_float(&left.value) < vigil_value_as_float(&right.value);
                    break;
                default:
                    comparison_result =
                        vigil_value_as_float(&left.value) <= vigil_value_as_float(&right.value);
                    break;
            }
        }

        vigil_constant_result_release(&left);
        vigil_value_init_bool(&left.value, comparison_result);
        left.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_equality(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int is_equal;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_comparison(program, cursor, &left);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = vigil_program_cursor_peek(program, *cursor);
        if (
            token == NULL ||
            (token->kind != VIGIL_TOKEN_EQUAL_EQUAL && token->kind != VIGIL_TOKEN_BANG_EQUAL)
        ) {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_comparison(program, cursor, &right);
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_equal(left.type, right.type)) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(
                program,
                token->span,
                "equality operands must have matching types"
            );
        }

        is_equal = vigil_program_values_equal(&left.value, &right.value);
        if (token->kind == VIGIL_TOKEN_BANG_EQUAL) {
            is_equal = !is_equal;
        }
        vigil_constant_result_release(&left);
        vigil_value_init_bool(&left.value, is_equal);
        left.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_bitwise_and(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_equality(program, cursor, &left);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_AMPERSAND) {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_equality(program, cursor, &right);
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_is_integer(left.type) ||
            !vigil_parser_type_equal(left.type, right.type)) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(
                program,
                token->span,
                "bitwise operators require matching integer operands"
            );
        }

        if (vigil_parser_type_is_unsigned_integer(left.type)) {
            uinteger_result = vigil_value_as_uint(&left.value) & vigil_value_as_uint(&right.value);
        } else {
            integer_result = vigil_value_as_int(&left.value) & vigil_value_as_int(&right.value);
        }
        status = vigil_program_validate_integer_value_for_type(
            program,
            token->span,
            left.type,
            vigil_parser_type_is_unsigned_integer(left.type)
                ? &(vigil_value_t){ vigil_nanbox_encode_uint(uinteger_result ) }
                : &(vigil_value_t){ vigil_nanbox_encode_int(integer_result ) }
        );
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return status;
        }
        {
            vigil_parser_type_t integer_type = left.type;

            vigil_constant_result_release(&left);
            if (vigil_parser_type_is_unsigned_integer(integer_type)) {
                vigil_value_init_uint(&left.value, uinteger_result);
            } else {
                vigil_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_bitwise_xor(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_bitwise_and(program, cursor, &left);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_CARET) {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_bitwise_and(program, cursor, &right);
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_is_integer(left.type) ||
            !vigil_parser_type_equal(left.type, right.type)) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(
                program,
                token->span,
                "bitwise operators require matching integer operands"
            );
        }

        if (vigil_parser_type_is_unsigned_integer(left.type)) {
            uinteger_result = vigil_value_as_uint(&left.value) ^ vigil_value_as_uint(&right.value);
        } else {
            integer_result = vigil_value_as_int(&left.value) ^ vigil_value_as_int(&right.value);
        }
        status = vigil_program_validate_integer_value_for_type(
            program,
            token->span,
            left.type,
            vigil_parser_type_is_unsigned_integer(left.type)
                ? &(vigil_value_t){ vigil_nanbox_encode_uint(uinteger_result ) }
                : &(vigil_value_t){ vigil_nanbox_encode_int(integer_result ) }
        );
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return status;
        }
        {
            vigil_parser_type_t integer_type = left.type;

            vigil_constant_result_release(&left);
            if (vigil_parser_type_is_unsigned_integer(integer_type)) {
                vigil_value_init_uint(&left.value, uinteger_result);
            } else {
                vigil_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_bitwise_or(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int64_t integer_result = 0;
    uint64_t uinteger_result = 0U;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_bitwise_xor(program, cursor, &left);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_PIPE) {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_bitwise_xor(program, cursor, &right);
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            return status;
        }
        if (!vigil_parser_type_is_integer(left.type) ||
            !vigil_parser_type_equal(left.type, right.type)) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(
                program,
                token->span,
                "bitwise operators require matching integer operands"
            );
        }

        if (vigil_parser_type_is_unsigned_integer(left.type)) {
            uinteger_result = vigil_value_as_uint(&left.value) | vigil_value_as_uint(&right.value);
        } else {
            integer_result = vigil_value_as_int(&left.value) | vigil_value_as_int(&right.value);
        }
        status = vigil_program_validate_integer_value_for_type(
            program,
            token->span,
            left.type,
            vigil_parser_type_is_unsigned_integer(left.type)
                ? &(vigil_value_t){ vigil_nanbox_encode_uint(uinteger_result ) }
                : &(vigil_value_t){ vigil_nanbox_encode_int(integer_result ) }
        );
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return status;
        }
        {
            vigil_parser_type_t integer_type = left.type;

            vigil_constant_result_release(&left);
            if (vigil_parser_type_is_unsigned_integer(integer_type)) {
                vigil_value_init_uint(&left.value, uinteger_result);
            } else {
                vigil_value_init_int(&left.value, integer_result);
            }
            left.type = integer_type;
        }
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_logical_and(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int boolean_result;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_bitwise_or(program, cursor, &left);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_AMPERSAND_AMPERSAND) {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_bitwise_or(program, cursor, &right);
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            return status;
        }
        if (
            !vigil_parser_type_equal(left.type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL)) ||
            !vigil_parser_type_equal(right.type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL))
        ) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(
                program,
                token->span,
                "logical '&&' requires bool operands"
            );
        }

        boolean_result =
            vigil_value_as_bool(&left.value) && vigil_value_as_bool(&right.value);
        vigil_constant_result_release(&left);
        vigil_value_init_bool(&left.value, boolean_result);
        left.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        vigil_constant_result_release(&right);
    }
}

static vigil_status_t vigil_program_parse_constant_logical_or(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
    vigil_status_t status;
    vigil_constant_result_t left;
    vigil_constant_result_t right;
    const vigil_token_t *token;
    int boolean_result;

    vigil_constant_result_clear(&left);
    vigil_constant_result_clear(&right);
    status = vigil_program_parse_constant_logical_and(program, cursor, &left);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        token = vigil_program_cursor_peek(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_PIPE_PIPE) {
            *out_result = left;
            return VIGIL_STATUS_OK;
        }
        vigil_program_cursor_advance(program, cursor);
        vigil_constant_result_clear(&right);
        status = vigil_program_parse_constant_logical_and(program, cursor, &right);
        if (status != VIGIL_STATUS_OK) {
            vigil_constant_result_release(&left);
            return status;
        }
        if (
            !vigil_parser_type_equal(left.type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL)) ||
            !vigil_parser_type_equal(right.type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL))
        ) {
            vigil_constant_result_release(&left);
            vigil_constant_result_release(&right);
            return vigil_compile_report(
                program,
                token->span,
                "logical '||' requires bool operands"
            );
        }

        boolean_result =
            vigil_value_as_bool(&left.value) || vigil_value_as_bool(&right.value);
        vigil_constant_result_release(&left);
        vigil_value_init_bool(&left.value, boolean_result);
        left.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
        vigil_constant_result_release(&right);
    }
}

vigil_status_t vigil_program_parse_constant_expression(
    vigil_program_state_t *program,
    size_t *cursor,
    vigil_constant_result_t *out_result
) {
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

    status = vigil_program_parse_constant_logical_or(
        program,
        cursor,
        &condition_result
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    question_token = vigil_program_cursor_peek(program, *cursor);
    if (question_token == NULL || question_token->kind != VIGIL_TOKEN_QUESTION) {
        *out_result = condition_result;
        return VIGIL_STATUS_OK;
    }

    if (
        !vigil_parser_type_equal(
            condition_result.type,
            vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
        )
    ) {
        vigil_constant_result_release(&condition_result);
        return vigil_compile_report(
            program,
            question_token->span,
            "ternary condition must be bool"
        );
    }

    vigil_program_cursor_advance(program, cursor);
    status = vigil_program_parse_constant_expression(program, cursor, &then_result);
    if (status != VIGIL_STATUS_OK) {
        vigil_constant_result_release(&condition_result);
        return status;
    }

    colon_token = vigil_program_cursor_peek(program, *cursor);
    if (colon_token == NULL || colon_token->kind != VIGIL_TOKEN_COLON) {
        vigil_constant_result_release(&condition_result);
        vigil_constant_result_release(&then_result);
        return vigil_compile_report(
            program,
            question_token->span,
            "expected ':' in ternary expression"
        );
    }
    vigil_program_cursor_advance(program, cursor);

    status = vigil_program_parse_constant_expression(program, cursor, &else_result);
    if (status != VIGIL_STATUS_OK) {
        vigil_constant_result_release(&condition_result);
        vigil_constant_result_release(&then_result);
        return status;
    }

    if (!vigil_parser_type_equal(then_result.type, else_result.type)) {
        vigil_constant_result_release(&condition_result);
        vigil_constant_result_release(&then_result);
        vigil_constant_result_release(&else_result);
        return vigil_compile_report(
            program,
            colon_token->span,
            "ternary branches must have the same type"
        );
    }

    take_then_branch = vigil_value_as_bool(&condition_result.value);
    if (take_then_branch) {
        *out_result = then_result;
        vigil_constant_result_clear(&then_result);
    } else {
        *out_result = else_result;
        vigil_constant_result_clear(&else_result);
    }

    vigil_constant_result_release(&condition_result);
    vigil_constant_result_release(&then_result);
    vigil_constant_result_release(&else_result);
    return VIGIL_STATUS_OK;
}


/* ── extern fn declarations ───────────────────────────────────────── */

static const char *vigil_type_to_ffi_sig(vigil_type_kind_t kind) {
    switch (kind) {
        case VIGIL_TYPE_I32:    return "i32";
        case VIGIL_TYPE_I64:    return "i64";
        case VIGIL_TYPE_U8:     return "u8";
        case VIGIL_TYPE_U32:    return "u32";
        case VIGIL_TYPE_F64:    return "f64";
        case VIGIL_TYPE_BOOL:   return "i32";
        case VIGIL_TYPE_STRING: return "ptr";
        case VIGIL_TYPE_VOID:   return "void";
        default:                return "ptr";
    }
}

static vigil_status_t vigil_program_parse_extern_fn(
    vigil_program_state_t *program,
    size_t *cursor,
    int is_public
) {
    vigil_status_t status;
    const vigil_token_t *token;
    const vigil_token_t *name_token;
    const vigil_token_t *type_token;
    const vigil_token_t *param_name_token;
    vigil_function_decl_t *decl;
    vigil_extern_fn_decl_t *ext;
    const char *name_text;
    size_t name_length;

    /* Skip 'extern'. */
    token = vigil_program_token_at(program, *cursor);
    *cursor += 1U;

    /* Expect 'fn'. */
    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_FN) {
        return vigil_compile_report(program, token ? token->span :
            vigil_program_eof_span(program), "expected 'fn' after 'extern'");
    }
    *cursor += 1U;

    /* Function name. */
    name_token = vigil_program_token_at(program, *cursor);
    if (name_token == NULL || name_token->kind != VIGIL_TOKEN_IDENTIFIER) {
        return vigil_compile_report(program, token->span,
            "expected function name after 'extern fn'");
    }
    name_text = vigil_program_token_text(program, name_token, &name_length);
    *cursor += 1U;

    /* Register in function table so call resolution works normally. */
    status = vigil_program_grow_functions(program, program->functions.count + 1U);
    if (status != VIGIL_STATUS_OK) return status;

    decl = &program->functions.functions[program->functions.count];
    vigil_binding_function_init(decl);
    decl->name = name_text;
    decl->name_length = name_length;
    decl->name_span = name_token->span;
    decl->is_public = is_public;
    decl->source = program->source;
    decl->tokens = program->tokens;

    /* Parse parameter list: '(' type name, ... ')' */
    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_LPAREN) {
        return vigil_program_fail_partial_decl(program, decl,
            vigil_compile_report(program, name_token->span,
                "expected '(' after extern function name"));
    }
    *cursor += 1U;

    token = vigil_program_token_at(program, *cursor);
    if (token != NULL && token->kind != VIGIL_TOKEN_RPAREN) {
        while (1) {
            vigil_parser_type_t param_type;
            type_token = vigil_program_token_at(program, *cursor);
            status = vigil_program_parse_type_reference(
                program, cursor, "unsupported extern parameter type", &param_type);
            if (status != VIGIL_STATUS_OK)
                return vigil_program_fail_partial_decl(program, decl, status);
            status = vigil_program_require_non_void_type(
                program, type_token ? type_token->span : name_token->span,
                param_type, "extern parameters cannot use type void");
            if (status != VIGIL_STATUS_OK)
                return vigil_program_fail_partial_decl(program, decl, status);

            param_name_token = vigil_program_token_at(program, *cursor);
            if (param_name_token == NULL ||
                param_name_token->kind != VIGIL_TOKEN_IDENTIFIER) {
                return vigil_program_fail_partial_decl(program, decl,
                    vigil_compile_report(program,
                        type_token ? type_token->span : name_token->span,
                        "expected parameter name"));
            }
            status = vigil_program_add_param(program, decl, param_type,
                                             param_name_token);
            if (status != VIGIL_STATUS_OK)
                return vigil_program_fail_partial_decl(program, decl, status);
            *cursor += 1U;

            token = vigil_program_token_at(program, *cursor);
            if (token != NULL && token->kind == VIGIL_TOKEN_COMMA) {
                *cursor += 1U;
                continue;
            }
            break;
        }
    }

    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_RPAREN) {
        return vigil_program_fail_partial_decl(program, decl,
            vigil_compile_report(program, name_token->span,
                "expected ')' after extern parameter list"));
    }
    *cursor += 1U;

    /* Parse return type: '-> type' */
    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_ARROW) {
        return vigil_program_fail_partial_decl(program, decl,
            vigil_compile_report(program, name_token->span,
                "expected '->' after extern function signature"));
    }
    *cursor += 1U;

    status = vigil_program_parse_function_return_types(
        program, cursor, "unsupported extern return type", decl);
    if (status != VIGIL_STATUS_OK)
        return vigil_program_fail_partial_decl(program, decl, status);

    /* Parse 'from "libpath"' clause. */
    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_IDENTIFIER) {
        return vigil_program_fail_partial_decl(program, decl,
            vigil_compile_report(program, name_token->span,
                "expected 'from' after extern return type"));
    }
    {
        const char *kw;
        size_t kw_len;
        kw = vigil_program_token_text(program, token, &kw_len);
        if (kw_len != 4U || memcmp(kw, "from", 4U) != 0) {
            return vigil_program_fail_partial_decl(program, decl,
                vigil_compile_report(program, token->span,
                    "expected 'from' after extern return type"));
        }
    }
    *cursor += 1U;

    token = vigil_program_token_at(program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_STRING_LITERAL) {
        return vigil_program_fail_partial_decl(program, decl,
            vigil_compile_report(program, name_token->span,
                "expected library path string after 'from'"));
    }
    *cursor += 1U;

    /* Expect semicolon. */
    {
        const vigil_token_t *semi = vigil_program_token_at(program, *cursor);
        if (semi == NULL || semi->kind != VIGIL_TOKEN_SEMICOLON) {
            return vigil_program_fail_partial_decl(program, decl,
                vigil_compile_report(program, name_token->span,
                    "expected ';' after extern fn declaration"));
        }
        *cursor += 1U;
    }

    /* Mark as extern: body_start == body_end == 0 signals no body. */
    decl->body_start = 0U;
    decl->body_end = 0U;

    /* Grow extern fn table. */
    if (program->extern_fn_count >= program->extern_fn_capacity) {
        size_t new_cap = program->extern_fn_capacity == 0U ? 4U :
                         program->extern_fn_capacity * 2U;
        vigil_extern_fn_decl_t *new_arr = realloc(
            program->extern_fns, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            vigil_error_set_literal(program->error, VIGIL_STATUS_OUT_OF_MEMORY,
                                   "extern fn table alloc failed");
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        program->extern_fns = new_arr;
        program->extern_fn_capacity = new_cap;
    }
    ext = &program->extern_fns[program->extern_fn_count];
    memset(ext, 0, sizeof(*ext));
    ext->name = name_text;
    ext->name_length = name_length;
    ext->name_span = name_token->span;
    ext->is_public = is_public;
    ext->source_id = program->source->id;
    ext->function_index = program->functions.count;

    /* Extract library path from the string literal token (strip quotes). */
    {
        const char *lib_text;
        size_t lib_len;
        lib_text = vigil_program_token_text(program, token, &lib_len);
        if (lib_len >= 2U && lib_text[0] == '"') {
            lib_text++;
            lib_len -= 2U;
        }
        if (lib_len >= sizeof(ext->lib_path)) lib_len = sizeof(ext->lib_path) - 1;
        memcpy(ext->lib_path, lib_text, lib_len);
        ext->lib_path[lib_len] = '\0';
    }

    /* C function name defaults to Vigil name. */
    if (name_length >= sizeof(ext->c_name)) name_length = sizeof(ext->c_name) - 1;
    memcpy(ext->c_name, name_text, name_length);
    ext->c_name[name_length] = '\0';

    /* Build FFI signature string: "ret(p1,p2,...)". */
    {
        char *s = ext->sig;
        size_t rem = sizeof(ext->sig);
        size_t n;
        const char *rt = vigil_type_to_ffi_sig(decl->return_type.kind);
        n = strlen(rt);
        if (n < rem) { memcpy(s, rt, n); s += n; rem -= n; }
        if (rem > 1) { *s++ = '('; rem--; }
        for (size_t i = 0; i < decl->param_count; i++) {
            if (i > 0 && rem > 1) { *s++ = ','; rem--; }
            const char *pt = vigil_type_to_ffi_sig(decl->params[i].type.kind);
            n = strlen(pt);
            if (n < rem) { memcpy(s, pt, n); s += n; rem -= n; }
        }
        if (rem > 1) { *s++ = ')'; rem--; }
        *s = '\0';
    }

    /* Store params for type checking at call sites. */
    ext->return_type = decl->return_type;
    ext->params = decl->params;
    ext->param_count = decl->param_count;

    program->extern_fn_count++;
    program->functions.count += 1U;

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_declarations(
    vigil_program_state_t *program
) {
    vigil_status_t status;
    size_t cursor;
    const vigil_token_t *token;
    const vigil_token_t *name_token;
    const vigil_token_t *type_token;
    const vigil_token_t *param_name_token;
    vigil_function_decl_t *decl;
    const char *name_text;
    size_t name_length;
    size_t body_depth;
    int is_public;

    cursor = 0U;
    while (1) {
        token = vigil_program_token_at(program, cursor);
        if (token == NULL || token->kind == VIGIL_TOKEN_EOF) {
            break;
        }
        is_public = vigil_program_parse_optional_pub(program, &cursor);
        token = vigil_program_token_at(program, cursor);
        if (token == NULL || token->kind == VIGIL_TOKEN_EOF) {
            return vigil_compile_report(
                program,
                vigil_program_eof_span(program),
                "expected declaration after 'pub'"
            );
        }
        if (token->kind == VIGIL_TOKEN_IMPORT) {
            if (is_public) {
                return vigil_compile_report(program, token->span, "imports cannot be declared 'pub'");
            }
            status = vigil_program_parse_import(program, &cursor);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (token->kind == VIGIL_TOKEN_CONST) {
            status = vigil_program_parse_constant_declaration(program, &cursor, is_public);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (token->kind == VIGIL_TOKEN_ENUM) {
            status = vigil_program_parse_enum_declaration(program, &cursor, is_public);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (token->kind == VIGIL_TOKEN_INTERFACE) {
            status = vigil_program_parse_interface_declaration(program, &cursor, is_public);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (token->kind == VIGIL_TOKEN_CLASS) {
            status = vigil_program_parse_class_declaration(program, &cursor, is_public);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (vigil_program_is_global_variable_declaration_start(program, cursor)) {
            status = vigil_program_parse_global_variable_declaration(program, &cursor, is_public);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (token->kind == VIGIL_TOKEN_EXTERN) {
            status = vigil_program_parse_extern_fn(program, &cursor, is_public);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }
        if (token->kind != VIGIL_TOKEN_FN) {
            if (program->compile_mode == VIGIL_COMPILE_MODE_REPL && !is_public) {
                /* In REPL mode, non-declaration tokens are top-level
                   statements.  Record their bounds for the synthetic main. */
                size_t end_cursor = cursor;
                size_t depth = 0;
                while (1) {
                    const vigil_token_t *t = vigil_program_token_at(program, end_cursor);
                    if (t == NULL || t->kind == VIGIL_TOKEN_EOF) break;
                    if (t->kind == VIGIL_TOKEN_LBRACE) depth++;
                    else if (t->kind == VIGIL_TOKEN_RBRACE) { if (depth) depth--; }
                    end_cursor++;
                }
                program->repl_stmts_start = cursor;
                program->repl_stmts_end = end_cursor;
                break;
            }
            return vigil_compile_report(
                program,
                token->span,
                "expected top-level 'import', 'const', 'enum', 'interface', 'class', variable declaration, 'extern fn', or 'fn'"
            );
        }
        cursor += 1U;

        name_token = vigil_program_token_at(program, cursor);
        if (name_token == NULL || name_token->kind != VIGIL_TOKEN_IDENTIFIER) {
            return vigil_compile_report(
                program,
                token->span,
                "expected function name"
            );
        }

        name_text = vigil_program_token_text(program, name_token, &name_length);
        if (program->compile_mode != VIGIL_COMPILE_MODE_REPL) {
        if (
            vigil_program_find_top_level_function_name_in_source(
                program,
                program->source->id,
                name_text,
                name_length,
                NULL,
                NULL
            )
        ) {
            return vigil_compile_report(
                program,
                name_token->span,
                "function is already declared"
            );
        }
        if (
            vigil_program_find_constant_in_source(
                program,
                program->source->id,
                name_text,
                name_length,
                NULL
            )
        ) {
            return vigil_compile_report(
                program,
                name_token->span,
                "function name conflicts with global constant"
            );
        }
        if (
            vigil_program_find_global_in_source(
                program,
                program->source->id,
                name_text,
                name_length,
                NULL,
                NULL
            )
        ) {
            return vigil_compile_report(
                program,
                name_token->span,
                "function name conflicts with global variable"
            );
        }
        if (
            vigil_program_find_class_in_source(
                program,
                program->source->id,
                name_text,
                name_length,
                NULL,
                NULL
            )
        ) {
            return vigil_compile_report(
                program,
                name_token->span,
                "function name conflicts with class"
            );
        }
        if (
            vigil_program_find_enum_in_source(
                program,
                program->source->id,
                name_text,
                name_length,
                NULL,
                NULL
            )
        ) {
            return vigil_compile_report(
                program,
                name_token->span,
                "function name conflicts with enum"
            );
        }
        if (
            vigil_program_find_interface_in_source(
                program,
                program->source->id,
                name_text,
                name_length,
                NULL,
                NULL
            )
        ) {
            return vigil_compile_report(
                program,
                name_token->span,
                "function name conflicts with interface"
            );
        }
        } /* end REPL redefinition guard */

        status = vigil_program_grow_functions(program, program->functions.count + 1U);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        decl = &program->functions.functions[program->functions.count];
        vigil_binding_function_init(decl);
        decl->name = name_text;
        decl->name_length = name_length;
        decl->name_span = name_token->span;
        decl->is_public = is_public;
        decl->source = program->source;
        decl->tokens = program->tokens;
        cursor += 1U;

        token = vigil_program_token_at(program, cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_LPAREN) {
            return vigil_program_fail_partial_decl(
                program,
                decl,
                vigil_compile_report(
                    program,
                    name_token->span,
                    "expected '(' after function name"
                )
            );
        }
        cursor += 1U;

        token = vigil_program_token_at(program, cursor);
        if (token != NULL && token->kind != VIGIL_TOKEN_RPAREN) {
            while (1) {
                vigil_parser_type_t param_type;

                type_token = vigil_program_token_at(program, cursor);
                status = vigil_program_parse_type_reference(
                    program,
                    &cursor,
                    "unsupported function parameter type",
                    &param_type
                );
                if (status != VIGIL_STATUS_OK) {
                    return vigil_program_fail_partial_decl(program, decl, status);
                }
                status = vigil_program_require_non_void_type(
                    program,
                    type_token == NULL ? decl->name_span : type_token->span,
                    param_type,
                    "function parameters cannot use type void"
                );
                if (status != VIGIL_STATUS_OK) {
                    return vigil_program_fail_partial_decl(program, decl, status);
                }

                param_name_token = vigil_program_token_at(program, cursor);
                if (param_name_token == NULL ||
                    param_name_token->kind != VIGIL_TOKEN_IDENTIFIER) {
                    return vigil_program_fail_partial_decl(
                        program,
                        decl,
                        vigil_compile_report(
                            program,
                            type_token->span,
                            "expected parameter name"
                        )
                    );
                }
                status = vigil_program_add_param(
                    program,
                    decl,
                    param_type,
                    param_name_token
                );
                if (status != VIGIL_STATUS_OK) {
                    return vigil_program_fail_partial_decl(program, decl, status);
                }
                cursor += 1U;

                token = vigil_program_token_at(program, cursor);
                if (token != NULL && token->kind == VIGIL_TOKEN_COMMA) {
                    cursor += 1U;
                    continue;
                }
                break;
            }
        }

        token = vigil_program_token_at(program, cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_RPAREN) {
            return vigil_program_fail_partial_decl(
                program,
                decl,
                vigil_compile_report(
                    program,
                    decl->name_span,
                    "expected ')' after parameter list"
                )
            );
        }
        cursor += 1U;

        token = vigil_program_token_at(program, cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_ARROW) {
            return vigil_program_fail_partial_decl(
                program,
                decl,
                vigil_compile_report(
                    program,
                    decl->name_span,
                    "expected '->' after function signature"
                )
            );
        }
        cursor += 1U;

        if (vigil_program_names_equal(name_text, name_length, "main", 4U)) {
            program->functions.main_index = program->functions.count;
            program->functions.has_main = 1;
            type_token = vigil_program_token_at(program, cursor);
            status = vigil_program_parse_function_return_types(
                program,
                &cursor,
                "main entrypoint must declare return type i32",
                decl
            );
            if (status != VIGIL_STATUS_OK) {
                return vigil_program_fail_partial_decl(program, decl, status);
            }
            status = vigil_program_validate_main_signature(program, decl, type_token);
            if (status != VIGIL_STATUS_OK) {
                return vigil_program_fail_partial_decl(program, decl, status);
            }
        } else {
            status = vigil_program_parse_function_return_types(
                program,
                &cursor,
                "unsupported function return type",
                decl
            );
            if (status != VIGIL_STATUS_OK) {
                return vigil_program_fail_partial_decl(program, decl, status);
            }
        }

        token = vigil_program_token_at(program, cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_LBRACE) {
            return vigil_program_fail_partial_decl(
                program,
                decl,
                vigil_compile_report(
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
            token = vigil_program_token_at(program, cursor);
            if (token == NULL || token->kind == VIGIL_TOKEN_EOF) {
                return vigil_program_fail_partial_decl(
                    program,
                    decl,
                    vigil_compile_report(
                        program,
                        vigil_program_eof_span(program),
                        "expected '}' after function body"
                    )
                );
            }

            if (token->kind == VIGIL_TOKEN_LBRACE) {
                body_depth += 1U;
            } else if (token->kind == VIGIL_TOKEN_RBRACE) {
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

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_program_parse_source(
    vigil_program_state_t *program,
    vigil_source_id_t source_id
) {
    vigil_status_t status;
    const vigil_source_file_t *previous_source;
    const vigil_token_list_t *previous_tokens;
    const vigil_source_file_t *source;
    vigil_token_list_t tokens;
    size_t module_index;

    module_index = 0U;
    if (vigil_program_module_find(program, source_id, &module_index)) {
        if (program->modules[module_index].state != VIGIL_MODULE_UNSEEN) {
            return VIGIL_STATUS_OK;
        }
    }

    source = vigil_source_registry_get(program->registry, source_id);
    if (source == NULL) {
        vigil_error_set_literal(
            program->error,
            VIGIL_STATUS_INVALID_ARGUMENT,
            "source_id must reference a registered source file"
        );
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    vigil_token_list_init(&tokens, program->registry->runtime);
    status = vigil_lex_source(
        program->registry,
        source_id,
        &tokens,
        program->diagnostics,
        program->error
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_token_list_free(&tokens);
        return status;
    }
    status = vigil_program_add_module(
        program,
        source_id,
        source,
        &tokens,
        VIGIL_MODULE_PARSING,
        &module_index
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_token_list_free(&tokens);
        return status;
    }
    previous_source = program->source;
    previous_tokens = program->tokens;
    vigil_program_set_module_context(program, source, program->modules[module_index].tokens);
    status = vigil_program_parse_declarations(program);
    vigil_program_set_module_context(program, previous_source, previous_tokens);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    program->modules[module_index].state = VIGIL_MODULE_PARSED;
    return VIGIL_STATUS_OK;
}

static const vigil_token_t *vigil_parser_previous(const vigil_parser_state_t *state);

static vigil_source_span_t vigil_parser_fallback_span(
    const vigil_parser_state_t *state
) {
    vigil_source_span_t span;
    const vigil_token_t *token;

    vigil_source_span_clear(&span);
    if (state == NULL || state->program == NULL || state->program->source == NULL) {
        return span;
    }

    span.source_id = state->program->source->id;
    token = vigil_parser_previous(state);
    if (token != NULL) {
        return token->span;
    }

    return span;
}

const vigil_token_t *vigil_parser_peek(const vigil_parser_state_t *state) {
    if (state == NULL || state->current >= state->body_end) {
        return NULL;
    }

    return vigil_program_token_at(state->program, state->current);
}

static const vigil_token_t *vigil_parser_previous(const vigil_parser_state_t *state) {
    if (state == NULL || state->current == 0U) {
        return NULL;
    }

    return vigil_program_token_at(state->program, state->current - 1U);
}

int vigil_parser_check(
    const vigil_parser_state_t *state,
    vigil_token_kind_t kind
) {
    const vigil_token_t *token;

    token = vigil_parser_peek(state);
    return token != NULL && token->kind == kind;
}

static int vigil_parser_is_at_end(const vigil_parser_state_t *state) {
    return vigil_parser_peek(state) == NULL;
}

static const vigil_token_t *vigil_parser_advance(vigil_parser_state_t *state) {
    if (!vigil_parser_is_at_end(state)) {
        state->current += 1U;
    }

    return vigil_parser_previous(state);
}

static int vigil_parser_match(
    vigil_parser_state_t *state,
    vigil_token_kind_t kind
) {
    if (!vigil_parser_check(state, kind)) {
        return 0;
    }

    vigil_parser_advance(state);
    return 1;
}

vigil_status_t vigil_parser_report(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    const char *message
) {
    return vigil_compile_report(state->program, span, message);
}

vigil_status_t vigil_parser_require_type(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    vigil_parser_type_t actual_type,
    vigil_parser_type_t expected_type,
    const char *message
) {
    if (vigil_program_type_is_assignable(state->program, expected_type, actual_type)) {
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_report(state, span, message);
}

static vigil_status_t vigil_parser_require_bool_type(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    vigil_parser_type_t actual_type,
    const char *message
) {
    return vigil_parser_require_type(
        state,
        span,
        actual_type,
        vigil_binding_type_primitive(VIGIL_TYPE_BOOL),
        message
    );
}

static vigil_status_t vigil_parser_require_same_type(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    vigil_parser_type_t left_type,
    vigil_parser_type_t right_type,
    const char *message
) {
    if (
        vigil_parser_type_supports_binary_operator(
            VIGIL_BINARY_OPERATOR_EQUAL,
            left_type,
            right_type
        )
    ) {
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_report(state, span, message);
}

static vigil_status_t vigil_parser_require_i32_operands(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    vigil_parser_type_t left_type,
    vigil_parser_type_t right_type,
    vigil_binary_operator_kind_t operator_kind,
    const char *message
) {
    if (vigil_parser_type_supports_binary_operator(operator_kind, left_type, right_type)) {
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_report(state, span, message);
}

static vigil_status_t vigil_parser_require_unary_operator(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    vigil_unary_operator_kind_t operator_kind,
    vigil_parser_type_t operand_type,
    const char *message
) {
    if (vigil_parser_type_supports_unary_operator(operator_kind, operand_type)) {
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_report(state, span, message);
}

vigil_status_t vigil_parser_expect(
    vigil_parser_state_t *state,
    vigil_token_kind_t kind,
    const char *message,
    const vigil_token_t **out_token
) {
    const vigil_token_t *token;

    token = vigil_parser_peek(state);
    if (token == NULL) {
        return vigil_parser_report(
            state,
            vigil_program_eof_span(state->program),
            message
        );
    }
    if (token->kind != kind) {
        return vigil_parser_report(state, token->span, message);
    }

    token = vigil_parser_advance(state);
    if (out_token != NULL) {
        *out_token = token;
    }
    return VIGIL_STATUS_OK;
}

const char *vigil_parser_token_text(
    const vigil_parser_state_t *state,
    const vigil_token_t *token,
    size_t *out_length
) {
    return vigil_program_token_text(state->program, token, out_length);
}

static vigil_status_t vigil_parser_parse_int_literal(
    vigil_parser_state_t *state,
    const vigil_token_t *token,
    vigil_value_t *out_value,
    vigil_parser_type_t *out_type
) {
    const char *text;
    size_t length;
    unsigned long long parsed;

    text = vigil_parser_token_text(state, token, &length);
    if (text == NULL || length == 0U) {
        return vigil_parser_report(state, token->span, "invalid integer literal");
    }
    if (!vigil_parse_integer_literal_text(text, length, &parsed)) {
        return vigil_parser_report(state, token->span, "invalid integer literal");
    }
    if (parsed <= (unsigned long long)INT64_MAX) {
        vigil_value_init_int(out_value, (int64_t)parsed);
    } else {
        vigil_value_init_uint(out_value, (uint64_t)parsed);
    }
    if (out_type != NULL) {
        *out_type = vigil_program_integer_literal_type(out_value);
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_float_literal(
    vigil_parser_state_t *state,
    const vigil_token_t *token,
    vigil_value_t *out_value
) {
    const char *text;
    size_t length;
    char buffer[128];
    char *end;
    double parsed;

    text = vigil_parser_token_text(state, token, &length);
    if (text == NULL || length == 0U) {
        return vigil_parser_report(state, token->span, "invalid float literal");
    }
    if (length >= sizeof(buffer)) {
        return vigil_parser_report(state, token->span, "float literal is too long");
    }

    memcpy(buffer, text, length);
    buffer[length] = '\0';
    errno = 0;
    parsed = strtod(buffer, &end);
    if (end == buffer || *end != '\0') {
        return vigil_parser_report(state, token->span, "invalid float literal");
    }
    /* Reject overflow (HUGE_VAL) but allow underflow to zero/denormal. */
    if (errno == ERANGE && (parsed == HUGE_VAL || parsed == -HUGE_VAL)) {
        return vigil_parser_report(state, token->span, "invalid float literal");
    }

    vigil_value_init_float(out_value, parsed);
    return VIGIL_STATUS_OK;
}

/*
 * Peephole: try to fuse GET_LOCAL + GET_LOCAL + <i64_op> into a single
 * LOCALS_<op>_I64 superinstruction.  Called just before emitting an i64
 * binary opcode.  If the last 10 emitted bytes are two GET_LOCAL
 * original opcode if fusion is not possible.
 */
static vigil_opcode_t vigil_parser_try_fuse_locals_i64(
    vigil_parser_state_t *state,
    vigil_opcode_t opcode,
    size_t pre_left_size
) {
    uint8_t *code;
    size_t len;
    vigil_opcode_t fused;

    len = state->chunk.code.length;
    /* Only fuse if exactly 10 bytes were emitted for the two operands
       (5 bytes each: 1 opcode + 4 u32).  This ensures we don't match
       stale GET_LOCALs from earlier code. */
    if (len < 10U || len - pre_left_size != 10U) {
        return opcode;
    }
    code = state->chunk.code.data;
    if (code[len - 10U] != VIGIL_OPCODE_GET_LOCAL ||
        code[len - 5U] != VIGIL_OPCODE_GET_LOCAL) {
        return opcode;
    }

    switch (opcode) {
        case VIGIL_OPCODE_ADD_I64:           fused = VIGIL_OPCODE_LOCALS_ADD_I64; break;
        case VIGIL_OPCODE_SUBTRACT_I64:      fused = VIGIL_OPCODE_LOCALS_SUBTRACT_I64; break;
        case VIGIL_OPCODE_MULTIPLY_I64:      fused = VIGIL_OPCODE_LOCALS_MULTIPLY_I64; break;
        case VIGIL_OPCODE_MODULO_I64:        fused = VIGIL_OPCODE_LOCALS_MODULO_I64; break;
        case VIGIL_OPCODE_LESS_I64:          fused = VIGIL_OPCODE_LOCALS_LESS_I64; break;
        case VIGIL_OPCODE_LESS_EQUAL_I64:    fused = VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64; break;
        case VIGIL_OPCODE_GREATER_I64:       fused = VIGIL_OPCODE_LOCALS_GREATER_I64; break;
        case VIGIL_OPCODE_GREATER_EQUAL_I64: fused = VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64; break;
        case VIGIL_OPCODE_EQUAL_I64:         fused = VIGIL_OPCODE_LOCALS_EQUAL_I64; break;
        case VIGIL_OPCODE_NOT_EQUAL_I64:     fused = VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64; break;
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
    return (vigil_opcode_t)255;
}

vigil_status_t vigil_parser_emit_opcode(
    vigil_parser_state_t *state,
    vigil_opcode_t opcode,
    vigil_source_span_t span
) {
    return vigil_chunk_write_opcode(&state->chunk, opcode, span, state->program->error);
}

/* Emit an i64 binary opcode, attempting superinstruction fusion first.
   If the preceding bytecode is GET_LOCAL + GET_LOCAL, rewrites to a
   single LOCALS_<op>_I64 superinstruction and returns OK without
   emitting a separate opcode byte. */
static vigil_status_t vigil_parser_emit_i64_binop(
    vigil_parser_state_t *state,
    vigil_opcode_t opcode,
    vigil_source_span_t span,
    size_t pre_left_size
) {
    vigil_opcode_t result = vigil_parser_try_fuse_locals_i64(state, opcode, pre_left_size);
    if (result == (vigil_opcode_t)255) {
        return VIGIL_STATUS_OK; /* fused — opcode already rewritten in place */
    }
    return vigil_parser_emit_opcode(state, result, span);
}

/*
 * Map an i64 opcode to its i32 equivalent.
 */
static vigil_opcode_t vigil_parser_i64_to_i32(vigil_opcode_t op) {
    switch (op) {
        case VIGIL_OPCODE_ADD_I64:           return VIGIL_OPCODE_ADD_I32;
        case VIGIL_OPCODE_SUBTRACT_I64:      return VIGIL_OPCODE_SUBTRACT_I32;
        case VIGIL_OPCODE_MULTIPLY_I64:      return VIGIL_OPCODE_MULTIPLY_I32;
        case VIGIL_OPCODE_DIVIDE_I64:        return VIGIL_OPCODE_DIVIDE_I32;
        case VIGIL_OPCODE_MODULO_I64:        return VIGIL_OPCODE_MODULO_I32;
        case VIGIL_OPCODE_LESS_I64:          return VIGIL_OPCODE_LESS_I32;
        case VIGIL_OPCODE_LESS_EQUAL_I64:    return VIGIL_OPCODE_LESS_EQUAL_I32;
        case VIGIL_OPCODE_GREATER_I64:       return VIGIL_OPCODE_GREATER_I32;
        case VIGIL_OPCODE_GREATER_EQUAL_I64: return VIGIL_OPCODE_GREATER_EQUAL_I32;
        case VIGIL_OPCODE_EQUAL_I64:         return VIGIL_OPCODE_EQUAL_I32;
        case VIGIL_OPCODE_NOT_EQUAL_I64:     return VIGIL_OPCODE_NOT_EQUAL_I32;
        default:                            return op;
    }
}

/*
 * Emit an i32 binary opcode.  Tries LOCALS fusion first (reusing the
 * i64 fusion which avoids push/pop).  If no fusion, emits the i32
 * stack opcode which skips i64 overflow checks.
 */
static vigil_status_t vigil_parser_emit_i32_binop(
    vigil_parser_state_t *state,
    vigil_opcode_t i64_opcode,
    vigil_source_span_t span,
    size_t pre_left_size
) {
    vigil_opcode_t fused = vigil_parser_try_fuse_locals_i64(state, i64_opcode, pre_left_size);
    if (fused == (vigil_opcode_t)255) {
        return VIGIL_STATUS_OK; /* LOCALS_*_I64 fusion succeeded */
    }
    /* No fusion — emit the i32 stack opcode. */
    return vigil_parser_emit_opcode(state, vigil_parser_i64_to_i32(i64_opcode), span);
}

vigil_status_t vigil_parser_emit_u32(
    vigil_parser_state_t *state,
    uint32_t value,
    vigil_source_span_t span
) {
    return vigil_chunk_write_u32(&state->chunk, value, span, state->program->error);
}

static vigil_status_t vigil_parser_emit_jump(
    vigil_parser_state_t *state,
    vigil_opcode_t opcode,
    vigil_source_span_t span,
    size_t *out_operand_offset
) {
    vigil_status_t status;

    status = vigil_parser_emit_opcode(state, opcode, span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (out_operand_offset != NULL) {
        *out_operand_offset = vigil_chunk_code_size(&state->chunk);
    }

    return vigil_parser_emit_u32(state, 0U, span);
}

static vigil_status_t vigil_parser_patch_u32(
    vigil_parser_state_t *state,
    size_t operand_offset,
    uint32_t value
) {
    uint8_t *code;
    size_t code_size;

    code = state->chunk.code.data;
    code_size = vigil_chunk_code_size(&state->chunk);
    if (code == NULL || operand_offset + 3U >= code_size) {
        vigil_error_set_literal(
            state->program->error,
            VIGIL_STATUS_INTERNAL,
            "jump patch offset is out of range"
        );
        return VIGIL_STATUS_INTERNAL;
    }

    code[operand_offset] = (uint8_t)(value & 0xffU);
    code[operand_offset + 1U] = (uint8_t)((value >> 8U) & 0xffU);
    code[operand_offset + 2U] = (uint8_t)((value >> 16U) & 0xffU);
    code[operand_offset + 3U] = (uint8_t)((value >> 24U) & 0xffU);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_patch_jump(
    vigil_parser_state_t *state,
    size_t operand_offset
) {
    size_t code_size;
    size_t jump_distance;

    code_size = vigil_chunk_code_size(&state->chunk);
    if (code_size < operand_offset + 4U) {
        vigil_error_set_literal(
            state->program->error,
            VIGIL_STATUS_INTERNAL,
            "jump patch target is invalid"
        );
        return VIGIL_STATUS_INTERNAL;
    }

    jump_distance = code_size - (operand_offset + 4U);
    if (jump_distance > UINT32_MAX) {
        vigil_error_set_literal(
            state->program->error,
            VIGIL_STATUS_OUT_OF_MEMORY,
            "jump distance overflow"
        );
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    return vigil_parser_patch_u32(state, operand_offset, (uint32_t)jump_distance);
}

static vigil_status_t vigil_parser_emit_loop(
    vigil_parser_state_t *state,
    size_t loop_start,
    vigil_source_span_t span
) {
    vigil_status_t status;
    size_t loop_end;
    size_t distance;

    loop_end = vigil_chunk_code_size(&state->chunk) + 5U;
    if (loop_end < loop_start) {
        vigil_error_set_literal(
            state->program->error,
            VIGIL_STATUS_INTERNAL,
            "loop target is invalid"
        );
        return VIGIL_STATUS_INTERNAL;
    }

    distance = loop_end - loop_start;
    if (distance > UINT32_MAX) {
        vigil_error_set_literal(
            state->program->error,
            VIGIL_STATUS_OUT_OF_MEMORY,
            "loop distance overflow"
        );
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_LOOP, span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    return vigil_parser_emit_u32(state, (uint32_t)distance, span);
}

static vigil_status_t vigil_parser_grow_loops(
    vigil_parser_state_t *state,
    size_t minimum_capacity
) {
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= state->loop_capacity) {
        return VIGIL_STATUS_OK;
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
        vigil_error_set_literal(
            state->program->error,
            VIGIL_STATUS_OUT_OF_MEMORY,
            "loop context allocation overflow"
        );
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = state->loops;
    if (memory == NULL) {
        status = vigil_runtime_alloc(
            state->program->registry->runtime,
            next_capacity * sizeof(*state->loops),
            &memory,
            state->program->error
        );
    } else {
        status = vigil_runtime_realloc(
            state->program->registry->runtime,
            &memory,
            next_capacity * sizeof(*state->loops),
            state->program->error
        );
        if (status == VIGIL_STATUS_OK) {
            memset(
                (vigil_loop_context_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*state->loops)
            );
        }
    }

    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    state->loops = (vigil_loop_context_t *)memory;
    state->loop_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_loop_context_grow_breaks(
    vigil_parser_state_t *state,
    vigil_loop_context_t *loop,
    size_t minimum_capacity
) {
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= loop->break_capacity) {
        return VIGIL_STATUS_OK;
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
        vigil_error_set_literal(
            state->program->error,
            VIGIL_STATUS_OUT_OF_MEMORY,
            "loop break table allocation overflow"
        );
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = loop->break_jumps;
    if (memory == NULL) {
        status = vigil_runtime_alloc(
            state->program->registry->runtime,
            next_capacity * sizeof(*loop->break_jumps),
            &memory,
            state->program->error
        );
    } else {
        status = vigil_runtime_realloc(
            state->program->registry->runtime,
            &memory,
            next_capacity * sizeof(*loop->break_jumps),
            state->program->error
        );
        if (status == VIGIL_STATUS_OK) {
            memset(
                (vigil_loop_jump_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*loop->break_jumps)
            );
        }
    }

    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    loop->break_jumps = (vigil_loop_jump_t *)memory;
    loop->break_capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_loop_context_t *vigil_parser_current_loop(
    vigil_parser_state_t *state
) {
    if (state == NULL || state->loop_count == 0U) {
        return NULL;
    }

    return &state->loops[state->loop_count - 1U];
}

static vigil_status_t vigil_parser_grow_jump_offsets(
    vigil_parser_state_t *state,
    size_t **offsets,
    size_t *capacity,
    size_t minimum_capacity,
    const char *overflow_message
) {
    vigil_status_t status;
    size_t old_capacity;
    size_t next_capacity;
    void *memory;

    if (minimum_capacity <= *capacity) {
        return VIGIL_STATUS_OK;
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
        vigil_error_set_literal(
            state->program->error,
            VIGIL_STATUS_OUT_OF_MEMORY,
            overflow_message
        );
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memory = *offsets;
    if (memory == NULL) {
        status = vigil_runtime_alloc(
            state->program->registry->runtime,
            next_capacity * sizeof(**offsets),
            &memory,
            state->program->error
        );
    } else {
        status = vigil_runtime_realloc(
            state->program->registry->runtime,
            &memory,
            next_capacity * sizeof(**offsets),
            state->program->error
        );
        if (status == VIGIL_STATUS_OK) {
            memset(
                (size_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(**offsets)
            );
        }
    }
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    *offsets = (size_t *)memory;
    *capacity = next_capacity;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_push_loop(
    vigil_parser_state_t *state,
    size_t loop_start
) {
    vigil_status_t status;
    vigil_loop_context_t *loop;

    status = vigil_parser_grow_loops(state, state->loop_count + 1U);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    loop = &state->loops[state->loop_count];
    memset(loop, 0, sizeof(*loop));
    loop->loop_start = loop_start;
    loop->scope_depth = vigil_binding_scope_stack_depth(&state->locals);
    state->loop_count += 1U;
    return VIGIL_STATUS_OK;
}

static void vigil_parser_pop_loop(
    vigil_parser_state_t *state
) {
    vigil_loop_context_t *loop;
    void *memory;

    if (state == NULL || state->loop_count == 0U) {
        return;
    }

    loop = &state->loops[state->loop_count - 1U];
    memory = loop->break_jumps;
    vigil_runtime_free(state->program->registry->runtime, &memory);
    memset(loop, 0, sizeof(*loop));
    state->loop_count -= 1U;
}

static vigil_status_t vigil_parser_emit_scope_cleanup_to_depth(
    vigil_parser_state_t *state,
    size_t target_depth,
    vigil_source_span_t span
) {
    vigil_status_t status;
    size_t count;
    size_t index;

    count = vigil_binding_scope_stack_count_above_depth(&state->locals, target_depth);
    for (index = 0U; index < count; index += 1U) {
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}

static int vigil_parser_local_matches_token(
    const vigil_parser_state_t *state,
    const vigil_token_t *left,
    const vigil_binding_local_t *right
) {
    size_t left_length;
    const char *left_text;

    left_text = vigil_parser_token_text(state, left, &left_length);
    return right != NULL &&
           left_text != NULL &&
           left_length == right->length &&
           memcmp(left_text, right->name, left_length) == 0;
}

static int vigil_parser_find_local_symbol(
    const vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    size_t *out_index
) {
    size_t i;

    for (i = vigil_binding_scope_stack_count(&state->locals); i > 0U; --i) {
        if (
            vigil_parser_local_matches_token(
                state,
                name_token,
                vigil_binding_scope_stack_local_at(&state->locals, i - 1U)
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

static vigil_status_t vigil_parser_resolve_local_symbol(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    size_t *out_index,
    vigil_parser_type_t *out_type,
    int *out_is_capture,
    size_t *out_capture_index,
    int *out_found
) {
    vigil_function_decl_t *decl;
    size_t local_index;
    size_t capture_index;
    size_t parent_local_index;
    vigil_parser_type_t parent_type;
    int parent_is_capture;
    int found;
    const char *name_text;
    size_t name_length;
    vigil_status_t status;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_type != NULL) {
        *out_type = vigil_binding_type_invalid();
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
        return VIGIL_STATUS_OK;
    }

    if (vigil_parser_find_local_symbol(state, name_token, &local_index)) {
        if (out_index != NULL) {
            *out_index = local_index;
        }
        if (out_type != NULL) {
            *out_type = vigil_binding_scope_stack_local_at(&state->locals, local_index)->type;
        }
        if (out_found != NULL) {
            *out_found = 1;
        }
        return VIGIL_STATUS_OK;
    }

    if (state->parent == NULL) {
        return VIGIL_STATUS_OK;
    }

    status = vigil_parser_resolve_local_symbol(
        state->parent,
        name_token,
        &parent_local_index,
        &parent_type,
        &parent_is_capture,
        NULL,
        &found
    );
    if (status != VIGIL_STATUS_OK || !found) {
        return status;
    }

    name_text = vigil_parser_token_text(state, name_token, &name_length);
    decl = vigil_binding_function_table_get_mutable(
        (vigil_binding_function_table_t *)&state->program->functions,
        state->function_index
    );
    if (decl == NULL) {
        vigil_error_set_literal(
            state->program->error,
            VIGIL_STATUS_INTERNAL,
            "nested function declaration is missing"
        );
        return VIGIL_STATUS_INTERNAL;
    }
    if (vigil_binding_function_find_capture(decl, name_text, name_length, &capture_index)) {
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
        return VIGIL_STATUS_OK;
    }

    status = vigil_binding_function_grow_captures(
        (vigil_program_state_t *)state->program,
        decl,
        decl->capture_count + 1U
    );
    if (status != VIGIL_STATUS_OK) {
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
    return VIGIL_STATUS_OK;
}

int vigil_program_find_top_level_function_name_in_source(
    const vigil_program_state_t *program,
    vigil_source_id_t source_id,
    const char *name_text,
    size_t name_length,
    size_t *out_index,
    const vigil_function_decl_t **out_decl
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
        if (program->functions.functions[i].owner_class_index != VIGIL_BINDING_INVALID_CLASS_INDEX) {
            continue;
        }
        if (
            program->functions.functions[i].source == NULL ||
            program->functions.functions[i].source->id != source_id
        ) {
            continue;
        }
        if (
            vigil_program_names_equal(
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

static vigil_status_t vigil_parser_declare_local_symbol(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    vigil_parser_type_t type,
    int is_const,
    size_t *out_index
) {
    vigil_status_t status;
    const char *name;
    size_t name_length;
    size_t slot;

    name = vigil_parser_token_text(state, name_token, &name_length);
    status = vigil_binding_scope_stack_declare_local(
        &state->locals,
        name,
        name_length,
        type,
        is_const,
        out_index,
        state->program->error
    );
    if (status == VIGIL_STATUS_INVALID_ARGUMENT) {
        return vigil_parser_report(
            state,
            name_token->span,
            "local variable is already declared in this scope"
        );
    }
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    /* Record debug info for this local. */
    slot = vigil_binding_scope_stack_count(&state->locals) - 1U;
    (void)vigil_debug_local_table_add(
        &state->chunk.debug_locals,
        name,
        name_length,
        slot,
        vigil_chunk_code_size(&state->chunk),
        state->program->error
    );

    return VIGIL_STATUS_OK;
}

static int vigil_parser_token_is_discard_identifier(
    const vigil_parser_state_t *state,
    const vigil_token_t *token
) {
    size_t length;
    const char *text;

    text = vigil_parser_token_text(state, token, &length);
    return text != NULL && length == 1U && text[0] == '_';
}

static vigil_status_t vigil_parser_parse_binding_target_list(
    vigil_parser_state_t *state,
    const char *unsupported_type_message,
    const char *non_void_message,
    const char *name_message,
    vigil_binding_target_list_t *targets
) {
    vigil_status_t status;
    vigil_parser_type_t declared_type;
    const vigil_token_t *name_token;
    const vigil_token_t *type_token;

    if (state == NULL || targets == NULL) {
        vigil_error_set_literal(
            state == NULL ? NULL : state->program->error,
            VIGIL_STATUS_INVALID_ARGUMENT,
            "binding target list arguments are invalid"
        );
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    do {
        status = vigil_program_parse_type_reference(
            state->program,
            &state->current,
            unsupported_type_message,
            &declared_type
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        type_token = vigil_parser_previous(state);
        status = vigil_program_require_non_void_type(
            state->program,
            type_token == NULL ? vigil_parser_fallback_span(state) : type_token->span,
            declared_type,
            non_void_message
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_IDENTIFIER,
            name_message,
            &name_token
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_binding_target_list_append(
            (vigil_program_state_t *)state->program,
            targets,
            declared_type,
            name_token,
            vigil_parser_token_is_discard_identifier(state, name_token)
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    } while (vigil_parser_match(state, VIGIL_TOKEN_COMMA));

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_require_binding_initializer_shape(
    vigil_parser_state_t *state,
    vigil_source_span_t span,
    const vigil_binding_target_list_t *targets,
    const vigil_expression_result_t *initializer_result,
    const char *count_message,
    const char *type_message
) {
    vigil_status_t status;
    size_t index;

    if (targets == NULL || initializer_result == NULL) {
        vigil_error_set_literal(
            state == NULL ? NULL : state->program->error,
            VIGIL_STATUS_INVALID_ARGUMENT,
            "binding initializer arguments are invalid"
        );
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (initializer_result->type_count != targets->count) {
        return vigil_parser_report(state, span, count_message);
    }

    for (index = 0U; index < targets->count; index += 1U) {
        status = vigil_parser_require_type(
            state,
            span,
            vigil_expression_result_type_at(initializer_result, index),
            targets->items[index].type,
            type_message
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_bind_targets(
    vigil_parser_state_t *state,
    const vigil_binding_target_list_t *targets,
    int is_const,
    size_t *out_last_slot
) {
    vigil_status_t status;
    size_t index;
    size_t slot_index;

    if (out_last_slot != NULL) {
        *out_last_slot = 0U;
    }
    if (state == NULL || targets == NULL) {
        vigil_error_set_literal(
            state == NULL ? NULL : state->program->error,
            VIGIL_STATUS_INVALID_ARGUMENT,
            "binding target arguments are invalid"
        );
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < targets->count; index += 1U) {
        slot_index = 0U;
        if (targets->items[index].is_discard) {
            status = vigil_binding_scope_stack_declare_hidden_local(
                &state->locals,
                targets->items[index].type,
                is_const,
                &slot_index,
                state->program->error
            );
        } else {
            status = vigil_parser_declare_local_symbol(
                state,
                targets->items[index].name_token,
                targets->items[index].type,
                is_const,
                &slot_index
            );
        }
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (out_last_slot != NULL) {
            *out_last_slot = slot_index;
        }
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_parser_parse_expression(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
);

static int vigil_builtin_error_kind_by_name(
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
        if (vigil_program_names_equal(name, length, kinds[index].name, kinds[index].length)) {
            if (out_kind != NULL) {
                *out_kind = kinds[index].kind;
            }
            return 1;
        }
    }

    return 0;
}

vigil_status_t vigil_parser_emit_ok_constant(
    vigil_parser_state_t *state,
    vigil_source_span_t span
) {
    vigil_status_t status;
    vigil_value_t value;
    vigil_object_t *object;

    vigil_value_init_nil(&value);
    object = NULL;
    status = vigil_error_object_new_cstr(
        state->program->registry->runtime,
        "",
        0,
        &object,
        state->program->error
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    vigil_value_init_object(&value, &object);
    status = vigil_chunk_write_constant(
        &state->chunk,
        &value,
        span,
        NULL,
        state->program->error
    );
    vigil_value_release(&value);
    return status;
}

static vigil_status_t vigil_parser_parse_builtin_error_constructor(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t message_result;
    vigil_expression_result_t kind_result;

    vigil_expression_result_clear(&message_result);
    vigil_expression_result_clear(&kind_result);

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after err",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_parse_expression(state, &message_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_type(
        state,
        name_token->span,
        message_result.type,
        vigil_binding_type_primitive(VIGIL_TYPE_STRING),
        "err(...) message must be a string"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_COMMA,
        "expected ',' after error message",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_parse_expression(state, &kind_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_type(
        state,
        name_token->span,
        kind_result.type,
        vigil_binding_type_primitive(VIGIL_TYPE_I32),
        "err(...) kind must be an i32"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_RPAREN,
        "expected ')' after err arguments",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_NEW_ERROR, name_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_ERR));
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_builtin_char(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t arg_result;

    vigil_expression_result_clear(&arg_result);

    status = vigil_parser_expect(state, VIGIL_TOKEN_LPAREN, "expected '(' after char", NULL);
    if (status != VIGIL_STATUS_OK) return status;

    status = vigil_parser_parse_expression(state, &arg_result);
    if (status != VIGIL_STATUS_OK) return status;

    if (!vigil_parser_type_is_integer(arg_result.type)) {
        return vigil_parser_report(state, name_token->span, "char() requires an integer argument");
    }

    status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after char argument", NULL);
    if (status != VIGIL_STATUS_OK) return status;

    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_CHAR_FROM_INT, name_token->span);
    if (status != VIGIL_STATUS_OK) return status;

    vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_STRING));
    return VIGIL_STATUS_OK;
}

static int vigil_parser_resolve_builtin_conversion_kind(
    const vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    vigil_type_kind_t *out_kind
) {
    const char *name_text;
    size_t name_length;
    vigil_type_kind_t kind;

    if (out_kind != NULL) {
        *out_kind = VIGIL_TYPE_INVALID;
    }
    if (state == NULL || name_token == NULL) {
        return 0;
    }

    name_text = vigil_parser_token_text(state, name_token, &name_length);
    kind = vigil_type_kind_from_name(name_text, name_length);
    if (
        kind != VIGIL_TYPE_I32 &&
        kind != VIGIL_TYPE_I64 &&
        kind != VIGIL_TYPE_U8 &&
        kind != VIGIL_TYPE_U32 &&
        kind != VIGIL_TYPE_U64 &&
        kind != VIGIL_TYPE_F64 &&
        kind != VIGIL_TYPE_STRING &&
        kind != VIGIL_TYPE_BOOL
    ) {
        return 0;
    }

    if (out_kind != NULL) {
        *out_kind = kind;
    }
    return 1;
}

static vigil_status_t vigil_parser_parse_builtin_conversion(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    vigil_type_kind_t target_kind,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t argument_result;
    vigil_opcode_t opcode;
    int needs_opcode;

    vigil_expression_result_clear(&argument_result);
    opcode = VIGIL_OPCODE_TO_STRING;
    needs_opcode = 0;

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after conversion name",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_parse_expression(state, &argument_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    if (vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
        return vigil_parser_report(
            state,
            name_token->span,
            "built-in conversions accept exactly one argument"
        );
    }
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_RPAREN,
        "expected ')' after conversion argument",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    switch (target_kind) {
        case VIGIL_TYPE_I32:
            opcode = VIGIL_OPCODE_TO_I32;
            goto integer_conversion;
        case VIGIL_TYPE_I64:
            opcode = VIGIL_OPCODE_TO_I64;
            goto integer_conversion;
        case VIGIL_TYPE_U8:
            opcode = VIGIL_OPCODE_TO_U8;
            goto integer_conversion;
        case VIGIL_TYPE_U32:
            opcode = VIGIL_OPCODE_TO_U32;
            goto integer_conversion;
        case VIGIL_TYPE_U64:
            opcode = VIGIL_OPCODE_TO_U64;
            goto integer_conversion;
        case VIGIL_TYPE_F64:
            if (vigil_parser_type_is_f64(argument_result.type)) {
                needs_opcode = 0;
            } else if (vigil_parser_type_is_integer(argument_result.type)) {
                opcode = VIGIL_OPCODE_TO_F64;
                needs_opcode = 1;
            } else {
                return vigil_parser_report(
                    state,
                    name_token->span,
                    "f64(...) requires an integer or f64 argument"
                );
            }
            break;
        case VIGIL_TYPE_STRING:
            if (vigil_parser_type_is_string(argument_result.type)) {
                needs_opcode = 0;
            } else if (
                vigil_parser_type_is_integer(argument_result.type) ||
                vigil_parser_type_is_f64(argument_result.type) ||
                vigil_parser_type_is_bool(argument_result.type)
            ) {
                opcode = VIGIL_OPCODE_TO_STRING;
                needs_opcode = 1;
            } else {
                return vigil_parser_report(
                    state,
                    name_token->span,
                    "string(...) requires a string, integer, f64, or bool argument"
                );
            }
            break;
        case VIGIL_TYPE_BOOL:
            if (!vigil_parser_type_is_bool(argument_result.type)) {
                return vigil_parser_report(
                    state,
                    name_token->span,
                    "bool(...) requires a bool argument"
                );
            }
            needs_opcode = 0;
            break;
        default:
            return vigil_parser_report(
                state,
                name_token->span,
                "unsupported built-in conversion"
            );
    }

integer_conversion:
    if (
        target_kind == VIGIL_TYPE_I32 ||
        target_kind == VIGIL_TYPE_I64 ||
        target_kind == VIGIL_TYPE_U8 ||
        target_kind == VIGIL_TYPE_U32 ||
        target_kind == VIGIL_TYPE_U64
    ) {
        if (
            vigil_parser_type_equal(
                argument_result.type,
                vigil_binding_type_primitive(target_kind)
            )
        ) {
            needs_opcode = 0;
        } else if (
            vigil_parser_type_is_integer(argument_result.type) ||
            vigil_parser_type_is_f64(argument_result.type)
        ) {
            needs_opcode = 1;
        } else {
            return vigil_parser_report(
                state,
                name_token->span,
                "integer conversions require an integer or f64 argument"
            );
        }
    }

    if (needs_opcode) {
        status = vigil_parser_emit_opcode(state, opcode, name_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    vigil_expression_result_set_type(
        out_result,
        vigil_binding_type_primitive(target_kind)
    );
    return VIGIL_STATUS_OK;
}

static int vigil_program_find_function_symbol_in_source(
    const vigil_program_state_t *program,
    vigil_source_id_t source_id,
    const vigil_token_t *name_token,
    size_t *out_index,
    const vigil_function_decl_t **out_decl
) {
    const char *name_text;
    size_t name_length;

    name_text = vigil_program_token_text(program, name_token, &name_length);
    return vigil_program_find_top_level_function_name_in_source(
        program,
        source_id,
        name_text,
        name_length,
        out_index,
        out_decl
    );
}

static vigil_status_t vigil_parser_lookup_function_symbol(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    size_t *out_index,
    const vigil_function_decl_t **out_decl
) {
    if (vigil_program_find_function_symbol_in_source(
            state->program,
            state->program->source == NULL ? 0U : state->program->source->id,
            name_token,
            out_index,
            out_decl
        )) {
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_report(state, name_token->span, "unknown function");
}

static int vigil_program_find_class_symbol_in_source(
    const vigil_program_state_t *program,
    vigil_source_id_t source_id,
    const vigil_token_t *name_token,
    size_t *out_index,
    const vigil_class_decl_t **out_decl
) {
    const char *name_text;
    size_t name_length;

    name_text = vigil_program_token_text(program, name_token, &name_length);
    return vigil_program_find_class_in_source(
        program,
        source_id,
        name_text,
        name_length,
        out_index,
        out_decl
    );
}

static vigil_status_t vigil_parser_lookup_class_symbol(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    size_t *out_index,
    const vigil_class_decl_t **out_decl
) {
    if (
        vigil_program_find_class_symbol_in_source(
            state->program,
            state->program->source == NULL ? 0U : state->program->source->id,
            name_token,
            out_index,
            out_decl
        )
    ) {
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_report(state, name_token->span, "unknown class");
}

static vigil_status_t vigil_parser_lookup_field(
    vigil_parser_state_t *state,
    vigil_parser_type_t receiver_type,
    const vigil_token_t *field_token,
    size_t *out_index,
    const vigil_class_field_t **out_field
) {
    const vigil_class_decl_t *class_decl;
    const vigil_class_field_t *field;
    const char *field_name;
    size_t field_length;

    if (!vigil_parser_type_is_class(receiver_type)) {
        return vigil_parser_report(
            state,
            field_token->span,
            "field access requires a class instance"
        );
    }

    class_decl = &state->program->classes[receiver_type.object_index];
    field_name = vigil_parser_token_text(state, field_token, &field_length);
    field = NULL;
    if (vigil_class_decl_find_field(class_decl, field_name, field_length, out_index, &field)) {
        if (
            !field->is_public &&
            class_decl->source_id != state->program->source->id
        ) {
            return vigil_parser_report(state, field_token->span, "class field is not public");
        }
        if (out_field != NULL) {
            *out_field = field;
        }
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_report(state, field_token->span, "unknown class field");
}

static int vigil_parser_find_field_by_name(
    const vigil_parser_state_t *state,
    vigil_parser_type_t receiver_type,
    const vigil_token_t *field_token,
    size_t *out_index,
    const vigil_class_field_t **out_field
) {
    const vigil_class_decl_t *class_decl;
    const char *field_name;
    size_t field_length;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_field != NULL) {
        *out_field = NULL;
    }
    if (state == NULL || !vigil_parser_type_is_class(receiver_type)) {
        return 0;
    }

    class_decl = &state->program->classes[receiver_type.object_index];
    field_name = vigil_parser_token_text(state, field_token, &field_length);
    if (!vigil_class_decl_find_field(class_decl, field_name, field_length, out_index, out_field)) {
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

static int vigil_parser_find_method_by_name(
    const vigil_parser_state_t *state,
    vigil_parser_type_t receiver_type,
    const vigil_token_t *method_token,
    size_t *out_index,
    const vigil_class_method_t **out_method
) {
    const vigil_class_decl_t *class_decl;
    const char *method_name;
    size_t method_length;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_method != NULL) {
        *out_method = NULL;
    }
    if (state == NULL || !vigil_parser_type_is_class(receiver_type)) {
        return 0;
    }

    class_decl = &state->program->classes[receiver_type.object_index];
    method_name = vigil_parser_token_text(state, method_token, &method_length);
    return vigil_class_decl_find_method(class_decl, method_name, method_length, out_index, out_method);
}

static int vigil_parser_find_interface_method_by_name(
    const vigil_parser_state_t *state,
    vigil_parser_type_t receiver_type,
    const vigil_token_t *method_token,
    size_t *out_index,
    const vigil_interface_method_t **out_method
) {
    const vigil_interface_decl_t *interface_decl;
    const char *method_name;
    size_t method_length;

    if (out_index != NULL) {
        *out_index = 0U;
    }
    if (out_method != NULL) {
        *out_method = NULL;
    }
    if (state == NULL || !vigil_parser_type_is_interface(receiver_type)) {
        return 0;
    }

    interface_decl = &state->program->interfaces[receiver_type.object_index];
    method_name = vigil_parser_token_text(state, method_token, &method_length);
    return vigil_interface_decl_find_method(
        interface_decl,
        method_name,
        method_length,
        out_index,
        out_method
    );
}

static void vigil_parser_begin_scope(vigil_parser_state_t *state) {
    vigil_binding_scope_stack_begin_scope(&state->locals);
}

static vigil_status_t vigil_parser_end_scope(vigil_parser_state_t *state) {
    vigil_status_t status;
    vigil_source_span_t span;
    size_t popped_count;
    size_t index;
    size_t locals_before;
    size_t end_ip;

    if (vigil_binding_scope_stack_depth(&state->locals) == 0U) {
        return VIGIL_STATUS_OK;
    }

    locals_before = vigil_binding_scope_stack_count(&state->locals);
    vigil_binding_scope_stack_end_scope(&state->locals, &popped_count);

    end_ip = vigil_chunk_code_size(&state->chunk);
    if (popped_count > 0U) {
        vigil_debug_local_table_close_scope(
            &state->chunk.debug_locals,
            locals_before - popped_count,
            end_ip
        );
    }

    for (index = 0U; index < popped_count; index += 1U) {
        span = vigil_parser_fallback_span(state);
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_parser_parse_expression(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
);
static vigil_status_t vigil_parser_parse_expression_with_expected_type(
    vigil_parser_state_t *state,
    vigil_parser_type_t expected_type,
    vigil_expression_result_t *out_result
);
static vigil_status_t vigil_parser_parse_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
);

static vigil_status_t vigil_parser_parse_call(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    vigil_expression_result_t *out_result
) ;
static vigil_status_t vigil_parser_parse_value_call(
    vigil_parser_state_t *state,
    vigil_source_span_t call_span,
    vigil_parser_type_t callee_type,
    vigil_expression_result_t *out_result
);
static vigil_status_t vigil_parser_parse_call_resolved(
    vigil_parser_state_t *state,
    vigil_source_span_t call_span,
    size_t function_index,
    const vigil_function_decl_t *decl,
    vigil_expression_result_t *out_result
);
static vigil_status_t vigil_parser_parse_constructor(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    vigil_expression_result_t *out_result
) ;
static vigil_status_t vigil_parser_parse_constructor_resolved(
    vigil_parser_state_t *state,
    vigil_source_span_t call_span,
    size_t class_index,
    const vigil_class_decl_t *decl,
    vigil_expression_result_t *out_result
);

static vigil_status_t vigil_parser_parse_call(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    size_t function_index;
    const vigil_function_decl_t *decl;

    function_index = 0U;
    decl = NULL;

    status = vigil_parser_lookup_function_symbol(state, name_token, &function_index, &decl);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    return vigil_parser_parse_call_resolved(
        state,
        name_token->span,
        function_index,
        decl,
        out_result
    );
}

static vigil_status_t vigil_parser_parse_value_call(
    vigil_parser_state_t *state,
    vigil_source_span_t call_span,
    vigil_parser_type_t callee_type,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t arg_result;
    const vigil_function_type_decl_t *function_type;
    size_t arg_count;
    int defer_call;

    vigil_expression_result_clear(&arg_result);
    function_type = vigil_program_function_type_decl(state->program, callee_type);
    if (function_type == NULL) {
        return vigil_parser_report(state, call_span, "call target is not callable");
    }
    if (function_type->is_any) {
        return vigil_parser_report(
            state,
            call_span,
            "indirect calls require a concrete function signature"
        );
    }

    defer_call = state->defer_mode;
    state->defer_mode = 0;
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after callable value",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    arg_count = 0U;
    if (!vigil_parser_check(state, VIGIL_TOKEN_RPAREN)) {
        while (1) {
            status = vigil_parser_parse_expression(state, &arg_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state,
                call_span,
                &arg_result,
                "call arguments must be single values"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (arg_count >= function_type->param_count) {
                return vigil_parser_report(
                    state,
                    call_span,
                    "call argument count does not match function signature"
                );
            }
            status = vigil_parser_require_type(
                state,
                call_span,
                arg_result.type,
                function_type->param_types[arg_count],
                "call argument type does not match parameter type"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            arg_count += 1U;

            if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_RPAREN,
        "expected ')' after argument list",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    if (arg_count != function_type->param_count || arg_count > UINT32_MAX) {
        return vigil_parser_report(
            state,
            call_span,
            "call argument count does not match function signature"
        );
    }

    status = vigil_parser_emit_opcode(
        state,
        defer_call ? VIGIL_OPCODE_DEFER_CALL_VALUE : VIGIL_OPCODE_CALL_VALUE,
        call_span
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)arg_count, call_span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (defer_call) {
        state->defer_emitted = 1;
        vigil_expression_result_set_type(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_VOID)
        );
    } else {
        vigil_expression_result_set_return_types(
            out_result,
            function_type->return_type,
            function_type->return_types,
            function_type->return_count
        );
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_call_resolved(
    vigil_parser_state_t *state,
    vigil_source_span_t call_span,
    size_t function_index,
    const vigil_function_decl_t *decl,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t arg_result;
    size_t arg_count;
    int defer_call;

    vigil_expression_result_clear(&arg_result);
    defer_call = state->defer_mode;
    state->defer_mode = 0;

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after function name",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    arg_count = 0U;
    if (!vigil_parser_check(state, VIGIL_TOKEN_RPAREN)) {
        while (1) {
            status = vigil_parser_parse_expression(state, &arg_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state,
                call_span,
                &arg_result,
                "call arguments must be single values"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (arg_count >= decl->param_count) {
                return vigil_parser_report(
                    state,
                    call_span,
                    "call argument count does not match function signature"
                );
            }
            status = vigil_parser_require_type(
                state,
                call_span,
                arg_result.type,
                decl->params[arg_count].type,
                "call argument type does not match parameter type"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            arg_count += 1U;

            if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_RPAREN,
        "expected ')' after argument list",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (arg_count != decl->param_count) {
        return vigil_parser_report(
            state,
            call_span,
            "call argument count does not match function signature"
        );
    }
    if (function_index > UINT32_MAX || arg_count > UINT32_MAX) {
        vigil_error_set_literal(
            state->program->error,
            VIGIL_STATUS_OUT_OF_MEMORY,
            "call operand overflow"
        );
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    status = vigil_parser_emit_opcode(
        state,
        defer_call ? VIGIL_OPCODE_DEFER_CALL : VIGIL_OPCODE_CALL,
        call_span
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)function_index, call_span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)arg_count, call_span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (defer_call) {
        state->defer_emitted = 1;
        vigil_expression_result_set_type(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_VOID)
        );
    } else {
        vigil_expression_result_set_return_types(
            out_result,
            decl->return_type,
            vigil_function_return_types(decl),
            decl->return_count
        );
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_constructor(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    size_t class_index;
    const vigil_class_decl_t *decl;

    class_index = 0U;
    decl = NULL;

    status = vigil_parser_lookup_class_symbol(state, name_token, &class_index, &decl);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    return vigil_parser_parse_constructor_resolved(
        state,
        name_token->span,
        class_index,
        decl,
        out_result
    );
}

static vigil_status_t vigil_parser_parse_constructor_resolved(
    vigil_parser_state_t *state,
    vigil_source_span_t call_span,
    size_t class_index,
    const vigil_class_decl_t *decl,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t arg_result;
    const vigil_function_decl_t *ctor_decl;
    size_t arg_count;
    size_t expected_arg_count;
    int use_constructor_function;
    int defer_call;

    vigil_expression_result_clear(&arg_result);
    ctor_decl = NULL;
    defer_call = state->defer_mode;
    state->defer_mode = 0;
    use_constructor_function = decl->constructor_function_index != (size_t)-1;
    if (use_constructor_function) {
        ctor_decl = vigil_binding_function_table_get(
            &state->program->functions,
            decl->constructor_function_index
        );
        if (ctor_decl == NULL) {
            return vigil_parser_report(state, call_span, "unknown constructor");
        }
        expected_arg_count = ctor_decl->param_count;
    } else {
        expected_arg_count = decl->field_count;
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after class name",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    arg_count = 0U;
    if (!vigil_parser_check(state, VIGIL_TOKEN_RPAREN)) {
        while (1) {
            status = vigil_parser_parse_expression(state, &arg_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state,
                call_span,
                &arg_result,
                "call arguments must be single values"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (arg_count >= expected_arg_count) {
                return vigil_parser_report(
                    state,
                    call_span,
                    "constructor argument count does not match class signature"
                );
            }
            status = vigil_parser_require_type(
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
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            arg_count += 1U;

            if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_RPAREN,
        "expected ')' after constructor arguments",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (arg_count != expected_arg_count || arg_count > UINT32_MAX || class_index > UINT32_MAX) {
        return vigil_parser_report(
            state,
            call_span,
            "constructor argument count does not match class signature"
        );
    }

    if (use_constructor_function) {
        if (decl->constructor_function_index > UINT32_MAX) {
            return vigil_parser_report(state, call_span, "constructor index overflow");
        }
        status = vigil_parser_emit_opcode(
            state,
            defer_call ? VIGIL_OPCODE_DEFER_CALL : VIGIL_OPCODE_CALL,
            call_span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(
            state,
            (uint32_t)decl->constructor_function_index,
            call_span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)arg_count, call_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    } else {
        status = vigil_parser_emit_opcode(
            state,
            defer_call ? VIGIL_OPCODE_DEFER_NEW_INSTANCE : VIGIL_OPCODE_NEW_INSTANCE,
            call_span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)class_index, call_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)arg_count, call_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    if (defer_call) {
        state->defer_emitted = 1;
        vigil_expression_result_set_type(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_VOID)
        );
    } else {
        if (use_constructor_function) {
            vigil_expression_result_set_return_types(
                out_result,
                ctor_decl->return_type,
                vigil_function_return_types(ctor_decl),
                ctor_decl->return_count
            );
        } else {
            vigil_expression_result_set_type(out_result, vigil_binding_type_class(class_index));
        }
    }
    return VIGIL_STATUS_OK;
}

/* Convert a vigil_native_type_t to vigil_binding_type_t, interning array/map types as needed. */
static vigil_status_t vigil_native_type_to_binding_type(
    vigil_parser_state_t *state,
    const vigil_native_type_t *native_type,
    vigil_binding_type_t *out_type
) {
    vigil_status_t status;
    vigil_binding_type_t elem_type, key_type, value_type;

    if (native_type->object_kind == 0) {
        /* Primitive type */
        *out_type = vigil_binding_type_primitive((vigil_type_kind_t)native_type->kind);
        return VIGIL_STATUS_OK;
    }

    if (native_type->object_kind == 4) {
        /* Array type */
        elem_type = vigil_binding_type_primitive((vigil_type_kind_t)native_type->element_type);
        status = vigil_program_intern_array_type(
            (vigil_program_state_t *)state->program,
            elem_type,
            out_type
        );
        return status;
    }

    if (native_type->object_kind == 5) {
        /* Map type */
        key_type = vigil_binding_type_primitive((vigil_type_kind_t)native_type->key_type);
        value_type = vigil_binding_type_primitive((vigil_type_kind_t)native_type->value_type);
        status = vigil_program_intern_map_type(
            (vigil_program_state_t *)state->program,
            key_type,
            value_type,
            out_type
        );
        return status;
    }

    /* Unknown object kind, fall back to generic object */
    *out_type = vigil_binding_type_primitive(VIGIL_TYPE_OBJECT);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_native_call(
    vigil_parser_state_t *state,
    const vigil_token_t *member_token,
    vigil_source_id_t source_id,
    const char *member_name,
    size_t member_name_length,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t arg_result;
    const vigil_native_module_t *mod;
    const vigil_native_module_function_t *fn;
    size_t mod_idx;
    size_t i;
    size_t arg_count;
    vigil_object_t *native_obj;
    vigil_value_t native_val;
    int defer_call;

    defer_call = state->defer_mode;

    mod_idx = VIGIL_NATIVE_SOURCE_INDEX(source_id);
    if (mod_idx >= state->program->natives->module_count) {
        return vigil_parser_report(state, member_token->span, "unknown native module");
    }
    mod = state->program->natives->modules[mod_idx];

    /* Find the function in the native module. */
    fn = NULL;
    for (i = 0U; i < mod->function_count; i++) {
        if (mod->functions[i].name_length == member_name_length &&
            memcmp(mod->functions[i].name, member_name, member_name_length) == 0) {
            fn = &mod->functions[i];
            break;
        }
    }
    if (fn == NULL) {
        return vigil_parser_report(state, member_token->span, "unknown function");
    }

    /* Parse arguments. */
    vigil_expression_result_clear(&arg_result);
    status = vigil_parser_expect(
        state, VIGIL_TOKEN_LPAREN, "expected '(' after function name", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    arg_count = 0U;
    if (!vigil_parser_check(state, VIGIL_TOKEN_RPAREN)) {
        while (1) {
            status = vigil_parser_parse_expression(state, &arg_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (arg_count < fn->param_count) {
                status = vigil_parser_require_scalar_expression(
                    state, member_token->span, &arg_result,
                    "call arguments must be single values"
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
            }
            if (arg_count < fn->param_count) {
                /* Check if we should do type checking for this parameter */
                int should_check = 1;
                vigil_binding_type_t expected_type = vigil_binding_type_invalid();
                
                if (fn->param_types_ext != NULL) {
                    /* Use extended type info - always check */
                    status = vigil_native_type_to_binding_type(
                        state, &fn->param_types_ext[arg_count], &expected_type);
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                } else if (fn->param_types[arg_count] != VIGIL_TYPE_OBJECT) {
                    /* Legacy: check primitive types, skip generic OBJECT */
                    expected_type = vigil_binding_type_primitive(
                        (vigil_type_kind_t)fn->param_types[arg_count]);
                } else {
                    /* Legacy OBJECT type - skip checking */
                    should_check = 0;
                }
                
                if (should_check) {
                    status = vigil_parser_require_type(
                        state, member_token->span, arg_result.type,
                        expected_type,
                        "call argument type does not match parameter type"
                    );
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                }
            }
            arg_count += 1U;
            if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                break;
            }
        }
    }
    status = vigil_parser_expect(
        state, VIGIL_TOKEN_RPAREN, "expected ')' after argument list", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    if (arg_count != fn->param_count) {
        return vigil_parser_report(
            state, member_token->span,
            "call argument count does not match function signature"
        );
    }

    /* Create native function object and store in constant pool. */
    native_obj = NULL;
    status = vigil_native_function_object_create(
        state->program->registry->runtime,
        fn->name, fn->name_length,
        fn->param_count, fn->native_fn,
        &native_obj, state->program->error
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    vigil_value_init_object(&native_val, &native_obj);
    {
        size_t const_idx;

        const_idx = 0U;
        status = vigil_chunk_add_constant(
            &state->chunk, &native_val, &const_idx,
            state->program->error
        );
        vigil_value_release(&native_val);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(
            state, defer_call ? VIGIL_OPCODE_DEFER_CALL_NATIVE : VIGIL_OPCODE_CALL_NATIVE, member_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(
            state, (uint32_t)const_idx, member_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(
            state, (uint32_t)arg_count, member_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    if (defer_call) {
        state->defer_emitted = 1;
        vigil_expression_result_set_type(
            out_result, vigil_binding_type_primitive(VIGIL_TYPE_VOID));
        return VIGIL_STATUS_OK;
    }

    /* Set return type. */
    if (fn->return_count <= 1U) {
        if (fn->return_type_ext != NULL) {
            /* Use extended return type info */
            vigil_binding_type_t ret_type;
            status = vigil_native_type_to_binding_type(state, fn->return_type_ext, &ret_type);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(out_result, ret_type);
        } else if (fn->return_type == VIGIL_TYPE_OBJECT && fn->return_element_type != 0) {
            /* Array return - look up the pre-registered array type */
            vigil_parser_type_t elem_type = vigil_binding_type_primitive(
                (vigil_type_kind_t)fn->return_element_type);
            size_t arr_idx;
            if (vigil_program_find_array_type(state->program, elem_type, &arr_idx)) {
                vigil_expression_result_set_type(out_result, vigil_binding_type_array(arr_idx));
            } else {
                /* Fallback to generic object if not found */
                vigil_expression_result_set_type(
                    out_result,
                    vigil_binding_type_primitive(VIGIL_TYPE_OBJECT)
                );
            }
        } else {
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive((vigil_type_kind_t)fn->return_type)
            );
        }
    } else {
        /* Multi-return: use owned_types storage in the result. */
        if (fn->return_types == NULL) {
            return vigil_parser_report(state, member_token->span,
                "native function declares multiple returns but has no return type list");
        }
        if (fn->return_count == 2U) {
            vigil_expression_result_set_pair(
                out_result,
                vigil_binding_type_primitive((vigil_type_kind_t)fn->return_types[0]),
                vigil_binding_type_primitive((vigil_type_kind_t)fn->return_types[1])
            );
        } else if (fn->return_count >= 3U) {
            vigil_expression_result_set_triple(
                out_result,
                vigil_binding_type_primitive((vigil_type_kind_t)fn->return_types[0]),
                vigil_binding_type_primitive((vigil_type_kind_t)fn->return_types[1]),
                vigil_binding_type_primitive((vigil_type_kind_t)fn->return_types[2])
            );
        }
    }
    return VIGIL_STATUS_OK;
}

/* Resolve the return class index for a native method.
 * If return_class_name is set, look it up; otherwise use class_index. */
static size_t vigil_parser_resolve_return_class(
    const vigil_parser_state_t *state,
    const vigil_native_class_method_t *method,
    size_t class_index
) {
    size_t k;
    if (method->return_class_name != NULL) {
        for (k = 0U; k < state->program->class_count; k++) {
            if (state->program->classes[k].name_length == method->return_class_name_length &&
                memcmp(state->program->classes[k].name, method->return_class_name,
                       method->return_class_name_length) == 0) {
                return k;
            }
        }
    }
    return class_index;
}

/* Resolve the return type for a native class method, handling array returns. */
static void vigil_parser_set_native_method_return_type(
    vigil_parser_state_t *state,
    const vigil_native_class_method_t *method,
    size_t class_index,
    vigil_expression_result_t *out_result
) {
    if (method->return_count <= 1U) {
        if (method->return_type == VIGIL_TYPE_OBJECT &&
            method->return_element_type != 0) {
            vigil_parser_type_t elem_type = vigil_binding_type_primitive(
                (vigil_type_kind_t)method->return_element_type);
            size_t arr_idx;
            if (vigil_program_find_array_type(state->program, elem_type, &arr_idx)) {
                vigil_expression_result_set_type(out_result, vigil_binding_type_array(arr_idx));
            } else {
                vigil_expression_result_set_type(
                    out_result, vigil_binding_type_primitive(VIGIL_TYPE_OBJECT));
            }
        } else if (method->return_type == VIGIL_TYPE_OBJECT) {
            vigil_expression_result_set_type(
                out_result, vigil_binding_type_class(
                    vigil_parser_resolve_return_class(state, method, class_index)));
        } else {
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive((vigil_type_kind_t)method->return_type));
        }
    } else {
        if (method->return_types == NULL) {
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive((vigil_type_kind_t)method->return_type));
        } else if (method->return_count == 2U) {
            vigil_expression_result_set_pair(
                out_result,
                vigil_binding_type_primitive((vigil_type_kind_t)method->return_types[0]),
                vigil_binding_type_primitive((vigil_type_kind_t)method->return_types[1])
            );
        } else if (method->return_count >= 3U) {
            vigil_expression_result_set_triple(
                out_result,
                vigil_binding_type_primitive((vigil_type_kind_t)method->return_types[0]),
                vigil_binding_type_primitive((vigil_type_kind_t)method->return_types[1]),
                vigil_binding_type_primitive((vigil_type_kind_t)method->return_types[2])
            );
        }
    }
}

/* Parse a static method call on a native class: no self on the stack. */
static vigil_status_t vigil_parser_parse_native_static_method_call(
    vigil_parser_state_t *state,
    const vigil_token_t *method_token,
    const vigil_native_class_method_t *method,
    size_t class_index,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t arg_result;
    size_t arg_count;
    vigil_object_t *native_obj;
    vigil_value_t native_val;
    vigil_value_t ci_val;

    vigil_expression_result_clear(&arg_result);

    /* Push class_index as hidden first arg so the C impl can create
     * instances of the correct class. */
    vigil_value_init_int(&ci_val, (int64_t)class_index);
    status = vigil_chunk_write_constant(
        &state->chunk, &ci_val, method_token->span, NULL,
        state->program->error);
    vigil_value_release(&ci_val);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_expect(
        state, VIGIL_TOKEN_LPAREN, "expected '(' after method name", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    arg_count = 0U;
    if (!vigil_parser_check(state, VIGIL_TOKEN_RPAREN)) {
        while (1) {
            status = vigil_parser_parse_expression(state, &arg_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state, method_token->span, &arg_result,
                "call arguments must be single values"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (arg_count < method->param_count &&
                method->param_types[arg_count] != VIGIL_TYPE_OBJECT) {
                status = vigil_parser_require_type(
                    state, method_token->span, arg_result.type,
                    vigil_binding_type_primitive(
                        (vigil_type_kind_t)method->param_types[arg_count]),
                    "call argument type does not match parameter type"
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
            }
            arg_count += 1U;
            if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                break;
            }
        }
    }
    status = vigil_parser_expect(
        state, VIGIL_TOKEN_RPAREN, "expected ')' after argument list", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    if (arg_count != method->param_count) {
        return vigil_parser_report(
            state, method_token->span,
            "call argument count does not match method signature"
        );
    }

    native_obj = NULL;
    status = vigil_native_function_object_create(
        state->program->registry->runtime,
        method->name, method->name_length,
        method->param_count + 1U, method->native_fn,
        &native_obj, state->program->error
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    vigil_value_init_object(&native_val, &native_obj);
    {
        size_t const_idx = 0U;

        status = vigil_chunk_add_constant(
            &state->chunk, &native_val, &const_idx,
            state->program->error
        );
        vigil_value_release(&native_val);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(
            state, VIGIL_OPCODE_CALL_NATIVE, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(
            state, (uint32_t)const_idx, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(
            state, (uint32_t)(arg_count + 1U), method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    /* Set return type. */
    vigil_parser_set_native_method_return_type(
        state, method, class_index, out_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_native_method_call(
    vigil_parser_state_t *state,
    const vigil_token_t *method_token,
    const vigil_native_class_t *nc,
    const vigil_native_class_method_t *method,
    size_t class_index,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t arg_result;
    size_t arg_count;
    vigil_object_t *native_obj;
    vigil_value_t native_val;

    (void)nc;
    vigil_expression_result_clear(&arg_result);
    status = vigil_parser_expect(
        state, VIGIL_TOKEN_LPAREN, "expected '(' after method name", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    /* self is already on the stack; parse remaining args. */
    arg_count = 0U;
    if (!vigil_parser_check(state, VIGIL_TOKEN_RPAREN)) {
        while (1) {
            status = vigil_parser_parse_expression(state, &arg_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state, method_token->span, &arg_result,
                "call arguments must be single values"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (arg_count < method->param_count &&
                method->param_types[arg_count] != VIGIL_TYPE_OBJECT) {
                status = vigil_parser_require_type(
                    state, method_token->span, arg_result.type,
                    vigil_binding_type_primitive(
                        (vigil_type_kind_t)method->param_types[arg_count]),
                    "call argument type does not match parameter type"
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
            }
            arg_count += 1U;
            if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                break;
            }
        }
    }
    status = vigil_parser_expect(
        state, VIGIL_TOKEN_RPAREN, "expected ')' after argument list", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    if (arg_count != method->param_count) {
        return vigil_parser_report(
            state, method_token->span,
            "call argument count does not match method signature"
        );
    }

    /* Create native function object and emit CALL_NATIVE.
     * arg_count + 1 accounts for self on the stack. */
    native_obj = NULL;
    status = vigil_native_function_object_create(
        state->program->registry->runtime,
        method->name, method->name_length,
        method->param_count + 1U, method->native_fn,
        &native_obj, state->program->error
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    vigil_value_init_object(&native_val, &native_obj);
    {
        size_t const_idx = 0U;

        status = vigil_chunk_add_constant(
            &state->chunk, &native_val, &const_idx,
            state->program->error
        );
        vigil_value_release(&native_val);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(
            state, VIGIL_OPCODE_CALL_NATIVE, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(
            state, (uint32_t)const_idx, method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(
            state, (uint32_t)(arg_count + 1U), method_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    /* Set return type. */
    vigil_parser_set_native_method_return_type(
        state, method, class_index, out_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_qualified_symbol(
    vigil_parser_state_t *state,
    const vigil_token_t *module_token,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_value_t value;
    const vigil_token_t *member_token;
    const vigil_token_t *enum_member_token;
    const vigil_global_constant_t *constant;
    const vigil_function_decl_t *function_decl;
    const vigil_class_decl_t *class_decl;
    const vigil_enum_member_t *enum_member;
    const char *module_name;
    const char *member_name;
    const char *enum_member_name;
    size_t module_name_length;
    size_t member_name_length;
    size_t enum_member_name_length;
    size_t function_index;
    size_t class_index;
    size_t enum_index;
    vigil_source_id_t source_id;

    constant = NULL;
    function_decl = NULL;
    class_decl = NULL;
    enum_member = NULL;
    function_index = 0U;
    class_index = 0U;
    enum_index = 0U;
    source_id = 0U;
    vigil_value_init_nil(&value);

    module_name = vigil_parser_token_text(state, module_token, &module_name_length);
    if (
        !vigil_program_resolve_import_alias(
            state->program,
            module_name,
            module_name_length,
            &source_id
        )
    ) {
        return vigil_parser_report(state, module_token->span, "unknown local variable");
    }

    status = vigil_parser_expect(state, VIGIL_TOKEN_DOT, "expected '.' after module name", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_IDENTIFIER,
        "expected module member name after '.'",
        &member_token
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    member_name = vigil_parser_token_text(state, member_token, &member_name_length);
    if (
        vigil_parser_check(state, VIGIL_TOKEN_DOT) &&
        vigil_program_find_enum_in_source(
            state->program,
            source_id,
            member_name,
            member_name_length,
            &enum_index,
            NULL
        )
    ) {
        vigil_parser_advance(state);
        enum_member_token = vigil_parser_peek(state);
        if (enum_member_token == NULL || enum_member_token->kind != VIGIL_TOKEN_IDENTIFIER) {
            return vigil_parser_report(state, member_token->span, "unknown enum member");
        }
        vigil_parser_advance(state);
        enum_member_name = vigil_parser_token_text(
            state,
            enum_member_token,
            &enum_member_name_length
        );
        if (
            !vigil_program_lookup_enum_member_in_source(
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
            return vigil_parser_report(state, enum_member_token->span, "unknown enum member");
        }
        if (!vigil_program_is_enum_public(&state->program->enums[enum_index])) {
            return vigil_parser_report(state, member_token->span, "module member is not public");
        }

        vigil_value_init_int(&value, enum_member->value);
        status = vigil_chunk_write_constant(
            &state->chunk,
            &value,
            enum_member_token->span,
            NULL,
            state->program->error
        );
        vigil_value_release(&value);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(out_result, vigil_binding_type_enum(enum_index));
        return VIGIL_STATUS_OK;
    }

    /* Static method call: module.ClassName.method(...) */
    if (
        vigil_parser_check(state, VIGIL_TOKEN_DOT) &&
        VIGIL_IS_NATIVE_SOURCE_ID(source_id) &&
        state->program->natives != NULL &&
        vigil_program_find_class_in_source(
            state->program, source_id,
            member_name, member_name_length,
            &class_index, &class_decl
        ) &&
        class_decl->native_class != NULL
    ) {
        const vigil_native_class_t *nc = class_decl->native_class;
        const vigil_token_t *static_method_token;
        const char *sm_name;
        size_t sm_len;
        size_t smi;

        vigil_parser_advance(state); /* consume '.' */
        status = vigil_parser_expect(
            state, VIGIL_TOKEN_IDENTIFIER,
            "expected static method name after '.'",
            &static_method_token
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        sm_name = vigil_parser_token_text(state, static_method_token, &sm_len);
        for (smi = 0U; smi < nc->method_count; smi++) {
            if (nc->methods[smi].is_static &&
                nc->methods[smi].name_length == sm_len &&
                memcmp(nc->methods[smi].name, sm_name, sm_len) == 0) {
                return vigil_parser_parse_native_static_method_call(
                    state, static_method_token,
                    &nc->methods[smi], class_index, out_result
                );
            }
        }
        return vigil_parser_report(
            state, static_method_token->span,
            "unknown static method"
        );
    }

    if (vigil_parser_check(state, VIGIL_TOKEN_LPAREN)) {
        /* Native module function call. */
        if (VIGIL_IS_NATIVE_SOURCE_ID(source_id) && state->program->natives != NULL) {
            /* Check if it's a function first; if not, fall through to class lookup. */
            const vigil_native_module_t *nmod;
            size_t nmod_idx = VIGIL_NATIVE_SOURCE_INDEX(source_id);
            int is_native_fn = 0;
            if (nmod_idx < state->program->natives->module_count) {
                size_t ni;
                nmod = state->program->natives->modules[nmod_idx];
                for (ni = 0U; ni < nmod->function_count; ni++) {
                    if (nmod->functions[ni].name_length == member_name_length &&
                        memcmp(nmod->functions[ni].name, member_name, member_name_length) == 0) {
                        is_native_fn = 1;
                        break;
                    }
                }
            }
            if (is_native_fn) {
                return vigil_parser_parse_native_call(
                    state, member_token, source_id, member_name,
                    member_name_length, out_result
                );
            }
        }
        if (
            vigil_program_find_top_level_function_name_in_source(
                state->program,
                source_id,
                member_name,
                member_name_length,
                &function_index,
                &function_decl
            )
        ) {
            if (!vigil_program_is_function_public(function_decl)) {
                return vigil_parser_report(state, member_token->span, "module member is not public");
            }
            return vigil_parser_parse_call_resolved(
                state,
                member_token->span,
                function_index,
                function_decl,
                out_result
            );
        }
        if (
            vigil_program_find_class_in_source(
                state->program,
                source_id,
                member_name,
                member_name_length,
                &class_index,
                &class_decl
            )
        ) {
            if (!vigil_program_is_class_public(class_decl)) {
                return vigil_parser_report(state, member_token->span, "module member is not public");
            }
            return vigil_parser_parse_constructor_resolved(
                state,
                member_token->span,
                class_index,
                class_decl,
                out_result
            );
        }
        return vigil_parser_report(state, member_token->span, "unknown function");
    }

    if (
        vigil_program_find_top_level_function_name_in_source(
            state->program,
            source_id,
            member_name,
            member_name_length,
            &function_index,
            &function_decl
        )
    ) {
        vigil_parser_type_t function_type;

        if (!vigil_program_is_function_public(function_decl)) {
            return vigil_parser_report(state, member_token->span, "module member is not public");
        }
        function_type = vigil_binding_type_invalid();
        status = vigil_program_intern_function_type_from_decl(
            (vigil_program_state_t *)state->program,
            function_decl,
            &function_type
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(out_result, function_type);
        status = vigil_parser_emit_opcode(
            state,
            VIGIL_OPCODE_GET_FUNCTION,
            member_token->span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        return vigil_parser_emit_u32(state, (uint32_t)function_index, member_token->span);
    }

    if (
        vigil_program_find_constant_in_source(
            state->program,
            source_id,
            member_name,
            member_name_length,
            &constant
        )
    ) {
        if (!vigil_program_is_constant_public(constant)) {
            return vigil_parser_report(state, member_token->span, "module member is not public");
        }
        status = vigil_chunk_write_constant(
            &state->chunk,
            &constant->value,
            member_token->span,
            NULL,
            state->program->error
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(out_result, constant->type);
        return VIGIL_STATUS_OK;
    }

    {
        const vigil_global_variable_t *global_decl;
        size_t global_index;

        global_decl = NULL;
        global_index = 0U;
        if (
            vigil_program_find_global_in_source(
                state->program,
                source_id,
                member_name,
                member_name_length,
                &global_index,
                &global_decl
            )
        ) {
            if (!vigil_program_is_global_public(global_decl)) {
                return vigil_parser_report(
                    state,
                    member_token->span,
                    "module member is not public"
                );
            }
            status = vigil_parser_emit_opcode(
                state,
                VIGIL_OPCODE_GET_GLOBAL,
                member_token->span
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_u32(state, (uint32_t)global_index, member_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(out_result, global_decl->type);
            return VIGIL_STATUS_OK;
        }
    }

    return vigil_parser_report(state, member_token->span, "unknown module member");
}

static vigil_status_t vigil_parser_parse_method_call(
    vigil_parser_state_t *state,
    const vigil_token_t *method_token,
    const vigil_class_method_t *method,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    const vigil_function_decl_t *decl;
    vigil_expression_result_t arg_result;
    size_t arg_index;
    int defer_call;

    decl = vigil_binding_function_table_get(&state->program->functions, method->function_index);
    if (decl == NULL) {
        return vigil_parser_report(state, method_token->span, "unknown class method");
    }

    vigil_expression_result_clear(&arg_result);
    defer_call = state->defer_mode;
    state->defer_mode = 0;
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after method name",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    arg_index = 1U;
    if (!vigil_parser_check(state, VIGIL_TOKEN_RPAREN)) {
        while (1) {
            status = vigil_parser_parse_expression(state, &arg_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state,
                method_token->span,
                &arg_result,
                "call arguments must be single values"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (arg_index >= decl->param_count) {
                return vigil_parser_report(
                    state,
                    method_token->span,
                    "call argument count does not match function signature"
                );
            }
            status = vigil_parser_require_type(
                state,
                method_token->span,
                arg_result.type,
                decl->params[arg_index].type,
                "call argument type does not match parameter type"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            arg_index += 1U;

            if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_RPAREN,
        "expected ')' after argument list",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (arg_index != decl->param_count || method->function_index > UINT32_MAX) {
        return vigil_parser_report(
            state,
            method_token->span,
            "call argument count does not match function signature"
        );
    }

    status = vigil_parser_emit_opcode(
        state,
        defer_call ? VIGIL_OPCODE_DEFER_CALL : VIGIL_OPCODE_CALL,
        method_token->span
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)method->function_index, method_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)decl->param_count, method_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (defer_call) {
        state->defer_emitted = 1;
        vigil_expression_result_set_type(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_VOID)
        );
    } else {
        vigil_expression_result_set_return_types(
            out_result,
            decl->return_type,
            vigil_function_return_types(decl),
            decl->return_count
        );
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_interface_method_call(
    vigil_parser_state_t *state,
    vigil_parser_type_t receiver_type,
    size_t method_index,
    const vigil_token_t *method_token,
    const vigil_interface_method_t *method,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t arg_result;
    size_t arg_count;
    int defer_call;

    vigil_expression_result_clear(&arg_result);
    defer_call = state->defer_mode;
    state->defer_mode = 0;
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after method name",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    arg_count = 0U;
    if (!vigil_parser_check(state, VIGIL_TOKEN_RPAREN)) {
        while (1) {
            status = vigil_parser_parse_expression(state, &arg_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state,
                method_token->span,
                &arg_result,
                "call arguments must be single values"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (arg_count >= method->param_count) {
                return vigil_parser_report(
                    state,
                    method_token->span,
                    "call argument count does not match function signature"
                );
            }
            status = vigil_parser_require_type(
                state,
                method_token->span,
                arg_result.type,
                method->param_types[arg_count],
                "call argument type does not match parameter type"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            arg_count += 1U;

            if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_RPAREN,
        "expected ')' after argument list",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (
        arg_count != method->param_count ||
        receiver_type.object_index > UINT32_MAX ||
        method_index > UINT32_MAX ||
        arg_count > UINT32_MAX
    ) {
        return vigil_parser_report(
            state,
            method_token->span,
            "call argument count does not match function signature"
        );
    }

    status = vigil_parser_emit_opcode(
        state,
        defer_call ? VIGIL_OPCODE_DEFER_CALL_INTERFACE : VIGIL_OPCODE_CALL_INTERFACE,
        method_token->span
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)receiver_type.object_index, method_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)method_index, method_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)arg_count, method_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (defer_call) {
        state->defer_emitted = 1;
        vigil_expression_result_set_type(
            out_result,
            vigil_binding_type_primitive(VIGIL_TYPE_VOID)
        );
    } else {
        vigil_expression_result_set_return_types(
            out_result,
            method->return_type,
            vigil_interface_method_return_types(method),
            method->return_count
        );
    }
    return VIGIL_STATUS_OK;
}


static vigil_status_t vigil_parser_parse_postfix_suffixes(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *field_token;
    const vigil_class_field_t *field;
    const vigil_class_method_t *class_method;
    const vigil_interface_method_t *interface_method;
    vigil_expression_result_t index_result;
    vigil_parser_type_t indexed_type;
    size_t field_index;
    size_t method_index;

    vigil_expression_result_clear(&index_result);
    while (1) {
        if (vigil_parser_check(state, VIGIL_TOKEN_LPAREN)) {
            status = vigil_parser_require_scalar_expression(
                state,
                vigil_parser_previous(state) == NULL
                    ? vigil_parser_fallback_span(state)
                    : vigil_parser_previous(state)->span,
                out_result,
                "multi-value expressions do not support calls"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (!vigil_parser_type_is_function(out_result->type)) {
                break;
            }
            status = vigil_parser_parse_value_call(
                state,
                vigil_parser_previous(state) == NULL
                    ? vigil_parser_fallback_span(state)
                    : vigil_parser_previous(state)->span,
                out_result->type,
                out_result
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }

        if (vigil_parser_match(state, VIGIL_TOKEN_DOT)) {
            status = vigil_parser_require_scalar_expression(
                state,
                vigil_parser_previous(state)->span,
                out_result,
                "multi-value expressions do not support member access"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_expect(
                state,
                VIGIL_TOKEN_IDENTIFIER,
                "expected field name after '.'",
                &field_token
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }

            class_method = NULL;
            interface_method = NULL;
            field = NULL;
            field_index = 0U;
            method_index = 0U;
            if (vigil_parser_find_field_by_name(state, out_result->type, field_token, &field_index, &field)) {
                if (field_index > UINT32_MAX) {
                    vigil_error_set_literal(
                        state->program->error,
                        VIGIL_STATUS_OUT_OF_MEMORY,
                        "field operand overflow"
                    );
                    return VIGIL_STATUS_OUT_OF_MEMORY;
                }

                status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_FIELD, field_token->span);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                status = vigil_parser_emit_u32(state, (uint32_t)field_index, field_token->span);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                vigil_expression_result_set_type(out_result, field->type);
                continue;
            }
            if (
                vigil_parser_check(state, VIGIL_TOKEN_LPAREN) &&
                vigil_parser_type_is_string(out_result->type)
            ) {
                status = vigil_parser_parse_string_method_call(state, field_token, out_result);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                continue;
            }
            if (
                vigil_parser_check(state, VIGIL_TOKEN_LPAREN) &&
                vigil_parser_type_is_array(out_result->type)
            ) {
                status = vigil_parser_parse_array_method_call(
                    state,
                    out_result->type,
                    field_token,
                    out_result
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                continue;
            }
            if (
                vigil_parser_check(state, VIGIL_TOKEN_LPAREN) &&
                vigil_parser_type_is_map(out_result->type)
            ) {
                status = vigil_parser_parse_map_method_call(
                    state,
                    out_result->type,
                    field_token,
                    out_result
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                continue;
            }
            if (
                vigil_parser_check(state, VIGIL_TOKEN_LPAREN) &&
                vigil_parser_type_is_class(out_result->type)
            ) {
                const vigil_class_decl_t *class_decl;

                class_method = NULL;
                if (vigil_parser_find_method_by_name(
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
                        return vigil_parser_report(
                            state,
                            field_token->span,
                            "class method is not public"
                        );
                    }
                    if (class_decl->native_class != NULL) {
                        if (class_decl->native_class->methods[method_index].is_static) {
                            return vigil_parser_report(
                                state, field_token->span,
                                "static method cannot be called on an instance"
                            );
                        }
                        status = vigil_parser_parse_native_method_call(
                            state, field_token,
                            class_decl->native_class,
                            &class_decl->native_class->methods[method_index],
                            out_result->type.object_index,
                            out_result);
                    } else {
                        status = vigil_parser_parse_method_call(state, field_token, class_method, out_result);
                    }
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    continue;
                }
            }
            if (
                vigil_parser_check(state, VIGIL_TOKEN_LPAREN) &&
                vigil_parser_find_interface_method_by_name(
                    state,
                    out_result->type,
                    field_token,
                    &method_index,
                    &interface_method
                )
            ) {
                status = vigil_parser_parse_interface_method_call(
                    state,
                    out_result->type,
                    method_index,
                    field_token,
                    interface_method,
                    out_result
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                continue;
            }

            if (
                vigil_parser_check(state, VIGIL_TOKEN_LPAREN) &&
                vigil_parser_type_is_class(out_result->type)
            ) {
                return vigil_parser_report(state, field_token->span, "unknown class method");
            }
            if (
                vigil_parser_check(state, VIGIL_TOKEN_LPAREN) &&
                vigil_parser_type_is_interface(out_result->type)
            ) {
                return vigil_parser_report(state, field_token->span, "unknown interface method");
            }

            if (
                vigil_parser_check(state, VIGIL_TOKEN_LPAREN) &&
                vigil_parser_type_is_err(out_result->type)
            ) {
                const char *error_method_name;
                size_t error_method_length;

                error_method_name = vigil_parser_token_text(state, field_token, &error_method_length);
                status = vigil_parser_expect(
                    state,
                    VIGIL_TOKEN_LPAREN,
                    "expected '(' after error method name",
                    NULL
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                status = vigil_parser_expect(
                    state,
                    VIGIL_TOKEN_RPAREN,
                    "error methods do not accept arguments",
                    NULL
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                if (vigil_program_names_equal(error_method_name, error_method_length, "kind", 4U)) {
                    status = vigil_parser_emit_opcode(
                        state,
                        VIGIL_OPCODE_GET_ERROR_KIND,
                        field_token->span
                    );
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    vigil_expression_result_set_type(
                        out_result,
                        vigil_binding_type_primitive(VIGIL_TYPE_I32)
                    );
                    continue;
                }
                if (vigil_program_names_equal(error_method_name, error_method_length, "message", 7U)) {
                    status = vigil_parser_emit_opcode(
                        state,
                        VIGIL_OPCODE_GET_ERROR_MESSAGE,
                        field_token->span
                    );
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    vigil_expression_result_set_type(
                        out_result,
                        vigil_binding_type_primitive(VIGIL_TYPE_STRING)
                    );
                    continue;
                }
                return vigil_parser_report(state, field_token->span, "unknown error method");
            }

            field = NULL;
            field_index = 0U;
            status = vigil_parser_lookup_field(
                state,
                out_result->type,
                field_token,
                &field_index,
                &field
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (field_index > UINT32_MAX) {
                vigil_error_set_literal(
                    state->program->error,
                    VIGIL_STATUS_OUT_OF_MEMORY,
                    "field operand overflow"
                );
                return VIGIL_STATUS_OUT_OF_MEMORY;
            }

            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_FIELD, field_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_u32(state, (uint32_t)field_index, field_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(out_result, field->type);
            continue;
        }

        if (vigil_parser_match(state, VIGIL_TOKEN_LBRACKET)) {
            vigil_expression_result_clear(&index_result);
            status = vigil_parser_require_scalar_expression(
                state,
                vigil_parser_previous(state)->span,
                out_result,
                "multi-value expressions do not support indexing"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_parse_expression(state, &index_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state,
                vigil_parser_previous(state) == NULL
                    ? vigil_parser_fallback_span(state)
                    : vigil_parser_previous(state)->span,
                &index_result,
                "index expressions must evaluate to a single value"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_expect(
                state,
                VIGIL_TOKEN_RBRACKET,
                "expected ']' after index expression",
                NULL
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }

            indexed_type = vigil_binding_type_invalid();
            if (vigil_parser_type_is_array(out_result->type)) {
                status = vigil_parser_require_type(
                    state,
                    vigil_parser_previous(state)->span,
                    index_result.type,
                    vigil_binding_type_primitive(VIGIL_TYPE_I32),
                    "array index must be i32"
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                indexed_type = vigil_program_array_type_element(state->program, out_result->type);
            } else if (vigil_parser_type_is_map(out_result->type)) {
                status = vigil_parser_require_type(
                    state,
                    vigil_parser_previous(state)->span,
                    index_result.type,
                    vigil_program_map_type_key(state->program, out_result->type),
                    "map index must match map key type"
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                indexed_type = vigil_program_map_type_value(state->program, out_result->type);
            } else {
                return vigil_parser_report(
                    state,
                    vigil_parser_previous(state)->span,
                    "index access requires an array or map"
                );
            }

            status = vigil_parser_emit_opcode(
                state,
                VIGIL_OPCODE_GET_INDEX,
                vigil_parser_previous(state)->span
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(out_result, indexed_type);
            continue;
        }

        break;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_nested_function_value(
    vigil_parser_state_t *state,
    int named_declaration,
    const vigil_token_t *decl_name_token,
    vigil_parser_type_t *out_type,
    size_t *out_function_index
) {
    vigil_status_t status;
    const vigil_token_t *fn_token;
    const vigil_token_t *name_token;
    const vigil_token_t *token;
    const vigil_token_t *type_token;
    const vigil_token_t *param_name_token;
    vigil_function_decl_t *decl;
    vigil_parser_type_t param_type;
    vigil_parser_type_t function_type;
    size_t function_index;
    size_t body_depth;
    size_t capture_index;

    if (out_type != NULL) {
        *out_type = vigil_binding_type_invalid();
    }
    if (out_function_index != NULL) {
        *out_function_index = 0U;
    }

    status = vigil_parser_expect(state, VIGIL_TOKEN_FN, "expected 'fn'", &fn_token);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    name_token = decl_name_token;
    if (named_declaration) {
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_IDENTIFIER,
            "expected local function name",
            &name_token
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    status = vigil_program_grow_functions(
        (vigil_program_state_t *)state->program,
        state->program->functions.count + 1U
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    function_index = state->program->functions.count;
    decl = &((vigil_program_state_t *)state->program)->functions.functions[function_index];
    vigil_binding_function_init(decl);
    decl->name = named_declaration ? vigil_parser_token_text(state, name_token, &decl->name_length) : "<anon>";
    decl->name_length = named_declaration ? decl->name_length : 6U;
    decl->name_span = named_declaration ? name_token->span : fn_token->span;
    decl->source = state->program->source;
    decl->tokens = state->program->tokens;
    decl->is_local = 1;

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after function name",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (!vigil_parser_check(state, VIGIL_TOKEN_RPAREN)) {
        while (1) {
            type_token = vigil_parser_peek(state);
            param_type = vigil_binding_type_invalid();
            status = vigil_program_parse_type_reference(
                state->program,
                &state->current,
                "unsupported local function parameter type",
                &param_type
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_program_require_non_void_type(
                state->program,
                type_token == NULL ? fn_token->span : type_token->span,
                param_type,
                "function parameters cannot use type void"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }

            status = vigil_parser_expect(
                state,
                VIGIL_TOKEN_IDENTIFIER,
                "expected parameter name",
                &param_name_token
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_program_add_param(
                (vigil_program_state_t *)state->program,
                decl,
                param_type,
                param_name_token
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }

            if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                break;
            }
        }
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_RPAREN,
        "expected ')' after parameter list",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_ARROW,
        "expected '->' after function signature",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_program_parse_function_return_types(
        (vigil_program_state_t *)state->program,
        &state->current,
        "unsupported local function return type",
        decl
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LBRACE,
        "expected '{' before function body",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    decl->body_start = state->current;
    body_depth = 1U;
    while (body_depth > 0U) {
        token = vigil_program_token_at(state->program, state->current);
        if (token == NULL || token->kind == VIGIL_TOKEN_EOF) {
            return vigil_parser_report(
                state,
                vigil_program_eof_span(state->program),
                "expected '}' after function body"
            );
        }
        if (token->kind == VIGIL_TOKEN_LBRACE) {
            body_depth += 1U;
        } else if (token->kind == VIGIL_TOKEN_RBRACE) {
            body_depth -= 1U;
            if (body_depth == 0U) {
                decl->body_end = state->current;
                state->current += 1U;
                break;
            }
        }
        state->current += 1U;
    }

    ((vigil_program_state_t *)state->program)->functions.count += 1U;
    status = vigil_compile_function_with_parent(
        (vigil_program_state_t *)state->program,
        function_index,
        state
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_program_intern_function_type_from_decl(
        (vigil_program_state_t *)state->program,
        decl,
        &function_type
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    for (capture_index = 0U; capture_index < decl->capture_count; ++capture_index) {
        status = vigil_parser_emit_opcode(
            state,
            decl->captures[capture_index].source_is_capture
                ? VIGIL_OPCODE_GET_CAPTURE
                : VIGIL_OPCODE_GET_LOCAL,
            decl->name_span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(
            state,
            (uint32_t)decl->captures[capture_index].source_local_index,
            decl->name_span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_NEW_CLOSURE, decl->name_span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)function_index, decl->name_span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)decl->capture_count, decl->name_span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (out_type != NULL) {
        *out_type = function_type;
    }
    if (out_function_index != NULL) {
        *out_function_index = function_index;
    }
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_local_function_declaration(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    vigil_parser_type_t function_type;
    const vigil_token_t *name_token;

    function_type = vigil_binding_type_invalid();
    name_token = vigil_program_token_at(state->program, state->current + 1U);
    status = vigil_parser_parse_nested_function_value(
        state,
        1,
        name_token,
        &function_type,
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_declare_local_symbol(state, name_token, function_type, 1, NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_primary_base(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *token;
    const vigil_global_constant_t *constant;
    vigil_value_t value;
    size_t local_index;
    size_t capture_index;
    size_t global_index;
    vigil_parser_type_t local_type;
    const vigil_global_variable_t *global_decl;
    const vigil_enum_member_t *enum_member;
    const char *name_text;
    const char *member_text;
    size_t name_length;
    size_t member_length;
    vigil_source_id_t source_id;
    size_t enum_index;
    int local_found;
    int local_is_capture;

    constant = NULL;
    local_index = 0U;
    capture_index = 0U;
    global_index = 0U;
    local_type = vigil_binding_type_invalid();
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
    token = vigil_parser_peek(state);
    if (token == NULL) {
        return vigil_parser_report(
            state,
            vigil_parser_fallback_span(state),
            "expected expression"
        );
    }

    switch (token->kind) {
        case VIGIL_TOKEN_INT_LITERAL:
            vigil_parser_advance(state);
            status = vigil_parser_parse_int_literal(state, token, &value, &local_type);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_chunk_write_constant(
                &state->chunk,
                &value,
                token->span,
                NULL,
                state->program->error
            );
            vigil_value_release(&value);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(out_result, local_type);
            return VIGIL_STATUS_OK;
        case VIGIL_TOKEN_TRUE:
            vigil_parser_advance(state);
            vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
            return vigil_parser_emit_opcode(state, VIGIL_OPCODE_TRUE, token->span);
        case VIGIL_TOKEN_FALSE:
            vigil_parser_advance(state);
            vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_BOOL));
            return vigil_parser_emit_opcode(state, VIGIL_OPCODE_FALSE, token->span);
        case VIGIL_TOKEN_NIL:
            vigil_parser_advance(state);
            vigil_expression_result_set_type(out_result, vigil_binding_type_primitive(VIGIL_TYPE_NIL));
            return vigil_parser_emit_opcode(state, VIGIL_OPCODE_NIL, token->span);
        case VIGIL_TOKEN_FN:
            status = vigil_parser_parse_nested_function_value(
                state,
                0,
                NULL,
                &local_type,
                NULL
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(out_result, local_type);
            return VIGIL_STATUS_OK;
        case VIGIL_TOKEN_IDENTIFIER:
            vigil_parser_advance(state);
            name_text = vigil_parser_token_text(state, token, &name_length);
            status = vigil_parser_resolve_local_symbol(
                state,
                token,
                &local_index,
                &local_type,
                &local_is_capture,
                &capture_index,
                &local_found
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (
                vigil_parser_check(state, VIGIL_TOKEN_DOT) &&
                !local_found &&
                vigil_program_resolve_import_alias(
                    state->program,
                    name_text,
                    name_length,
                    &source_id
                )
            ) {
                return vigil_parser_parse_qualified_symbol(state, token, out_result);
            }
            (void)vigil_program_find_global_in_source(
                state->program,
                state->program->source == NULL ? 0U : state->program->source->id,
                name_text,
                name_length,
                &global_index,
                &global_decl
            );
            if (vigil_parser_check(state, VIGIL_TOKEN_LPAREN)) {
                vigil_type_kind_t conversion_kind;

                if (
                    vigil_parser_resolve_builtin_conversion_kind(
                        state,
                        token,
                        &conversion_kind
                    )
                ) {
                    return vigil_parser_parse_builtin_conversion(
                        state,
                        token,
                        conversion_kind,
                        out_result
                    );
                }
                if (
                    !local_found &&
                    vigil_program_names_equal(name_text, name_length, "err", 3U)
                ) {
                    return vigil_parser_parse_builtin_error_constructor(state, token, out_result);
                }
                if (
                    !local_found &&
                    vigil_program_names_equal(name_text, name_length, "char", 4U)
                ) {
                    return vigil_parser_parse_builtin_char(state, token, out_result);
                }
                if (vigil_binding_type_is_valid(local_type) && vigil_parser_type_is_function(local_type)) {
                    vigil_expression_result_set_type(out_result, local_type);
                    status = vigil_parser_emit_opcode(
                        state,
                        local_is_capture ? VIGIL_OPCODE_GET_CAPTURE : VIGIL_OPCODE_GET_LOCAL,
                        token->span
                    );
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    status = vigil_parser_emit_u32(
                        state,
                        (uint32_t)(local_is_capture ? capture_index : local_index),
                        token->span
                    );
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    return vigil_parser_parse_value_call(state, token->span, local_type, out_result);
                }
                if (global_decl != NULL && vigil_parser_type_is_function(global_decl->type)) {
                    vigil_expression_result_set_type(out_result, global_decl->type);
                    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_GLOBAL, token->span);
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    status = vigil_parser_emit_u32(state, (uint32_t)global_index, token->span);
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    return vigil_parser_parse_value_call(
                        state,
                        token->span,
                        global_decl->type,
                        out_result
                    );
                }
                if (
                    vigil_program_find_function_symbol_in_source(
                        state->program,
                        state->program->source == NULL ? 0U : state->program->source->id,
                        token,
                        NULL,
                        NULL
                    )
                ) {
                    return vigil_parser_parse_call(state, token, out_result);
                }
                if (
                    vigil_program_find_class_symbol_in_source(
                        state->program,
                        state->program->source == NULL ? 0U : state->program->source->id,
                        token,
                        NULL,
                        NULL
                    )
                ) {
                    return vigil_parser_parse_constructor(state, token, out_result);
                }
                return vigil_parser_report(state, token->span, "unknown function");
            }
            if (
                vigil_parser_check(state, VIGIL_TOKEN_DOT) &&
                !local_found &&
                (
                    vigil_program_names_equal(name_text, name_length, "err", 3U) ||
                    vigil_program_find_enum_in_source(
                        state->program,
                        state->program->source == NULL ? 0U : state->program->source->id,
                        name_text,
                        name_length,
                        &enum_index,
                        NULL
                    )
                )
            ) {
                vigil_parser_advance(state);
                {
                    const vigil_token_t *member_token = vigil_parser_peek(state);
                    int64_t error_kind;

                    if (member_token == NULL || member_token->kind != VIGIL_TOKEN_IDENTIFIER) {
                        return vigil_parser_report(state, token->span, "unknown enum member");
                    }
                    vigil_parser_advance(state);
                    member_text = vigil_parser_token_text(state, member_token, &member_length);
                    error_kind = 0;
                    if (
                        vigil_program_names_equal(name_text, name_length, "err", 3U) &&
                        vigil_builtin_error_kind_by_name(member_text, member_length, &error_kind)
                    ) {
                        vigil_value_init_int(&value, error_kind);
                        status = vigil_chunk_write_constant(
                            &state->chunk,
                            &value,
                            member_token->span,
                            NULL,
                            state->program->error
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
                    if (
                        !vigil_program_lookup_enum_member_in_source(
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
                        return vigil_parser_report(state, member_token->span, "unknown enum member");
                    }

                    vigil_value_init_int(&value, enum_member->value);
                    status = vigil_chunk_write_constant(
                        &state->chunk,
                        &value,
                        member_token->span,
                        NULL,
                        state->program->error
                    );
                    vigil_value_release(&value);
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    vigil_expression_result_set_type(
                        out_result,
                        vigil_binding_type_enum(enum_index)
                    );
                    return VIGIL_STATUS_OK;
                }
            }

            if (local_found && vigil_binding_type_is_valid(local_type)) {
                vigil_expression_result_set_type(out_result, local_type);
                status = vigil_parser_emit_opcode(
                    state,
                    local_is_capture ? VIGIL_OPCODE_GET_CAPTURE : VIGIL_OPCODE_GET_LOCAL,
                    token->span
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                return vigil_parser_emit_u32(
                    state,
                    (uint32_t)(local_is_capture ? capture_index : local_index),
                    token->span
                );
            }

            if (global_decl != NULL) {
                vigil_expression_result_set_type(out_result, global_decl->type);
                status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_GLOBAL, token->span);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                return vigil_parser_emit_u32(state, (uint32_t)global_index, token->span);
            }

            if (
                vigil_program_find_function_symbol_in_source(
                    state->program,
                    state->program->source == NULL ? 0U : state->program->source->id,
                    token,
                    &global_index,
                    NULL
                )
            ) {
                vigil_parser_type_t function_type;

                function_type = vigil_binding_type_invalid();
                status = vigil_program_intern_function_type_from_decl(
                    (vigil_program_state_t *)state->program,
                    vigil_binding_function_table_get(&state->program->functions, global_index),
                    &function_type
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                vigil_expression_result_set_type(out_result, function_type);
                status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_FUNCTION, token->span);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                return vigil_parser_emit_u32(state, (uint32_t)global_index, token->span);
            }

            constant = NULL;
            if (
                !vigil_program_find_constant_in_source(
                    state->program,
                    state->program->source == NULL ? 0U : state->program->source->id,
                    name_text,
                    name_length,
                    &constant
                )
            ) {
                if (vigil_program_names_equal(name_text, name_length, "ok", 2U)) {
                    status = vigil_parser_emit_ok_constant(state, token->span);
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    vigil_expression_result_set_type(
                        out_result,
                        vigil_binding_type_primitive(VIGIL_TYPE_ERR)
                    );
                    return VIGIL_STATUS_OK;
                }
                return vigil_parser_report(state, token->span, "unknown local variable");
            }
            status = vigil_chunk_write_constant(
                &state->chunk,
                &constant->value,
                token->span,
                NULL,
                state->program->error
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(out_result, constant->type);
            return VIGIL_STATUS_OK;
        case VIGIL_TOKEN_LBRACKET:
            vigil_parser_advance(state);
            {
                vigil_expression_result_t item_result;
                vigil_parser_type_t element_type;
                vigil_parser_type_t array_type;
                size_t item_count;

                vigil_expression_result_clear(&item_result);
                element_type = vigil_binding_type_invalid();
                array_type = vigil_binding_type_invalid();
                item_count = 0U;

                if (vigil_parser_match(state, VIGIL_TOKEN_RBRACKET)) {
                    return vigil_parser_report(
                        state,
                        token->span,
                        "array literals require at least one element"
                    );
                }

                while (1) {
                    vigil_expression_result_clear(&item_result);
                    status = vigil_parser_parse_expression(state, &item_result);
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    status = vigil_parser_require_scalar_expression(
                        state,
                        vigil_parser_previous(state) == NULL
                            ? token->span
                            : vigil_parser_previous(state)->span,
                        &item_result,
                        "array literal elements must be single values"
                    );
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    if (item_count == 0U) {
                        element_type = item_result.type;
                    } else {
                        status = vigil_parser_require_type(
                            state,
                            vigil_parser_previous(state) == NULL
                                ? token->span
                                : vigil_parser_previous(state)->span,
                            item_result.type,
                            element_type,
                            "array literal elements must have matching types"
                        );
                        if (status != VIGIL_STATUS_OK) {
                            return status;
                        }
                    }
                    item_count += 1U;
                    if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                        break;
                    }
                }

                status = vigil_parser_expect(
                    state,
                    VIGIL_TOKEN_RBRACKET,
                    "expected ']' after array literal",
                    NULL
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                status = vigil_program_intern_array_type(
                    (vigil_program_state_t *)state->program,
                    element_type,
                    &array_type
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                if (array_type.object_index > UINT32_MAX || item_count > UINT32_MAX) {
                    vigil_error_set_literal(
                        state->program->error,
                        VIGIL_STATUS_OUT_OF_MEMORY,
                        "array literal operand overflow"
                    );
                    return VIGIL_STATUS_OUT_OF_MEMORY;
                }
                status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_NEW_ARRAY, token->span);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                status = vigil_parser_emit_u32(
                    state,
                    (uint32_t)array_type.object_index,
                    token->span
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                status = vigil_parser_emit_u32(state, (uint32_t)item_count, token->span);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                vigil_expression_result_set_type(out_result, array_type);
                return VIGIL_STATUS_OK;
            }
        case VIGIL_TOKEN_LBRACE:
            vigil_parser_advance(state);
            {
                vigil_expression_result_t key_result;
                vigil_expression_result_t value_result;
                vigil_parser_type_t key_type;
                vigil_parser_type_t map_type;
                vigil_parser_type_t value_type;
                size_t pair_count;

                vigil_expression_result_clear(&key_result);
                vigil_expression_result_clear(&value_result);
                key_type = vigil_binding_type_invalid();
                map_type = vigil_binding_type_invalid();
                value_type = vigil_binding_type_invalid();
                pair_count = 0U;

                if (vigil_parser_match(state, VIGIL_TOKEN_RBRACE)) {
                    return vigil_parser_report(
                        state,
                        token->span,
                        "map literals require at least one entry"
                    );
                }

                while (1) {
                    vigil_expression_result_clear(&key_result);
                    vigil_expression_result_clear(&value_result);
                    status = vigil_parser_parse_expression(state, &key_result);
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    status = vigil_parser_require_scalar_expression(
                        state,
                        vigil_parser_previous(state) == NULL
                            ? token->span
                            : vigil_parser_previous(state)->span,
                        &key_result,
                        "map literal keys must be single values"
                    );
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    if (pair_count == 0U) {
                        if (!vigil_parser_type_supports_map_key(key_result.type)) {
                            return vigil_parser_report(
                                state,
                                vigil_parser_previous(state) == NULL
                                    ? token->span
                                    : vigil_parser_previous(state)->span,
                                "map literal keys must use an integer, bool, string, or enum type"
                            );
                        }
                        key_type = key_result.type;
                    } else {
                        status = vigil_parser_require_type(
                            state,
                            vigil_parser_previous(state) == NULL
                                ? token->span
                                : vigil_parser_previous(state)->span,
                            key_result.type,
                            key_type,
                            "map literal keys must have matching types"
                        );
                        if (status != VIGIL_STATUS_OK) {
                            return status;
                        }
                    }
                    status = vigil_parser_expect(
                        state,
                        VIGIL_TOKEN_COLON,
                        "expected ':' after map key",
                        NULL
                    );
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    status = vigil_parser_parse_expression(state, &value_result);
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    status = vigil_parser_require_scalar_expression(
                        state,
                        vigil_parser_previous(state) == NULL
                            ? token->span
                            : vigil_parser_previous(state)->span,
                        &value_result,
                        "map literal values must be single values"
                    );
                    if (status != VIGIL_STATUS_OK) {
                        return status;
                    }
                    if (pair_count == 0U) {
                        value_type = value_result.type;
                    } else {
                        status = vigil_parser_require_type(
                            state,
                            vigil_parser_previous(state) == NULL
                                ? token->span
                                : vigil_parser_previous(state)->span,
                            value_result.type,
                            value_type,
                            "map literal values must have matching types"
                        );
                        if (status != VIGIL_STATUS_OK) {
                            return status;
                        }
                    }
                    pair_count += 1U;
                    if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                        break;
                    }
                }

                status = vigil_parser_expect(
                    state,
                    VIGIL_TOKEN_RBRACE,
                    "expected '}' after map literal",
                    NULL
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                status = vigil_program_intern_map_type(
                    (vigil_program_state_t *)state->program,
                    key_type,
                    value_type,
                    &map_type
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                if (map_type.object_index > UINT32_MAX || pair_count > UINT32_MAX) {
                    vigil_error_set_literal(
                        state->program->error,
                        VIGIL_STATUS_OUT_OF_MEMORY,
                        "map literal operand overflow"
                    );
                    return VIGIL_STATUS_OUT_OF_MEMORY;
                }
                status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_NEW_MAP, token->span);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                status = vigil_parser_emit_u32(
                    state,
                    (uint32_t)map_type.object_index,
                    token->span
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                status = vigil_parser_emit_u32(state, (uint32_t)pair_count, token->span);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                vigil_expression_result_set_type(out_result, map_type);
                return VIGIL_STATUS_OK;
            }
        case VIGIL_TOKEN_LPAREN:
            vigil_parser_advance(state);
            status = vigil_parser_parse_expression(state, out_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            return vigil_parser_expect(
                state,
                VIGIL_TOKEN_RPAREN,
                "expected ')' after expression",
                NULL
            );
        case VIGIL_TOKEN_STRING_LITERAL:
        case VIGIL_TOKEN_RAW_STRING_LITERAL:
        case VIGIL_TOKEN_CHAR_LITERAL:
            vigil_parser_advance(state);
            {
                vigil_value_t string_value;

                vigil_value_init_nil(&string_value);
                status = vigil_program_parse_string_literal_value(
                    state->program,
                    token,
                    &string_value
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                status = vigil_chunk_write_constant(
                    &state->chunk,
                    &string_value,
                    token->span,
                    NULL,
                    state->program->error
                );
                vigil_value_release(&string_value);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING)
            );
            return VIGIL_STATUS_OK;
        case VIGIL_TOKEN_FLOAT_LITERAL:
            vigil_parser_advance(state);
            {
                vigil_value_t float_value;

                vigil_value_init_nil(&float_value);
                status = vigil_parser_parse_float_literal(state, token, &float_value);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                status = vigil_chunk_write_constant(
                    &state->chunk,
                    &float_value,
                    token->span,
                    NULL,
                    state->program->error
                );
                vigil_value_release(&float_value);
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_F64)
            );
            return VIGIL_STATUS_OK;
        case VIGIL_TOKEN_FSTRING_LITERAL:
            vigil_parser_advance(state);
            return vigil_parser_parse_fstring_literal(state, token, out_result);
        default:
            return vigil_parser_report(state, token->span, "expected expression");
    }
}

static vigil_status_t vigil_parser_parse_primary(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;

    status = vigil_parser_parse_primary_base(state, out_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    return vigil_parser_parse_postfix_suffixes(state, out_result);
}

static vigil_status_t vigil_parser_parse_unary(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *operator_token;
    vigil_expression_result_t operand_result;

    vigil_expression_result_clear(&operand_result);

    operator_token = vigil_parser_peek(state);
    if (operator_token != NULL &&
        (operator_token->kind == VIGIL_TOKEN_MINUS ||
         operator_token->kind == VIGIL_TOKEN_BANG ||
         operator_token->kind == VIGIL_TOKEN_TILDE)) {
        vigil_parser_advance(state);
        status = vigil_parser_parse_unary(state, &operand_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_token->span,
            &operand_result,
            "multi-value expressions cannot be used with unary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        if (operator_token->kind == VIGIL_TOKEN_MINUS) {
            status = vigil_parser_require_unary_operator(
                state,
                operator_token->span,
                VIGIL_UNARY_OPERATOR_NEGATE,
                operand_result.type,
                "unary '-' requires a signed integer or f64 operand"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(out_result, operand_result.type);
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_NEGATE, operator_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            return vigil_parser_emit_integer_cast(state, operand_result.type, operator_token->span);
        }

        if (operator_token->kind == VIGIL_TOKEN_BANG) {
            status = vigil_parser_require_unary_operator(
                state,
                operator_token->span,
                VIGIL_UNARY_OPERATOR_LOGICAL_NOT,
                operand_result.type,
                "logical '!' requires a bool operand"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            vigil_expression_result_set_type(
                out_result,
                vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
            );
            return vigil_parser_emit_opcode(state, VIGIL_OPCODE_NOT, operator_token->span);
        }

        status = vigil_parser_require_unary_operator(
            state,
            operator_token->span,
            VIGIL_UNARY_OPERATOR_BITWISE_NOT,
            operand_result.type,
            "bitwise '~' requires a signed integer operand"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(out_result, operand_result.type);
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_BITWISE_NOT, operator_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        return vigil_parser_emit_integer_cast(state, operand_result.type, operator_token->span);
    }

    return vigil_parser_parse_primary(state, out_result);
}

static vigil_status_t vigil_parser_parse_factor(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t left_result;
    vigil_expression_result_t right_result;
    vigil_token_kind_t operator_kind;
    vigil_source_span_t operator_span;
    size_t pre_left_size;

    vigil_expression_result_clear(&left_result);
    vigil_expression_result_clear(&right_result);

    pre_left_size = state->chunk.code.length;
    status = vigil_parser_parse_unary(state, &left_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (vigil_parser_check(state, VIGIL_TOKEN_STAR)) {
            operator_kind = VIGIL_TOKEN_STAR;
        } else if (vigil_parser_check(state, VIGIL_TOKEN_SLASH)) {
            operator_kind = VIGIL_TOKEN_SLASH;
        } else if (vigil_parser_check(state, VIGIL_TOKEN_PERCENT)) {
            operator_kind = VIGIL_TOKEN_PERCENT;
        } else {
            break;
        }

        operator_span = vigil_parser_advance(state)->span;
        status = vigil_parser_parse_unary(state, &right_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (operator_kind == VIGIL_TOKEN_PERCENT) {
            status = vigil_parser_require_i32_operands(
                state,
                operator_span,
                left_result.type,
                right_result.type,
                VIGIL_BINARY_OPERATOR_MODULO,
                "modulo requires matching integer operands"
            );
        } else if (
            !vigil_parser_type_supports_binary_operator(
                operator_kind == VIGIL_TOKEN_STAR
                    ? VIGIL_BINARY_OPERATOR_MULTIPLY
                    : VIGIL_BINARY_OPERATOR_DIVIDE,
                left_result.type,
                right_result.type
            )
        ) {
            status = vigil_parser_report(
                state,
                operator_span,
                "arithmetic operators require matching integer or f64 operands"
            );
        } else {
            status = VIGIL_STATUS_OK;
        }
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        if (operator_kind == VIGIL_TOKEN_STAR) {
            int both_i32 = vigil_parser_type_is_i32(left_result.type) &&
                           vigil_parser_type_is_i32(right_result.type);
            int both_si = !both_i32 &&
                          vigil_parser_type_is_signed_integer(left_result.type) &&
                          vigil_parser_type_is_signed_integer(right_result.type);
            status = both_i32
                ? vigil_parser_emit_i32_binop(state, VIGIL_OPCODE_MULTIPLY_I64, operator_span, pre_left_size)
                : both_si
                ? vigil_parser_emit_i64_binop(state, VIGIL_OPCODE_MULTIPLY_I64, operator_span, pre_left_size)
                : vigil_parser_emit_opcode(state, VIGIL_OPCODE_MULTIPLY, operator_span);
        } else if (operator_kind == VIGIL_TOKEN_SLASH) {
            int both_i32 = vigil_parser_type_is_i32(left_result.type) &&
                           vigil_parser_type_is_i32(right_result.type);
            int both_si = !both_i32 &&
                          vigil_parser_type_is_signed_integer(left_result.type) &&
                          vigil_parser_type_is_signed_integer(right_result.type);
            status = both_i32
                ? vigil_parser_emit_i32_binop(state, VIGIL_OPCODE_DIVIDE_I64, operator_span, pre_left_size)
                : both_si
                ? vigil_parser_emit_i64_binop(state, VIGIL_OPCODE_DIVIDE_I64, operator_span, pre_left_size)
                : vigil_parser_emit_opcode(state, VIGIL_OPCODE_DIVIDE, operator_span);
        } else {
            int both_i32 = vigil_parser_type_is_i32(left_result.type) &&
                           vigil_parser_type_is_i32(right_result.type);
            int both_si = !both_i32 &&
                          vigil_parser_type_is_signed_integer(left_result.type) &&
                          vigil_parser_type_is_signed_integer(right_result.type);
            status = both_i32
                ? vigil_parser_emit_i32_binop(state, VIGIL_OPCODE_MODULO_I64, operator_span, pre_left_size)
                : both_si
                ? vigil_parser_emit_i64_binop(state, VIGIL_OPCODE_MODULO_I64, operator_span, pre_left_size)
                : vigil_parser_emit_opcode(state, VIGIL_OPCODE_MODULO, operator_span);
        }
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        /* i32 opcodes already produce i32 results — skip the cast. */
        if (!vigil_parser_type_is_i32(left_result.type) ||
            !vigil_parser_type_is_i32(right_result.type)) {
            status = vigil_parser_emit_integer_cast(state, left_result.type, operator_span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
        }
    }

    vigil_expression_result_copy(out_result, &left_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_term(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t left_result;
    vigil_expression_result_t right_result;
    vigil_token_kind_t operator_kind;
    vigil_source_span_t operator_span;
    size_t pre_left_size;

    vigil_expression_result_clear(&left_result);
    vigil_expression_result_clear(&right_result);

    pre_left_size = state->chunk.code.length;
    status = vigil_parser_parse_factor(state, &left_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (vigil_parser_check(state, VIGIL_TOKEN_PLUS)) {
            operator_kind = VIGIL_TOKEN_PLUS;
        } else if (vigil_parser_check(state, VIGIL_TOKEN_MINUS)) {
            operator_kind = VIGIL_TOKEN_MINUS;
        } else {
            break;
        }

        operator_span = vigil_parser_advance(state)->span;
        status = vigil_parser_parse_factor(state, &right_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (
            !vigil_parser_type_supports_binary_operator(
                operator_kind == VIGIL_TOKEN_PLUS
                    ? VIGIL_BINARY_OPERATOR_ADD
                    : VIGIL_BINARY_OPERATOR_SUBTRACT,
                left_result.type,
                right_result.type
            )
        ) {
            return vigil_parser_report(
                state,
                operator_span,
                operator_kind == VIGIL_TOKEN_PLUS
                    ? "'+' requires matching integer, f64, or string operands"
                    : "arithmetic operators require matching integer or f64 operands"
            );
        }

        {
            int both_i32 = vigil_parser_type_is_i32(left_result.type) &&
                           vigil_parser_type_is_i32(right_result.type);
            int both_si = !both_i32 &&
                          vigil_parser_type_is_signed_integer(left_result.type) &&
                          vigil_parser_type_is_signed_integer(right_result.type);
            vigil_opcode_t op = operator_kind == VIGIL_TOKEN_PLUS
                ? ((both_si || both_i32) ? VIGIL_OPCODE_ADD_I64 : VIGIL_OPCODE_ADD)
                : ((both_si || both_i32) ? VIGIL_OPCODE_SUBTRACT_I64 : VIGIL_OPCODE_SUBTRACT);
            status = both_i32
                ? vigil_parser_emit_i32_binop(state, op, operator_span, pre_left_size)
                : (both_si
                    ? vigil_parser_emit_i64_binop(state, op, operator_span, pre_left_size)
                    : vigil_parser_emit_opcode(state, op, operator_span));
        }
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (
            operator_kind == VIGIL_TOKEN_PLUS &&
            vigil_parser_type_equal(
                left_result.type,
                vigil_binding_type_primitive(VIGIL_TYPE_STRING)
            )
        ) {
            left_result.type = vigil_binding_type_primitive(VIGIL_TYPE_STRING);
        } else if (!(vigil_parser_type_is_i32(left_result.type) &&
                     vigil_parser_type_is_i32(right_result.type))) {
            status = vigil_parser_emit_integer_cast(state, left_result.type, operator_span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
        }
    }

    vigil_expression_result_copy(out_result, &left_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_shift(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t left_result;
    vigil_expression_result_t right_result;
    vigil_token_kind_t operator_kind;
    vigil_source_span_t operator_span;

    vigil_expression_result_clear(&left_result);
    vigil_expression_result_clear(&right_result);

    status = vigil_parser_parse_term(state, &left_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (vigil_parser_check(state, VIGIL_TOKEN_SHIFT_LEFT)) {
            operator_kind = VIGIL_TOKEN_SHIFT_LEFT;
        } else if (vigil_parser_check(state, VIGIL_TOKEN_SHIFT_RIGHT)) {
            operator_kind = VIGIL_TOKEN_SHIFT_RIGHT;
        } else {
            break;
        }

        operator_span = vigil_parser_advance(state)->span;
        status = vigil_parser_parse_term(state, &right_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            operator_kind == VIGIL_TOKEN_SHIFT_LEFT
                ? VIGIL_BINARY_OPERATOR_SHIFT_LEFT
                : VIGIL_BINARY_OPERATOR_SHIFT_RIGHT,
            "shift operators require matching integer operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_emit_opcode(
            state,
            operator_kind == VIGIL_TOKEN_SHIFT_LEFT
                ? VIGIL_OPCODE_SHIFT_LEFT
                : VIGIL_OPCODE_SHIFT_RIGHT,
            operator_span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_integer_cast(state, left_result.type, operator_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    vigil_expression_result_copy(out_result, &left_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_comparison(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t left_result;
    vigil_expression_result_t right_result;
    vigil_token_kind_t operator_kind;
    vigil_source_span_t operator_span;
    size_t pre_left_size;

    vigil_expression_result_clear(&left_result);
    vigil_expression_result_clear(&right_result);

    pre_left_size = state->chunk.code.length;
    status = vigil_parser_parse_shift(state, &left_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (vigil_parser_check(state, VIGIL_TOKEN_GREATER)) {
            operator_kind = VIGIL_TOKEN_GREATER;
        } else if (vigil_parser_check(state, VIGIL_TOKEN_GREATER_EQUAL)) {
            operator_kind = VIGIL_TOKEN_GREATER_EQUAL;
        } else if (vigil_parser_check(state, VIGIL_TOKEN_LESS)) {
            operator_kind = VIGIL_TOKEN_LESS;
        } else if (vigil_parser_check(state, VIGIL_TOKEN_LESS_EQUAL)) {
            operator_kind = VIGIL_TOKEN_LESS_EQUAL;
        } else {
            break;
        }

        operator_span = vigil_parser_advance(state)->span;
        status = vigil_parser_parse_shift(state, &right_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (
            !vigil_parser_type_supports_binary_operator(
                operator_kind == VIGIL_TOKEN_GREATER
                    ? VIGIL_BINARY_OPERATOR_GREATER
                    : (operator_kind == VIGIL_TOKEN_GREATER_EQUAL
                           ? VIGIL_BINARY_OPERATOR_GREATER_EQUAL
                           : (operator_kind == VIGIL_TOKEN_LESS
                                  ? VIGIL_BINARY_OPERATOR_LESS
                                  : VIGIL_BINARY_OPERATOR_LESS_EQUAL)),
                left_result.type,
                right_result.type
            )
        ) {
            status = vigil_parser_report(
                state,
                operator_span,
                "comparison operators require matching integer, f64, or string operands"
            );
        } else {
            status = VIGIL_STATUS_OK;
        }
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        {
            int both_i32 = vigil_parser_type_is_i32(left_result.type) &&
                           vigil_parser_type_is_i32(right_result.type);
            int both_signed = !both_i32 &&
                              vigil_parser_type_is_signed_integer(left_result.type) &&
                              vigil_parser_type_is_signed_integer(right_result.type);
            switch (operator_kind) {
                case VIGIL_TOKEN_GREATER:
                    status = both_i32
                        ? vigil_parser_emit_i32_binop(state, VIGIL_OPCODE_GREATER_I64, operator_span, pre_left_size)
                        : both_signed
                        ? vigil_parser_emit_i64_binop(state, VIGIL_OPCODE_GREATER_I64, operator_span, pre_left_size)
                        : vigil_parser_emit_opcode(state, VIGIL_OPCODE_GREATER, operator_span);
                    break;
                case VIGIL_TOKEN_LESS:
                    status = both_i32
                        ? vigil_parser_emit_i32_binop(state, VIGIL_OPCODE_LESS_I64, operator_span, pre_left_size)
                        : both_signed
                        ? vigil_parser_emit_i64_binop(state, VIGIL_OPCODE_LESS_I64, operator_span, pre_left_size)
                        : vigil_parser_emit_opcode(state, VIGIL_OPCODE_LESS, operator_span);
                    break;
                case VIGIL_TOKEN_GREATER_EQUAL:
                    if (both_i32) {
                        status = vigil_parser_emit_i32_binop(state,
                            VIGIL_OPCODE_GREATER_EQUAL_I64, operator_span, pre_left_size);
                    } else if (both_signed) {
                        status = vigil_parser_emit_i64_binop(state,
                            VIGIL_OPCODE_GREATER_EQUAL_I64, operator_span, pre_left_size);
                    } else {
                        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_LESS, operator_span);
                        if (status == VIGIL_STATUS_OK) {
                            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_NOT, operator_span);
                        }
                    }
                    break;
                case VIGIL_TOKEN_LESS_EQUAL:
                    if (both_i32) {
                        status = vigil_parser_emit_i32_binop(state,
                            VIGIL_OPCODE_LESS_EQUAL_I64, operator_span, pre_left_size);
                    } else if (both_signed) {
                        status = vigil_parser_emit_i64_binop(state,
                            VIGIL_OPCODE_LESS_EQUAL_I64, operator_span, pre_left_size);
                    } else {
                        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GREATER, operator_span);
                        if (status == VIGIL_STATUS_OK) {
                            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_NOT, operator_span);
                        }
                    }
                    break;
                default:
                    status = VIGIL_STATUS_INTERNAL;
                    break;
            }
        }
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        left_result.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
    }

    vigil_expression_result_copy(out_result, &left_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_equality(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t left_result;
    vigil_expression_result_t right_result;
    vigil_token_kind_t operator_kind;
    vigil_source_span_t operator_span;
    size_t pre_left_size;

    vigil_expression_result_clear(&left_result);
    vigil_expression_result_clear(&right_result);

    pre_left_size = state->chunk.code.length;
    status = vigil_parser_parse_comparison(state, &left_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (1) {
        if (vigil_parser_check(state, VIGIL_TOKEN_EQUAL_EQUAL)) {
            operator_kind = VIGIL_TOKEN_EQUAL_EQUAL;
        } else if (vigil_parser_check(state, VIGIL_TOKEN_BANG_EQUAL)) {
            operator_kind = VIGIL_TOKEN_BANG_EQUAL;
        } else {
            break;
        }

        operator_span = vigil_parser_advance(state)->span;
        status = vigil_parser_parse_comparison(state, &right_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with equality operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with equality operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_same_type(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            "equality operators require operands of the same type"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        if (vigil_parser_type_is_i32(left_result.type) &&
            vigil_parser_type_is_i32(right_result.type)) {
            vigil_opcode_t eq_op = operator_kind == VIGIL_TOKEN_BANG_EQUAL
                ? VIGIL_OPCODE_NOT_EQUAL_I64 : VIGIL_OPCODE_EQUAL_I64;
            status = vigil_parser_emit_i32_binop(state, eq_op, operator_span, pre_left_size);
        } else if (vigil_parser_type_is_signed_integer(left_result.type) &&
            vigil_parser_type_is_signed_integer(right_result.type)) {
            vigil_opcode_t eq_op = operator_kind == VIGIL_TOKEN_BANG_EQUAL
                ? VIGIL_OPCODE_NOT_EQUAL_I64 : VIGIL_OPCODE_EQUAL_I64;
            status = vigil_parser_emit_i64_binop(state, eq_op, operator_span, pre_left_size);
        } else {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_EQUAL, operator_span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (operator_kind == VIGIL_TOKEN_BANG_EQUAL) {
                status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_NOT, operator_span);
            }
        }
        left_result.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
    }

    vigil_expression_result_copy(out_result, &left_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_bitwise_and(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t left_result;
    vigil_expression_result_t right_result;
    vigil_source_span_t operator_span;

    vigil_expression_result_clear(&left_result);
    vigil_expression_result_clear(&right_result);

    status = vigil_parser_parse_equality(state, &left_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (vigil_parser_check(state, VIGIL_TOKEN_AMPERSAND)) {
        operator_span = vigil_parser_advance(state)->span;
        status = vigil_parser_parse_equality(state, &right_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            VIGIL_BINARY_OPERATOR_BITWISE_AND,
            "bitwise operators require matching integer operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_BITWISE_AND, operator_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_integer_cast(state, left_result.type, operator_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    vigil_expression_result_copy(out_result, &left_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_bitwise_xor(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t left_result;
    vigil_expression_result_t right_result;
    vigil_source_span_t operator_span;

    vigil_expression_result_clear(&left_result);
    vigil_expression_result_clear(&right_result);

    status = vigil_parser_parse_bitwise_and(state, &left_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (vigil_parser_check(state, VIGIL_TOKEN_CARET)) {
        operator_span = vigil_parser_advance(state)->span;
        status = vigil_parser_parse_bitwise_and(state, &right_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            VIGIL_BINARY_OPERATOR_BITWISE_XOR,
            "bitwise operators require matching integer operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_BITWISE_XOR, operator_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_integer_cast(state, left_result.type, operator_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    vigil_expression_result_copy(out_result, &left_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_bitwise_or(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t left_result;
    vigil_expression_result_t right_result;
    vigil_source_span_t operator_span;

    vigil_expression_result_clear(&left_result);
    vigil_expression_result_clear(&right_result);

    status = vigil_parser_parse_bitwise_xor(state, &left_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (vigil_parser_check(state, VIGIL_TOKEN_PIPE)) {
        operator_span = vigil_parser_advance(state)->span;
        status = vigil_parser_parse_bitwise_xor(state, &right_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "multi-value expressions cannot be used with binary operators"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_i32_operands(
            state,
            operator_span,
            left_result.type,
            right_result.type,
            VIGIL_BINARY_OPERATOR_BITWISE_OR,
            "bitwise operators require matching integer operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_BITWISE_OR, operator_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_integer_cast(state, left_result.type, operator_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    vigil_expression_result_copy(out_result, &left_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_logical_and(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t left_result;
    vigil_expression_result_t right_result;
    vigil_source_span_t operator_span;
    size_t false_jump_offset;

    vigil_expression_result_clear(&left_result);
    vigil_expression_result_clear(&right_result);

    status = vigil_parser_parse_bitwise_or(state, &left_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (vigil_parser_check(state, VIGIL_TOKEN_AMPERSAND_AMPERSAND)) {
        operator_span = vigil_parser_advance(state)->span;
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "logical '&&' requires bool operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_bool_type(
            state,
            operator_span,
            left_result.type,
            "logical '&&' requires bool operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_emit_jump(
            state,
            VIGIL_OPCODE_JUMP_IF_FALSE,
            operator_span,
            &false_jump_offset
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, operator_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_parse_bitwise_or(state, &right_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "logical '&&' requires bool operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_bool_type(
            state,
            operator_span,
            right_result.type,
            "logical '&&' requires bool operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_patch_jump(state, false_jump_offset);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        left_result.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
    }

    vigil_expression_result_copy(out_result, &left_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_logical_or(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t left_result;
    vigil_expression_result_t right_result;
    vigil_source_span_t operator_span;
    size_t false_jump_offset;
    size_t end_jump_offset;

    vigil_expression_result_clear(&left_result);
    vigil_expression_result_clear(&right_result);

    status = vigil_parser_parse_logical_and(state, &left_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    while (vigil_parser_check(state, VIGIL_TOKEN_PIPE_PIPE)) {
        operator_span = vigil_parser_advance(state)->span;
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &left_result,
            "logical '||' requires bool operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_bool_type(
            state,
            operator_span,
            left_result.type,
            "logical '||' requires bool operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_emit_jump(
            state,
            VIGIL_OPCODE_JUMP_IF_FALSE,
            operator_span,
            &false_jump_offset
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_jump(
            state,
            VIGIL_OPCODE_JUMP,
            operator_span,
            &end_jump_offset
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_patch_jump(state, false_jump_offset);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, operator_span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_parse_logical_and(state, &right_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_span,
            &right_result,
            "logical '||' requires bool operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_bool_type(
            state,
            operator_span,
            right_result.type,
            "logical '||' requires bool operands"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_patch_jump(state, end_jump_offset);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        left_result.type = vigil_binding_type_primitive(VIGIL_TYPE_BOOL);
    }

    vigil_expression_result_copy(out_result, &left_result);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_ternary(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t condition_result;
    vigil_expression_result_t then_result;
    vigil_expression_result_t else_result;
    const vigil_token_t *question_token;
    const vigil_token_t *colon_token;
    size_t false_jump_offset;
    size_t end_jump_offset;

    vigil_expression_result_clear(&condition_result);
    vigil_expression_result_clear(&then_result);
    vigil_expression_result_clear(&else_result);

    status = vigil_parser_parse_logical_or(state, &condition_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (!vigil_parser_check(state, VIGIL_TOKEN_QUESTION)) {
        vigil_expression_result_copy(out_result, &condition_result);
        return VIGIL_STATUS_OK;
    }

    question_token = vigil_parser_advance(state);
    status = vigil_parser_require_scalar_expression(
        state,
        question_token->span,
        &condition_result,
        "ternary condition must be a single bool value"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_bool_type(
        state,
        question_token->span,
        condition_result.type,
        "ternary condition must be bool"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_jump(
        state,
        VIGIL_OPCODE_JUMP_IF_FALSE,
        question_token->span,
        &false_jump_offset
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, question_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_parse_expression(state, &then_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_scalar_expression(
        state,
        question_token->span,
        &then_result,
        "ternary branches must be single values"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_jump(
        state,
        VIGIL_OPCODE_JUMP,
        question_token->span,
        &end_jump_offset
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_patch_jump(state, false_jump_offset);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, question_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_COLON,
        "expected ':' in ternary expression",
        &colon_token
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_parse_ternary(state, &else_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_scalar_expression(
        state,
        colon_token->span,
        &else_result,
        "ternary branches must be single values"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_same_type(
        state,
        colon_token->span,
        then_result.type,
        else_result.type,
        "ternary branches must have the same type"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_patch_jump(state, end_jump_offset);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    vigil_expression_result_copy(out_result, &then_result);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_parser_parse_expression(
    vigil_parser_state_t *state,
    vigil_expression_result_t *out_result
) {
    return vigil_parser_parse_ternary(state, out_result);
}

static vigil_status_t vigil_parser_parse_expression_with_expected_type(
    vigil_parser_state_t *state,
    vigil_parser_type_t expected_type,
    vigil_expression_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *token;
    const vigil_token_t *next_token;

    token = vigil_parser_peek(state);
    next_token = vigil_program_token_at(state->program, state->current + 1U);
    if (
        token != NULL &&
        token->kind == VIGIL_TOKEN_LBRACKET &&
        next_token != NULL &&
        next_token->kind == VIGIL_TOKEN_RBRACKET &&
        vigil_parser_type_is_array(expected_type)
    ) {
        if (expected_type.object_index > UINT32_MAX) {
            vigil_error_set_literal(
                state->program->error,
                VIGIL_STATUS_OUT_OF_MEMORY,
                "array literal operand overflow"
            );
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        vigil_parser_advance(state);
        vigil_parser_advance(state);
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_NEW_ARRAY, token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)expected_type.object_index, token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(state, 0U, token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(out_result, expected_type);
        return VIGIL_STATUS_OK;
    }
    if (
        token != NULL &&
        token->kind == VIGIL_TOKEN_LBRACE &&
        next_token != NULL &&
        next_token->kind == VIGIL_TOKEN_RBRACE &&
        vigil_parser_type_is_map(expected_type)
    ) {
        if (expected_type.object_index > UINT32_MAX) {
            vigil_error_set_literal(
                state->program->error,
                VIGIL_STATUS_OUT_OF_MEMORY,
                "map literal operand overflow"
            );
            return VIGIL_STATUS_OUT_OF_MEMORY;
        }
        vigil_parser_advance(state);
        vigil_parser_advance(state);
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_NEW_MAP, token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)expected_type.object_index, token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(state, 0U, token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        vigil_expression_result_set_type(out_result, expected_type);
        return VIGIL_STATUS_OK;
    }

    return vigil_parser_parse_expression(state, out_result);
}

static vigil_status_t vigil_parser_parse_block_contents(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
);

static vigil_status_t vigil_parser_parse_block_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    vigil_statement_result_t block_result;

    vigil_statement_result_clear(&block_result);

    status = vigil_parser_expect(state, VIGIL_TOKEN_LBRACE, "expected '{'", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    vigil_parser_begin_scope(state);
    status = vigil_parser_parse_block_contents(state, &block_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_expect(state, VIGIL_TOKEN_RBRACE, "expected '}' after block", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_end_scope(state);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    vigil_statement_result_set_guaranteed_return(
        out_result,
        block_result.guaranteed_return
    );
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_return_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *return_token;
    const vigil_token_t *next_token;
    vigil_expression_result_t return_result;
    size_t return_index;

    vigil_expression_result_clear(&return_result);

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_RETURN,
        "expected return statement",
        &return_token
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    next_token = vigil_parser_peek(state);
    if (
        state->expected_return_count == 1U &&
        vigil_parser_type_is_void(state->expected_return_type)
    ) {
        if (next_token != NULL && next_token->kind != VIGIL_TOKEN_SEMICOLON) {
            return vigil_parser_report(
                state,
                return_token->span,
                "void functions cannot return a value"
            );
        }
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_SEMICOLON,
            "expected ';' after return",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_RETURN, return_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(state, 0U, return_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        vigil_statement_result_set_guaranteed_return(out_result, 1);
        return VIGIL_STATUS_OK;
    }
    if (next_token != NULL && next_token->kind == VIGIL_TOKEN_SEMICOLON) {
        return vigil_parser_report(
            state,
            return_token->span,
            state->function_index == state->program->functions.main_index
                ? "main entrypoint must return an i32 expression"
                : "return statement requires a value"
        );
    }

    if (state->expected_return_count > 1U) {
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_LPAREN,
            "expected '(' after return for multi-value function",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        for (return_index = 0U; return_index < state->expected_return_count; return_index += 1U) {
            status = vigil_parser_parse_expression_with_expected_type(
                state,
                state->expected_return_types[return_index],
                &return_result
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state,
                return_token->span,
                &return_result,
                "return values must be single expressions"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_type(
                state,
                return_token->span,
                return_result.type,
                state->expected_return_types[return_index],
                "return expression type does not match function return type"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            if (return_index + 1U < state->expected_return_count) {
                status = vigil_parser_expect(
                    state,
                    VIGIL_TOKEN_COMMA,
                    "expected ',' between return values",
                    NULL
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
            }
        }

        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_RPAREN,
            "expected ')' after return values",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    } else {
        status = vigil_parser_parse_expression_with_expected_type(
            state,
            state->expected_return_type,
            &return_result
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            return_token->span,
            &return_result,
            "return statement requires the function's declared number of values"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_type(
            state,
            return_token->span,
            return_result.type,
            state->expected_return_type,
            state->function_index == state->program->functions.main_index
                ? "main entrypoint must return an i32 expression"
                : "return expression type does not match function return type"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_SEMICOLON,
        "expected ';' after return value",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_RETURN, return_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)state->expected_return_count, return_token->span);
    if (status != VIGIL_STATUS_OK) {
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
            c[len - 14U] == VIGIL_OPCODE_CALL &&
            c[len - 5U] == VIGIL_OPCODE_RETURN) {
            /* Verify RETURN operand is 1. */
            uint32_t ret_count = (uint32_t)c[len - 4U]
                | ((uint32_t)c[len - 3U] << 8U)
                | ((uint32_t)c[len - 2U] << 16U)
                | ((uint32_t)c[len - 1U] << 24U);
            if (ret_count == 1U) {
                c[len - 14U] = VIGIL_OPCODE_TAIL_CALL;
                state->chunk.code.length = len - 5U;
                if (state->chunk.span_count > len - 5U) {
                    state->chunk.span_count = len - 5U;
                }
            }
        }
    }

    vigil_statement_result_set_guaranteed_return(out_result, 1);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_defer_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *defer_token;
    vigil_expression_result_t expression_result;

    vigil_expression_result_clear(&expression_result);
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_DEFER,
        "expected 'defer'",
        &defer_token
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    state->defer_mode = 1;
    state->defer_emitted = 0;
    status = vigil_parser_parse_primary(state, &expression_result);
    state->defer_mode = 0;
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    if (!state->defer_emitted) {
        return vigil_parser_report(
            state,
            defer_token->span,
            "defer requires a call expression"
        );
    }
    state->defer_emitted = 0;

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_SEMICOLON,
        "expected ';' after defer call",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_guard_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *guard_token;
    vigil_binding_target_list_t targets;
    vigil_expression_result_t initializer_result;
    size_t error_slot;
    size_t body_jump_offset;
    size_t end_jump_offset;

    guard_token = NULL;
    error_slot = 0U;
    body_jump_offset = 0U;
    end_jump_offset = 0U;
    vigil_binding_target_list_init(&targets);
    vigil_expression_result_clear(&initializer_result);

    status = vigil_parser_expect(state, VIGIL_TOKEN_GUARD, "expected 'guard'", &guard_token);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_parse_binding_target_list(
        state,
        "unsupported guard binding type",
        "guard bindings cannot use type void",
        "expected guard binding name",
        &targets
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    if (
        targets.count == 0U ||
        !vigil_binding_type_equal(
            targets.items[targets.count - 1U].type,
            vigil_binding_type_primitive(VIGIL_TYPE_ERR)
        )
    ) {
        status = vigil_parser_report(
            state,
            guard_token->span,
            "guard must end with an err binding"
        );
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }
    if (targets.items[targets.count - 1U].is_discard) {
        status = vigil_parser_report(
            state,
            targets.items[targets.count - 1U].name_token->span,
            "guard error binding must be named"
        );
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_ASSIGN,
        "expected '=' after guard bindings",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    status = vigil_parser_parse_expression_with_expected_type(
        state,
        targets.count == 1U ? targets.items[0].type : vigil_binding_type_invalid(),
        &initializer_result
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }
    status = vigil_parser_require_binding_initializer_shape(
        state,
        guard_token->span,
        &targets,
        &initializer_result,
        "guard binding count does not match expression result count",
        "guard binding type does not match expression result type"
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    status = vigil_parser_bind_targets(state, &targets, 0, &error_slot);
    vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_LOCAL, guard_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)error_slot, guard_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_ok_constant(state, guard_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_EQUAL, guard_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_jump(
        state,
        VIGIL_OPCODE_JUMP_IF_FALSE,
        guard_token->span,
        &body_jump_offset
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, guard_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_jump(
        state,
        VIGIL_OPCODE_JUMP,
        guard_token->span,
        &end_jump_offset
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_patch_jump(state, body_jump_offset);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, guard_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_parse_block_statement(state, out_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_patch_jump(state, end_jump_offset);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_if_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *if_token;
    vigil_expression_result_t condition_result;
    size_t false_jump_offset;
    size_t end_jump_offset;
    vigil_statement_result_t then_result;
    vigil_statement_result_t else_result;
    int has_else_branch;

    vigil_expression_result_clear(&condition_result);
    vigil_statement_result_clear(&then_result);
    vigil_statement_result_clear(&else_result);
    has_else_branch = 0;

    status = vigil_parser_expect(state, VIGIL_TOKEN_IF, "expected 'if'", &if_token);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_expect(state, VIGIL_TOKEN_LPAREN, "expected '(' after 'if'", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_parse_expression(state, &condition_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_scalar_expression(
        state,
        if_token->span,
        &condition_result,
        "if condition must be a single bool value"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_bool_type(
        state,
        if_token->span,
        condition_result.type,
        "if condition must be bool"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after if condition", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_jump(
        state,
        VIGIL_OPCODE_JUMP_IF_FALSE,
        if_token->span,
        &false_jump_offset
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, if_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_parse_statement(state, &then_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (vigil_parser_match(state, VIGIL_TOKEN_ELSE)) {
        has_else_branch = 1;
    }

    status = vigil_parser_emit_jump(
        state,
        VIGIL_OPCODE_JUMP,
        if_token->span,
        &end_jump_offset
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_patch_jump(state, false_jump_offset);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, if_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (has_else_branch) {
        status = vigil_parser_parse_statement(state, &else_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    status = vigil_parser_patch_jump(state, end_jump_offset);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    vigil_statement_result_set_guaranteed_return(
        out_result,
        has_else_branch &&
            then_result.guaranteed_return &&
            else_result.guaranteed_return
    );
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_while_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *while_token;
    vigil_expression_result_t condition_result;
    size_t loop_start;
    size_t exit_jump_offset;
    vigil_loop_context_t *loop;
    size_t i;

    while_token = NULL;
    vigil_expression_result_clear(&condition_result);

    status = vigil_parser_expect(state, VIGIL_TOKEN_WHILE, "expected 'while'", &while_token);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    loop_start = vigil_chunk_code_size(&state->chunk);
    status = vigil_parser_expect(state, VIGIL_TOKEN_LPAREN, "expected '(' after 'while'", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_parse_expression(state, &condition_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_scalar_expression(
        state,
        while_token->span,
        &condition_result,
        "while condition must be a single bool value"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_bool_type(
        state,
        while_token->span,
        condition_result.type,
        "while condition must be bool"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after while condition", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_jump(
        state,
        VIGIL_OPCODE_JUMP_IF_FALSE,
        while_token->span,
        &exit_jump_offset
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, while_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_push_loop(state, loop_start);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_parse_statement(state, NULL);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup_loop;
    }

    status = vigil_parser_emit_loop(state, loop_start, while_token->span);
    if (status != VIGIL_STATUS_OK) {
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
            c[len - 11U] == VIGIL_OPCODE_INCREMENT_LOCAL_I32 &&
            c[len - 5U] == VIGIL_OPCODE_LOOP) {
            /* Condition at loop_start: GET_LOCAL(5) + CONSTANT(5) + cmp(1)
               + JUMP_IF_FALSE(5) + POP(1) = 17 bytes */
            size_t cs = loop_start;
            if (cs + 17U <= len &&
                c[cs] == VIGIL_OPCODE_GET_LOCAL &&
                c[cs + 5U] == VIGIL_OPCODE_CONSTANT) {
                uint8_t cmp_op = c[cs + 10U];
                uint8_t cmp_type = 255;
                switch ((vigil_opcode_t)cmp_op) {
                    case VIGIL_OPCODE_LESS_I32:          cmp_type = 0; break;
                    case VIGIL_OPCODE_LESS_EQUAL_I32:    cmp_type = 1; break;
                    case VIGIL_OPCODE_GREATER_I32:       cmp_type = 2; break;
                    case VIGIL_OPCODE_GREATER_EQUAL_I32: cmp_type = 3; break;
                    case VIGIL_OPCODE_NOT_EQUAL_I32:     cmp_type = 4; break;
                    default: break;
                }
                if (cmp_type != 255 &&
                    c[cs + 11U] == VIGIL_OPCODE_JUMP_IF_FALSE &&
                    c[cs + 16U] == VIGIL_OPCODE_POP) {
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
                        c[forloop_pos] = VIGIL_OPCODE_FORLOOP_I32;
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
    status = vigil_parser_patch_jump(state, exit_jump_offset);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup_loop;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, while_token->span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup_loop;
    }

    loop = vigil_parser_current_loop(state);
    if (loop != NULL) {
        for (i = 0U; i < loop->break_count; ++i) {
            status = vigil_parser_patch_jump(
                state,
                loop->break_jumps[i].operand_offset
            );
            if (status != VIGIL_STATUS_OK) {
                goto cleanup_loop;
            }
        }
    }

cleanup_loop:
    vigil_parser_pop_loop(state);
    if (status == VIGIL_STATUS_OK) {
        vigil_statement_result_set_guaranteed_return(out_result, 0);
    }
    return status;
}

static vigil_status_t vigil_parser_parse_c_for_statement(
    vigil_parser_state_t *state,
    const vigil_token_t *for_token,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    vigil_expression_result_t condition_result;
    size_t condition_start;
    size_t loop_start;
    size_t exit_jump_offset;
    size_t body_jump_offset;
    size_t increment_start;
    vigil_loop_context_t *loop;
    size_t i;
    int has_condition;
    int has_increment;
    int loop_pushed;

    vigil_expression_result_clear(&condition_result);
    body_jump_offset = 0U;
    increment_start = 0U;
    has_condition = 0;
    has_increment = 0;
    loop_pushed = 0;

    status = vigil_parser_expect(state, VIGIL_TOKEN_LPAREN, "expected '(' after 'for'", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    vigil_parser_begin_scope(state);
    if (vigil_parser_match(state, VIGIL_TOKEN_SEMICOLON)) {
    } else if (vigil_parser_is_variable_declaration_start(state)) {
        status = vigil_parser_parse_variable_declaration(state, NULL);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    } else if (vigil_parser_is_assignment_start(state)) {
        status = vigil_parser_parse_assignment_statement_internal(state, NULL, 1);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    } else {
        status = vigil_parser_parse_expression_statement_internal(state, NULL, 1);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    condition_start = vigil_chunk_code_size(&state->chunk);
    if (!vigil_parser_check(state, VIGIL_TOKEN_SEMICOLON)) {
        has_condition = 1;
        status = vigil_parser_parse_expression(state, &condition_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            for_token->span,
            &condition_result,
            "for condition must be a single bool value"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_bool_type(
            state,
            for_token->span,
            condition_result.type,
            "for condition must be bool"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_SEMICOLON, "expected ';' after for condition", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    exit_jump_offset = 0U;
    if (has_condition) {
        status = vigil_parser_emit_jump(
            state,
            VIGIL_OPCODE_JUMP_IF_FALSE,
            for_token->span,
            &exit_jump_offset
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    loop_start = condition_start;
    if (!vigil_parser_check(state, VIGIL_TOKEN_RPAREN)) {
        has_increment = 1;
        status = vigil_parser_emit_jump(
            state,
            VIGIL_OPCODE_JUMP,
            for_token->span,
            &body_jump_offset
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        increment_start = vigil_chunk_code_size(&state->chunk);
        if (vigil_parser_is_assignment_start(state)) {
            status = vigil_parser_parse_assignment_statement_internal(state, NULL, 0);
        } else {
            status = vigil_parser_parse_expression_statement_internal(state, NULL, 0);
        }
        if (status != VIGIL_STATUS_OK) {
            return status;
        }

        status = vigil_parser_emit_loop(state, condition_start, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        loop_start = increment_start;
    }

    status = vigil_parser_expect(state, VIGIL_TOKEN_RPAREN, "expected ')' after for clauses", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    if (has_increment) {
        status = vigil_parser_patch_jump(state, body_jump_offset);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    status = vigil_parser_push_loop(state, loop_start);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    loop_pushed = 1;

    status = vigil_parser_parse_statement(state, NULL);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup_loop;
    }

    status = vigil_parser_emit_loop(state, loop_start, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup_loop;
    }
    if (has_condition) {
        status = vigil_parser_patch_jump(state, exit_jump_offset);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup_loop;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup_loop;
        }
    }

    loop = vigil_parser_current_loop(state);
    if (loop != NULL) {
        for (i = 0U; i < loop->break_count; ++i) {
            status = vigil_parser_patch_jump(
                state,
                loop->break_jumps[i].operand_offset
            );
            if (status != VIGIL_STATUS_OK) {
                goto cleanup_loop;
            }
        }
    }

cleanup_loop:
    if (loop_pushed) {
        vigil_parser_pop_loop(state);
    }
    if (status == VIGIL_STATUS_OK) {
        status = vigil_parser_end_scope(state);
        if (status == VIGIL_STATUS_OK) {
            vigil_statement_result_set_guaranteed_return(out_result, 0);
        }
    }
    return status;
}

static vigil_status_t vigil_parser_bind_inferred_target(
    vigil_parser_state_t *state,
    const vigil_token_t *name_token,
    vigil_parser_type_t type
) {
    if (vigil_parser_token_is_discard_identifier(state, name_token)) {
        return vigil_binding_scope_stack_declare_hidden_local(
            &state->locals,
            type,
            0,
            NULL,
            state->program->error
        );
    }

    return vigil_parser_declare_local_symbol(state, name_token, type, 0, NULL);
}

static vigil_status_t vigil_parser_parse_for_in_statement(
    vigil_parser_state_t *state,
    const vigil_token_t *for_token,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *first_name;
    const vigil_token_t *second_name;
    vigil_expression_result_t iterable_result;
    vigil_parser_type_t iterable_type;
    vigil_parser_type_t element_type;
    vigil_parser_type_t key_type;
    vigil_parser_type_t value_type;
    size_t collection_slot;
    size_t index_slot;
    size_t condition_start;
    size_t exit_jump_offset;
    size_t body_jump_offset;
    size_t increment_start;
    vigil_loop_context_t *loop;
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
    vigil_expression_result_clear(&iterable_result);
    iterable_type = vigil_binding_type_invalid();
    element_type = vigil_binding_type_invalid();
    key_type = vigil_binding_type_invalid();
    value_type = vigil_binding_type_invalid();

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_IDENTIFIER,
        "expected loop binding name after 'for'",
        &first_name
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    if (vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_IDENTIFIER,
            "expected second loop binding name after ','",
            &second_name
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }
    status = vigil_parser_expect(state, VIGIL_TOKEN_IN, "expected 'in' after loop bindings", NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_parse_expression(state, &iterable_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_scalar_expression(
        state,
        for_token->span,
        &iterable_result,
        "for-in iterable must be a single array or map value"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    iterable_type = iterable_result.type;
    if (vigil_parser_type_is_array(iterable_type)) {
        if (second_name != NULL) {
            return vigil_parser_report(
                state,
                second_name->span,
                "for-in over arrays requires a single loop binding"
            );
        }
        element_type = vigil_program_array_type_element(state->program, iterable_type);
    } else if (vigil_parser_type_is_map(iterable_type)) {
        if (second_name == NULL) {
            return vigil_parser_report(
                state,
                first_name->span,
                "for-in over maps requires key and value bindings"
            );
        }
        key_type = vigil_program_map_type_key(state->program, iterable_type);
        value_type = vigil_program_map_type_value(state->program, iterable_type);
    } else {
        return vigil_parser_report(
            state,
            for_token->span,
            "for-in requires an array or map iterable"
        );
    }

    vigil_parser_begin_scope(state);

    status = vigil_binding_scope_stack_declare_hidden_local(
        &state->locals,
        iterable_type,
        0,
        &collection_slot,
        state->program->error
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_i32_constant(state, 0, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_binding_scope_stack_declare_hidden_local(
        &state->locals,
        vigil_binding_type_primitive(VIGIL_TYPE_I32),
        0,
        &index_slot,
        state->program->error
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    condition_start = vigil_chunk_code_size(&state->chunk);
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_LOCAL, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_LOCAL, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)collection_slot, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_COLLECTION_SIZE, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_LESS, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_jump(
        state,
        VIGIL_OPCODE_JUMP_IF_FALSE,
        for_token->span,
        &exit_jump_offset
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_jump(state, VIGIL_OPCODE_JUMP, for_token->span, &body_jump_offset);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    increment_start = vigil_chunk_code_size(&state->chunk);
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_LOCAL, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_i32_constant(state, 1, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_ADD, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_SET_LOCAL, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_emit_loop(state, condition_start, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_patch_jump(state, body_jump_offset);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_push_loop(state, increment_start);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    loop_pushed = 1;

    vigil_parser_begin_scope(state);
    iteration_scope_begun = 1;
    if (vigil_parser_type_is_array(iterable_type)) {
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_LOCAL, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)collection_slot, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_LOCAL, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_INDEX, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_bind_inferred_target(state, first_name, element_type);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
    } else {
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_LOCAL, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)collection_slot, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_LOCAL, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_MAP_KEY_AT, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_bind_inferred_target(state, first_name, key_type);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }

        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_LOCAL, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)collection_slot, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_LOCAL, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)index_slot, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_MAP_VALUE_AT, for_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_bind_inferred_target(state, second_name, value_type);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
    }

    status = vigil_parser_parse_statement(state, NULL);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }

    status = vigil_parser_end_scope(state);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    iteration_scope_begun = 0;

    status = vigil_parser_emit_loop(state, increment_start, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_patch_jump(state, exit_jump_offset);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, for_token->span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }

    loop = vigil_parser_current_loop(state);
    if (loop != NULL) {
        for (i = 0U; i < loop->break_count; ++i) {
            status = vigil_parser_patch_jump(state, loop->break_jumps[i].operand_offset);
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
        }
    }

cleanup:
    if (iteration_scope_begun) {
        (void)vigil_parser_end_scope(state);
    }
    if (loop_pushed) {
        vigil_parser_pop_loop(state);
    }
    if (status == VIGIL_STATUS_OK) {
        status = vigil_parser_end_scope(state);
        if (status == VIGIL_STATUS_OK) {
            vigil_statement_result_set_guaranteed_return(out_result, 0);
        }
    }
    return status;
}

static vigil_status_t vigil_parser_parse_for_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *for_token;

    for_token = NULL;
    status = vigil_parser_expect(state, VIGIL_TOKEN_FOR, "expected 'for'", &for_token);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (vigil_parser_check(state, VIGIL_TOKEN_LPAREN)) {
        return vigil_parser_parse_c_for_statement(state, for_token, out_result);
    }

    return vigil_parser_parse_for_in_statement(state, for_token, out_result);
}

static vigil_status_t vigil_parser_parse_switch_case_contents(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    vigil_statement_result_t declaration_result;
    vigil_statement_result_t block_result;
    const vigil_token_t *token;

    vigil_statement_result_clear(&declaration_result);
    vigil_statement_result_clear(&block_result);

    while (!vigil_parser_is_at_end(state)) {
        token = vigil_parser_peek(state);
        if (
            token == NULL ||
            token->kind == VIGIL_TOKEN_RBRACE ||
            token->kind == VIGIL_TOKEN_CASE ||
            token->kind == VIGIL_TOKEN_DEFAULT
        ) {
            break;
        }

        status = vigil_parser_parse_declaration(state, &declaration_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (declaration_result.guaranteed_return) {
            block_result.guaranteed_return = 1;
        }
    }

    vigil_statement_result_set_guaranteed_return(
        out_result,
        block_result.guaranteed_return
    );
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_switch_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *switch_token;
    const vigil_token_t *token;
    vigil_expression_result_t switch_result;
    vigil_expression_result_t case_result;
    vigil_statement_result_t case_body_result;
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

    vigil_expression_result_clear(&switch_result);
    vigil_expression_result_clear(&case_result);
    vigil_statement_result_clear(&case_body_result);
    end_jumps = NULL;
    end_jump_count = 0U;
    end_jump_capacity = 0U;
    body_jumps = NULL;
    body_jump_count = 0U;
    body_jump_capacity = 0U;
    has_default = 0;
    all_branches_return = 1;

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_SWITCH,
        "expected 'switch'",
        &switch_token
    );
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LPAREN,
        "expected '(' after 'switch'",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_parse_expression(state, &switch_result);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    if (
        !vigil_parser_type_is_integer(switch_result.type) &&
        !vigil_parser_type_equal(
            switch_result.type,
            vigil_binding_type_primitive(VIGIL_TYPE_BOOL)
        ) &&
        !vigil_parser_type_is_enum(switch_result.type)
    ) {
        status = vigil_parser_report(
            state,
            switch_token->span,
            "switch expression must be an integer, bool, or enum"
        );
        goto cleanup;
    }
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_RPAREN,
        "expected ')' after switch expression",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_LBRACE,
        "expected '{' after switch expression",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }

    while (!vigil_parser_is_at_end(state)) {
        token = vigil_parser_peek(state);
        if (token == NULL) {
            status = vigil_parser_report(
                state,
                vigil_parser_fallback_span(state),
                "expected '}' after switch body"
            );
            goto cleanup;
        }
        if (token->kind == VIGIL_TOKEN_RBRACE) {
            vigil_parser_advance(state);
            break;
        }

        if (token->kind == VIGIL_TOKEN_DEFAULT) {
            if (has_default) {
                status = vigil_parser_report(
                    state,
                    token->span,
                    "switch already has a default case"
                );
                goto cleanup;
            }
            has_default = 1;
            vigil_parser_advance(state);
            status = vigil_parser_expect(
                state,
                VIGIL_TOKEN_COLON,
                "expected ':' after default",
                NULL
            );
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, token->span);
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            vigil_statement_result_clear(&case_body_result);
            status = vigil_parser_parse_switch_case_contents(state, &case_body_result);
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            all_branches_return =
                all_branches_return && case_body_result.guaranteed_return;
            status = vigil_parser_grow_jump_offsets(
                state,
                &end_jumps,
                &end_jump_capacity,
                end_jump_count + 1U,
                "switch jump table allocation overflow"
            );
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            status = vigil_parser_emit_jump(
                state,
                VIGIL_OPCODE_JUMP,
                token->span,
                &end_jumps[end_jump_count]
            );
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            end_jump_count += 1U;
            continue;
        }

        if (token->kind != VIGIL_TOKEN_CASE) {
            status = vigil_parser_report(
                state,
                token->span,
                "expected 'case', 'default', or '}' in switch body"
            );
            goto cleanup;
        }
        vigil_parser_advance(state);

        body_jump_count = 0U;
        while (1) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_DUP, token->span);
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            vigil_expression_result_clear(&case_result);
            status = vigil_parser_parse_expression(state, &case_result);
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            status = vigil_parser_require_same_type(
                state,
                token->span,
                case_result.type,
                switch_result.type,
                "switch case value type does not match switch expression"
            );
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_EQUAL, token->span);
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            status = vigil_parser_emit_jump(
                state,
                VIGIL_OPCODE_JUMP_IF_FALSE,
                token->span,
                &false_jump_offset
            );
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, token->span);
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            status = vigil_parser_grow_jump_offsets(
                state,
                &body_jumps,
                &body_jump_capacity,
                body_jump_count + 1U,
                "switch jump table allocation overflow"
            );
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            status = vigil_parser_emit_jump(
                state,
                VIGIL_OPCODE_JUMP,
                token->span,
                &body_jumps[body_jump_count]
            );
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            body_jump_count += 1U;
            status = vigil_parser_patch_jump(state, false_jump_offset);
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, token->span);
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }

            if (!vigil_parser_match(state, VIGIL_TOKEN_COMMA)) {
                break;
            }
        }

        status = vigil_parser_expect(
            state,
            VIGIL_TOKEN_COLON,
            "expected ':' after case value",
            NULL
        );
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_jump(
            state,
            VIGIL_OPCODE_JUMP,
            token->span,
            &jump_offset
        );
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        for (false_jump_offset = 0U; false_jump_offset < body_jump_count; false_jump_offset += 1U) {
            status = vigil_parser_patch_jump(state, body_jumps[false_jump_offset]);
            if (status != VIGIL_STATUS_OK) {
                goto cleanup;
            }
        }

        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        vigil_statement_result_clear(&case_body_result);
        status = vigil_parser_parse_switch_case_contents(state, &case_body_result);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        all_branches_return = all_branches_return && case_body_result.guaranteed_return;
        status = vigil_parser_grow_jump_offsets(
            state,
            &end_jumps,
            &end_jump_capacity,
            end_jump_count + 1U,
            "switch jump table allocation overflow"
        );
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_jump(
            state,
            VIGIL_OPCODE_JUMP,
            token->span,
            &end_jumps[end_jump_count]
        );
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        end_jump_count += 1U;
        status = vigil_parser_patch_jump(state, jump_offset);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
    }

    if (!has_default) {
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, switch_token->span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
    }
    for (jump_offset = 0U; jump_offset < end_jump_count; jump_offset += 1U) {
        status = vigil_parser_patch_jump(state, end_jumps[jump_offset]);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
    }

    vigil_statement_result_set_guaranteed_return(
        out_result,
        has_default && all_branches_return
    );
    status = VIGIL_STATUS_OK;

cleanup:
    if (end_jumps != NULL) {
        void *memory = end_jumps;
        vigil_runtime_free(state->program->registry->runtime, &memory);
    }
    if (body_jumps != NULL) {
        void *memory = body_jumps;
        vigil_runtime_free(state->program->registry->runtime, &memory);
    }
    return status;
}

static vigil_status_t vigil_parser_parse_break_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *break_token;
    vigil_loop_context_t *loop;
    size_t operand_offset;

    status = vigil_parser_expect(state, VIGIL_TOKEN_BREAK, "expected 'break'", &break_token);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    loop = vigil_parser_current_loop(state);
    if (loop == NULL) {
        return vigil_parser_report(state, break_token->span, "'break' is only valid inside a loop");
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_SEMICOLON,
        "expected ';' after break",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_scope_cleanup_to_depth(
        state,
        loop->scope_depth,
        break_token->span
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_loop_context_grow_breaks(state, loop, loop->break_count + 1U);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_jump(
        state,
        VIGIL_OPCODE_JUMP,
        break_token->span,
        &operand_offset
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    loop = vigil_parser_current_loop(state);
    loop->break_jumps[loop->break_count].operand_offset = operand_offset;
    loop->break_jumps[loop->break_count].span = break_token->span;
    loop->break_count += 1U;
    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_continue_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *continue_token;
    vigil_loop_context_t *loop;

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_CONTINUE,
        "expected 'continue'",
        &continue_token
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    loop = vigil_parser_current_loop(state);
    if (loop == NULL) {
        return vigil_parser_report(
            state,
            continue_token->span,
            "'continue' is only valid inside a loop"
        );
    }

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_SEMICOLON,
        "expected ';' after continue",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_scope_cleanup_to_depth(
        state,
        loop->scope_depth,
        continue_token->span
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_emit_loop(state, loop->loop_start, continue_token->span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

static int vigil_parser_is_assignment_operator(
    vigil_token_kind_t kind
) {
    return kind == VIGIL_TOKEN_ASSIGN ||
           kind == VIGIL_TOKEN_PLUS_ASSIGN ||
           kind == VIGIL_TOKEN_MINUS_ASSIGN ||
           kind == VIGIL_TOKEN_STAR_ASSIGN ||
           kind == VIGIL_TOKEN_SLASH_ASSIGN ||
           kind == VIGIL_TOKEN_PERCENT_ASSIGN ||
           kind == VIGIL_TOKEN_PLUS_PLUS ||
           kind == VIGIL_TOKEN_MINUS_MINUS;
}

static vigil_status_t vigil_parser_emit_i32_constant(
    vigil_parser_state_t *state,
    int64_t value,
    vigil_source_span_t span
) {
    vigil_status_t status;
    vigil_value_t constant;

    vigil_value_init_int(&constant, value);
    status = vigil_chunk_write_constant(
        &state->chunk,
        &constant,
        span,
        NULL,
        state->program->error
    );
    vigil_value_release(&constant);
    return status;
}

vigil_status_t vigil_parser_emit_f64_constant(
    vigil_parser_state_t *state,
    double value,
    vigil_source_span_t span
) {
    vigil_status_t status;
    vigil_value_t constant;

    vigil_value_init_float(&constant, value);
    status = vigil_chunk_write_constant(
        &state->chunk,
        &constant,
        span,
        NULL,
        state->program->error
    );
    vigil_value_release(&constant);
    return status;
}

static vigil_status_t vigil_parser_emit_integer_cast(
    vigil_parser_state_t *state,
    vigil_parser_type_t target_type,
    vigil_source_span_t span
) {
    vigil_opcode_t opcode;

    if (!vigil_parser_type_is_integer(target_type)) {
        return VIGIL_STATUS_OK;
    }

    if (vigil_parser_type_is_i32(target_type)) {
        opcode = VIGIL_OPCODE_TO_I32;
    } else if (vigil_parser_type_is_i64(target_type)) {
        opcode = VIGIL_OPCODE_TO_I64;
    } else if (vigil_parser_type_is_u8(target_type)) {
        opcode = VIGIL_OPCODE_TO_U8;
    } else if (vigil_parser_type_is_u32(target_type)) {
        opcode = VIGIL_OPCODE_TO_U32;
    } else {
        opcode = VIGIL_OPCODE_TO_U64;
    }

    return vigil_parser_emit_opcode(state, opcode, span);
}

vigil_status_t vigil_parser_emit_integer_constant(
    vigil_parser_state_t *state,
    vigil_parser_type_t target_type,
    int64_t value,
    vigil_source_span_t span
) {
    vigil_status_t status;

    status = vigil_parser_emit_i32_constant(state, value, span);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    return vigil_parser_emit_integer_cast(state, target_type, span);
}


static int vigil_parser_skip_bracketed_suffix(
    const vigil_parser_state_t *state,
    size_t *cursor
) {
    const vigil_token_t *token;
    size_t paren_depth;
    size_t bracket_depth;
    size_t brace_depth;

    if (cursor == NULL) {
        return 0;
    }

    token = vigil_program_token_at(state->program, *cursor);
    if (token == NULL || token->kind != VIGIL_TOKEN_LBRACKET) {
        return 0;
    }

    paren_depth = 0U;
    bracket_depth = 1U;
    brace_depth = 0U;
    *cursor += 1U;
    while ((token = vigil_program_token_at(state->program, *cursor)) != NULL) {
        switch (token->kind) {
            case VIGIL_TOKEN_LPAREN:
                paren_depth += 1U;
                break;
            case VIGIL_TOKEN_RPAREN:
                if (paren_depth == 0U) {
                    return 0;
                }
                paren_depth -= 1U;
                break;
            case VIGIL_TOKEN_LBRACE:
                brace_depth += 1U;
                break;
            case VIGIL_TOKEN_RBRACE:
                if (brace_depth == 0U) {
                    return 0;
                }
                brace_depth -= 1U;
                break;
            case VIGIL_TOKEN_LBRACKET:
                bracket_depth += 1U;
                break;
            case VIGIL_TOKEN_RBRACKET:
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

static int vigil_parser_skip_type_reference_tokens(
    const vigil_program_state_t *program,
    size_t *cursor
) {
    const vigil_token_t *token;
    const vigil_token_t *next_token;
    const char *name_text;
    size_t name_length;

    if (program == NULL || cursor == NULL) {
        return 0;
    }

    token = vigil_program_token_at(program, *cursor);
    if (token == NULL) {
        return 0;
    }

    if (token->kind == VIGIL_TOKEN_FN) {
        *cursor += 1U;
        token = vigil_program_token_at(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_LPAREN) {
            return 1;
        }

        *cursor += 1U;
        token = vigil_program_token_at(program, *cursor);
        if (token != NULL && token->kind != VIGIL_TOKEN_RPAREN) {
            while (1) {
                if (!vigil_parser_skip_type_reference_tokens(program, cursor)) {
                    return 0;
                }
                token = vigil_program_token_at(program, *cursor);
                if (token != NULL && token->kind == VIGIL_TOKEN_COMMA) {
                    *cursor += 1U;
                    continue;
                }
                break;
            }
        }

        token = vigil_program_token_at(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_RPAREN) {
            return 0;
        }
        *cursor += 1U;

        token = vigil_program_token_at(program, *cursor);
        if (token != NULL && token->kind == VIGIL_TOKEN_ARROW) {
            *cursor += 1U;
            token = vigil_program_token_at(program, *cursor);
            if (token != NULL && token->kind == VIGIL_TOKEN_LPAREN) {
                *cursor += 1U;
                while (1) {
                    if (!vigil_parser_skip_type_reference_tokens(program, cursor)) {
                        return 0;
                    }
                    token = vigil_program_token_at(program, *cursor);
                    if (token != NULL && token->kind == VIGIL_TOKEN_COMMA) {
                        *cursor += 1U;
                        continue;
                    }
                    if (token != NULL && token->kind == VIGIL_TOKEN_RPAREN) {
                        *cursor += 1U;
                        break;
                    }
                    return 0;
                }
            } else if (!vigil_parser_skip_type_reference_tokens(program, cursor)) {
                return 0;
            }
        }
        return 1;
    }

    if (token->kind != VIGIL_TOKEN_IDENTIFIER) {
        return 0;
    }

    name_text = vigil_program_token_text(program, token, &name_length);
    next_token = vigil_program_token_at(program, *cursor + 1U);
    if (
        next_token != NULL &&
        next_token->kind == VIGIL_TOKEN_LESS &&
        vigil_program_names_equal(name_text, name_length, "array", 5U)
    ) {
        *cursor += 2U;
        if (!vigil_parser_skip_type_reference_tokens(program, cursor)) {
            return 0;
        }
        if (!vigil_program_consume_type_close(program, cursor)) {
            return 0;
        }
        return 1;
    }
    if (
        next_token != NULL &&
        next_token->kind == VIGIL_TOKEN_LESS &&
        vigil_program_names_equal(name_text, name_length, "map", 3U)
    ) {
        *cursor += 2U;
        if (!vigil_parser_skip_type_reference_tokens(program, cursor)) {
            return 0;
        }
        token = vigil_program_token_at(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_COMMA) {
            return 0;
        }
        *cursor += 1U;
        if (!vigil_parser_skip_type_reference_tokens(program, cursor)) {
            return 0;
        }
        if (!vigil_program_consume_type_close(program, cursor)) {
            return 0;
        }
        return 1;
    }

    *cursor += 1U;
    token = vigil_program_token_at(program, *cursor);
    if (token != NULL && token->kind == VIGIL_TOKEN_DOT) {
        *cursor += 1U;
        token = vigil_program_token_at(program, *cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_IDENTIFIER) {
            return 0;
        }
        *cursor += 1U;
    }

    return 1;
}

static int vigil_parser_is_assignment_start(
    const vigil_parser_state_t *state
) {
    size_t cursor;
    const vigil_token_t *token;

    token = vigil_parser_peek(state);
    if (token == NULL || token->kind != VIGIL_TOKEN_IDENTIFIER) {
        return 0;
    }

    cursor = state->current + 1U;
    token = vigil_program_token_at(state->program, cursor);
    while (token != NULL) {
        if (token->kind == VIGIL_TOKEN_DOT) {
            cursor += 1U;
            token = vigil_program_token_at(state->program, cursor);
            if (token == NULL || token->kind != VIGIL_TOKEN_IDENTIFIER) {
                return 0;
            }
            cursor += 1U;
        } else if (token->kind == VIGIL_TOKEN_LBRACKET) {
            if (!vigil_parser_skip_bracketed_suffix(state, &cursor)) {
                return 0;
            }
        } else {
            break;
        }
        token = vigil_program_token_at(state->program, cursor);
    }

    return token != NULL && vigil_parser_is_assignment_operator(token->kind);
}

static vigil_status_t vigil_parser_parse_assignment_statement_internal(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result,
    int expect_semicolon
) {
    vigil_status_t status;
    const vigil_token_t *name_token;
    const vigil_token_t *field_token;
    const vigil_token_t *operator_token;
    size_t local_index;
    size_t capture_index;
    size_t global_index;
    size_t field_index;
    vigil_source_id_t import_source_id;
    vigil_parser_type_t local_type;
    vigil_parser_type_t target_type;
    vigil_expression_result_t value_result;
    vigil_expression_result_t index_result;
    const vigil_class_field_t *field;
    const vigil_binding_local_t *local_decl;
    const vigil_global_variable_t *global_decl;
    const vigil_token_t *target_token;
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
    local_type = vigil_binding_type_invalid();
    target_type = vigil_binding_type_invalid();
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
    vigil_expression_result_clear(&value_result);
    vigil_expression_result_clear(&index_result);

    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_IDENTIFIER,
        "expected local variable name",
        &name_token
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    target_token = name_token;

    status = vigil_parser_resolve_local_symbol(
        state,
        name_token,
        &local_index,
        &local_type,
        &is_capture_local,
        &capture_index,
        &local_found
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    if (local_found) {
        if (!is_capture_local) {
            local_decl = vigil_binding_scope_stack_local_at(&state->locals, local_index);
            local_type = local_decl->type;
            is_const_local = local_decl->is_const;
        } else {
            is_const_local = 0;
        }
    } else {
        const char *name_text;
        size_t name_length;

        name_text = vigil_parser_token_text(state, name_token, &name_length);
        if (
            vigil_parser_check(state, VIGIL_TOKEN_DOT) &&
            vigil_program_resolve_import_alias(
                state->program,
                name_text,
                name_length,
                &import_source_id
            )
        ) {
            const vigil_token_t *member_token;
            const vigil_global_constant_t *constant_decl;
            const vigil_function_decl_t *function_decl;
            const vigil_class_decl_t *class_decl;
            const vigil_interface_decl_t *interface_decl;
            const vigil_enum_decl_t *enum_decl;
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

            vigil_parser_advance(state);
            status = vigil_parser_expect(
                state,
                VIGIL_TOKEN_IDENTIFIER,
                "expected module member name after '.'",
                &member_token
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            target_token = member_token;
            member_text = vigil_parser_token_text(state, member_token, &member_length);

            if (
                vigil_program_find_global_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
                    &global_index,
                    &global_decl
                )
            ) {
                if (!vigil_program_is_global_public(global_decl)) {
                    return vigil_parser_report(
                        state,
                        member_token->span,
                        "module member is not public"
                    );
                }
                is_global_assignment = 1;
                local_type = global_decl->type;
            } else if (
                vigil_program_find_constant_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
                    &constant_decl
                )
            ) {
                if (!vigil_program_is_constant_public(constant_decl)) {
                    return vigil_parser_report(
                        state,
                        member_token->span,
                        "module member is not public"
                    );
                }
                return vigil_parser_report(
                    state,
                    member_token->span,
                    "cannot assign to module constant"
                );
            } else if (
                vigil_program_find_top_level_function_name_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
                    &function_index,
                    &function_decl
                )
            ) {
                if (!vigil_program_is_function_public(function_decl)) {
                    return vigil_parser_report(
                        state,
                        member_token->span,
                        "module member is not public"
                    );
                }
                return vigil_parser_report(
                    state,
                    member_token->span,
                    "module member is not assignable"
                );
            } else if (
                vigil_program_find_class_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
                    &object_index,
                    &class_decl
                )
            ) {
                if (!vigil_program_is_class_public(class_decl)) {
                    return vigil_parser_report(
                        state,
                        member_token->span,
                        "module member is not public"
                    );
                }
                return vigil_parser_report(
                    state,
                    member_token->span,
                    "module member is not assignable"
                );
            } else if (
                vigil_program_find_interface_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
                    &object_index,
                    &interface_decl
                )
            ) {
                if (!vigil_program_is_interface_public(interface_decl)) {
                    return vigil_parser_report(
                        state,
                        member_token->span,
                        "module member is not public"
                    );
                }
                return vigil_parser_report(
                    state,
                    member_token->span,
                    "module member is not assignable"
                );
            } else if (
                vigil_program_find_enum_in_source(
                    state->program,
                    import_source_id,
                    member_text,
                    member_length,
                    &object_index,
                    &enum_decl
                )
            ) {
                if (!vigil_program_is_enum_public(enum_decl)) {
                    return vigil_parser_report(
                        state,
                        member_token->span,
                        "module member is not public"
                    );
                }
                return vigil_parser_report(
                    state,
                    member_token->span,
                    "module member is not assignable"
                );
            } else {
                return vigil_parser_report(
                    state,
                    member_token->span,
                    "unknown module member"
                );
            }
        } else if (
            !vigil_program_find_global_in_source(
                state->program,
                state->program->source == NULL ? 0U : state->program->source->id,
                name_text,
                name_length,
                &global_index,
                &global_decl
            )
        ) {
            return vigil_parser_report(state, name_token->span, "unknown local variable");
        }
        if (!is_global_assignment) {
            is_global_assignment = 1;
            local_type = global_decl->type;
        }
    }

    target_type = local_type;
    while (
        vigil_parser_check(state, VIGIL_TOKEN_DOT) ||
        vigil_parser_check(state, VIGIL_TOKEN_LBRACKET)
    ) {
        if (!emitted_target_base) {
            status = vigil_parser_emit_opcode(
                state,
                is_global_assignment
                    ? VIGIL_OPCODE_GET_GLOBAL
                    : (is_capture_local ? VIGIL_OPCODE_GET_CAPTURE : VIGIL_OPCODE_GET_LOCAL),
                name_token->span
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_u32(
                state,
                (uint32_t)(is_global_assignment
                               ? global_index
                               : (is_capture_local ? capture_index : local_index)),
                name_token->span
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            emitted_target_base = 1;
        }

        if (vigil_parser_match(state, VIGIL_TOKEN_DOT)) {
            is_field_assignment = 1;
            is_index_assignment = 0;
            status = vigil_parser_expect(
                state,
                VIGIL_TOKEN_IDENTIFIER,
                "expected field name after '.'",
                &field_token
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }

            field = NULL;
            field_index = 0U;
            status = vigil_parser_lookup_field(
                state,
                target_type,
                field_token,
                &field_index,
                &field
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            target_type = field->type;

            operator_token = vigil_parser_peek(state);
            if (
                operator_token != NULL &&
                vigil_parser_is_assignment_operator(operator_token->kind)
            ) {
                break;
            }

            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_FIELD, field_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_u32(state, (uint32_t)field_index, field_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }

        if (vigil_parser_match(state, VIGIL_TOKEN_LBRACKET)) {
            vigil_parser_type_t indexed_type;

            vigil_expression_result_clear(&index_result);
            indexed_type = vigil_binding_type_invalid();
            status = vigil_parser_parse_expression(state, &index_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state,
                vigil_parser_previous(state) == NULL
                    ? name_token->span
                    : vigil_parser_previous(state)->span,
                &index_result,
                "index expressions must evaluate to a single value"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_expect(
                state,
                VIGIL_TOKEN_RBRACKET,
                "expected ']' after index expression",
                NULL
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }

            if (vigil_parser_type_is_array(target_type)) {
                status = vigil_parser_require_type(
                    state,
                    name_token->span,
                    index_result.type,
                    vigil_binding_type_primitive(VIGIL_TYPE_I32),
                    "array index must be i32"
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                indexed_type = vigil_program_array_type_element(state->program, target_type);
            } else if (vigil_parser_type_is_map(target_type)) {
                status = vigil_parser_require_type(
                    state,
                    name_token->span,
                    index_result.type,
                    vigil_program_map_type_key(state->program, target_type),
                    "map index must match map key type"
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                indexed_type = vigil_program_map_type_value(state->program, target_type);
            } else {
                return vigil_parser_report(
                    state,
                    name_token->span,
                    "index assignment requires an array or map"
                );
            }
            target_type = indexed_type;
            is_field_assignment = 0;
            is_index_assignment = 1;

            operator_token = vigil_parser_peek(state);
            if (
                operator_token != NULL &&
                vigil_parser_is_assignment_operator(operator_token->kind)
            ) {
                break;
            }

            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_INDEX, name_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            continue;
        }
    }

    if (is_const_local && !is_field_assignment && !is_index_assignment) {
        return vigil_parser_report(
            state,
            target_token->span,
            "cannot assign to const local variable"
        );
    }

    operator_token = vigil_parser_peek(state);
    if (operator_token == NULL || !vigil_parser_is_assignment_operator(operator_token->kind)) {
        return vigil_parser_report(state, target_token->span, "expected assignment operator");
    }
    vigil_parser_advance(state);

    if (operator_token->kind == VIGIL_TOKEN_ASSIGN) {
        status = vigil_parser_parse_expression_with_expected_type(
            state,
            target_type,
            &value_result
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_scalar_expression(
            state,
            operator_token->span,
            &value_result,
            "assigned expression must be a single value"
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_require_type(
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
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    } else {
        vigil_opcode_t opcode;
        vigil_binary_operator_kind_t operator_kind;

        opcode = VIGIL_OPCODE_ADD;
        operator_kind = VIGIL_BINARY_OPERATOR_ADD;

        if (is_field_assignment) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_DUP, operator_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_FIELD, operator_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_u32(state, (uint32_t)field_index, operator_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
        } else if (is_index_assignment) {
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_DUP_TWO, operator_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_GET_INDEX, operator_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
        } else {
            if (!emitted_target_base) {
                status = vigil_parser_emit_opcode(
                    state,
                    is_global_assignment
                        ? VIGIL_OPCODE_GET_GLOBAL
                        : (is_capture_local ? VIGIL_OPCODE_GET_CAPTURE : VIGIL_OPCODE_GET_LOCAL),
                    operator_token->span
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
                status = vigil_parser_emit_u32(
                    state,
                    (uint32_t)(is_global_assignment
                                   ? global_index
                                   : (is_capture_local ? capture_index : local_index)),
                    operator_token->span
                );
                if (status != VIGIL_STATUS_OK) {
                    return status;
                }
            }
        }

        if (
            operator_token->kind == VIGIL_TOKEN_PLUS_PLUS ||
            operator_token->kind == VIGIL_TOKEN_MINUS_MINUS
        ) {
            if (vigil_parser_type_is_integer(target_type)) {
                status = vigil_parser_emit_integer_constant(
                    state,
                    target_type,
                    1,
                    operator_token->span
                );
            } else if (vigil_parser_type_is_f64(target_type)) {
                status = vigil_parser_emit_f64_constant(state, 1.0, operator_token->span);
            } else {
                status = vigil_parser_report(
                    state,
                    operator_token->span,
                    "increment and decrement require an integer or f64 target"
                );
            }
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            opcode = operator_token->kind == VIGIL_TOKEN_PLUS_PLUS
                ? VIGIL_OPCODE_ADD
                : VIGIL_OPCODE_SUBTRACT;
            value_result.type = target_type;
        } else {
            status = vigil_parser_parse_expression(state, &value_result);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
            status = vigil_parser_require_scalar_expression(
                state,
                operator_token->span,
                &value_result,
                "assigned expression must be a single value"
            );
            if (status != VIGIL_STATUS_OK) {
                return status;
            }

            switch (operator_token->kind) {
                case VIGIL_TOKEN_PLUS_ASSIGN:
                    operator_kind = VIGIL_BINARY_OPERATOR_ADD;
                    opcode = VIGIL_OPCODE_ADD;
                    break;
                case VIGIL_TOKEN_MINUS_ASSIGN:
                    operator_kind = VIGIL_BINARY_OPERATOR_SUBTRACT;
                    opcode = VIGIL_OPCODE_SUBTRACT;
                    break;
                case VIGIL_TOKEN_STAR_ASSIGN:
                    operator_kind = VIGIL_BINARY_OPERATOR_MULTIPLY;
                    opcode = VIGIL_OPCODE_MULTIPLY;
                    break;
                case VIGIL_TOKEN_SLASH_ASSIGN:
                    operator_kind = VIGIL_BINARY_OPERATOR_DIVIDE;
                    opcode = VIGIL_OPCODE_DIVIDE;
                    break;
                case VIGIL_TOKEN_PERCENT_ASSIGN:
                    operator_kind = VIGIL_BINARY_OPERATOR_MODULO;
                    opcode = VIGIL_OPCODE_MODULO;
                    break;
                default:
                    return vigil_parser_report(
                        state,
                        operator_token->span,
                        "unsupported assignment operator"
                    );
            }

            if (
                !vigil_parser_type_supports_binary_operator(
                    operator_kind,
                    target_type,
                    value_result.type
                )
            ) {
                status = vigil_parser_report(
                    state,
                    operator_token->span,
                    operator_kind == VIGIL_BINARY_OPERATOR_ADD
                        ? "compound assignment requires matching integer, f64, or string operands"
                        : (operator_kind == VIGIL_BINARY_OPERATOR_MODULO
                               ? "compound assignment modulo requires matching integer operands"
                               : "compound assignment requires matching integer or f64 operands")
                );
            } else {
                status = VIGIL_STATUS_OK;
            }
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
        }

        /* Specialize to i32/i64 opcodes when both operands are signed integers. */
        if (vigil_parser_type_is_i32(target_type) &&
            vigil_parser_type_is_i32(value_result.type)) {
            switch (opcode) {
                case VIGIL_OPCODE_ADD:      opcode = VIGIL_OPCODE_ADD_I32;      break;
                case VIGIL_OPCODE_SUBTRACT: opcode = VIGIL_OPCODE_SUBTRACT_I32; break;
                case VIGIL_OPCODE_MULTIPLY: opcode = VIGIL_OPCODE_MULTIPLY_I32; break;
                case VIGIL_OPCODE_DIVIDE:   opcode = VIGIL_OPCODE_DIVIDE_I32;   break;
                case VIGIL_OPCODE_MODULO:   opcode = VIGIL_OPCODE_MODULO_I32;   break;
                default: break;
            }
        } else if (vigil_parser_type_is_signed_integer(target_type) &&
            vigil_parser_type_is_signed_integer(value_result.type)) {
            switch (opcode) {
                case VIGIL_OPCODE_ADD:      opcode = VIGIL_OPCODE_ADD_I64;      break;
                case VIGIL_OPCODE_SUBTRACT: opcode = VIGIL_OPCODE_SUBTRACT_I64; break;
                case VIGIL_OPCODE_MULTIPLY: opcode = VIGIL_OPCODE_MULTIPLY_I64; break;
                case VIGIL_OPCODE_DIVIDE:   opcode = VIGIL_OPCODE_DIVIDE_I64;   break;
                case VIGIL_OPCODE_MODULO:   opcode = VIGIL_OPCODE_MODULO_I64;   break;
                default: break;
            }
        }

        status = vigil_parser_emit_opcode(state, opcode, operator_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        /* i32 opcodes already produce i32 results — skip the cast. */
        if (!(vigil_parser_type_is_i32(target_type) &&
              vigil_parser_type_is_i32(value_result.type))) {
            status = vigil_parser_emit_integer_cast(state, target_type, operator_token->span);
            if (status != VIGIL_STATUS_OK) {
                return status;
            }
        }
    }

    if (expect_semicolon) {
        status = vigil_parser_expect(state, VIGIL_TOKEN_SEMICOLON, "expected ';' after assignment", NULL);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    if (is_field_assignment) {
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_SET_FIELD, name_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)field_index, name_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    } else if (is_index_assignment) {
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_SET_INDEX, name_token->span);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    } else {
        status = vigil_parser_emit_opcode(
            state,
            is_global_assignment
                ? VIGIL_OPCODE_SET_GLOBAL
                : (is_capture_local ? VIGIL_OPCODE_SET_CAPTURE : VIGIL_OPCODE_SET_LOCAL),
            target_token->span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_u32(
            state,
            (uint32_t)(is_global_assignment
                           ? global_index
                           : (is_capture_local ? capture_index : local_index)),
            target_token->span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        status = vigil_parser_emit_opcode(state, VIGIL_OPCODE_POP, target_token->span);
        if (status != VIGIL_STATUS_OK) {
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
                if (code[base] == VIGIL_OPCODE_GET_LOCAL &&
                    code[base + 5U] == VIGIL_OPCODE_CONSTANT &&
                    (code[base + 10U] == VIGIL_OPCODE_ADD_I32 ||
                     code[base + 10U] == VIGIL_OPCODE_SUBTRACT_I32) &&
                    code[base + 11U] == VIGIL_OPCODE_SET_LOCAL &&
                    code[base + 16U] == VIGIL_OPCODE_POP) {
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
                        const vigil_value_t *cv = (ci < state->chunk.constant_count)
                            ? &state->chunk.constants[ci] : NULL;
                        if (cv != NULL && vigil_value_kind(cv) == VIGIL_VALUE_INT) {
                            int64_t val = vigil_value_as_int(cv);
                            int is_sub = (code[base + 10U] == VIGIL_OPCODE_SUBTRACT_I32);
                            if (is_sub) val = -val;
                            if (val >= -128 && val <= 127) {
                                /* Rewrite to INCREMENT_LOCAL_I32. */
                                code[base] = VIGIL_OPCODE_INCREMENT_LOCAL_I32;
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
            vigil_parser_type_is_i32(target_type)) {
            uint8_t *code = state->chunk.code.data;
            size_t len = state->chunk.code.length;
            if (len >= 15U) {
                size_t base = len - 15U;
                uint8_t op = code[base];
                vigil_opcode_t store_op = (vigil_opcode_t)0;
                switch ((vigil_opcode_t)op) {
                    case VIGIL_OPCODE_LOCALS_ADD_I64:       store_op = VIGIL_OPCODE_LOCALS_ADD_I32_STORE; break;
                    case VIGIL_OPCODE_LOCALS_SUBTRACT_I64:  store_op = VIGIL_OPCODE_LOCALS_SUBTRACT_I32_STORE; break;
                    case VIGIL_OPCODE_LOCALS_MULTIPLY_I64:  store_op = VIGIL_OPCODE_LOCALS_MULTIPLY_I32_STORE; break;
                    case VIGIL_OPCODE_LOCALS_MODULO_I64:    store_op = VIGIL_OPCODE_LOCALS_MODULO_I32_STORE; break;
                    case VIGIL_OPCODE_LOCALS_LESS_I64:      store_op = VIGIL_OPCODE_LOCALS_LESS_I32_STORE; break;
                    case VIGIL_OPCODE_LOCALS_LESS_EQUAL_I64: store_op = VIGIL_OPCODE_LOCALS_LESS_EQUAL_I32_STORE; break;
                    case VIGIL_OPCODE_LOCALS_GREATER_I64:   store_op = VIGIL_OPCODE_LOCALS_GREATER_I32_STORE; break;
                    case VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I64: store_op = VIGIL_OPCODE_LOCALS_GREATER_EQUAL_I32_STORE; break;
                    case VIGIL_OPCODE_LOCALS_EQUAL_I64:     store_op = VIGIL_OPCODE_LOCALS_EQUAL_I32_STORE; break;
                    case VIGIL_OPCODE_LOCALS_NOT_EQUAL_I64: store_op = VIGIL_OPCODE_LOCALS_NOT_EQUAL_I32_STORE; break;
                    default: break;
                }
                if (store_op != (vigil_opcode_t)0 &&
                    code[base + 9U] == VIGIL_OPCODE_SET_LOCAL &&
                    code[base + 14U] == VIGIL_OPCODE_POP) {
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
    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_assignment_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    return vigil_parser_parse_assignment_statement_internal(state, out_result, 1);
}

static vigil_status_t vigil_parser_parse_expression_statement_internal(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result,
    int expect_semicolon
) {
    vigil_status_t status;
    vigil_expression_result_t expression_result;
    const vigil_token_t *last_token;

    vigil_expression_result_clear(&expression_result);

    status = vigil_parser_parse_expression(state, &expression_result);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    if (expression_result.type_count > 1U) {
        return vigil_parser_report(
            state,
            vigil_parser_previous(state) == NULL
                ? vigil_parser_fallback_span(state)
                : vigil_parser_previous(state)->span,
            "multi-value expressions must be bound explicitly"
        );
    }

    last_token = vigil_parser_previous(state);
    if (expect_semicolon) {
        status = vigil_parser_expect(state, VIGIL_TOKEN_SEMICOLON, "expected ';' after expression", NULL);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    if (!vigil_parser_type_is_void(expression_result.type)) {
        status = vigil_parser_emit_opcode(
            state,
            VIGIL_OPCODE_POP,
            last_token == NULL ? vigil_parser_fallback_span(state) : last_token->span
        );
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }
    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_expression_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    return vigil_parser_parse_expression_statement_internal(state, out_result, 1);
}

static vigil_status_t vigil_parser_parse_statement(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    if (vigil_parser_check(state, VIGIL_TOKEN_RETURN)) {
        return vigil_parser_parse_return_statement(state, out_result);
    }
    if (vigil_parser_check(state, VIGIL_TOKEN_DEFER)) {
        return vigil_parser_parse_defer_statement(state, out_result);
    }
    if (vigil_parser_check(state, VIGIL_TOKEN_GUARD)) {
        return vigil_parser_parse_guard_statement(state, out_result);
    }
    if (vigil_parser_check(state, VIGIL_TOKEN_IF)) {
        return vigil_parser_parse_if_statement(state, out_result);
    }
    if (vigil_parser_check(state, VIGIL_TOKEN_SWITCH)) {
        return vigil_parser_parse_switch_statement(state, out_result);
    }
    if (vigil_parser_check(state, VIGIL_TOKEN_FOR)) {
        return vigil_parser_parse_for_statement(state, out_result);
    }
    if (vigil_parser_check(state, VIGIL_TOKEN_WHILE)) {
        return vigil_parser_parse_while_statement(state, out_result);
    }
    if (vigil_parser_check(state, VIGIL_TOKEN_BREAK)) {
        return vigil_parser_parse_break_statement(state, out_result);
    }
    if (vigil_parser_check(state, VIGIL_TOKEN_CONTINUE)) {
        return vigil_parser_parse_continue_statement(state, out_result);
    }
    if (vigil_parser_check(state, VIGIL_TOKEN_LBRACE)) {
        return vigil_parser_parse_block_statement(state, out_result);
    }
    if (vigil_parser_is_assignment_start(state)) {
        return vigil_parser_parse_assignment_statement(state, out_result);
    }

    return vigil_parser_parse_expression_statement(state, out_result);
}

static vigil_status_t vigil_parser_parse_variable_declaration(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    vigil_binding_target_list_t targets;
    vigil_expression_result_t initializer_result;

    vigil_binding_target_list_init(&targets);
    vigil_expression_result_clear(&initializer_result);

    status = vigil_parser_parse_binding_target_list(
        state,
        "unsupported local variable type",
        "local variables cannot use type void",
        "expected local variable name",
        &targets
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    if (!vigil_parser_match(state, VIGIL_TOKEN_ASSIGN)) {
        status = vigil_parser_report(
            state,
            targets.items[0].name_token->span,
            "variables must be initialized at declaration"
        );
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    status = vigil_parser_parse_expression_with_expected_type(
        state,
        targets.count == 1U ? targets.items[0].type : vigil_binding_type_invalid(),
        &initializer_result
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }
    status = vigil_parser_require_binding_initializer_shape(
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
    if (status != VIGIL_STATUS_OK) {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    status = vigil_parser_expect(state, VIGIL_TOKEN_SEMICOLON, "expected ';' after local declaration", NULL);
    if (status != VIGIL_STATUS_OK) {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }

    status = vigil_parser_bind_targets(state, &targets, 0, NULL);
    if (status != VIGIL_STATUS_OK) {
        vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
        return status;
    }
    vigil_binding_target_list_free((vigil_program_state_t *)state->program, &targets);
    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_parser_parse_const_declaration(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    const vigil_token_t *const_token;
    const vigil_token_t *name_token;
    vigil_parser_type_t declared_type;
    vigil_expression_result_t initializer_result;

    vigil_expression_result_clear(&initializer_result);
    status = vigil_parser_expect(state, VIGIL_TOKEN_CONST, "expected 'const'", &const_token);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_program_parse_type_reference(
        state->program,
        &state->current,
        "unsupported local constant type",
        &declared_type
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_program_require_non_void_type(
        state->program,
        vigil_parser_previous(state) == NULL
            ? const_token->span
            : vigil_parser_previous(state)->span,
        declared_type,
        "local constants cannot use type void"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_IDENTIFIER,
        "expected local constant name",
        &name_token
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_ASSIGN,
        "constants must be initialized at declaration",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_parse_expression_with_expected_type(
        state,
        declared_type,
        &initializer_result
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_scalar_expression(
        state,
        name_token->span,
        &initializer_result,
        "initializer must be a single value"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_require_type(
        state,
        name_token->span,
        initializer_result.type,
        declared_type,
        "initializer type does not match local constant type"
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    status = vigil_parser_expect(
        state,
        VIGIL_TOKEN_SEMICOLON,
        "expected ';' after local constant declaration",
        NULL
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    status = vigil_parser_declare_local_symbol(state, name_token, declared_type, 1, NULL);
    if (status != VIGIL_STATUS_OK) {
        return status;
    }
    vigil_statement_result_set_guaranteed_return(out_result, 0);
    return VIGIL_STATUS_OK;
}

static int vigil_parser_is_variable_declaration_start(
    const vigil_parser_state_t *state
) {
    const vigil_token_t *token;
    size_t cursor;

    cursor = state->current;
    while (1) {
        if (!vigil_parser_skip_type_reference_tokens(state->program, &cursor)) {
            return 0;
        }

        token = vigil_program_token_at(state->program, cursor);
        if (token == NULL || token->kind != VIGIL_TOKEN_IDENTIFIER) {
            return 0;
        }
        cursor += 1U;

        token = vigil_program_token_at(state->program, cursor);
        if (token == NULL) {
            return 0;
        }
        if (token->kind == VIGIL_TOKEN_ASSIGN) {
            return 1;
        }
        if (token->kind == VIGIL_TOKEN_SEMICOLON) {
            return 1;
        }
        if (token->kind != VIGIL_TOKEN_COMMA) {
            return 0;
        }
        cursor += 1U;
    }
}

static vigil_status_t vigil_parser_parse_declaration(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    if (
        vigil_parser_check(state, VIGIL_TOKEN_FN) &&
        vigil_program_token_at(state->program, state->current + 1U) != NULL &&
        vigil_program_token_at(state->program, state->current + 1U)->kind == VIGIL_TOKEN_IDENTIFIER &&
        vigil_program_token_at(state->program, state->current + 2U) != NULL &&
        vigil_program_token_at(state->program, state->current + 2U)->kind == VIGIL_TOKEN_LPAREN
    ) {
        return vigil_parser_parse_local_function_declaration(state, out_result);
    }
    if (vigil_parser_check(state, VIGIL_TOKEN_CONST)) {
        return vigil_parser_parse_const_declaration(state, out_result);
    }
    if (vigil_parser_is_variable_declaration_start(state)) {
        return vigil_parser_parse_variable_declaration(state, out_result);
    }

    return vigil_parser_parse_statement(state, out_result);
}

static vigil_status_t vigil_parser_parse_block_contents(
    vigil_parser_state_t *state,
    vigil_statement_result_t *out_result
) {
    vigil_status_t status;
    vigil_statement_result_t declaration_result;
    vigil_statement_result_t block_result;

    vigil_statement_result_clear(&declaration_result);
    vigil_statement_result_clear(&block_result);

    while (!vigil_parser_is_at_end(state) && !vigil_parser_check(state, VIGIL_TOKEN_RBRACE)) {
        status = vigil_parser_parse_declaration(state, &declaration_result);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
        if (declaration_result.guaranteed_return) {
            block_result.guaranteed_return = 1;
        }
    }

    vigil_statement_result_set_guaranteed_return(
        out_result,
        block_result.guaranteed_return
    );
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_compile_seed_parameter_symbols(
    vigil_parser_state_t *state,
    const vigil_function_decl_t *decl
) {
    vigil_status_t status;
    size_t i;

    for (i = 0U; i < decl->param_count; ++i) {
        if (
            decl->owner_class_index != VIGIL_BINDING_INVALID_CLASS_INDEX &&
            i == 0U &&
            decl->params[i].length == 4U &&
            memcmp(decl->params[i].name, "self", 4U) == 0
        ) {
            status = vigil_binding_scope_stack_declare_local(
                &state->locals,
                "self",
                4U,
                decl->params[i].type,
                0,
                NULL,
                state->program->error
            );
        } else {
            const vigil_token_t fake_name = {
                VIGIL_TOKEN_IDENTIFIER,
                decl->params[i].span
            };

            status = vigil_parser_declare_local_symbol(
                state,
                &fake_name,
                decl->params[i].type,
                0,
                NULL
            );
        }
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_compile_emit_global_initializers(
    vigil_program_state_t *program,
    vigil_parser_state_t *state
) {
    vigil_status_t status;
    size_t i;
    const vigil_source_file_t *previous_source;
    const vigil_token_list_t *previous_tokens;
    size_t previous_current;
    size_t previous_body_end;

    previous_source = program->source;
    previous_tokens = program->tokens;
    previous_current = state->current;
    previous_body_end = state->body_end;

    for (i = 0U; i < program->global_count; i += 1U) {
        vigil_expression_result_t initializer_result;

        vigil_expression_result_clear(&initializer_result);
        vigil_program_set_module_context(program, program->globals[i].source, program->globals[i].tokens);
        state->current = program->globals[i].initializer_start;
        state->body_end = program->globals[i].initializer_end;

        status = vigil_parser_parse_expression_with_expected_type(
            state,
            program->globals[i].type,
            &initializer_result
        );
        if (status != VIGIL_STATUS_OK) {
            goto restore;
        }
        if (state->current != program->globals[i].initializer_end) {
            status = vigil_compile_report(
                program,
                program->globals[i].name_span,
                "invalid global initializer expression"
            );
            goto restore;
        }
        status = vigil_parser_require_type(
            state,
            program->globals[i].name_span,
            initializer_result.type,
            program->globals[i].type,
            "initializer type does not match global variable type"
        );
        if (status != VIGIL_STATUS_OK) {
            goto restore;
        }

        status = vigil_parser_emit_opcode(
            state,
            VIGIL_OPCODE_SET_GLOBAL,
            program->globals[i].name_span
        );
        if (status != VIGIL_STATUS_OK) {
            goto restore;
        }
        status = vigil_parser_emit_u32(state, (uint32_t)i, program->globals[i].name_span);
        if (status != VIGIL_STATUS_OK) {
            goto restore;
        }
        status = vigil_parser_emit_opcode(
            state,
            VIGIL_OPCODE_POP,
            program->globals[i].name_span
        );
        if (status != VIGIL_STATUS_OK) {
            goto restore;
        }
    }

    status = VIGIL_STATUS_OK;

restore:
    vigil_program_set_module_context(program, previous_source, previous_tokens);
    state->current = previous_current;
    state->body_end = previous_body_end;
    return status;
}

static vigil_status_t vigil_compile_synthetic_constructor(
    vigil_program_state_t *program,
    size_t function_index,
    size_t class_index,
    size_t init_function_index
) {
    vigil_status_t status;
    vigil_parser_state_t state;
    vigil_function_decl_t *decl;
    const vigil_class_decl_t *class_decl;
    vigil_object_t *object;
    size_t field_index;
    size_t param_index;
    uint32_t init_arg_count;

    decl = &program->functions.functions[function_index];
    if (class_index >= program->class_count || init_function_index >= program->functions.count) {
        vigil_error_set_literal(
            program->error,
            VIGIL_STATUS_INTERNAL,
            "synthetic constructor metadata is invalid"
        );
        return VIGIL_STATUS_INTERNAL;
    }

    class_decl = &program->classes[class_index];
    if (decl->param_count > UINT32_MAX - 1U) {
        vigil_error_set_literal(
            program->error,
            VIGIL_STATUS_OUT_OF_MEMORY,
            "constructor arity overflow"
        );
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }
    init_arg_count = (uint32_t)(decl->param_count + 1U);

    memset(&state, 0, sizeof(state));
    state.program = program;
    state.function_index = function_index;
    state.expected_return_type = decl->return_type;
    vigil_chunk_init(&state.chunk, program->registry->runtime);
    vigil_binding_scope_stack_init(&state.locals, program->registry->runtime);

    for (field_index = 0U; field_index < class_decl->field_count; field_index += 1U) {
        status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_NIL, decl->name_span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
    }
    status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_NEW_INSTANCE, decl->name_span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_emit_u32(&state, (uint32_t)class_index, decl->name_span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_emit_u32(&state, (uint32_t)class_decl->field_count, decl->name_span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_DUP, decl->name_span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }

    for (param_index = 0U; param_index < decl->param_count; param_index += 1U) {
        status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_GET_LOCAL, decl->name_span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
        status = vigil_parser_emit_u32(&state, (uint32_t)param_index, decl->name_span);
        if (status != VIGIL_STATUS_OK) {
            goto cleanup;
        }
    }

    status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_CALL, decl->name_span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_emit_u32(
        &state,
        (uint32_t)init_function_index,
        decl->name_span
    );
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_emit_u32(&state, init_arg_count, decl->name_span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_RETURN, decl->name_span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }
    status = vigil_parser_emit_u32(&state, (uint32_t)decl->return_count, decl->name_span);
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }

    object = NULL;
    status = vigil_function_object_new(
        program->registry->runtime,
        decl->name,
        decl->name_length,
        decl->param_count,
        decl->return_count,
        &state.chunk,
        &object,
        program->error
    );
    if (status != VIGIL_STATUS_OK) {
        goto cleanup;
    }

    vigil_parser_state_free(&state);
    decl->object = object;
    return VIGIL_STATUS_OK;

cleanup:
    vigil_chunk_free(&state.chunk);
    vigil_parser_state_free(&state);
    return status;
}

static vigil_status_t vigil_compile_require_function_returns(
    vigil_program_state_t *program,
    const vigil_function_decl_t *decl,
    size_t function_index,
    int guaranteed_return
) {
    if (decl->return_count == 1U && vigil_parser_type_is_void(decl->return_type)) {
        return VIGIL_STATUS_OK;
    }
    if (guaranteed_return) {
        return VIGIL_STATUS_OK;
    }

    return vigil_compile_report(
        program,
        decl->name_span,
        function_index == program->functions.main_index
            ? "main entrypoint must return an i32 value on all paths"
            : "function must return a value on all paths"
    );
}

/* ── Compile extern fn as synthetic bytecode ──────────────────────── */

static vigil_status_t vigil_compile_extern_fn(
    vigil_program_state_t *program,
    size_t function_index,
    const vigil_extern_fn_decl_t *ext
) {
    vigil_status_t status;
    vigil_parser_state_t state;
    vigil_function_decl_t *decl;
    vigil_object_t *object;

    decl = &program->functions.functions[function_index];

    memset(&state, 0, sizeof(state));
    state.program = program;
    state.function_index = function_index;
    state.expected_return_type = decl->return_type;
    vigil_chunk_init(&state.chunk, program->registry->runtime);
    vigil_binding_scope_stack_init(&state.locals, program->registry->runtime);

    /* Push all parameters onto the stack via GET_LOCAL. */
    for (size_t i = 0; i < decl->param_count; i++) {
        status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_GET_LOCAL,
                                          decl->name_span);
        if (status != VIGIL_STATUS_OK) goto cleanup;
        status = vigil_parser_emit_u32(&state, (uint32_t)i, decl->name_span);
        if (status != VIGIL_STATUS_OK) goto cleanup;
    }

    /* Emit CALL_EXTERN <descriptor_const> <arg_count>.
     * Descriptor is a string with embedded NULs: "lib\0name\0sig". */
    {
        size_t lib_len = strlen(ext->lib_path);
        size_t name_len = strlen(ext->c_name);
        size_t sig_len = strlen(ext->sig);
        size_t desc_len = lib_len + 1 + name_len + 1 + sig_len;
        char *desc = malloc(desc_len + 1);
        if (!desc) {
            status = VIGIL_STATUS_OUT_OF_MEMORY;
            goto cleanup;
        }
        char *p = desc;
        memcpy(p, ext->lib_path, lib_len); p += lib_len; *p++ = '\0';
        memcpy(p, ext->c_name, name_len); p += name_len; *p++ = '\0';
        memcpy(p, ext->sig, sig_len); p += sig_len; *p = '\0';

        vigil_object_t *str_obj = NULL;
        status = vigil_string_object_new(
            program->registry->runtime,
            desc, desc_len,
            &str_obj, program->error);
        free(desc);
        if (status != VIGIL_STATUS_OK) goto cleanup;

        vigil_value_t str_val;
        vigil_value_init_object(&str_val, &str_obj);
        size_t const_idx;
        status = vigil_chunk_add_constant(&state.chunk, &str_val, &const_idx,
                                          program->error);
        vigil_value_release(&str_val);
        if (status != VIGIL_STATUS_OK) goto cleanup;

        status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_CALL_EXTERN,
                                          decl->name_span);
        if (status != VIGIL_STATUS_OK) goto cleanup;
        status = vigil_parser_emit_u32(&state, (uint32_t)const_idx,
                                       decl->name_span);
        if (status != VIGIL_STATUS_OK) goto cleanup;
        status = vigil_parser_emit_u32(&state, (uint32_t)decl->param_count,
                                       decl->name_span);
        if (status != VIGIL_STATUS_OK) goto cleanup;
    }

    /* Emit RETURN. */
    status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_RETURN,
                                      decl->name_span);
    if (status != VIGIL_STATUS_OK) goto cleanup;
    {
        uint32_t rc = vigil_parser_type_is_void(decl->return_type) ? 0U : 1U;
        status = vigil_parser_emit_u32(&state, rc, decl->name_span);
        if (status != VIGIL_STATUS_OK) goto cleanup;
    }

    object = NULL;
    status = vigil_function_object_new(
        program->registry->runtime,
        decl->name, decl->name_length,
        decl->param_count, decl->return_count,
        &state.chunk, &object, program->error);
    if (status != VIGIL_STATUS_OK) goto cleanup;

    vigil_parser_state_free(&state);
    decl->object = object;
    return VIGIL_STATUS_OK;

cleanup:
    vigil_chunk_free(&state.chunk);
    vigil_parser_state_free(&state);
    return status;
}

static vigil_status_t vigil_compile_function_with_parent(
    vigil_program_state_t *program,
    size_t function_index,
    const vigil_parser_state_t *parent_state
) {
    vigil_status_t status;
    vigil_parser_state_t state;
    vigil_function_decl_t *decl;
    vigil_object_t *object;
    vigil_statement_result_t body_result;
    size_t class_index;

    decl = &program->functions.functions[function_index];
    if (decl->object != NULL) {
        return VIGIL_STATUS_OK;
    }
    for (class_index = 0U; class_index < program->class_count; class_index += 1U) {
        const vigil_class_decl_t *class_decl;
        const vigil_class_method_t *init_method;

        class_decl = &program->classes[class_index];
        if (class_decl->constructor_function_index != function_index) {
            continue;
        }
        init_method = NULL;
        if (!vigil_class_decl_find_method(class_decl, "init", 4U, NULL, &init_method) ||
            init_method == NULL) {
            vigil_error_set_literal(
                program->error,
                VIGIL_STATUS_INTERNAL,
                "class init declaration is missing"
            );
            return VIGIL_STATUS_INTERNAL;
        }
        return vigil_compile_synthetic_constructor(
            program,
            function_index,
            class_index,
            init_method->function_index
        );
    }

    /* Check if this is an extern fn. */
    for (size_t ei = 0; ei < program->extern_fn_count; ei++) {
        if (program->extern_fns[ei].function_index == function_index) {
            return vigil_compile_extern_fn(program, function_index,
                                           &program->extern_fns[ei]);
        }
    }

    memset(&state, 0, sizeof(state));
    vigil_program_set_module_context(program, decl->source, decl->tokens);
    state.program = program;
    state.parent = (vigil_parser_state_t *)parent_state;
    state.current = decl->body_start;
    state.body_end = decl->body_end;
    state.function_index = function_index;
    state.expected_return_type = decl->return_type;
    state.expected_return_types = vigil_function_return_types(decl);
    state.expected_return_count = decl->return_count;
    vigil_chunk_init(&state.chunk, program->registry->runtime);
    vigil_binding_scope_stack_init(&state.locals, program->registry->runtime);
    vigil_binding_scope_stack_begin_scope(&state.locals);
    vigil_statement_result_clear(&body_result);

    status = vigil_compile_seed_parameter_symbols(&state, decl);
    if (status != VIGIL_STATUS_OK) {
        vigil_chunk_free(&state.chunk);
        vigil_parser_state_free(&state);
        return status;
    }

    if (function_index == program->functions.main_index) {
        status = vigil_compile_emit_global_initializers(program, &state);
        if (status != VIGIL_STATUS_OK) {
            vigil_chunk_free(&state.chunk);
            vigil_parser_state_free(&state);
            return status;
        }
    }

    status = vigil_parser_parse_block_contents(&state, &body_result);
    if (status != VIGIL_STATUS_OK) {
        vigil_chunk_free(&state.chunk);
        vigil_parser_state_free(&state);
        return status;
    }

    /* Re-fetch: the function table may have been reallocated while
       parsing the body (e.g. local/anonymous function declarations
       call vigil_program_grow_functions). */
    decl = &program->functions.functions[function_index];

    if (
        !body_result.guaranteed_return &&
        decl->return_count == 1U &&
        vigil_parser_type_is_void(decl->return_type)
    ) {
        status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_RETURN, decl->name_span);
        if (status != VIGIL_STATUS_OK) {
            vigil_chunk_free(&state.chunk);
            vigil_parser_state_free(&state);
            return status;
        }
        status = vigil_parser_emit_u32(&state, 0U, decl->name_span);
        if (status != VIGIL_STATUS_OK) {
            vigil_chunk_free(&state.chunk);
            vigil_parser_state_free(&state);
            return status;
        }
        body_result.guaranteed_return = 1;
    }

    /* In REPL mode, emit a synthetic 'return 0' for the synthetic main
       when the user's statements don't include an explicit return. */
    if (
        !body_result.guaranteed_return &&
        program->compile_mode == VIGIL_COMPILE_MODE_REPL &&
        function_index == program->functions.main_index
    ) {
        status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_CONSTANT, decl->name_span);
        if (status != VIGIL_STATUS_OK) {
            vigil_chunk_free(&state.chunk);
            vigil_parser_state_free(&state);
            return status;
        }
        {
            vigil_value_t zero_val;
            size_t const_index;
            vigil_value_init_int(&zero_val, 0);
            status = vigil_chunk_add_constant(&state.chunk, &zero_val, &const_index, program->error);
            if (status != VIGIL_STATUS_OK) {
                vigil_chunk_free(&state.chunk);
                vigil_parser_state_free(&state);
                return status;
            }
            status = vigil_parser_emit_u32(&state, (uint32_t)const_index, decl->name_span);
        }
        if (status != VIGIL_STATUS_OK) {
            vigil_chunk_free(&state.chunk);
            vigil_parser_state_free(&state);
            return status;
        }
        status = vigil_parser_emit_opcode(&state, VIGIL_OPCODE_RETURN, decl->name_span);
        if (status != VIGIL_STATUS_OK) {
            vigil_chunk_free(&state.chunk);
            vigil_parser_state_free(&state);
            return status;
        }
        status = vigil_parser_emit_u32(&state, 1U, decl->name_span);
        if (status != VIGIL_STATUS_OK) {
            vigil_chunk_free(&state.chunk);
            vigil_parser_state_free(&state);
            return status;
        }
        body_result.guaranteed_return = 1;
    }

    status = vigil_compile_require_function_returns(
        program,
        decl,
        function_index,
        body_result.guaranteed_return
    );
    if (status != VIGIL_STATUS_OK) {
        vigil_chunk_free(&state.chunk);
        vigil_parser_state_free(&state);
        return status;
    }

    object = NULL;
    status = vigil_function_object_new(
        program->registry->runtime,
        decl->name,
        decl->name_length,
        decl->param_count,
        decl->return_count,
        &state.chunk,
        &object,
        program->error
    );
    vigil_parser_state_free(&state);
    if (status != VIGIL_STATUS_OK) {
        vigil_chunk_free(&state.chunk);
        return status;
    }

    decl->object = object;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_compile_function(
    vigil_program_state_t *program,
    size_t function_index
) {
    return vigil_compile_function_with_parent(program, function_index, NULL);
}

static vigil_status_t vigil_compile_validate_inputs(
    const vigil_source_registry_t *registry,
    vigil_diagnostic_list_t *diagnostics,
    vigil_object_t **out_function,
    vigil_compile_mode_t mode,
    vigil_error_t *error
) {
    if (registry == NULL) {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "source registry must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if (diagnostics == NULL) {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "diagnostic list must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }
    if ((mode == VIGIL_COMPILE_MODE_BUILD_ENTRYPOINT || mode == VIGIL_COMPILE_MODE_REPL) && out_function == NULL) {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_function must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_compile_all_functions(
    vigil_program_state_t *program
) {
    vigil_status_t status;
    size_t i;

    for (i = 0U; i < program->functions.count; ++i) {
        status = vigil_compile_function(program, i);
        if (status != VIGIL_STATUS_OK) {
            return status;
        }
    }

    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_compile_attach_entrypoint(
    vigil_program_state_t *program,
    vigil_object_t **out_function
) {
    vigil_status_t status;
    vigil_object_t **function_table;
    vigil_value_t *initial_globals;
    vigil_runtime_class_init_t *class_inits;
    size_t i;
    void *memory;

    memory = NULL;
    status = vigil_runtime_alloc(
        program->registry->runtime,
        program->functions.count * sizeof(*function_table),
        &memory,
        program->error
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    function_table = (vigil_object_t **)memory;
    for (i = 0U; i < program->functions.count; ++i) {
        function_table[i] = program->functions.functions[i].object;
    }

    initial_globals = NULL;
    if (program->global_count != 0U) {
        memory = NULL;
        status = vigil_runtime_alloc(
            program->registry->runtime,
            program->global_count * sizeof(*initial_globals),
            &memory,
            program->error
        );
        if (status != VIGIL_STATUS_OK) {
            memory = function_table;
            vigil_runtime_free(program->registry->runtime, &memory);
            return status;
        }

        initial_globals = (vigil_value_t *)memory;
        for (i = 0U; i < program->global_count; ++i) {
            vigil_value_init_nil(&initial_globals[i]);
        }
    }

    class_inits = NULL;
    if (program->class_count != 0U) {
        memory = NULL;
        status = vigil_runtime_alloc(
            program->registry->runtime,
            program->class_count * sizeof(*class_inits),
            &memory,
            program->error
        );
        if (status != VIGIL_STATUS_OK) {
            if (initial_globals != NULL) {
                memory = initial_globals;
                vigil_runtime_free(program->registry->runtime, &memory);
            }
            memory = function_table;
            vigil_runtime_free(program->registry->runtime, &memory);
            return status;
        }

        class_inits = (vigil_runtime_class_init_t *)memory;
        memset(class_inits, 0, program->class_count * sizeof(*class_inits));
        for (i = 0U; i < program->class_count; ++i) {
            const vigil_class_decl_t *decl = &program->classes[i];

            class_inits[i].interface_impl_count = decl->interface_impl_count;
            if (decl->interface_impl_count == 0U) {
                continue;
            }

            memory = NULL;
            status = vigil_runtime_alloc(
                program->registry->runtime,
                decl->interface_impl_count * sizeof(*class_inits[i].interface_impls),
                &memory,
                program->error
            );
            if (status != VIGIL_STATUS_OK) {
                size_t class_index;

                for (class_index = 0U; class_index < i; ++class_index) {
                    memory = (void *)class_inits[class_index].interface_impls;
                    vigil_runtime_free(program->registry->runtime, &memory);
                }
                memory = class_inits;
                vigil_runtime_free(program->registry->runtime, &memory);
                if (initial_globals != NULL) {
                    memory = initial_globals;
                    vigil_runtime_free(program->registry->runtime, &memory);
                }
                memory = function_table;
                vigil_runtime_free(program->registry->runtime, &memory);
                return status;
            }

            class_inits[i].interface_impls = (const vigil_runtime_interface_impl_init_t *)memory;
            memset(
                (void *)class_inits[i].interface_impls,
                0,
                decl->interface_impl_count * sizeof(*class_inits[i].interface_impls)
            );

            {
                size_t impl_index;
                vigil_runtime_interface_impl_init_t *impls =
                    (vigil_runtime_interface_impl_init_t *)class_inits[i].interface_impls;

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

    status = vigil_function_object_attach_siblings(
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
        vigil_runtime_free(program->registry->runtime, &memory);
    }
    if (class_inits != NULL) {
        for (i = 0U; i < program->class_count; ++i) {
            memory = (void *)class_inits[i].interface_impls;
            vigil_runtime_free(program->registry->runtime, &memory);
        }
        memory = class_inits;
        vigil_runtime_free(program->registry->runtime, &memory);
    }
    if (status != VIGIL_STATUS_OK) {
        memory = function_table;
        vigil_runtime_free(program->registry->runtime, &memory);
        return status;
    }

    *out_function = program->functions.functions[program->functions.main_index].object;
    for (i = 0U; i < program->functions.count; ++i) {
        program->functions.functions[i].object = NULL;
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_compile_source_internal(
    const vigil_source_registry_t *registry,
    vigil_source_id_t source_id,
    vigil_compile_mode_t mode,
    const vigil_native_registry_t *natives,
    vigil_object_t **out_function,
    int *out_repl_has_statements,
    vigil_diagnostic_list_t *diagnostics,
    vigil_error_t *error
) {
    vigil_status_t status;
    vigil_program_state_t program;
    const vigil_source_file_t *source;

    vigil_error_clear(error);
    if (out_repl_has_statements) *out_repl_has_statements = 0;
    if (out_function != NULL) {
        *out_function = NULL;
    }

    status = vigil_compile_validate_inputs(
        registry,
        diagnostics,
        out_function,
        mode,
        error
    );
    if (status != VIGIL_STATUS_OK) {
        return status;
    }

    source = vigil_source_registry_get(registry, source_id);
    if (source == NULL) {
        vigil_error_set_literal(
            error,
            VIGIL_STATUS_INVALID_ARGUMENT,
            "source_id must reference a registered source file"
        );
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    memset(&program, 0, sizeof(program));
    program.registry = registry;
    program.diagnostics = diagnostics;
    program.error = error;
    program.natives = natives;
    program.compile_mode = (int)mode;
    vigil_binding_function_table_init(&program.functions, registry->runtime);
    vigil_program_set_module_context(&program, source, NULL);

    status = vigil_program_parse_source(&program, source_id);
    if (status != VIGIL_STATUS_OK) {
        vigil_program_free(&program);
        return status;
    }

    vigil_program_set_module_context(&program, source, NULL);

    /* In REPL mode, synthesize fn main from top-level statements.
       Also synthesize when there are globals (even without statements)
       so their initializers get compiled and type-checked. */
    if (mode == VIGIL_COMPILE_MODE_REPL && !program.functions.has_main &&
        (program.repl_stmts_start < program.repl_stmts_end || program.global_count > 0U)) {
        vigil_binding_function_t *decl;
        vigil_binding_type_t ret_type;
        size_t mod_idx = 0;

        status = vigil_program_grow_functions(&program, program.functions.count + 1U);
        if (status != VIGIL_STATUS_OK) {
            vigil_program_free(&program);
            return status;
        }
        decl = &program.functions.functions[program.functions.count];
        vigil_binding_function_init(decl);
        decl->name = "main";
        decl->name_length = 4U;
        decl->name_span = vigil_program_eof_span(&program);
        decl->is_public = 0;
        decl->source = source;
        /* Retrieve the module's token list so the parser can read the body. */
        if (vigil_program_module_find(&program, source_id, &mod_idx)) {
            decl->tokens = program.modules[mod_idx].tokens;
        }
        decl->body_start = program.repl_stmts_start;
        decl->body_end = program.repl_stmts_end;
        memset(&ret_type, 0, sizeof(ret_type));
        ret_type.kind = VIGIL_TYPE_I32;
        decl->return_type = ret_type;
        decl->return_count = 1U;
        program.functions.main_index = program.functions.count;
        program.functions.has_main = 1;
        program.repl_has_statements = (program.repl_stmts_start < program.repl_stmts_end) ? 1 : 0;
        program.functions.count += 1U;
    }

    if (!program.functions.has_main) {
        if (mode == VIGIL_COMPILE_MODE_REPL) {
            /* No statements and no main — nothing to execute. */
            vigil_program_free(&program);
            return VIGIL_STATUS_OK;
        }
        status = vigil_compile_report(
            &program,
            vigil_program_eof_span(&program),
            "expected top-level function 'main'"
        );
        vigil_program_free(&program);
        return status;
    }

    status = vigil_compile_all_functions(&program);
    if (status != VIGIL_STATUS_OK) {
        vigil_program_free(&program);
        return status;
    }

    if (mode == VIGIL_COMPILE_MODE_BUILD_ENTRYPOINT || mode == VIGIL_COMPILE_MODE_REPL) {
        status = vigil_compile_attach_entrypoint(&program, out_function);
        if (status != VIGIL_STATUS_OK) {
            vigil_program_free(&program);
            return status;
        }
    }

    if (out_repl_has_statements) *out_repl_has_statements = program.repl_has_statements;
    vigil_program_free(&program);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_compile_source(
    const vigil_source_registry_t *registry,
    vigil_source_id_t source_id,
    vigil_object_t **out_function,
    vigil_diagnostic_list_t *diagnostics,
    vigil_error_t *error
) {
    return vigil_compile_source_internal(
        registry,
        source_id,
        VIGIL_COMPILE_MODE_BUILD_ENTRYPOINT,
        NULL,
        out_function,
        NULL,
        diagnostics,
        error
    );
}

vigil_status_t vigil_compile_source_with_natives(
    const vigil_source_registry_t *registry,
    vigil_source_id_t source_id,
    const vigil_native_registry_t *natives,
    vigil_object_t **out_function,
    vigil_diagnostic_list_t *diagnostics,
    vigil_error_t *error
) {
    return vigil_compile_source_internal(
        registry,
        source_id,
        VIGIL_COMPILE_MODE_BUILD_ENTRYPOINT,
        natives,
        out_function,
        NULL,
        diagnostics,
        error
    );
}

vigil_status_t vigil_compile_source_repl(
    const vigil_source_registry_t *registry,
    vigil_source_id_t source_id,
    const vigil_native_registry_t *natives,
    vigil_object_t **out_function,
    int *out_has_statements,
    vigil_diagnostic_list_t *diagnostics,
    vigil_error_t *error
) {
    return vigil_compile_source_internal(
        registry,
        source_id,
        VIGIL_COMPILE_MODE_REPL,
        natives,
        out_function,
        out_has_statements,
        diagnostics,
        error
    );
}

/* ── Debug symbol table extraction ───────────────────────────────── */

static vigil_status_t vigil_compile_emit_symbols(
    const vigil_program_state_t *program,
    vigil_debug_symbol_table_t *out_symbols,
    vigil_error_t *error
) {
    vigil_status_t status;
    size_t i;
    size_t j;
    size_t class_sym_index;

    if (out_symbols == NULL) return VIGIL_STATUS_OK;

    /* Functions. */
    for (i = 0U; i < program->functions.count; i += 1U) {
        const vigil_binding_function_t *fn = &program->functions.functions[i];
        if (fn->is_local) continue;
        status = vigil_debug_symbol_table_add(
            out_symbols, VIGIL_DEBUG_SYMBOL_FUNCTION,
            fn->name, fn->name_length, fn->name_span,
            fn->is_public, SIZE_MAX, error
        );
        if (status != VIGIL_STATUS_OK) return status;
    }

    /* Classes with fields and methods. */
    for (i = 0U; i < program->class_count; i += 1U) {
        const vigil_class_decl_t *cls = &program->classes[i];
        if (cls->native_class != NULL) continue;
        class_sym_index = out_symbols->count;
        status = vigil_debug_symbol_table_add(
            out_symbols, VIGIL_DEBUG_SYMBOL_CLASS,
            cls->name, cls->name_length, cls->name_span,
            cls->is_public, SIZE_MAX, error
        );
        if (status != VIGIL_STATUS_OK) return status;
        for (j = 0U; j < cls->field_count; j += 1U) {
            const vigil_class_field_t *f = &cls->fields[j];
            status = vigil_debug_symbol_table_add(
                out_symbols, VIGIL_DEBUG_SYMBOL_FIELD,
                f->name, f->name_length, f->name_span,
                f->is_public, class_sym_index, error
            );
            if (status != VIGIL_STATUS_OK) return status;
        }
        for (j = 0U; j < cls->method_count; j += 1U) {
            const vigil_class_method_t *m = &cls->methods[j];
            status = vigil_debug_symbol_table_add(
                out_symbols, VIGIL_DEBUG_SYMBOL_METHOD,
                m->name, m->name_length, m->name_span,
                m->is_public, class_sym_index, error
            );
            if (status != VIGIL_STATUS_OK) return status;
        }
    }

    /* Interfaces. */
    for (i = 0U; i < program->interface_count; i += 1U) {
        const vigil_interface_decl_t *iface = &program->interfaces[i];
        status = vigil_debug_symbol_table_add(
            out_symbols, VIGIL_DEBUG_SYMBOL_INTERFACE,
            iface->name, iface->name_length, iface->name_span,
            iface->is_public, SIZE_MAX, error
        );
        if (status != VIGIL_STATUS_OK) return status;
    }

    /* Enums with members. */
    for (i = 0U; i < program->enum_count; i += 1U) {
        const vigil_enum_decl_t *en = &program->enums[i];
        size_t enum_sym_index = out_symbols->count;
        status = vigil_debug_symbol_table_add(
            out_symbols, VIGIL_DEBUG_SYMBOL_ENUM,
            en->name, en->name_length, en->name_span,
            en->is_public, SIZE_MAX, error
        );
        if (status != VIGIL_STATUS_OK) return status;
        for (j = 0U; j < en->member_count; j += 1U) {
            const vigil_enum_member_t *m = &en->members[j];
            status = vigil_debug_symbol_table_add(
                out_symbols, VIGIL_DEBUG_SYMBOL_ENUM_MEMBER,
                m->name, m->name_length, m->name_span,
                1, enum_sym_index, error
            );
            if (status != VIGIL_STATUS_OK) return status;
        }
    }

    /* Global constants. */
    for (i = 0U; i < program->constant_count; i += 1U) {
        const vigil_global_constant_t *c = &program->constants[i];
        status = vigil_debug_symbol_table_add(
            out_symbols, VIGIL_DEBUG_SYMBOL_GLOBAL_CONST,
            c->name, c->name_length, c->name_span,
            c->is_public, SIZE_MAX, error
        );
        if (status != VIGIL_STATUS_OK) return status;
    }

    /* Global variables. */
    for (i = 0U; i < program->global_count; i += 1U) {
        const vigil_global_variable_t *g = &program->globals[i];
        status = vigil_debug_symbol_table_add(
            out_symbols, VIGIL_DEBUG_SYMBOL_GLOBAL_VAR,
            g->name, g->name_length, g->name_span,
            g->is_public, SIZE_MAX, error
        );
        if (status != VIGIL_STATUS_OK) return status;
    }

    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_compile_source_with_debug_info(
    const vigil_source_registry_t *registry,
    vigil_source_id_t source_id,
    const vigil_native_registry_t *natives,
    vigil_object_t **out_function,
    vigil_diagnostic_list_t *diagnostics,
    vigil_debug_symbol_table_t *out_symbols,
    vigil_error_t *error
) {
    vigil_status_t status;
    vigil_program_state_t program;
    const vigil_source_file_t *source;

    vigil_error_clear(error);
    if (out_function != NULL) {
        *out_function = NULL;
    }

    status = vigil_compile_validate_inputs(
        registry, diagnostics, out_function,
        VIGIL_COMPILE_MODE_BUILD_ENTRYPOINT, error
    );
    if (status != VIGIL_STATUS_OK) return status;

    source = vigil_source_registry_get(registry, source_id);
    if (source == NULL) {
        vigil_error_set_literal(
            error, VIGIL_STATUS_INVALID_ARGUMENT,
            "source_id must reference a registered source file"
        );
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    memset(&program, 0, sizeof(program));
    program.registry = registry;
    program.diagnostics = diagnostics;
    program.error = error;
    program.natives = natives;
    vigil_binding_function_table_init(&program.functions, registry->runtime);
    vigil_program_set_module_context(&program, source, NULL);

    status = vigil_program_parse_source(&program, source_id);
    if (status != VIGIL_STATUS_OK) {
        vigil_program_free(&program);
        return status;
    }

    vigil_program_set_module_context(&program, source, NULL);
    if (!program.functions.has_main) {
        status = vigil_compile_report(
            &program, vigil_program_eof_span(&program),
            "expected top-level function 'main'"
        );
        vigil_program_free(&program);
        return status;
    }

    status = vigil_compile_all_functions(&program);
    if (status != VIGIL_STATUS_OK) {
        vigil_program_free(&program);
        return status;
    }

    /* Extract symbols before freeing the program state. */
    status = vigil_compile_emit_symbols(&program, out_symbols, error);
    if (status != VIGIL_STATUS_OK) {
        vigil_program_free(&program);
        return status;
    }

    status = vigil_compile_attach_entrypoint(&program, out_function);
    if (status != VIGIL_STATUS_OK) {
        vigil_program_free(&program);
        return status;
    }

    vigil_program_free(&program);
    return VIGIL_STATUS_OK;
}
