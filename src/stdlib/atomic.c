/* BASL standard library: atomic module.
 *
 * Provides atomic operations for lock-free programming.
 */
#include <stdlib.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"
#include "basl/runtime.h"
#include "platform/platform.h"

#include "internal/basl_nanbox.h"

/* ── Handle registry ─────────────────────────────────────────── */

#define MAX_ATOMICS 1024

static volatile int64_t g_atomics[MAX_ATOMICS];
static volatile int64_t g_atomic_count = 0;

/* ── Helpers ─────────────────────────────────────────────────── */

static basl_status_t push_i64(basl_vm_t *vm, int64_t val, basl_error_t *error) {
    basl_value_t v;
    basl_value_init_int(&v, val);
    return basl_vm_stack_push(vm, &v, error);
}

static basl_status_t push_bool(basl_vm_t *vm, int val, basl_error_t *error) {
    basl_value_t v;
    basl_value_init_bool(&v, val);
    return basl_vm_stack_push(vm, &v, error);
}

static int64_t get_i64_arg(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    if (basl_nanbox_is_int(v)) return basl_nanbox_decode_int(v);
    return 0;
}

/* ── Functions ───────────────────────────────────────────────── */

static basl_status_t atomic_new(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t initial = arg_count > 0 ? get_i64_arg(vm, base, 0) : 0;
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t handle = basl_atomic_add(&g_atomic_count, 1);
    if (handle >= MAX_ATOMICS) {
        basl_atomic_sub(&g_atomic_count, 1);
        return push_i64(vm, -1, error);
    }
    
    basl_atomic_store(&g_atomics[handle], initial);
    return push_i64(vm, handle, error);
}

static basl_status_t atomic_load_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_atomic_count);
    if (handle < 0 || handle >= count) {
        return push_i64(vm, 0, error);
    }
    return push_i64(vm, basl_atomic_load(&g_atomics[handle]), error);
}

static basl_status_t atomic_store_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_atomic_count);
    if (handle < 0 || handle >= count) {
        return push_bool(vm, 0, error);
    }
    basl_atomic_store(&g_atomics[handle], val);
    return push_bool(vm, 1, error);
}

static basl_status_t atomic_add_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_atomic_count);
    if (handle < 0 || handle >= count) {
        return push_i64(vm, 0, error);
    }
    return push_i64(vm, basl_atomic_add(&g_atomics[handle], val), error);
}

static basl_status_t atomic_sub_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_atomic_count);
    if (handle < 0 || handle >= count) {
        return push_i64(vm, 0, error);
    }
    return push_i64(vm, basl_atomic_sub(&g_atomics[handle], val), error);
}

static basl_status_t atomic_cas_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t expected = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    int64_t desired = arg_count > 2 ? get_i64_arg(vm, base, 2) : 0;
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_atomic_count);
    if (handle < 0 || handle >= count) {
        return push_bool(vm, 0, error);
    }
    return push_bool(vm, basl_atomic_cas(&g_atomics[handle], expected, desired), error);
}

static basl_status_t atomic_inc_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_atomic_count);
    if (handle < 0 || handle >= count) {
        return push_i64(vm, 0, error);
    }
    return push_i64(vm, basl_atomic_add(&g_atomics[handle], 1), error);
}

static basl_status_t atomic_dec_fn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_atomic_count);
    if (handle < 0 || handle >= count) {
        return push_i64(vm, 0, error);
    }
    return push_i64(vm, basl_atomic_sub(&g_atomics[handle], 1), error);
}

/* ── Module definition ───────────────────────────────────────── */

static const int i64_param[] = { BASL_TYPE_I64 };
static const int i64_i64_param[] = { BASL_TYPE_I64, BASL_TYPE_I64 };
static const int i64_i64_i64_param[] = { BASL_TYPE_I64, BASL_TYPE_I64, BASL_TYPE_I64 };

static const basl_native_module_function_t atomic_funcs[] = {
    {"new", 3U, atomic_new, 1U, i64_param, BASL_TYPE_I64, 1U, NULL, 0},
    {"load", 4U, atomic_load_fn, 1U, i64_param, BASL_TYPE_I64, 1U, NULL, 0},
    {"store", 5U, atomic_store_fn, 2U, i64_i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"add", 3U, atomic_add_fn, 2U, i64_i64_param, BASL_TYPE_I64, 1U, NULL, 0},
    {"sub", 3U, atomic_sub_fn, 2U, i64_i64_param, BASL_TYPE_I64, 1U, NULL, 0},
    {"cas", 3U, atomic_cas_fn, 3U, i64_i64_i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"inc", 3U, atomic_inc_fn, 1U, i64_param, BASL_TYPE_I64, 1U, NULL, 0},
    {"dec", 3U, atomic_dec_fn, 1U, i64_param, BASL_TYPE_I64, 1U, NULL, 0},
};

BASL_API const basl_native_module_t basl_stdlib_atomic = {
    "atomic", 6U,
    atomic_funcs, sizeof(atomic_funcs) / sizeof(atomic_funcs[0]),
    NULL, 0U
};
