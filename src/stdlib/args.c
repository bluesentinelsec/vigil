/* BASL standard library: args module.
 *
 * Provides access to CLI arguments passed to the script.
 * args.count() -> i32           returns argument count
 * args.at(i32 index) -> string  returns argument at index
 */
#include <stdlib.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

/* ── native callbacks ────────────────────────────────────────────── */

static basl_status_t basl_args_count(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    const char *const *argv = NULL;
    size_t argc = 0;
    basl_value_t val;

    (void)arg_count;
    (void)error;
    basl_vm_get_args(vm, &argv, &argc);
    basl_value_init_int(&val, (int64_t)argc);
    return basl_vm_stack_push(vm, &val, error);
}

static basl_status_t basl_args_at(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    const char *const *argv = NULL;
    size_t argc = 0;
    size_t base;
    int64_t index;
    basl_value_t val;
    basl_object_t *str = NULL;
    basl_status_t status;

    (void)arg_count;
    base = basl_vm_stack_depth(vm) - 1;
    index = basl_value_as_int(&(basl_value_t){basl_vm_stack_get(vm, base)});
    basl_vm_stack_pop_n(vm, 1);

    basl_vm_get_args(vm, &argv, &argc);

    if (index < 0 || (size_t)index >= argc) {
        status = basl_string_object_new(basl_vm_runtime(vm), "", 0, &str, error);
        if (status != BASL_STATUS_OK) return status;
        basl_value_init_object(&val, &str);
        status = basl_vm_stack_push(vm, &val, error);
        basl_value_release(&val);
        return status;
    }

    status = basl_string_object_new_cstr(basl_vm_runtime(vm), argv[index], &str, error);
    if (status != BASL_STATUS_OK) return status;
    basl_value_init_object(&val, &str);
    status = basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
    return status;
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int basl_args_at_params[] = { BASL_TYPE_I32 };

static const basl_native_module_function_t basl_args_functions[] = {
    {
        "count", 5,
        basl_args_count,
        0, NULL,
        BASL_TYPE_I32,
        1, NULL, 0,
        NULL, NULL
    },
    {
        "at", 2,
        basl_args_at,
        1, basl_args_at_params,
        BASL_TYPE_STRING,
        1, NULL, 0,
        NULL, NULL
    }
};

BASL_API const basl_native_module_t basl_stdlib_args = {
    "args", 4,
    basl_args_functions,
    sizeof(basl_args_functions) / sizeof(basl_args_functions[0]),
    NULL, 0
};
