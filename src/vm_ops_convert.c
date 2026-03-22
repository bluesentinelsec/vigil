/*
 * vm_ops_convert.c — Extracted conversion, unary, and error opcode handlers.
 */

#include "vm_ops_convert.h"

#include <limits.h>
#include <stdint.h>

vigil_status_t vigil_vm_op_negate(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value, result;
    int64_t integer_result;

    value = vigil_vm_pop_or_nil(vm);
    if (vigil_nanbox_is_double(value))
    {
        vigil_value_init_float(&result, -vigil_nanbox_decode_double(value));
        VIGIL_VM_VALUE_RELEASE(&value);
        status = vigil_vm_push(vm, &result, error);
        vigil_value_release(&result);
        if (status != VIGIL_STATUS_OK)
            return status;
        frame->ip += 1U;
        return VIGIL_STATUS_OK;
    }
    if (!vigil_nanbox_is_int(value))
    {
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "negation requires an integer or float operand",
                                   error);
    }
    status = vigil_vm_checked_negate(vigil_value_as_int(&value), &integer_result);
    if (status != VIGIL_STATUS_OK)
    {
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                   "integer arithmetic overflow or invalid operation", error);
    }
    (result) = vigil_nanbox_encode_int(integer_result);
    VIGIL_VM_VALUE_RELEASE(&value);
    status = vigil_vm_push(vm, &result, error);
    VIGIL_VM_VALUE_RELEASE(&result);
    if (status != VIGIL_STATUS_OK)
        return status;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_not(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value, result;

    value = vigil_vm_pop_or_nil(vm);
    if (!vigil_nanbox_is_bool(value))
    {
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "logical not requires a bool operand", error);
    }
    vigil_value_init_bool(&result, !vigil_nanbox_decode_bool(value));
    VIGIL_VM_VALUE_RELEASE(&value);
    status = vigil_vm_push(vm, &result, error);
    VIGIL_VM_VALUE_RELEASE(&result);
    if (status != VIGIL_STATUS_OK)
        return status;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_bitwise_not(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value, result;

    value = vigil_vm_pop_or_nil(vm);
    if (!vigil_nanbox_is_int(value))
    {
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "bitwise not requires an integer operand", error);
    }
    vigil_value_init_int(&result, ~vigil_value_as_int(&value));
    VIGIL_VM_VALUE_RELEASE(&value);
    status = vigil_vm_push(vm, &result, error);
    VIGIL_VM_VALUE_RELEASE(&result);
    if (status != VIGIL_STATUS_OK)
        return status;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_to_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value;

    /* Fast path: if top of stack is already INT, just range-check */
    if (vm->stack_count > 0U && vigil_nanbox_is_int_inline(vm->stack[vm->stack_count - 1U]))
    {
        int64_t v = vigil_nanbox_decode_int(vm->stack[vm->stack_count - 1U]);
        if (v < (int64_t)INT32_MIN || v > (int64_t)INT32_MAX)
            return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "i32 conversion overflow or invalid value",
                                       error);
        frame->ip += 1U;
        return VIGIL_STATUS_OK;
    }
    value = vigil_vm_pop_or_nil(vm);
    status = vigil_vm_convert_to_signed_integer_type(vm, &value, (int64_t)INT32_MIN, (int64_t)INT32_MAX,
                                                     "i32 conversion requires an int or float operand",
                                                     "i32 conversion overflow or invalid value", error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, error->value, error);
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_to_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value;

    value = vigil_vm_pop_or_nil(vm);
    status = vigil_vm_convert_to_signed_integer_type(vm, &value, INT64_MIN, INT64_MAX,
                                                     "i64 conversion requires an int or float operand",
                                                     "i64 conversion overflow or invalid value", error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, error->value, error);
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_to_u8(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value;

    value = vigil_vm_pop_or_nil(vm);
    status = vigil_vm_convert_to_unsigned_integer_type(vm, &value, (uint64_t)UINT8_MAX,
                                                       "u8 conversion requires an int or float operand",
                                                       "u8 conversion overflow or invalid value", error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, error->value, error);
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_to_u32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value;

    value = vigil_vm_pop_or_nil(vm);
    status = vigil_vm_convert_to_unsigned_integer_type(vm, &value, (uint64_t)UINT32_MAX,
                                                       "u32 conversion requires an int or float operand",
                                                       "u32 conversion overflow or invalid value", error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, error->value, error);
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_to_u64(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value;

    value = vigil_vm_pop_or_nil(vm);
    status = vigil_vm_convert_to_unsigned_integer_type(vm, &value, UINT64_MAX,
                                                       "u64 conversion requires an int or float operand",
                                                       "u64 conversion overflow or invalid value", error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, error->value, error);
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_to_f64(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value, result;

    value = vigil_vm_pop_or_nil(vm);
    if (vigil_nanbox_is_double(value))
    {
        status = vigil_vm_push(vm, &value, error);
        VIGIL_VM_VALUE_RELEASE(&value);
        if (status != VIGIL_STATUS_OK)
            return status;
        frame->ip += 1U;
        return VIGIL_STATUS_OK;
    }
    if (!vigil_vm_value_is_integer(&value))
    {
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "f64 conversion requires an int or float operand",
                                   error);
    }
    if (vigil_nanbox_is_uint(value))
        vigil_value_init_float(&result, (double)vigil_value_as_uint(&value));
    else
        vigil_value_init_float(&result, (double)vigil_value_as_int(&value));
    VIGIL_VM_VALUE_RELEASE(&value);
    status = vigil_vm_push(vm, &result, error);
    VIGIL_VM_VALUE_RELEASE(&result);
    if (status != VIGIL_STATUS_OK)
        return status;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_to_string(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value, result;

    value = vigil_vm_pop_or_nil(vm);
    VIGIL_VM_VALUE_INIT_NIL(&result);
    status = vigil_vm_stringify_value(vm, &value, &result, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                   "string conversion requires a primitive or string operand", error);
    status = vigil_vm_push(vm, &result, error);
    VIGIL_VM_VALUE_RELEASE(&result);
    if (status != VIGIL_STATUS_OK)
        return status;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_format_f64(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value, result;
    uint32_t operand;
    (void)frame;

    if ((status = vigil_vm_read_u32(vm, &operand, error)) != VIGIL_STATUS_OK)
        return status;
    value = vigil_vm_pop_or_nil(vm);
    VIGIL_VM_VALUE_INIT_NIL(&result);
    status = vigil_vm_format_f64_value(vm, &value, operand, &result, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "f64 formatting requires an f64 operand", error);
    status = vigil_vm_push(vm, &result, error);
    VIGIL_VM_VALUE_RELEASE(&result);
    return status;
}

vigil_status_t vigil_vm_op_format_spec(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value, result;
    uint32_t w1, w2;
    (void)frame;

    if ((status = vigil_vm_read_u32(vm, &w1, error)) != VIGIL_STATUS_OK)
        return status;
    if ((status = vigil_vm_read_raw_u32(vm, &w2, error)) != VIGIL_STATUS_OK)
        return status;
    value = vigil_vm_pop_or_nil(vm);
    VIGIL_VM_VALUE_INIT_NIL(&result);
    status = vigil_vm_format_spec_value(vm, &value, w1, w2, &result, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "format specifier error", error);
    status = vigil_vm_push(vm, &result, error);
    VIGIL_VM_VALUE_RELEASE(&result);
    return status;
}

vigil_status_t vigil_vm_op_new_error(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t left, right, value;

    right = vigil_vm_pop_or_nil(vm);
    left = vigil_vm_pop_or_nil(vm);
    if (!vigil_nanbox_is_object(left) || ((vigil_object_t *)vigil_nanbox_decode_ptr(left)) == NULL ||
        vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(left))) != VIGIL_OBJECT_STRING ||
        !vigil_nanbox_is_int(right))
    {
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT,
                                   "error construction requires string message and i32 kind", error);
    }
    {
        vigil_object_t *error_object = NULL;
        status = vigil_error_object_new(vm->runtime,
                                        vigil_string_object_c_str(((vigil_object_t *)vigil_nanbox_decode_ptr(left))),
                                        vigil_string_object_length(((vigil_object_t *)vigil_nanbox_decode_ptr(left))),
                                        vigil_value_as_int(&right), &error_object, error);
        VIGIL_VM_VALUE_RELEASE(&left);
        VIGIL_VM_VALUE_RELEASE(&right);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_value_init_object(&value, &error_object);
    }
    status = vigil_vm_push(vm, &value, error);
    VIGIL_VM_VALUE_RELEASE(&value);
    if (status != VIGIL_STATUS_OK)
        return status;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_get_error_kind(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value, result;

    value = vigil_vm_pop_or_nil(vm);
    if (!vigil_nanbox_is_object(value) || ((vigil_object_t *)vigil_nanbox_decode_ptr(value)) == NULL ||
        vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(value))) != VIGIL_OBJECT_ERROR)
    {
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "error kind access requires an err value", error);
    }
    vigil_value_init_int(&result, vigil_error_object_kind(((vigil_object_t *)vigil_nanbox_decode_ptr(value))));
    VIGIL_VM_VALUE_RELEASE(&value);
    status = vigil_vm_push(vm, &result, error);
    VIGIL_VM_VALUE_RELEASE(&result);
    if (status != VIGIL_STATUS_OK)
        return status;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_vm_op_get_error_message(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_value_t value, result;

    value = vigil_vm_pop_or_nil(vm);
    if (!vigil_nanbox_is_object(value) || ((vigil_object_t *)vigil_nanbox_decode_ptr(value)) == NULL ||
        vigil_object_type(((vigil_object_t *)vigil_nanbox_decode_ptr(value))) != VIGIL_OBJECT_ERROR)
    {
        VIGIL_VM_VALUE_RELEASE(&value);
        return vigil_vm_fail_at_ip(vm, VIGIL_STATUS_INVALID_ARGUMENT, "error message access requires an err value",
                                   error);
    }
    {
        vigil_object_t *string_object = NULL;
        status = vigil_string_object_new(
            vm->runtime, vigil_error_object_message(((vigil_object_t *)vigil_nanbox_decode_ptr(value))),
            vigil_error_object_message_length(((vigil_object_t *)vigil_nanbox_decode_ptr(value))), &string_object,
            error);
        VIGIL_VM_VALUE_RELEASE(&value);
        if (status != VIGIL_STATUS_OK)
            return status;
        vigil_value_init_object(&result, &string_object);
    }
    status = vigil_vm_push(vm, &result, error);
    VIGIL_VM_VALUE_RELEASE(&result);
    if (status != VIGIL_STATUS_OK)
        return status;
    frame->ip += 1U;
    return VIGIL_STATUS_OK;
}
