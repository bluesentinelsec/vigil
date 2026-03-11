#include <stddef.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/vm.h"

typedef struct basl_vm_frame {
    const basl_chunk_t *chunk;
    size_t ip;
} basl_vm_frame_t;

struct basl_vm {
    basl_runtime_t *runtime;
    basl_value_t *stack;
    size_t stack_count;
    size_t stack_capacity;
    basl_vm_frame_t frame;
    int has_frame;
};

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

static basl_status_t basl_vm_fail_at_ip(
    basl_vm_t *vm,
    basl_status_t status,
    const char *message,
    basl_error_t *error
) {
    basl_source_span_t span;

    basl_error_set_literal(error, status, message);
    if (error != NULL && vm != NULL && vm->has_frame) {
        span = basl_chunk_span_at(vm->frame.chunk, vm->frame.ip);
        error->location.source_id = span.source_id;
    }

    return status;
}

static basl_status_t basl_vm_read_u32(
    basl_vm_t *vm,
    uint32_t *out_value,
    basl_error_t *error
) {
    const uint8_t *code;
    size_t code_size;
    size_t ip;

    code = basl_chunk_code(vm->frame.chunk);
    code_size = basl_chunk_code_size(vm->frame.chunk);
    ip = vm->frame.ip;
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
    vm->frame.ip += 5U;
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
    memory = resolved_vm->stack;
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

basl_status_t basl_vm_execute(
    basl_vm_t *vm,
    const basl_chunk_t *chunk,
    basl_value_t *out_value,
    basl_error_t *error
) {
    basl_status_t status;
    basl_value_t value;
    const basl_value_t *constant;
    uint32_t constant_index;
    const uint8_t *code;
    size_t code_size;

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
    vm->frame.chunk = chunk;
    vm->frame.ip = 0U;
    vm->has_frame = 1;
    code = basl_chunk_code(chunk);
    code_size = basl_chunk_code_size(chunk);
    while (vm->frame.ip < code_size) {
        switch ((basl_opcode_t)code[vm->frame.ip]) {
            case BASL_OPCODE_CONSTANT:
                status = basl_vm_read_u32(vm, &constant_index, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }

                constant = basl_chunk_constant(chunk, (size_t)constant_index);
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
            case BASL_OPCODE_NIL:
                basl_value_init_nil(&value);
                status = basl_vm_push(vm, &value, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                vm->frame.ip += 1U;
                break;
            case BASL_OPCODE_TRUE:
                basl_value_init_bool(&value, 1);
                status = basl_vm_push(vm, &value, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                vm->frame.ip += 1U;
                break;
            case BASL_OPCODE_FALSE:
                basl_value_init_bool(&value, 0);
                status = basl_vm_push(vm, &value, error);
                if (status != BASL_STATUS_OK) {
                    goto cleanup;
                }
                vm->frame.ip += 1U;
                break;
            case BASL_OPCODE_RETURN:
                vm->frame.ip += 1U;
                *out_value = basl_vm_pop_or_nil(vm);
                basl_vm_release_stack(vm);
                vm->has_frame = 0;
                return BASL_STATUS_OK;
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
    vm->has_frame = 0;
    return status;
}
