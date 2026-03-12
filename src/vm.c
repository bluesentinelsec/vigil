#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/vm.h"

typedef struct basl_vm_frame {
    const basl_object_t *function;
    const basl_chunk_t *chunk;
    size_t ip;
    size_t base_slot;
} basl_vm_frame_t;

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

static void basl_vm_clear_frames(basl_vm_t *vm) {
    if (vm == NULL) {
        return;
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
    frame->function = function;
    frame->chunk = chunk;
    frame->ip = 0U;
    frame->base_slot = base_slot;
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

static int basl_vm_values_equal(
    const basl_value_t *left,
    const basl_value_t *right
) {
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
            return left->as.object == right->as.object;
        default:
            return 0;
    }
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
                const basl_object_t *callee;
                size_t arg_count;
                size_t base_slot;

                status = basl_vm_read_u32(vm, &constant_index, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                status = basl_vm_read_raw_u32(vm, &operand, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                frame = basl_vm_current_frame(vm);
                if (frame == NULL || frame->function == NULL) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "call requires a function-backed frame",
                        error
                    );
                    goto cleanup;
                }

                callee = basl_function_object_sibling(
                    frame->function,
                    (size_t)constant_index
                );
                if (callee == NULL || basl_object_type(callee) != BASL_OBJECT_FUNCTION) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "call target is invalid",
                        error
                    );
                    goto cleanup;
                }

                arg_count = (size_t)operand;
                if (basl_function_object_arity(callee) != arg_count) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "call arity does not match function signature",
                        error
                    );
                    goto cleanup;
                }
                if (arg_count > vm->stack_count) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "call arguments are missing from the stack",
                        error
                    );
                    goto cleanup;
                }

                base_slot = vm->stack_count - arg_count;
                status = basl_vm_push_frame(
                    vm,
                    callee,
                    basl_function_object_chunk(callee),
                    base_slot,
                    error
                );
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            }
            case BASL_OPCODE_CALL_INTERFACE: {
                const basl_object_t *callee;
                const basl_value_t *receiver;
                size_t interface_index;
                size_t method_index;
                size_t arg_count;
                size_t base_slot;
                size_t class_index;

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
                if (arg_count + 1U > vm->stack_count) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "call arguments are missing from the stack",
                        error
                    );
                    goto cleanup;
                }

                base_slot = vm->stack_count - (arg_count + 1U);
                receiver = &vm->stack[base_slot];
                if (
                    basl_value_kind(receiver) != BASL_VALUE_OBJECT ||
                    basl_object_type(basl_value_as_object(receiver)) != BASL_OBJECT_INSTANCE
                ) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "interface call requires a class instance receiver",
                        error
                    );
                    goto cleanup;
                }

                frame = basl_vm_current_frame(vm);
                if (frame == NULL || frame->function == NULL) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "call requires a function-backed frame",
                        error
                    );
                    goto cleanup;
                }

                class_index = basl_instance_object_class_index(basl_value_as_object(receiver));
                callee = basl_function_object_resolve_interface_method(
                    frame->function,
                    class_index,
                    interface_index,
                    method_index
                );
                if (callee == NULL || basl_object_type(callee) != BASL_OBJECT_FUNCTION) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "interface call target is invalid",
                        error
                    );
                    goto cleanup;
                }
                if (basl_function_object_arity(callee) != arg_count + 1U) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "call arity does not match function signature",
                        error
                    );
                    goto cleanup;
                }

                status = basl_vm_push_frame(
                    vm,
                    callee,
                    basl_function_object_chunk(callee),
                    base_slot,
                    error
                );
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                break;
            }
            case BASL_OPCODE_NEW_INSTANCE: {
                basl_object_t *instance;
                size_t class_index;
                size_t field_count;
                size_t base_slot;

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
                if (field_count > vm->stack_count) {
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INTERNAL,
                        "constructor arguments are missing from the stack",
                        error
                    );
                    goto cleanup;
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
                    goto cleanup;
                }

                while (vm->stack_count > base_slot) {
                    value = basl_vm_pop_or_nil(vm);
                    basl_value_release(&value);
                }

                basl_value_init_object(&value, &instance);
                status = basl_vm_push(vm, &value, error);
                basl_value_release(&value);
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
                        (basl_opcode_t)code[frame->ip] == BASL_OPCODE_MODULO
                    ) {
                        basl_value_init_int(&value, integer_result);
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
                if (basl_value_kind(&value) != BASL_VALUE_INT) {
                    basl_value_release(&value);
                    status = basl_vm_fail_at_ip(
                        vm,
                        BASL_STATUS_INVALID_ARGUMENT,
                        "negation requires an integer operand",
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
            case BASL_OPCODE_RETURN:
                {
                    size_t base_slot;

                    frame->ip += 1U;
                    value = basl_vm_pop_or_nil(vm);
                    base_slot = frame->base_slot;
                    vm->frame_count -= 1U;
                    basl_vm_unwind_stack_to(vm, base_slot);
                    if (vm->frame_count == 0U) {
                        *out_value = value;
                        return BASL_STATUS_OK;
                    }

                    status = basl_vm_push(vm, &value, error);
                    basl_value_release(&value);
                    if (status != BASL_STATUS_OK) {
                        goto cleanup;
                    }
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
