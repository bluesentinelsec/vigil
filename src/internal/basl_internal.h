#ifndef BASL_INTERNAL_H
#define BASL_INTERNAL_H

#include "basl/runtime.h"
#include "basl/value.h"

struct basl_runtime {
    basl_allocator_t allocator;
    basl_logger_t logger;
};

typedef struct basl_runtime_interface_impl_init {
    size_t interface_index;
    const size_t *function_indices;
    size_t function_count;
} basl_runtime_interface_impl_init_t;

typedef struct basl_runtime_class_init {
    const basl_runtime_interface_impl_init_t *interface_impls;
    size_t interface_impl_count;
} basl_runtime_class_init_t;

basl_allocator_t basl_default_allocator(void);
int basl_allocator_is_valid(const basl_allocator_t *allocator);
void basl_error_set_literal(
    basl_error_t *error,
    basl_status_t type,
    const char *value
);
basl_status_t basl_function_object_attach_siblings(
    basl_object_t *owner_function,
    basl_object_t **functions,
    size_t function_count,
    size_t owner_index,
    const basl_runtime_class_init_t *classes,
    size_t class_count,
    basl_error_t *error
);
const basl_object_t *basl_function_object_sibling(
    const basl_object_t *function,
    size_t index
);
const basl_object_t *basl_function_object_resolve_interface_method(
    const basl_object_t *function,
    size_t class_index,
    size_t interface_index,
    size_t method_index
);

#endif
