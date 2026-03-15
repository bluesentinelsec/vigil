/* BASL standard library: test module.
 *
 * Provides test.T class with assert() and fail() methods for use in
 * _test.basl files.
 */
#include <string.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"

/* Thread-local buffer for test failure messages so the pointer survives
 * stack cleanup.  512 bytes is plenty for assertion messages. */
static char basl_test_msg_buf[512];

static const char *basl_test_capture_msg(
    basl_vm_t *vm, size_t base, size_t arg_count, size_t msg_arg_index,
    const char *default_msg
) {
    if (arg_count > msg_arg_index) {
        basl_value_t msg_val = basl_vm_stack_get(vm, base + msg_arg_index);
        if (basl_nanbox_is_object(msg_val)) {
            const basl_object_t *obj =
                (const basl_object_t *)basl_nanbox_decode_ptr(msg_val);
            if (obj != NULL && basl_object_type(obj) == BASL_OBJECT_STRING) {
                const char *text = basl_string_object_c_str(obj);
                size_t len = basl_string_object_length(obj);
                if (text && len > 0) {
                    if (len >= sizeof(basl_test_msg_buf))
                        len = sizeof(basl_test_msg_buf) - 1;
                    memcpy(basl_test_msg_buf, text, len);
                    basl_test_msg_buf[len] = '\0';
                    return basl_test_msg_buf;
                }
            }
        }
    }
    return default_msg;
}

/* ── T.assert(bool, string) ─────────────────────────────────────── */

static basl_status_t basl_test_assert(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_value_t cond = basl_vm_stack_get(vm, base + 1U);

    if (!basl_nanbox_decode_bool(cond)) {
        const char *msg = basl_test_capture_msg(vm, base, arg_count, 2U, "assertion failed");
        basl_vm_stack_pop_n(vm, arg_count);
        if (error) {
            error->type = BASL_STATUS_INTERNAL;
            error->value = msg;
            error->length = strlen(msg);
        }
        return BASL_STATUS_INTERNAL;
    }
    basl_vm_stack_pop_n(vm, arg_count);
    return BASL_STATUS_OK;
}

/* ── T.fail(string) ─────────────────────────────────────────────── */

static basl_status_t basl_test_fail(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *msg = basl_test_capture_msg(vm, base, arg_count, 1U, "test failed");
    basl_vm_stack_pop_n(vm, arg_count);
    if (error) {
        error->type = BASL_STATUS_INTERNAL;
        error->value = msg;
        error->length = strlen(msg);
    }
    return BASL_STATUS_INTERNAL;
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int basl_test_assert_params[] = { BASL_TYPE_BOOL, BASL_TYPE_STRING };
static const int basl_test_fail_params[] = { BASL_TYPE_STRING };

static const basl_native_class_method_t basl_test_t_methods[] = {
    { "assert", 6U, basl_test_assert, 2U, basl_test_assert_params,
      BASL_TYPE_VOID, 0U, NULL, 0, NULL, 0U },
    { "fail", 4U, basl_test_fail, 1U, basl_test_fail_params,
      BASL_TYPE_VOID, 0U, NULL, 0, NULL, 0U },
};

static const basl_native_class_t basl_test_classes[] = {
    { "T", 1U, NULL, 0U, basl_test_t_methods, 2U, NULL },
};

BASL_API const basl_native_module_t basl_stdlib_test = {
    "test", 4U,
    NULL, 0U,
    basl_test_classes, 1U
};
