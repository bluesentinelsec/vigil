#ifndef BASL_BINDING_H
#define BASL_BINDING_H

#include <stddef.h>

#include "basl/runtime.h"
#include "basl/source.h"
#include "basl/status.h"
#include "basl/type.h"
#include "basl/value.h"

typedef struct basl_binding_function_param {
    const char *name;
    size_t length;
    basl_type_kind_t type;
    basl_source_span_t span;
} basl_binding_function_param_t;

typedef struct basl_binding_function {
    const char *name;
    size_t name_length;
    basl_source_span_t name_span;
    basl_type_kind_t return_type;
    basl_binding_function_param_t *params;
    size_t param_count;
    size_t param_capacity;
    size_t body_start;
    size_t body_end;
    basl_object_t *object;
} basl_binding_function_t;

typedef struct basl_binding_function_table {
    basl_runtime_t *runtime;
    basl_binding_function_t *functions;
    size_t count;
    size_t capacity;
    size_t main_index;
    int has_main;
} basl_binding_function_table_t;

typedef struct basl_binding_local {
    const char *name;
    size_t length;
    size_t depth;
    basl_type_kind_t type;
} basl_binding_local_t;

typedef struct basl_binding_scope_stack {
    basl_runtime_t *runtime;
    basl_binding_local_t *locals;
    size_t local_count;
    size_t local_capacity;
    size_t scope_depth;
} basl_binding_scope_stack_t;

void basl_binding_function_init(basl_binding_function_t *function);
void basl_binding_function_free(
    basl_runtime_t *runtime,
    basl_binding_function_t *function
);
basl_status_t basl_binding_function_add_param(
    basl_runtime_t *runtime,
    basl_binding_function_t *function,
    const char *name,
    size_t name_length,
    basl_source_span_t span,
    basl_type_kind_t type,
    basl_error_t *error
);

void basl_binding_function_table_init(
    basl_binding_function_table_t *table,
    basl_runtime_t *runtime
);
void basl_binding_function_table_free(basl_binding_function_table_t *table);
basl_status_t basl_binding_function_table_append(
    basl_binding_function_table_t *table,
    basl_binding_function_t *function,
    size_t *out_index,
    basl_error_t *error
);
int basl_binding_function_table_find(
    const basl_binding_function_table_t *table,
    const char *name,
    size_t name_length,
    size_t *out_index,
    const basl_binding_function_t **out_function
);
const basl_binding_function_t *basl_binding_function_table_get(
    const basl_binding_function_table_t *table,
    size_t index
);
basl_binding_function_t *basl_binding_function_table_get_mutable(
    basl_binding_function_table_t *table,
    size_t index
);

void basl_binding_scope_stack_init(
    basl_binding_scope_stack_t *stack,
    basl_runtime_t *runtime
);
void basl_binding_scope_stack_free(basl_binding_scope_stack_t *stack);
void basl_binding_scope_stack_begin_scope(basl_binding_scope_stack_t *stack);
void basl_binding_scope_stack_end_scope(
    basl_binding_scope_stack_t *stack,
    size_t *out_popped_count
);
size_t basl_binding_scope_stack_depth(const basl_binding_scope_stack_t *stack);
size_t basl_binding_scope_stack_count(const basl_binding_scope_stack_t *stack);
size_t basl_binding_scope_stack_count_above_depth(
    const basl_binding_scope_stack_t *stack,
    size_t target_depth
);
basl_status_t basl_binding_scope_stack_declare_local(
    basl_binding_scope_stack_t *stack,
    const char *name,
    size_t name_length,
    basl_type_kind_t type,
    size_t *out_index,
    basl_error_t *error
);
int basl_binding_scope_stack_find_local(
    const basl_binding_scope_stack_t *stack,
    const char *name,
    size_t name_length,
    size_t *out_index,
    basl_type_kind_t *out_type
);
const basl_binding_local_t *basl_binding_scope_stack_local_at(
    const basl_binding_scope_stack_t *stack,
    size_t index
);

#endif
