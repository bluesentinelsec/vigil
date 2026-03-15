#ifndef BASL_VM_H
#define BASL_VM_H

#include <stddef.h>

#include "basl/chunk.h"
#include "basl/export.h"
#include "basl/runtime.h"
#include "basl/status.h"
#include "basl/value.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct basl_vm_options {
    size_t initial_stack_capacity;
} basl_vm_options_t;

typedef struct basl_vm basl_vm_t;

BASL_API void basl_vm_options_init(basl_vm_options_t *options);
BASL_API basl_status_t basl_vm_open(
    basl_vm_t **out_vm,
    basl_runtime_t *runtime,
    const basl_vm_options_t *options,
    basl_error_t *error
);
BASL_API void basl_vm_close(basl_vm_t **vm);
BASL_API basl_runtime_t *basl_vm_runtime(const basl_vm_t *vm);
BASL_API size_t basl_vm_stack_depth(const basl_vm_t *vm);
BASL_API size_t basl_vm_frame_depth(const basl_vm_t *vm);
/*
 * out_value must not be null and should be initialized to nil before reuse if
 * it may already own an object reference.
 */
BASL_API basl_status_t basl_vm_execute(
    basl_vm_t *vm,
    const basl_chunk_t *chunk,
    basl_value_t *out_value,
    basl_error_t *error
);
BASL_API basl_status_t basl_vm_execute_function(
    basl_vm_t *vm,
    const basl_object_t *function,
    basl_value_t *out_value,
    basl_error_t *error
);

/* Stack access for native function implementations. */
BASL_API basl_value_t basl_vm_stack_get(const basl_vm_t *vm, size_t index);
BASL_API basl_status_t basl_vm_stack_push(
    basl_vm_t *vm, const basl_value_t *value, basl_error_t *error);
BASL_API void basl_vm_stack_pop_n(basl_vm_t *vm, size_t count);

/* Debug hook — set by debugger, NULL when not debugging. */
BASL_API void basl_vm_set_debug_hook(
    basl_vm_t *vm,
    int (*hook)(basl_vm_t *vm, void *userdata),
    void *userdata
);

BASL_API void basl_vm_set_args(
    basl_vm_t *vm,
    const char *const *argv,
    size_t argc
);

BASL_API void basl_vm_get_args(
    const basl_vm_t *vm,
    const char *const **out_argv,
    size_t *out_argc
);

/* Frame inspection for debugger. */
BASL_API const basl_chunk_t *basl_vm_frame_chunk(
    const basl_vm_t *vm, size_t frame_index);
BASL_API size_t basl_vm_frame_ip(
    const basl_vm_t *vm, size_t frame_index);
BASL_API size_t basl_vm_frame_base_slot(
    const basl_vm_t *vm, size_t frame_index);
BASL_API const basl_object_t *basl_vm_frame_function(
    const basl_vm_t *vm, size_t frame_index);

#ifdef __cplusplus
}
#endif

#endif
