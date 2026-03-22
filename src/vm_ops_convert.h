/*
 * vm_ops_convert.h — Conversion, unary, and error opcode handler declarations.
 */

#ifndef VIGIL_VM_OPS_CONVERT_H
#define VIGIL_VM_OPS_CONVERT_H

#include "internal/vigil_vm_internal.h"

vigil_status_t vigil_vm_op_negate(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_not(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_bitwise_not(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_to_i32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_to_i64(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_to_u8(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_to_u32(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_to_u64(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_to_f64(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_to_string(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_format_f64(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_format_spec(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_new_error(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_get_error_kind(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);
vigil_status_t vigil_vm_op_get_error_message(vigil_vm_t *vm, vigil_vm_frame_t *frame, vigil_error_t *error);

#endif /* VIGIL_VM_OPS_CONVERT_H */
