#ifndef BASL_COMPILER_TYPES_H
#define BASL_COMPILER_TYPES_H

#include "basl/chunk.h"
#include "basl/diagnostic.h"
#include "basl/native_module.h"
#include "basl/source.h"
#include "basl/token.h"
#include "basl/value.h"
#include "basl_binding.h"

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
    const basl_native_class_t *native_class;  /* non-NULL = native */
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
    const struct basl_native_registry *natives;
    int compile_mode;
    size_t repl_stmts_start;
    size_t repl_stmts_end;
    int repl_has_statements;
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
    basl_parser_type_t owned_types[2];
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

/* --- compiler_types.c function declarations --- */

int basl_parser_type_is_primitive(basl_parser_type_t type);
int basl_parser_type_is_class(basl_parser_type_t type);
int basl_parser_type_is_interface(basl_parser_type_t type);
int basl_parser_type_is_enum(basl_parser_type_t type);
int basl_parser_type_is_array(basl_parser_type_t type);
int basl_parser_type_is_map(basl_parser_type_t type);
int basl_parser_type_supports_map_key(basl_parser_type_t type);
int basl_parser_type_is_function(basl_parser_type_t type);
int basl_parser_type_is_void(basl_parser_type_t type);
int basl_parser_type_is_i32(basl_parser_type_t type);
int basl_parser_type_is_i64(basl_parser_type_t type);
int basl_parser_type_is_u8(basl_parser_type_t type);
int basl_parser_type_is_u32(basl_parser_type_t type);
int basl_parser_type_is_u64(basl_parser_type_t type);
int basl_parser_type_is_integer(basl_parser_type_t type);
int basl_parser_type_is_signed_integer(basl_parser_type_t type);
int basl_parser_type_is_unsigned_integer(basl_parser_type_t type);
int basl_parser_type_is_f64(basl_parser_type_t type);
int basl_parser_type_is_bool(basl_parser_type_t type);
int basl_parser_type_is_string(basl_parser_type_t type);
int basl_parser_type_is_err(basl_parser_type_t type);
basl_parser_type_t basl_function_return_type_at(
    const basl_function_decl_t *decl, size_t index);
const basl_parser_type_t *basl_function_return_types(
    const basl_function_decl_t *decl);
basl_parser_type_t basl_interface_method_return_type_at(
    const basl_interface_method_t *method, size_t index);
const basl_parser_type_t *basl_interface_method_return_types(
    const basl_interface_method_t *method);
int basl_parser_type_equal(
    basl_parser_type_t left, basl_parser_type_t right);
int basl_program_type_is_assignable(
    const basl_program_state_t *program,
    basl_parser_type_t target_type, basl_parser_type_t source_type);
int basl_parser_type_supports_unary_operator(
    basl_unary_operator_kind_t operator_kind,
    basl_parser_type_t operand_type);
int basl_parser_type_supports_binary_operator(
    basl_binary_operator_kind_t operator_kind,
    basl_parser_type_t left_type, basl_parser_type_t right_type);

const basl_function_type_decl_t *basl_program_function_type_decl(
    const basl_program_state_t *program, basl_parser_type_t type);

int basl_class_decl_implements_interface(
    const basl_class_decl_t *decl, size_t interface_index);


/* compiler_program.c — grow/init/intern/free helpers */
void basl_statement_result_clear(
    basl_statement_result_t *result
);
void basl_statement_result_set_guaranteed_return(
    basl_statement_result_t *result,
    int guaranteed_return
);
void basl_program_set_module_context(
    basl_program_state_t *program,
    const basl_source_file_t *source,
    const basl_token_list_t *tokens
);
basl_status_t basl_program_grow_modules(
    basl_program_state_t *program,
    size_t minimum_capacity
);
void basl_program_import_default_alias(
    const char *path,
    size_t path_length,
    const char **out_alias,
    size_t *out_alias_length
);
void basl_class_decl_free(
    basl_program_state_t *program,
    basl_class_decl_t *decl
);
basl_status_t basl_program_grow_classes(
    basl_program_state_t *program,
    size_t minimum_capacity
);
void basl_interface_decl_free(
    basl_program_state_t *program,
    basl_interface_decl_t *decl
);
basl_status_t basl_program_grow_interfaces(
    basl_program_state_t *program,
    size_t minimum_capacity
);
void basl_enum_decl_free(
    basl_program_state_t *program,
    basl_enum_decl_t *decl
);
basl_status_t basl_program_grow_enums(
    basl_program_state_t *program,
    size_t minimum_capacity
);
basl_status_t basl_program_grow_array_types(
    basl_program_state_t *program,
    size_t minimum_capacity
);
int basl_program_find_array_type(
    const basl_program_state_t *program,
    basl_parser_type_t element_type,
    size_t *out_index
);
basl_status_t basl_program_intern_array_type(
    basl_program_state_t *program,
    basl_parser_type_t element_type,
    basl_parser_type_t *out_type
);
basl_status_t basl_program_grow_map_types(
    basl_program_state_t *program,
    size_t minimum_capacity
);
int basl_program_find_map_type(
    const basl_program_state_t *program,
    basl_parser_type_t key_type,
    basl_parser_type_t value_type,
    size_t *out_index
);
basl_status_t basl_program_intern_map_type(
    basl_program_state_t *program,
    basl_parser_type_t key_type,
    basl_parser_type_t value_type,
    basl_parser_type_t *out_type
);
void basl_function_type_decl_free(
    basl_program_state_t *program,
    basl_function_type_decl_t *decl
);
basl_status_t basl_function_type_decl_grow_params(
    basl_program_state_t *program,
    basl_function_type_decl_t *decl,
    size_t minimum_capacity
);
basl_status_t basl_function_type_decl_add_param(
    basl_program_state_t *program,
    basl_function_type_decl_t *decl,
    basl_parser_type_t type
);
basl_status_t basl_function_type_decl_grow_returns(
    basl_program_state_t *program,
    basl_function_type_decl_t *decl,
    size_t minimum_capacity
);
basl_status_t basl_function_type_decl_add_return(
    basl_program_state_t *program,
    basl_function_type_decl_t *decl,
    basl_parser_type_t type
);
basl_status_t basl_program_grow_function_types(
    basl_program_state_t *program,
    size_t minimum_capacity
);
int basl_program_find_function_type(
    const basl_program_state_t *program,
    const basl_function_type_decl_t *needle,
    size_t *out_index
);
basl_status_t basl_program_intern_function_type(
    basl_program_state_t *program,
    const basl_function_type_decl_t *decl,
    basl_parser_type_t *out_type
);
basl_status_t basl_program_intern_function_type_from_decl(
    basl_program_state_t *program,
    const basl_function_decl_t *decl,
    basl_parser_type_t *out_type
);
basl_status_t basl_class_decl_grow_fields(
    basl_program_state_t *program,
    basl_class_decl_t *decl,
    size_t minimum_capacity
);
basl_status_t basl_class_decl_grow_methods(
    basl_program_state_t *program,
    basl_class_decl_t *decl,
    size_t minimum_capacity
);
basl_status_t basl_class_decl_grow_implemented_interfaces(
    basl_program_state_t *program,
    basl_class_decl_t *decl,
    size_t minimum_capacity
);
basl_status_t basl_class_decl_grow_interface_impls(
    basl_program_state_t *program,
    basl_class_decl_t *decl,
    size_t minimum_capacity
);
basl_status_t basl_interface_decl_grow_methods(
    basl_program_state_t *program,
    basl_interface_decl_t *decl,
    size_t minimum_capacity
);
basl_status_t basl_program_grow_constants(
    basl_program_state_t *program,
    size_t minimum_capacity
);
basl_status_t basl_program_grow_globals(
    basl_program_state_t *program,
    size_t minimum_capacity
);
void basl_program_trim_text_range(
    const char *text,
    size_t start,
    size_t end,
    size_t *out_start,
    size_t *out_end
);
basl_status_t basl_program_grow_functions(
    basl_program_state_t *program,
    size_t minimum_capacity
);
void basl_program_free(basl_program_state_t *program);

/* compiler.c — shared helpers used by extracted modules */
void basl_expression_result_clear(basl_expression_result_t *result);
void basl_expression_result_set_type(
    basl_expression_result_t *result,
    basl_parser_type_t type);
basl_status_t basl_parser_emit_f64_constant(
    basl_parser_state_t *state, double value, basl_source_span_t span);
basl_status_t basl_parser_emit_integer_constant(
    basl_parser_state_t *state, basl_parser_type_t target_type,
    int64_t value, basl_source_span_t span);
basl_status_t basl_parser_emit_ok_constant(
    basl_parser_state_t *state, basl_source_span_t span);
basl_status_t basl_parser_emit_opcode(
    basl_parser_state_t *state, basl_opcode_t opcode,
    basl_source_span_t span);
basl_status_t basl_parser_emit_string_constant_text(
    basl_parser_state_t *state, basl_source_span_t span,
    const char *text, size_t length);
basl_status_t basl_parser_expect(
    basl_parser_state_t *state, basl_token_kind_t kind,
    const char *message, const basl_token_t **out_token);
const char *basl_parser_token_text(
    const basl_parser_state_t *state, const basl_token_t *token,
    size_t *out_length);
int basl_program_names_equal(
    const char *left, size_t left_length,
    const char *right, size_t right_length);
void basl_expression_result_set_pair(
    basl_expression_result_t *result,
    basl_parser_type_t first_type, basl_parser_type_t second_type);
basl_status_t basl_parser_parse_expression(
    basl_parser_state_t *state,
    basl_expression_result_t *out_result);
basl_status_t basl_parser_report(
    basl_parser_state_t *state, basl_source_span_t span,
    const char *message);
basl_status_t basl_parser_require_scalar_expression(
    basl_parser_state_t *state, basl_source_span_t span,
    const basl_expression_result_t *result, const char *message);
basl_status_t basl_parser_require_type(
    basl_parser_state_t *state, basl_source_span_t span,
    basl_parser_type_t actual_type, basl_parser_type_t expected_type,
    const char *message);
basl_parser_type_t basl_program_array_type_element(
    const basl_program_state_t *program, basl_parser_type_t array_type);
basl_parser_type_t basl_program_map_type_key(
    const basl_program_state_t *program, basl_parser_type_t map_type);
basl_parser_type_t basl_program_map_type_value(
    const basl_program_state_t *program, basl_parser_type_t map_type);
const basl_token_t *basl_parser_peek(const basl_parser_state_t *state);
int basl_parser_check(
    const basl_parser_state_t *state, basl_token_kind_t kind);
basl_status_t basl_parser_emit_u32(
    basl_parser_state_t *state, uint32_t value, basl_source_span_t span);
basl_status_t basl_compile_report(
    const basl_program_state_t *program, basl_source_span_t span,
    const char *message);
const basl_token_t *basl_program_token_at(
    const basl_program_state_t *program, size_t index);

/* compiler_typeparsing.c — type reference parsing */
extern int basl_type_close_pending;
int basl_program_consume_type_close(
    const basl_program_state_t *program, size_t *cursor);
basl_status_t basl_program_parse_type_reference(
    const basl_program_state_t *program, size_t *cursor,
    const char *unsupported_message, basl_parser_type_t *out_type);
basl_status_t basl_program_parse_primitive_type_reference(
    const basl_program_state_t *program, size_t *cursor,
    const char *unsupported_message, basl_parser_type_t *out_type);
int basl_program_skip_type_reference_syntax(
    const basl_program_state_t *program, size_t *cursor);
basl_source_span_t basl_program_eof_span(
    const basl_program_state_t *program);
int basl_program_find_class_in_source(
    const basl_program_state_t *program, basl_source_id_t source_id,
    const char *name, size_t name_length,
    size_t *out_index, const basl_class_decl_t **out_class);
int basl_program_find_enum_in_source(
    const basl_program_state_t *program, basl_source_id_t source_id,
    const char *name, size_t name_length,
    size_t *out_index, const basl_enum_decl_t **out_decl);
int basl_program_find_interface_in_source(
    const basl_program_state_t *program, basl_source_id_t source_id,
    const char *name, size_t name_length,
    size_t *out_index, const basl_interface_decl_t **out_interface);
int basl_program_is_class_public(const basl_class_decl_t *decl);
int basl_program_is_enum_public(const basl_enum_decl_t *decl);
int basl_program_is_interface_public(const basl_interface_decl_t *decl);
basl_status_t basl_program_require_non_void_type(
    const basl_program_state_t *program, basl_source_span_t span,
    basl_parser_type_t type, const char *message);
int basl_program_resolve_import_alias(
    const basl_program_state_t *program, const char *alias,
    size_t alias_length, basl_source_id_t *out_source_id);
const char *basl_program_token_text(
    const basl_program_state_t *program, const basl_token_t *token,
    size_t *out_length);

/* compiler_declarations.c — declaration parsing */
void basl_constant_result_clear(basl_constant_result_t *result);
const basl_token_t *basl_program_cursor_advance(
    const basl_program_state_t *program, size_t *cursor);
const basl_token_t *basl_program_cursor_peek(
    const basl_program_state_t *program, size_t cursor);
int basl_program_find_constant_in_source(
    const basl_program_state_t *program, basl_source_id_t source_id,
    const char *name, size_t name_length,
    const basl_global_constant_t **out_constant);
int basl_program_find_global_in_source(
    const basl_program_state_t *program, basl_source_id_t source_id,
    const char *name, size_t name_length,
    size_t *out_index, const basl_global_variable_t **out_global);
int basl_program_find_top_level_function_name_in_source(
    const basl_program_state_t *program, basl_source_id_t source_id,
    const char *name_text, size_t name_length,
    size_t *out_index, const basl_function_decl_t **out_decl);
int basl_class_decl_find_field(
    const basl_class_decl_t *decl, const char *name, size_t name_length,
    size_t *out_index, const basl_class_field_t **out_field);
int basl_class_decl_find_method(
    const basl_class_decl_t *decl, const char *name, size_t name_length,
    size_t *out_index, const basl_class_method_t **out_method);
void basl_constant_result_release(basl_constant_result_t *result);
int basl_enum_decl_find_member(
    const basl_enum_decl_t *decl, const char *name, size_t name_length,
    size_t *out_index, const basl_enum_member_t **out_member);
int basl_interface_decl_find_method(
    const basl_interface_decl_t *decl, const char *name, size_t name_length,
    size_t *out_index, const basl_interface_method_t **out_method);
basl_status_t basl_program_add_param(
    basl_program_state_t *program, basl_function_decl_t *decl,
    basl_parser_type_t type, const basl_token_t *name_token);
basl_status_t basl_program_parse_constant_expression(
    basl_program_state_t *program, size_t *cursor,
    basl_constant_result_t *out_result);
basl_status_t basl_program_parse_function_return_types(
    basl_program_state_t *program, size_t *cursor,
    const char *unsupported_message, basl_function_decl_t *decl);
int basl_program_parse_optional_pub(
    const basl_program_state_t *program, size_t *cursor);
basl_status_t basl_program_parse_global_variable_declaration(
    basl_program_state_t *program, size_t *cursor, int is_public);
basl_status_t basl_program_parse_constant_declaration(
    basl_program_state_t *program, size_t *cursor, int is_public);
basl_status_t basl_program_parse_enum_declaration(
    basl_program_state_t *program, size_t *cursor, int is_public);
basl_status_t basl_program_parse_interface_declaration(
    basl_program_state_t *program, size_t *cursor, int is_public);
basl_status_t basl_program_parse_class_declaration(
    basl_program_state_t *program, size_t *cursor, int is_public);

/* compiler_strings.c — string/f-string parsing */
basl_status_t basl_parser_parse_fstring_literal(
    basl_parser_state_t *state, const basl_token_t *token,
    basl_expression_result_t *out_result);

/* compiler_builtins.c — built-in method dispatch */
basl_status_t basl_parser_emit_default_value(
    basl_parser_state_t *state,
    basl_parser_type_t type,
    basl_source_span_t span);
basl_status_t basl_parser_parse_string_method_call(
    basl_parser_state_t *state,
    const basl_token_t *method_token,
    basl_expression_result_t *out_result);
basl_status_t basl_parser_parse_array_method_call(
    basl_parser_state_t *state,
    basl_parser_type_t receiver_type,
    const basl_token_t *method_token,
    basl_expression_result_t *out_result);
basl_status_t basl_parser_parse_map_method_call(
    basl_parser_state_t *state,
    basl_parser_type_t receiver_type,
    const basl_token_t *method_token,
    basl_expression_result_t *out_result);

#endif
