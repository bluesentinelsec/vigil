#ifndef BASL_COMPILER_TYPES_H
#define BASL_COMPILER_TYPES_H

#include "basl/chunk.h"
#include "basl/diagnostic.h"
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

#endif
