#include <stdlib.h>
#include <string.h>
#include "internal/basl_compiler_types.h"
#include "internal/basl_internal.h"

int basl_program_token_is_identifier_text(
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
basl_status_t basl_enum_decl_grow_members(
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
basl_status_t basl_interface_method_grow_params(
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

basl_status_t basl_interface_method_grow_returns(
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
basl_status_t basl_program_parse_interface_method_return_types(
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
basl_status_t basl_program_parse_global_variable_declaration(
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
        basl_program_find_top_level_function_name_in_source(
            program,
            program->source->id,
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
            "global variable name conflicts with interface"
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
            "global variable name conflicts with enum"
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
            "global variable name conflicts with class"
        );
    }
    if (
        basl_program_find_constant_in_source(
            program,
            program->source->id,
            name_text,
            name_length,
            &existing_constant
        )
    ) {
        return basl_compile_report(
            program,
            name_token->span,
            "global variable name conflicts with global constant"
        );
    }
    if (
        basl_program_find_global_in_source(
            program,
            program->source->id,
            name_text,
            name_length,
            NULL,
            &existing_global
        )
    ) {
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

basl_status_t basl_program_parse_constant_declaration(
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
        "global constants must use a primitive value type",
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
        basl_program_find_top_level_function_name_in_source(
            program,
            program->source->id,
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
            "global constant name conflicts with interface"
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
            "global constant name conflicts with enum"
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
            "global constant name conflicts with class"
        );
    }
    if (
        basl_program_find_constant_in_source(
            program,
            program->source->id,
            name_text,
            name_length,
            &existing_constant
        )
    ) {
        return basl_compile_report(
            program,
            name_token->span,
            "global constant is already declared"
        );
    }
    if (
        basl_program_find_global_in_source(
            program,
            program->source->id,
            name_text,
            name_length,
            NULL,
            &existing_global
        )
    ) {
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

basl_status_t basl_program_parse_enum_declaration(
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
        return basl_compile_report(program, name_token->span, "enum is already declared");
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
        return basl_compile_report(program, name_token->span, "enum name conflicts with class");
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
            "enum name conflicts with interface"
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
            "enum name conflicts with global constant"
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
            "enum name conflicts with global variable"
        );
    }
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

basl_status_t basl_program_parse_interface_declaration(
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
        return basl_compile_report(program, name_token->span, "interface is already declared");
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
        return basl_compile_report(program, name_token->span, "interface name conflicts with class");
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
            "interface name conflicts with global constant"
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
        return basl_compile_report(program, name_token->span, "interface name conflicts with enum");
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
            "interface name conflicts with global variable"
        );
    }
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

basl_status_t basl_program_validate_class_interface_conformance(
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

basl_status_t basl_program_synthesize_class_constructor(
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

basl_status_t basl_program_parse_class_declaration(
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
        return basl_compile_report(program, name_token->span, "class is already declared");
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
            "class name conflicts with global constant"
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
        return basl_compile_report(program, name_token->span, "class name conflicts with enum");
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
            "class name conflicts with global variable"
        );
    }
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
        return basl_compile_report(program, name_token->span, "class name conflicts with function");
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
