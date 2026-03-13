#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/vm.h"

typedef struct basl_vm_frame {
    const basl_object_t *function;
    const basl_chunk_t *chunk;
    size_t ip;
    size_t base_slot;
    struct basl_vm_defer_action *defers;
    size_t defer_count;
    size_t defer_capacity;
    basl_value_t pending_return;
    int has_pending_return;
    int draining_defers;
} basl_vm_frame_t;

typedef enum basl_vm_defer_kind {
    BASL_VM_DEFER_CALL = 0,
    BASL_VM_DEFER_NEW_INSTANCE = 1,
    BASL_VM_DEFER_CALL_INTERFACE = 2
} basl_vm_defer_kind_t;

typedef struct basl_vm_defer_action {
    basl_vm_defer_kind_t kind;
    uint32_t operand_a;
    uint32_t operand_b;
    uint32_t arg_count;
    basl_value_t *values;
    size_t value_count;
} basl_vm_defer_action_t;

struct basl_vm {
    basl_runtime_t *runtime;
    basl_value_t *stack;
    size_t stack_count;
    size_t stack_capacity;
    basl_vm_frame_t *frames;
    size_t frame_count;
    size_t frame_capacity;
};

static basl_status_t basl_vm_fail_at_ip(
    basl_vm_t *vm,
    basl_status_t status,
    const char *message,
    basl_error_t *error
);
static basl_value_t basl_vm_pop_or_nil(basl_vm_t *vm);
static void basl_vm_defer_action_clear(
    basl_runtime_t *runtime,
    basl_vm_defer_action_t *action
);
static basl_status_t basl_vm_complete_return(
    basl_vm_t *vm,
    basl_value_t returned_value,
    basl_value_t *out_value,
    basl_error_t *error
);

static basl_status_t basl_vm_validate(
    const basl_vm_t *vm,
    basl_error_t *error
) {
    basl_error_clear(error);

    if (vm == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "vm must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (vm->runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "vm runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return BASL_STATUS_OK;
}

static void basl_vm_release_stack(basl_vm_t *vm) {
    size_t i;

    if (vm == NULL) {
        return;
    }

    for (i = 0U; i < vm->stack_count; ++i) {
        basl_value_release(&vm->stack[i]);
    }

    vm->stack_count = 0U;
}

static void basl_vm_defer_action_clear(
    basl_runtime_t *runtime,
    basl_vm_defer_action_t *action
) {
    size_t i;
    void *memory;

    if (action == NULL) {
        return;
    }

    for (i = 0U; i < action->value_count; i += 1U) {
        basl_value_release(&action->values[i]);
    }
    memory = action->values;
    if (runtime != NULL) {
        basl_runtime_free(runtime, &memory);
    }
    memset(action, 0, sizeof(*action));
}

static void basl_vm_frame_clear(basl_runtime_t *runtime, basl_vm_frame_t *frame) {
    size_t i;
    void *memory;

    if (frame == NULL) {
        return;
    }

    for (i = 0U; i < frame->defer_count; i += 1U) {
        basl_vm_defer_action_clear(runtime, &frame->defers[i]);
    }
    memory = frame->defers;
    if (runtime != NULL) {
        basl_runtime_free(runtime, &memory);
    }
    basl_value_release(&frame->pending_return);
    memset(frame, 0, sizeof(*frame));
}

static void basl_vm_clear_frames(basl_vm_t *vm) {
    size_t i;

    if (vm == NULL) {
        return;
    }

    for (i = 0U; i < vm->frame_count; i += 1U) {
        basl_vm_frame_clear(vm->runtime, &vm->frames[i]);
    }
    vm->frame_count = 0U;
}

static void basl_vm_unwind_stack_to(basl_vm_t *vm, size_t target_count) {
    basl_value_t value;

    if (vm == NULL) {
        return;
    }

    while (vm->stack_count > target_count) {
        value = basl_vm_pop_or_nil(vm);
        basl_value_release(&value);
    }
}

static basl_status_t basl_vm_grow_stack(
    basl_vm_t *vm,
    size_t minimum_capacity,
    basl_error_t *error
) {
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    basl_status_t status;

    if (minimum_capacity <= vm->stack_capacity) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    old_capacity = vm->stack_capacity;
    next_capacity = old_capacity == 0U ? 16U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > (SIZE_MAX / 2U)) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*vm->stack))) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "vm stack allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    if (vm->stack == NULL) {
        memory = NULL;
        status = basl_runtime_alloc(
            vm->runtime,
            next_capacity * sizeof(*vm->stack),
            &memory,
            error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        memory = vm->stack;
        status = basl_runtime_realloc(
            vm->runtime,
            &memory,
            next_capacity * sizeof(*vm->stack),
            error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        memset(
            (basl_value_t *)memory + old_capacity,
            0,
            (next_capacity - old_capacity) * sizeof(*vm->stack)
        );
    }

    vm->stack = (basl_value_t *)memory;
    vm->stack_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_vm_grow_frames(
    basl_vm_t *vm,
    size_t minimum_capacity,
    basl_error_t *error
) {
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    basl_status_t status;

    if (minimum_capacity <= vm->frame_capacity) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    old_capacity = vm->frame_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > (SIZE_MAX / 2U)) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*vm->frames))) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "vm frame allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    if (vm->frames == NULL) {
        memory = NULL;
        status = basl_runtime_alloc(
            vm->runtime,
            next_capacity * sizeof(*vm->frames),
            &memory,
            error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    } else {
        memory = vm->frames;
        status = basl_runtime_realloc(
            vm->runtime,
            &memory,
            next_capacity * sizeof(*vm->frames),
            error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        memset(
            (basl_vm_frame_t *)memory + old_capacity,
            0,
            (next_capacity - old_capacity) * sizeof(*vm->frames)
        );
    }

    vm->frames = (basl_vm_frame_t *)memory;
    vm->frame_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_vm_frame_t *basl_vm_current_frame(basl_vm_t *vm) {
    if (vm == NULL || vm->frame_count == 0U) {
        return NULL;
    }

    return &vm->frames[vm->frame_count - 1U];
}

static basl_status_t basl_vm_push_frame(
    basl_vm_t *vm,
    const basl_object_t *function,
    const basl_chunk_t *chunk,
    size_t base_slot,
    basl_error_t *error
) {
    basl_status_t status;
    basl_vm_frame_t *frame;

    if (vm->frame_count == SIZE_MAX) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "vm frame stack overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    status = basl_vm_grow_frames(vm, vm->frame_count + 1U, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    frame = &vm->frames[vm->frame_count];
    memset(frame, 0, sizeof(*frame));
    frame->function = function;
    frame->chunk = chunk;
    frame->ip = 0U;
    frame->base_slot = base_slot;
    basl_value_init_nil(&frame->pending_return);
    vm->frame_count += 1U;
    return BASL_STATUS_OK;
}

static basl_status_t basl_vm_push(
    basl_vm_t *vm,
    const basl_value_t *value,
    basl_error_t *error
) {
    basl_status_t status;

    if (vm->stack_count == SIZE_MAX) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "vm stack overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    status = basl_vm_grow_stack(vm, vm->stack_count + 1U, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    vm->stack[vm->stack_count] = basl_value_copy(value);
    vm->stack_count += 1U;
    return BASL_STATUS_OK;
}

static basl_value_t basl_vm_pop_or_nil(basl_vm_t *vm) {
    basl_value_t value;

    basl_value_init_nil(&value);
    if (vm == NULL || vm->stack_count == 0U) {
        return value;
    }

    value = vm->stack[vm->stack_count - 1U];
    basl_value_init_nil(&vm->stack[vm->stack_count - 1U]);
    vm->stack_count -= 1U;
    return value;
}

static const basl_value_t *basl_vm_peek(
    const basl_vm_t *vm,
    size_t distance
) {
    if (vm == NULL || distance >= vm->stack_count) {
        return NULL;
    }

    return &vm->stack[vm->stack_count - 1U - distance];
}

static basl_status_t basl_vm_validate_local_slot(
    basl_vm_t *vm,
    uint32_t slot_index,
    size_t *out_index,
    basl_error_t *error
) {
    basl_vm_frame_t *frame;
    size_t index;

    frame = basl_vm_current_frame(vm);
    if (frame == NULL) {
        return basl_vm_fail_at_ip(
            vm,
            BASL_STATUS_INTERNAL,
            "vm frame is missing",
            error
        );
    }

    index = frame->base_slot + (size_t)slot_index;
    if (index >= vm->stack_count) {
        return basl_vm_fail_at_ip(
            vm,
            BASL_STATUS_INTERNAL,
            "local slot out of range",
            error
        );
    }

    if (out_index != NULL) {
        *out_index = index;
    }
    return BASL_STATUS_OK;
}

static basl_status_t basl_vm_frame_grow_defers(
    basl_vm_t *vm,
    basl_vm_frame_t *frame,
    size_t minimum_capacity,
    basl_error_t *error
) {
    size_t old_capacity;
    size_t next_capacity;
    void *memory;
    basl_status_t status;

    if (frame == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "vm frame must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (minimum_capacity <= frame->defer_capacity) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    old_capacity = frame->defer_capacity;
    next_capacity = old_capacity == 0U ? 4U : old_capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > SIZE_MAX / 2U) {
            next_capacity = minimum_capacity;
            break;
        }
        next_capacity *= 2U;
    }
    if (next_capacity > SIZE_MAX / sizeof(*frame->defers)) {
        basl_error_set_literal(error, BASL_STATUS_OUT_OF_MEMORY, "vm defer allocation overflow");
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    if (frame->defers == NULL) {
        memory = NULL;
        status = basl_runtime_alloc(
            vm->runtime,
            next_capacity * sizeof(*frame->defers),
            &memory,
            error
        );
    } else {
        memory = frame->defers;
        status = basl_runtime_realloc(
            vm->runtime,
            &memory,
            next_capacity * sizeof(*frame->defers),
            error
        );
        if (status == BASL_STATUS_OK) {
            memset(
                (basl_vm_defer_action_t *)memory + old_capacity,
                0,
                (next_capacity - old_capacity) * sizeof(*frame->defers)
            );
        }
    }
    if (status != BASL_STATUS_OK) {
        return status;
    }

    frame->defers = (basl_vm_defer_action_t *)memory;
    frame->defer_capacity = next_capacity;
    return BASL_STATUS_OK;
}

static basl_status_t basl_vm_copy_values(
    basl_vm_t *vm,
    const basl_value_t *values,
    size_t value_count,
    basl_value_t **out_values,
    basl_error_t *error
) {
    basl_status_t status;
    void *memory;
    size_t i;

    if (out_values == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "out_values must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    *out_values = NULL;
    if (value_count == 0U) {
        return BASL_STATUS_OK;
    }

    if (value_count > SIZE_MAX / sizeof(*values)) {
        basl_error_set_literal(error, BASL_STATUS_OUT_OF_MEMORY, "vm defer value allocation overflow");
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memory = NULL;
    status = basl_runtime_alloc(
        vm->runtime,
        value_count * sizeof(*values),
        &memory,
        error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    *out_values = (basl_value_t *)memory;
    for (i = 0U; i < value_count; i += 1U) {
        (*out_values)[i] = basl_value_copy(&values[i]);
    }
    return BASL_STATUS_OK;
}

static basl_status_t basl_vm_invoke_call(
    basl_vm_t *vm,
    basl_vm_frame_t *frame,
    size_t function_index,
    size_t arg_count,
    basl_error_t *error
) {
    const basl_object_t *callee;
    size_t base_slot;

    if (frame == NULL || frame->function == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INTERNAL,
            "call requires a function-backed frame"
        );
        return BASL_STATUS_INTERNAL;
    }

    callee = basl_function_object_sibling(frame->function, function_index);
    if (callee == NULL || basl_object_type(callee) != BASL_OBJECT_FUNCTION) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "call target is invalid");
        return BASL_STATUS_INTERNAL;
    }
    if (basl_function_object_arity(callee) != arg_count) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "call arity does not match function signature"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (arg_count > vm->stack_count) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INTERNAL,
            "call arguments are missing from the stack"
        );
        return BASL_STATUS_INTERNAL;
    }

    base_slot = vm->stack_count - arg_count;
    return basl_vm_push_frame(
        vm,
        callee,
        basl_function_object_chunk(callee),
        base_slot,
        error
    );
}

static basl_status_t basl_vm_invoke_interface_call(
    basl_vm_t *vm,
    basl_vm_frame_t *frame,
    size_t interface_index,
    size_t method_index,
    size_t arg_count,
    basl_error_t *error
) {
    const basl_object_t *callee;
    const basl_value_t *receiver;
    size_t base_slot;
    size_t class_index;

    if (arg_count + 1U > vm->stack_count) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INTERNAL,
            "call arguments are missing from the stack"
        );
        return BASL_STATUS_INTERNAL;
    }

    base_slot = vm->stack_count - (arg_count + 1U);
    receiver = &vm->stack[base_slot];
    if (
        basl_value_kind(receiver) != BASL_VALUE_OBJECT ||
        basl_object_type(basl_value_as_object(receiver)) != BASL_OBJECT_INSTANCE
    ) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "interface call requires a class instance receiver"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (frame == NULL || frame->function == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INTERNAL,
            "call requires a function-backed frame"
        );
        return BASL_STATUS_INTERNAL;
    }

    class_index = basl_instance_object_class_index(basl_value_as_object(receiver));
    callee = basl_function_object_resolve_interface_method(
        frame->function,
        class_index,
        interface_index,
        method_index
    );
    if (callee == NULL || basl_object_type(callee) != BASL_OBJECT_FUNCTION) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "interface call target is invalid");
        return BASL_STATUS_INTERNAL;
    }
    if (basl_function_object_arity(callee) != arg_count + 1U) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "call arity does not match function signature"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return basl_vm_push_frame(
        vm,
        callee,
        basl_function_object_chunk(callee),
        base_slot,
        error
    );
}

static basl_status_t basl_vm_invoke_new_instance(
    basl_vm_t *vm,
    size_t class_index,
    size_t field_count,
    int discard_result,
    basl_error_t *error
) {
    basl_status_t status;
    basl_object_t *instance;
    basl_value_t value;
    size_t base_slot;

    if (field_count > vm->stack_count) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INTERNAL,
            "constructor arguments are missing from the stack"
        );
        return BASL_STATUS_INTERNAL;
    }

    base_slot = vm->stack_count - field_count;
    instance = NULL;
    status = basl_instance_object_new(
        vm->runtime,
        class_index,
        vm->stack + base_slot,
        field_count,
        &instance,
        error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    while (vm->stack_count > base_slot) {
        value = basl_vm_pop_or_nil(vm);
        basl_value_release(&value);
    }

    basl_value_init_object(&value, &instance);
    if (discard_result) {
        basl_value_release(&value);
        return BASL_STATUS_OK;
    }
    status = basl_vm_push(vm, &value, error);
    basl_value_release(&value);
    return status;
}

static basl_status_t basl_vm_schedule_defer(
    basl_vm_t *vm,
    basl_vm_frame_t *frame,
    basl_vm_defer_kind_t kind,
    uint32_t operand_a,
    uint32_t operand_b,
    uint32_t arg_count,
    size_t value_count,
    basl_error_t *error
) {
    basl_status_t status;
    basl_vm_defer_action_t *action;
    size_t base_slot;
    basl_value_t value;

    if (frame == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "vm frame is missing");
        return BASL_STATUS_INTERNAL;
    }
    if (value_count > vm->stack_count) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INTERNAL,
            "defer arguments are missing from the stack"
        );
        return BASL_STATUS_INTERNAL;
    }

    status = basl_vm_frame_grow_defers(vm, frame, frame->defer_count + 1U, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    action = &frame->defers[frame->defer_count];
    memset(action, 0, sizeof(*action));
    action->kind = kind;
    action->operand_a = operand_a;
    action->operand_b = operand_b;
    action->arg_count = arg_count;
    action->value_count = value_count;

    base_slot = vm->stack_count - value_count;
    status = basl_vm_copy_values(
        vm,
        vm->stack + base_slot,
        value_count,
        &action->values,
        error
    );
    if (status != BASL_STATUS_OK) {
        basl_vm_defer_action_clear(vm->runtime, action);
        return status;
    }

    while (vm->stack_count > base_slot) {
        value = basl_vm_pop_or_nil(vm);
        basl_value_release(&value);
    }
    frame->defer_count += 1U;
    return BASL_STATUS_OK;
}

static basl_status_t basl_vm_execute_next_defer(
    basl_vm_t *vm,
    basl_vm_frame_t *frame,
    int *out_pushed_frame,
    basl_error_t *error
) {
    basl_status_t status;
    basl_vm_defer_action_t action;
    size_t i;

    if (out_pushed_frame != NULL) {
        *out_pushed_frame = 0;
    }
    if (frame == NULL || frame->defer_count == 0U) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "no deferred call is available");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    action = frame->defers[frame->defer_count - 1U];
    memset(&frame->defers[frame->defer_count - 1U], 0, sizeof(frame->defers[frame->defer_count - 1U]));
    frame->defer_count -= 1U;

    for (i = 0U; i < action.value_count; i += 1U) {
        status = basl_vm_push(vm, &action.values[i], error);
        if (status != BASL_STATUS_OK) {
            basl_vm_defer_action_clear(vm->runtime, &action);
            return status;
        }
    }

    switch (action.kind) {
        case BASL_VM_DEFER_CALL:
            status = basl_vm_invoke_call(
                vm,
                frame,
                (size_t)action.operand_a,
                (size_t)action.arg_count,
                error
            );
            if (status == BASL_STATUS_OK && out_pushed_frame != NULL) {
                *out_pushed_frame = 1;
            }
            break;
        case BASL_VM_DEFER_CALL_INTERFACE:
            status = basl_vm_invoke_interface_call(
                vm,
                frame,
                (size_t)action.operand_a,
                (size_t)action.operand_b,
                (size_t)action.arg_count,
                error
            );
            if (status == BASL_STATUS_OK && out_pushed_frame != NULL) {
                *out_pushed_frame = 1;
            }
            break;
        case BASL_VM_DEFER_NEW_INSTANCE:
            status = basl_vm_invoke_new_instance(
                vm,
                (size_t)action.operand_a,
                (size_t)action.arg_count,
                1,
                error
            );
            break;
        default:
            basl_error_set_literal(error, BASL_STATUS_INTERNAL, "defer target is invalid");
            status = BASL_STATUS_INTERNAL;
            break;
    }

    basl_vm_defer_action_clear(vm->runtime, &action);
    return status;
}

static basl_status_t basl_vm_complete_return(
    basl_vm_t *vm,
    basl_value_t returned_value,
    basl_value_t *out_value,
    basl_error_t *error
) {
    basl_status_t status;
    basl_value_t current_value;

    current_value = returned_value;
    while (1) {
        basl_vm_frame_t *frame;
        size_t base_slot;
        int pushed_frame;

        frame = basl_vm_current_frame(vm);
        if (frame == NULL) {
            basl_value_release(&current_value);
            basl_error_set_literal(error, BASL_STATUS_INTERNAL, "vm frame is missing");
            return BASL_STATUS_INTERNAL;
        }

        if (!frame->has_pending_return) {
            frame->pending_return = current_value;
            frame->has_pending_return = 1;
            current_value.kind = BASL_VALUE_NIL;
            frame->draining_defers = 1;
        } else {
            basl_value_release(&current_value);
        }

        while (frame->defer_count > 0U) {
            status = basl_vm_execute_next_defer(vm, frame, &pushed_frame, error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            if (pushed_frame) {
                return BASL_STATUS_OK;
            }
        }

        current_value = frame->pending_return;
        basl_value_init_nil(&frame->pending_return);
        frame->has_pending_return = 0;
        frame->draining_defers = 0;
        base_slot = frame->base_slot;
        vm->frame_count -= 1U;
        basl_vm_frame_clear(vm->runtime, &vm->frames[vm->frame_count]);
        basl_vm_unwind_stack_to(vm, base_slot);
        if (vm->frame_count == 0U) {
            *out_value = current_value;
            return BASL_STATUS_OK;
        }
        frame = basl_vm_current_frame(vm);
        if (frame != NULL && frame->draining_defers) {
            continue;
        }

        status = basl_vm_push(vm, &current_value, error);
        basl_value_release(&current_value);
        return status;
    }
}

static basl_status_t basl_vm_checked_add(
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

static basl_status_t basl_vm_checked_subtract(
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

static basl_status_t basl_vm_checked_multiply(
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

static basl_status_t basl_vm_checked_divide(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (right == 0) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (left == INT64_MIN && right == -1) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left / right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_vm_checked_modulo(
    int64_t left,
    int64_t right,
    int64_t *out_result
) {
    if (right == 0) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (left == INT64_MIN && right == -1) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = left % right;
    return BASL_STATUS_OK;
}

static basl_status_t basl_vm_checked_negate(
    int64_t value,
    int64_t *out_result
) {
    if (value == INT64_MIN) {
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_result = -value;
    return BASL_STATUS_OK;
}

static basl_status_t basl_vm_checked_shift_left(
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

static basl_status_t basl_vm_checked_shift_right(
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

static int basl_vm_values_equal(
    const basl_value_t *left,
    const basl_value_t *right
) {
    const basl_object_t *left_object;
    const basl_object_t *right_object;

    if (left == NULL || right == NULL) {
        return 0;
    }

    if (left->kind != right->kind) {
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
                basl_object_type(left_object) == BASL_OBJECT_STRING &&
                basl_object_type(right_object) == BASL_OBJECT_STRING
            ) {
                size_t left_length = basl_string_object_length(left_object);
                size_t right_length = basl_string_object_length(right_object);
                const char *left_text = basl_string_object_c_str(left_object);
                const char *right_text = basl_string_object_c_str(right_object);

                return left_length == right_length &&
                       left_text != NULL &&
                       right_text != NULL &&
                       memcmp(left_text, right_text, left_length) == 0;
            }
            return 0;
        default:
            return 0;
    }
}

static basl_status_t basl_vm_concat_strings(
    basl_vm_t *vm,
    const basl_value_t *left,
    const basl_value_t *right,
    basl_value_t *out_value,
    basl_error_t *error
) {
    basl_status_t status;
    basl_string_t text;
    const basl_object_t *left_object;
    const basl_object_t *right_object;
    basl_object_t *object;

    if (vm == NULL || left == NULL || right == NULL || out_value == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "string operands are required");
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
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "string operands are required");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    object = NULL;
    basl_string_init(&text, vm->runtime);
    status = basl_string_append(
        &text,
        basl_string_object_c_str(left_object),
        basl_string_object_length(left_object),
        error
    );
    if (status == BASL_STATUS_OK) {
        status = basl_string_append(
            &text,
            basl_string_object_c_str(right_object),
            basl_string_object_length(right_object),
            error
        );
    }
    if (status == BASL_STATUS_OK) {
        status = basl_string_object_new(
            vm->runtime,
            basl_string_c_str(&text),
            basl_string_length(&text),
            &object,
            error
        );
    }
    if (status == BASL_STATUS_OK) {
        basl_value_init_object(out_value, &object);
    }
    basl_object_release(&object);
    basl_string_free(&text);
    return status;
}

static basl_status_t basl_vm_stringify_value(
    basl_vm_t *vm,
    const basl_value_t *value,
    basl_value_t *out_value,
    basl_error_t *error
) {
    basl_status_t status;
    basl_object_t *object;
    const char *text;
    char buffer[128];
    int written;

    if (vm == NULL || value == NULL || out_value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "vm string conversion arguments must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    object = NULL;
    switch (basl_value_kind(value)) {
        case BASL_VALUE_BOOL:
            text = basl_value_as_bool(value) ? "true" : "false";
            status = basl_string_object_new_cstr(vm->runtime, text, &object, error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            break;
        case BASL_VALUE_INT:
            written = snprintf(
                buffer,
                sizeof(buffer),
                "%lld",
                (long long)basl_value_as_int(value)
            );
            if (written < 0 || (size_t)written >= sizeof(buffer)) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "failed to format integer string conversion"
                );
                return BASL_STATUS_INTERNAL;
            }
            status = basl_string_object_new(vm->runtime, buffer, (size_t)written, &object, error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            break;
        case BASL_VALUE_FLOAT:
            written = snprintf(
                buffer,
                sizeof(buffer),
                "%.17g",
                basl_value_as_float(value)
            );
            if (written < 0 || (size_t)written >= sizeof(buffer)) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INTERNAL,
                    "failed to format float string conversion"
                );
                return BASL_STATUS_INTERNAL;
            }
            status = basl_string_object_new(vm->runtime, buffer, (size_t)written, &object, error);
            if (status != BASL_STATUS_OK) {
                return status;
            }
            break;
        case BASL_VALUE_OBJECT:
            if (
                basl_value_as_object(value) == NULL ||
                basl_object_type(basl_value_as_object(value)) != BASL_OBJECT_STRING
            ) {
                basl_error_set_literal(
                    error,
                    BASL_STATUS_INVALID_ARGUMENT,
                    "string conversion requires a primitive or string operand"
                );
                return BASL_STATUS_INVALID_ARGUMENT;
            }
            *out_value = basl_value_copy(value);
            return BASL_STATUS_OK;
        default:
            basl_error_set_literal(
                error,
                BASL_STATUS_INVALID_ARGUMENT,
                "string conversion requires a primitive or string operand"
            );
            return BASL_STATUS_INVALID_ARGUMENT;
    }

    basl_value_init_object(out_value, &object);
    basl_object_release(&object);
    return BASL_STATUS_OK;
}

static basl_status_t basl_vm_fail_at_ip(
    basl_vm_t *vm,
    basl_status_t status,
    const char *message,
    basl_error_t *error
) {
    basl_source_span_t span;
    basl_vm_frame_t *frame;

    basl_error_set_literal(error, status, message);
    frame = basl_vm_current_frame(vm);
    if (error != NULL && frame != NULL) {
        span = basl_chunk_span_at(frame->chunk, frame->ip);
        error->location.source_id = span.source_id;
        error->location.offset = span.start_offset;
    }

    return status;
}

static basl_status_t basl_vm_read_u32(
    basl_vm_t *vm,
    uint32_t *out_value,
    basl_error_t *error
) {
    basl_vm_frame_t *frame;
    const uint8_t *code;
    size_t code_size;
    size_t ip;

    frame = basl_vm_current_frame(vm);
    if (frame == NULL) {
        return basl_vm_fail_at_ip(
            vm,
            BASL_STATUS_INTERNAL,
            "vm frame is missing",
            error
        );
    }

    code = basl_chunk_code(frame->chunk);
    code_size = basl_chunk_code_size(frame->chunk);
    ip = frame->ip;
    if (code == NULL || ip + 4U >= code_size) {
        return basl_vm_fail_at_ip(
            vm,
            BASL_STATUS_INTERNAL,
            "truncated operand in chunk",
            error
        );
    }

    *out_value = (uint32_t)code[ip + 1U];
    *out_value |= (uint32_t)code[ip + 2U] << 8U;
    *out_value |= (uint32_t)code[ip + 3U] << 16U;
    *out_value |= (uint32_t)code[ip + 4U] << 24U;
    frame->ip += 5U;
    return BASL_STATUS_OK;
}

static basl_status_t basl_vm_read_raw_u32(
    basl_vm_t *vm,
    uint32_t *out_value,
    basl_error_t *error
) {
    basl_vm_frame_t *frame;
    const uint8_t *code;
    size_t code_size;
    size_t ip;

    frame = basl_vm_current_frame(vm);
    if (frame == NULL) {
        return basl_vm_fail_at_ip(
            vm,
            BASL_STATUS_INTERNAL,
            "vm frame is missing",
            error
        );
    }

    code = basl_chunk_code(frame->chunk);
    code_size = basl_chunk_code_size(frame->chunk);
    ip = frame->ip;
    if (code == NULL || ip + 3U >= code_size) {
        return basl_vm_fail_at_ip(
            vm,
            BASL_STATUS_INTERNAL,
            "truncated operand in chunk",
            error
        );
    }

    *out_value = (uint32_t)code[ip];
    *out_value |= (uint32_t)code[ip + 1U] << 8U;
    *out_value |= (uint32_t)code[ip + 2U] << 16U;
    *out_value |= (uint32_t)code[ip + 3U] << 24U;
    frame->ip += 4U;
    return BASL_STATUS_OK;
}

void basl_vm_options_init(basl_vm_options_t *options) {
    if (options == NULL) {
        return;
    }

    memset(options, 0, sizeof(*options));
}

basl_status_t basl_vm_open(
    basl_vm_t **out_vm,
    basl_runtime_t *runtime,
    const basl_vm_options_t *options,
    basl_error_t *error
) {
    basl_vm_t *vm;
    void *memory;
    basl_status_t status;
    size_t initial_stack_capacity;

    basl_error_clear(error);
    if (out_vm == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "out_vm must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_vm = NULL;
    memory = NULL;
    status = basl_runtime_alloc(runtime, sizeof(*vm), &memory, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    vm = (basl_vm_t *)memory;
    vm->runtime = runtime;
    initial_stack_capacity = options == NULL ? 0U : options->initial_stack_capacity;
    if (initial_stack_capacity != 0U) {
        status = basl_vm_grow_stack(vm, initial_stack_capacity, error);
        if (status != BASL_STATUS_OK) {
            memory = vm;
            basl_runtime_free(runtime, &memory);
            return status;
        }
    }

    *out_vm = vm;
    return BASL_STATUS_OK;
}

void basl_vm_close(basl_vm_t **vm) {
    basl_vm_t *resolved_vm;
    basl_runtime_t *runtime;
    void *memory;

    if (vm == NULL || *vm == NULL) {
        return;
    }

    resolved_vm = *vm;
    *vm = NULL;
    runtime = resolved_vm->runtime;
    basl_vm_release_stack(resolved_vm);
    basl_vm_clear_frames(resolved_vm);
    memory = resolved_vm->stack;
    if (runtime != NULL) {
        basl_runtime_free(runtime, &memory);
    }

    memory = resolved_vm->frames;
    if (runtime != NULL) {
        basl_runtime_free(runtime, &memory);
    }

    memory = resolved_vm;
    if (runtime != NULL) {
        basl_runtime_free(runtime, &memory);
    }
}

basl_runtime_t *basl_vm_runtime(const basl_vm_t *vm) {
    if (vm == NULL) {
        return NULL;
    }

    return vm->runtime;
}

size_t basl_vm_stack_depth(const basl_vm_t *vm) {
    if (vm == NULL) {
        return 0U;
    }

    return vm->stack_count;
}

size_t basl_vm_frame_depth(const basl_vm_t *vm) {
    if (vm == NULL) {
        return 0U;
    }

    return vm->frame_count;
}

basl_status_t basl_vm_execute(
    basl_vm_t *vm,
    const basl_chunk_t *chunk,
    basl_value_t *out_value,
    basl_error_t *error
) {
    basl_status_t status;

    status = basl_vm_validate(vm, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (chunk == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "chunk must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (out_value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "out_value must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    basl_vm_release_stack(vm);
    basl_vm_clear_frames(vm);
    status = basl_vm_push_frame(vm, NULL, chunk, 0U, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    return basl_vm_execute_function(vm, NULL, out_value, error);
}

basl_status_t basl_vm_execute_function(
    basl_vm_t *vm,
    const basl_object_t *function,
    basl_value_t *out_value,
    basl_error_t *error
) {
    basl_status_t status;
    basl_value_t value;
    basl_value_t left;
    basl_value_t right;
    int64_t integer_result = 0;
    const basl_value_t *constant;
    const basl_value_t *peeked;
    uint32_t constant_index;
    uint32_t operand;
    basl_vm_frame_t *frame;
    const uint8_t *code;
    size_t code_size;
    size_t local_index;

    status = basl_vm_validate(vm, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (out_value == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "out_value must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (function != NULL) {
        if (basl_object_type(function) != BASL_OBJECT_FUNCTION) {
            basl_error_set_literal(
                error,
                BASL_STATUS_INVALID_ARGUMENT,
                "function must be a function object"
            );
            return BASL_STATUS_INVALID_ARGUMENT;
        }

        if (basl_function_object_arity(function) != 0U) {
            basl_error_set_literal(
                error,
                BASL_STATUS_INVALID_ARGUMENT,
                "top-level execute_function requires a zero-arity function"
            );
            return BASL_STATUS_INVALID_ARGUMENT;
        }

        basl_vm_release_stack(vm);
        basl_vm_clear_frames(vm);
        status = basl_vm_push_frame(
            vm,
            function,
            basl_function_object_chunk(function),
            0U,
            error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }
    }

    while (1) {
        frame = basl_vm_current_frame(vm);
        if (frame == NULL || frame->chunk == NULL) {
            basl_error_set_literal(
                error,
                BASL_STATUS_INVALID_ARGUMENT,
                "vm frame chunk must not be null"
            );
            return BASL_STATUS_INVALID_ARGUMENT;
        }

        code = basl_chunk_code(frame->chunk);
        code_size = basl_chunk_code_size(frame->chunk);
        if (frame->ip >= code_size) {
            break;
        }

        switch ((basl_opcode_t)code[frame->ip]) {
            case BASL_OPCODE_CONSTANT:
                status = basl_vm_read_u32(vm, &constant_index, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                constant = basl_chunk_constant(frame->chunk, (size_t)constant_index);
                if (constant == NULL) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "constant index out of range",
                        error
                    );
                    goto cleanup;
                }

                status = basl_vm_push(vm, constant, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            case BASL_OPCODE_POP:
                value = basl_vm_pop_or_nil(vm);
                basl_value_release(&value);
                frame->ip += 1U;
                break;
            case BASL_OPCODE_DUP:
                peeked = basl_vm_peek(vm, 0U);
                if (peeked == NULL) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "dup requires a value on the stack",
                        error
                    );
                    goto cleanup;
                }

                value = basl_value_copy(peeked);
                status = basl_vm_push(vm, &value, error);
                basl_value_release(&value);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                frame->ip += 1U;
                break;
            case BASL_OPCODE_GET_LOCAL:
                status = basl_vm_read_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                status = basl_vm_validate_local_slot(vm, operand, &local_index, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                value = basl_value_copy(&vm->stack[local_index]);
                status = basl_vm_push(vm, &value, error);
                basl_value_release(&value);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            case BASL_OPCODE_SET_LOCAL:
                status = basl_vm_read_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                peeked = basl_vm_peek(vm, 0U);
                if (peeked == NULL) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "assignment requires a value on the stack",
                        error
                    );
                    goto cleanup;
                }

                status = basl_vm_validate_local_slot(vm, operand, &local_index, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                value = basl_value_copy(peeked);
                basl_value_release(&vm->stack[local_index]);
                vm->stack[local_index] = value;
                break;
            case BASL_OPCODE_GET_GLOBAL:
                status = basl_vm_read_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                frame = basl_vm_current_frame(vm);
                if (frame == NULL || frame->function == NULL) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "global read requires a function-backed frame",
                        error
                    );
                    goto cleanup;
                }

                basl_value_init_nil(&value);
                if (!basl_function_object_get_global(frame->function, (size_t)operand, &value)) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "global index out of range",
                        error
                    );
                    goto cleanup;
                }

                status = basl_vm_push(vm, &value, error);
                basl_value_release(&value);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            case BASL_OPCODE_SET_GLOBAL:
                status = basl_vm_read_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                peeked = basl_vm_peek(vm, 0U);
                if (peeked == NULL) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "global assignment requires a value on the stack",
                        error
                    );
                    goto cleanup;
                }

                frame = basl_vm_current_frame(vm);
                if (frame == NULL || frame->function == NULL) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "global assignment requires a function-backed frame",
                        error
                    );
                    goto cleanup;
                }

                status = basl_function_object_set_global(
                    frame->function,
                    (size_t)operand,
                    peeked,
                    error
                );
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            case BASL_OPCODE_CALL: {
                status = basl_vm_read_u32(vm, &constant_index, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                status = basl_vm_read_raw_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                frame = basl_vm_current_frame(vm);
                status = basl_vm_invoke_call(
                    vm,
                    frame,
                    (size_t)constant_index,
                    (size_t)operand,
                    error
                );
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            }
            case BASL_OPCODE_CALL_INTERFACE: {
                size_t interface_index;
                size_t method_index;
                size_t arg_count;

                status = basl_vm_read_u32(vm, &constant_index, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                status = basl_vm_read_raw_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                interface_index = (size_t)constant_index;
                method_index = (size_t)operand;

                status = basl_vm_read_raw_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                arg_count = (size_t)operand;
                frame = basl_vm_current_frame(vm);
                status = basl_vm_invoke_interface_call(
                    vm,
                    frame,
                    interface_index,
                    method_index,
                    arg_count,
                    error
                );
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            }
            case BASL_OPCODE_NEW_INSTANCE: {
                size_t class_index;
                size_t field_count;

                status = basl_vm_read_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                class_index = (size_t)operand;

                status = basl_vm_read_raw_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                field_count = (size_t)operand;
                status = basl_vm_invoke_new_instance(
                    vm,
                    class_index,
                    field_count,
                    0,
                    error
                );
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            }
            case BASL_OPCODE_DEFER_CALL: {
                uint32_t arg_count;

                status = basl_vm_read_u32(vm, &constant_index, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                status = basl_vm_read_raw_u32(vm, &arg_count, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                frame = basl_vm_current_frame(vm);
                status = basl_vm_schedule_defer(
                    vm,
                    frame,
                    BASL_VM_DEFER_CALL,
                    constant_index,
                    0U,
                    arg_count,
                    (size_t)arg_count,
                    error
                );
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            }
            case BASL_OPCODE_DEFER_CALL_INTERFACE: {
                uint32_t interface_index;
                uint32_t method_index;
                uint32_t arg_count;

                status = basl_vm_read_u32(vm, &interface_index, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                status = basl_vm_read_raw_u32(vm, &method_index, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                status = basl_vm_read_raw_u32(vm, &arg_count, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                frame = basl_vm_current_frame(vm);
                status = basl_vm_schedule_defer(
                    vm,
                    frame,
                    BASL_VM_DEFER_CALL_INTERFACE,
                    interface_index,
                    method_index,
                    arg_count,
                    (size_t)arg_count + 1U,
                    error
                );
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            }
            case BASL_OPCODE_DEFER_NEW_INSTANCE: {
                uint32_t class_index;
                uint32_t field_count;

                status = basl_vm_read_u32(vm, &class_index, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                status = basl_vm_read_raw_u32(vm, &field_count, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                frame = basl_vm_current_frame(vm);
                status = basl_vm_schedule_defer(
                    vm,
                    frame,
                    BASL_VM_DEFER_NEW_INSTANCE,
                    class_index,
                    0U,
                    field_count,
                    (size_t)field_count,
                    error
                );
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            }
            case BASL_OPCODE_GET_FIELD:
                status = basl_vm_read_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                left = basl_vm_pop_or_nil(vm);
                if (
                    basl_value_kind(&left) != BASL_VALUE_OBJECT ||
                    basl_object_type(basl_value_as_object(&left)) != BASL_OBJECT_INSTANCE
                ) {
                    basl_value_release(&left);
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "field access requires a class instance",
                        error
                    );
                    goto cleanup;
                }

                if (
                    !basl_instance_object_get_field(
                        basl_value_as_object(&left),
                        (size_t)operand,
                        &value
                    )
                ) {
                    basl_value_release(&left);
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "field index out of range",
                        error
                    );
                    goto cleanup;
                }
                basl_value_release(&left);

                status = basl_vm_push(vm, &value, error);
                basl_value_release(&value);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            case BASL_OPCODE_SET_FIELD:
                status = basl_vm_read_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                right = basl_vm_pop_or_nil(vm);
                left = basl_vm_pop_or_nil(vm);
                if (
                    basl_value_kind(&left) != BASL_VALUE_OBJECT ||
                    basl_object_type(basl_value_as_object(&left)) != BASL_OBJECT_INSTANCE
                ) {
                    basl_value_release(&left);
                    basl_value_release(&right);
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "field assignment requires a class instance",
                        error
                    );
                    goto cleanup;
                }

                status = basl_instance_object_set_field(
                    basl_value_as_object(&left),
                    (size_t)operand,
                    &right,
                    error
                );
                basl_value_release(&left);
                basl_value_release(&right);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            case BASL_OPCODE_JUMP:
                status = basl_vm_read_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                if ((size_t)operand > SIZE_MAX - frame->ip) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "jump target overflow",
                        error
                    );
                    goto cleanup;
                }
                frame->ip += (size_t)operand;
                break;
            case BASL_OPCODE_JUMP_IF_FALSE:
                status = basl_vm_read_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                peeked = basl_vm_peek(vm, 0U);
                if (peeked == NULL || basl_value_kind(peeked) != BASL_VALUE_BOOL) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "condition must evaluate to bool",
                        error
                    );
                    goto cleanup;
                }
                if (!basl_value_as_bool(peeked)) {
                    if ((size_t)operand > SIZE_MAX - frame->ip) {
                        status = basl_vm_fail_at_ip(
                            vm,
                            BASL_STATUS_INTERNAL,
                            "jump target overflow",
                            error
                        );
                        goto cleanup;
                    }
                    frame->ip += (size_t)operand;
                }
                break;
            case BASL_OPCODE_LOOP:
                status = basl_vm_read_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                if ((size_t)operand > frame->ip) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "loop target out of range",
                        error
                    );
                    goto cleanup;
                }
                frame->ip -= (size_t)operand;
                break;
            case BASL_OPCODE_NIL:
                basl_value_init_nil(&value);
                status = basl_vm_push(vm, &value, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                frame->ip += 1U;
                break;
            case BASL_OPCODE_TRUE:
                basl_value_init_bool(&value, 1);
                status = basl_vm_push(vm, &value, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                frame->ip += 1U;
                break;
            case BASL_OPCODE_FALSE:
                basl_value_init_bool(&value, 0);
                status = basl_vm_push(vm, &value, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                frame->ip += 1U;
                break;
            case BASL_OPCODE_ADD:
            case BASL_OPCODE_SUBTRACT:
            case BASL_OPCODE_MULTIPLY:
            case BASL_OPCODE_DIVIDE:
            case BASL_OPCODE_MODULO:
            case BASL_OPCODE_BITWISE_AND:
            case BASL_OPCODE_BITWISE_OR:
            case BASL_OPCODE_BITWISE_XOR:
            case BASL_OPCODE_SHIFT_LEFT:
            case BASL_OPCODE_SHIFT_RIGHT:
            case BASL_OPCODE_GREATER:
            case BASL_OPCODE_LESS:
            case BASL_OPCODE_EQUAL:
                right = basl_vm_pop_or_nil(vm);
                left = basl_vm_pop_or_nil(vm);

                if ((basl_opcode_t)code[frame->ip] == BASL_OPCODE_EQUAL) {
                    basl_value_init_bool(
                        &value,
                        basl_vm_values_equal(&left, &right)
                    );
                } else {
                    if (
                        (basl_opcode_t)code[frame->ip] == BASL_OPCODE_ADD &&
                        basl_value_kind(&left) == BASL_VALUE_OBJECT &&
                        basl_value_kind(&right) == BASL_VALUE_OBJECT &&
                        basl_value_as_object(&left) != NULL &&
                        basl_value_as_object(&right) != NULL &&
                        basl_object_type(basl_value_as_object(&left)) == BASL_OBJECT_STRING &&
                        basl_object_type(basl_value_as_object(&right)) == BASL_OBJECT_STRING
                    ) {
                        status = basl_vm_concat_strings(vm, &left, &right, &value, error);
                        if (status != BASL_STATUS_OK) {
                            basl_value_release(&left);
                            basl_value_release(&right);
                            goto cleanup;
                        }
                    } else if (
                        basl_value_kind(&left) == BASL_VALUE_FLOAT &&
                        basl_value_kind(&right) == BASL_VALUE_FLOAT
                    ) {
                        switch ((basl_opcode_t)code[frame->ip]) {
                            case BASL_OPCODE_ADD:
                                basl_value_init_float(
                                    &value,
                                    basl_value_as_float(&left) + basl_value_as_float(&right)
                                );
                                break;
                            case BASL_OPCODE_SUBTRACT:
                                basl_value_init_float(
                                    &value,
                                    basl_value_as_float(&left) - basl_value_as_float(&right)
                                );
                                break;
                            case BASL_OPCODE_MULTIPLY:
                                basl_value_init_float(
                                    &value,
                                    basl_value_as_float(&left) * basl_value_as_float(&right)
                                );
                                break;
                            case BASL_OPCODE_DIVIDE:
                                basl_value_init_float(
                                    &value,
                                    basl_value_as_float(&left) / basl_value_as_float(&right)
                                );
                                break;
                            case BASL_OPCODE_GREATER:
                                basl_value_init_bool(
                                    &value,
                                    basl_value_as_float(&left) > basl_value_as_float(&right)
                                );
                                break;
                            case BASL_OPCODE_LESS:
                                basl_value_init_bool(
                                    &value,
                                    basl_value_as_float(&left) < basl_value_as_float(&right)
                                );
                                break;
                            default:
                                basl_value_release(&left);
                                basl_value_release(&right);
                                status = basl_vm_fail_at_ip(
                                    vm,
                                    BASL_STATUS_INVALID_ARGUMENT,
                                    "float operands are not supported for this opcode",
                                    error
                                );
                                goto cleanup;
                        }
                    } else {
                        if (
                            basl_value_kind(&left) != BASL_VALUE_INT ||
                            basl_value_kind(&right) != BASL_VALUE_INT
                        ) {
                            basl_value_release(&left);
                            basl_value_release(&right);
                            status = basl_vm_fail_at_ip(
                                vm,
                                BASL_STATUS_INVALID_ARGUMENT,
                                "integer operands are required",
                                error
                            );
                            goto cleanup;
                        }

                        switch ((basl_opcode_t)code[frame->ip]) {
                            case BASL_OPCODE_ADD:
                                status = basl_vm_checked_add(
                                    basl_value_as_int(&left),
                                    basl_value_as_int(&right),
                                    &integer_result
                                );
                                break;
                            case BASL_OPCODE_SUBTRACT:
                                status = basl_vm_checked_subtract(
                                    basl_value_as_int(&left),
                                    basl_value_as_int(&right),
                                    &integer_result
                                );
                                break;
                            case BASL_OPCODE_MULTIPLY:
                                status = basl_vm_checked_multiply(
                                    basl_value_as_int(&left),
                                    basl_value_as_int(&right),
                                    &integer_result
                                );
                                break;
                            case BASL_OPCODE_DIVIDE:
                                status = basl_vm_checked_divide(
                                    basl_value_as_int(&left),
                                    basl_value_as_int(&right),
                                    &integer_result
                                );
                                break;
                            case BASL_OPCODE_MODULO:
                                status = basl_vm_checked_modulo(
                                    basl_value_as_int(&left),
                                    basl_value_as_int(&right),
                                    &integer_result
                                );
                                break;
                            case BASL_OPCODE_BITWISE_AND:
                                status = BASL_STATUS_OK;
                                integer_result =
                                    basl_value_as_int(&left) & basl_value_as_int(&right);
                                break;
                            case BASL_OPCODE_BITWISE_OR:
                                status = BASL_STATUS_OK;
                                integer_result =
                                    basl_value_as_int(&left) | basl_value_as_int(&right);
                                break;
                            case BASL_OPCODE_BITWISE_XOR:
                                status = BASL_STATUS_OK;
                                integer_result =
                                    basl_value_as_int(&left) ^ basl_value_as_int(&right);
                                break;
                            case BASL_OPCODE_SHIFT_LEFT:
                                status = basl_vm_checked_shift_left(
                                    basl_value_as_int(&left),
                                    basl_value_as_int(&right),
                                    &integer_result
                                );
                                break;
                            case BASL_OPCODE_SHIFT_RIGHT:
                                status = basl_vm_checked_shift_right(
                                    basl_value_as_int(&left),
                                    basl_value_as_int(&right),
                                    &integer_result
                                );
                                break;
                            case BASL_OPCODE_GREATER:
                                status = BASL_STATUS_OK;
                                basl_value_init_bool(
                                    &value,
                                    basl_value_as_int(&left) > basl_value_as_int(&right)
                                );
                                break;
                            case BASL_OPCODE_LESS:
                                status = BASL_STATUS_OK;
                                basl_value_init_bool(
                                    &value,
                                    basl_value_as_int(&left) < basl_value_as_int(&right)
                                );
                                break;
                            default:
                                basl_value_init_nil(&value);
                                break;
                        }
                        if (status != BASL_STATUS_OK) {
                            basl_value_release(&left);
                            basl_value_release(&right);
                            status = basl_vm_fail_at_ip(
                                vm,
                                BASL_STATUS_INVALID_ARGUMENT,
                                "integer arithmetic overflow or invalid operation",
                                error
                            );
                            goto cleanup;
                        }
                        if (
                            (basl_opcode_t)code[frame->ip] == BASL_OPCODE_ADD ||
                            (basl_opcode_t)code[frame->ip] == BASL_OPCODE_SUBTRACT ||
                            (basl_opcode_t)code[frame->ip] == BASL_OPCODE_MULTIPLY ||
                            (basl_opcode_t)code[frame->ip] == BASL_OPCODE_DIVIDE ||
                            (basl_opcode_t)code[frame->ip] == BASL_OPCODE_MODULO ||
                            (basl_opcode_t)code[frame->ip] == BASL_OPCODE_BITWISE_AND ||
                            (basl_opcode_t)code[frame->ip] == BASL_OPCODE_BITWISE_OR ||
                            (basl_opcode_t)code[frame->ip] == BASL_OPCODE_BITWISE_XOR ||
                            (basl_opcode_t)code[frame->ip] == BASL_OPCODE_SHIFT_LEFT ||
                            (basl_opcode_t)code[frame->ip] == BASL_OPCODE_SHIFT_RIGHT
                        ) {
                            basl_value_init_int(&value, integer_result);
                        }
                    }
                }

                basl_value_release(&left);
                basl_value_release(&right);
                status = basl_vm_push(vm, &value, error);
                if (status != BASL_STATUS_OK) {
                    basl_value_release(&value);
                    goto cleanup;
                }
                basl_value_release(&value);
                frame->ip += 1U;
                break;
            case BASL_OPCODE_NEGATE:
                value = basl_vm_pop_or_nil(vm);
                if (basl_value_kind(&value) == BASL_VALUE_FLOAT) {
                    basl_value_t negated;

                    basl_value_init_float(&negated, -basl_value_as_float(&value));
                    basl_value_release(&value);
                    status = basl_vm_push(vm, &negated, error);
                    if (status != BASL_STATUS_OK) {
                        basl_value_release(&negated);
                        goto cleanup;
                    }
                    basl_value_release(&negated);
                    frame->ip += 1U;
                    break;
                }
                if (basl_value_kind(&value) != BASL_VALUE_INT) {
                    basl_value_release(&value);
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "negation requires an integer or float operand",
                        error
                    );
                    goto cleanup;
                }
                status = basl_vm_checked_negate(
                    basl_value_as_int(&value),
                    &integer_result
                );
                if (status != BASL_STATUS_OK) {
                    basl_value_release(&value);
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "integer arithmetic overflow or invalid operation",
                        error
                    );
                    goto cleanup;
                }
                basl_value_init_int(&left, integer_result);
                basl_value_release(&value);
                status = basl_vm_push(vm, &left, error);
                if (status != BASL_STATUS_OK) {
                    basl_value_release(&left);
                    goto cleanup;
                }
                basl_value_release(&left);
                frame->ip += 1U;
                break;
            case BASL_OPCODE_NOT:
                value = basl_vm_pop_or_nil(vm);
                if (basl_value_kind(&value) != BASL_VALUE_BOOL) {
                    basl_value_release(&value);
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "logical not requires a bool operand",
                        error
                    );
                    goto cleanup;
                }
                basl_value_init_bool(&left, !basl_value_as_bool(&value));
                basl_value_release(&value);
                status = basl_vm_push(vm, &left, error);
                if (status != BASL_STATUS_OK) {
                    basl_value_release(&left);
                    goto cleanup;
                }
                basl_value_release(&left);
                frame->ip += 1U;
                break;
            case BASL_OPCODE_BITWISE_NOT:
                value = basl_vm_pop_or_nil(vm);
                if (basl_value_kind(&value) != BASL_VALUE_INT) {
                    basl_value_release(&value);
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "bitwise not requires an integer operand",
                        error
                    );
                    goto cleanup;
                }
                basl_value_init_int(&left, ~basl_value_as_int(&value));
                basl_value_release(&value);
                status = basl_vm_push(vm, &left, error);
                if (status != BASL_STATUS_OK) {
                    basl_value_release(&left);
                    goto cleanup;
                }
                basl_value_release(&left);
                frame->ip += 1U;
                break;
            case BASL_OPCODE_TO_I32:
                value = basl_vm_pop_or_nil(vm);
                if (basl_value_kind(&value) == BASL_VALUE_INT) {
                    status = basl_vm_push(vm, &value, error);
                    basl_value_release(&value);
                    if (status != BASL_STATUS_OK) {
                        goto cleanup;
                    }
                    frame->ip += 1U;
                    break;
                }
                if (basl_value_kind(&value) != BASL_VALUE_FLOAT) {
                    basl_value_release(&value);
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "i32 conversion requires an int or float operand",
                        error
                    );
                    goto cleanup;
                }
                if (
                    !isfinite(basl_value_as_float(&value)) ||
                    basl_value_as_float(&value) > (double)INT64_MAX ||
                    basl_value_as_float(&value) < (double)INT64_MIN
                ) {
                    basl_value_release(&value);
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "i32 conversion overflow or invalid value",
                        error
                    );
                    goto cleanup;
                }
                basl_value_init_int(&left, (int64_t)basl_value_as_float(&value));
                basl_value_release(&value);
                status = basl_vm_push(vm, &left, error);
                if (status != BASL_STATUS_OK) {
                    basl_value_release(&left);
                    goto cleanup;
                }
                basl_value_release(&left);
                frame->ip += 1U;
                break;
            case BASL_OPCODE_TO_F64:
                value = basl_vm_pop_or_nil(vm);
                if (basl_value_kind(&value) == BASL_VALUE_FLOAT) {
                    status = basl_vm_push(vm, &value, error);
                    basl_value_release(&value);
                    if (status != BASL_STATUS_OK) {
                        goto cleanup;
                    }
                    frame->ip += 1U;
                    break;
                }
                if (basl_value_kind(&value) != BASL_VALUE_INT) {
                    basl_value_release(&value);
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "f64 conversion requires an int or float operand",
                        error
                    );
                    goto cleanup;
                }
                basl_value_init_float(&left, (double)basl_value_as_int(&value));
                basl_value_release(&value);
                status = basl_vm_push(vm, &left, error);
                if (status != BASL_STATUS_OK) {
                    basl_value_release(&left);
                    goto cleanup;
                }
                basl_value_release(&left);
                frame->ip += 1U;
                break;
            case BASL_OPCODE_TO_STRING:
                value = basl_vm_pop_or_nil(vm);
                basl_value_init_nil(&left);
                status = basl_vm_stringify_value(vm, &value, &left, error);
                basl_value_release(&value);
                if (status != BASL_STATUS_OK) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "string conversion requires a primitive or string operand",
                        error
                    );
                    goto cleanup;
                }
                status = basl_vm_push(vm, &left, error);
                if (status != BASL_STATUS_OK) {
                    basl_value_release(&left);
                    goto cleanup;
                }
                basl_value_release(&left);
                frame->ip += 1U;
                break;
            case BASL_OPCODE_RETURN:
                frame->ip += 1U;
                value = basl_vm_pop_or_nil(vm);
                status = basl_vm_complete_return(vm, value, out_value, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                if (vm->frame_count == 0U) {
                    return BASL_STATUS_OK;
                }
                break;
            default:
                status = basl_vm_fail_at_ip(
                    vm,
                    BASL_STATUS_UNSUPPORTED,
                    "unsupported opcode",
                    error
                );
                goto cleanup;
        }
    }

    status = basl_vm_fail_at_ip(
        vm,
        BASL_STATUS_INTERNAL,
        "chunk execution reached end without return",
        error
    );

cleanup:
    basl_vm_release_stack(vm);
    basl_vm_clear_frames(vm);
    return status;
}
