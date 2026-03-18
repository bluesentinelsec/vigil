/* VIGIL standard library: atomic module.
 *
 * Provides atomic operations for lock-free programming.
 *
 * Functions:
 *   atomic.new(i64 initial) -> i64       allocate atomic, return handle
 *   atomic.load(i64 handle) -> i64       atomic load
 *   atomic.store(i64 handle, i64 val)    atomic store
 *   atomic.add(i64 h, i64 v) -> i64      fetch-add (returns old)
 *   atomic.sub(i64 h, i64 v) -> i64      fetch-sub (returns old)
 *   atomic.cas(i64 h, i64 exp, i64 des)  compare-and-swap -> bool
 *   atomic.exchange(i64 h, i64 v) -> i64 atomic swap (returns old)
 *   atomic.fetch_or(i64 h, i64 v) -> i64
 *   atomic.fetch_and(i64 h, i64 v) -> i64
 *   atomic.fetch_xor(i64 h, i64 v) -> i64
 *   atomic.inc(i64 h) -> i64             fetch-add 1
 *   atomic.dec(i64 h) -> i64             fetch-sub 1
 *   atomic.fence() -> void               seq_cst memory fence
 */
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"
#include "vigil/runtime.h"
#include "platform/platform.h"

#include "internal/vigil_nanbox.h"

/* ── Dynamic handle registry ─────────────────────────────────────── */

#define INITIAL_CAPACITY 1024

static volatile int64_t *g_atomics = NULL;
static volatile int64_t g_atomic_count = 0;
static size_t g_atomic_capacity = 0;

/* Mutex for registry growth only — not on the hot path. */
static volatile int64_t g_registry_lock = 0;

static void registry_lock(void) {
    while (!vigil_atomic_cas(&g_registry_lock, 0, 1)) { /* spin */ }
}

static void registry_unlock(void) {
    vigil_atomic_store(&g_registry_lock, 0);
}

static void ensure_capacity(size_t needed) {
    if (needed <= g_atomic_capacity) return;
    registry_lock();
    /* Double-check after acquiring lock. */
    if (needed <= g_atomic_capacity) {
        registry_unlock();
        return;
    }
    size_t new_cap = g_atomic_capacity ? g_atomic_capacity : INITIAL_CAPACITY;
    while (new_cap < needed) new_cap *= 2;
    volatile int64_t *new_buf = (volatile int64_t *)calloc(new_cap, sizeof(int64_t));
    if (new_buf == NULL) {
        registry_unlock();
        return;
    }
    if (g_atomics != NULL) {
        memcpy((void *)new_buf, (const void *)g_atomics,
               (size_t)g_atomic_count * sizeof(int64_t));
        free((void *)g_atomics);
    }
    g_atomics = new_buf;
    g_atomic_capacity = new_cap;
    registry_unlock();
}

/* ── Helpers ─────────────────────────────────────────────────── */

static vigil_status_t push_i64(vigil_vm_t *vm, int64_t val, vigil_error_t *error) {
    vigil_value_t v;
    vigil_value_init_int(&v, val);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t push_bool(vigil_vm_t *vm, int val, vigil_error_t *error) {
    vigil_value_t v;
    vigil_value_init_bool(&v, val);
    return vigil_vm_stack_push(vm, &v, error);
}

static int64_t get_i64_arg(vigil_vm_t *vm, size_t base, size_t idx) {
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (vigil_nanbox_is_int(v)) return vigil_nanbox_decode_int(v);
    return 0;
}

static int valid_handle(int64_t handle) {
    return handle >= 0 && handle < vigil_atomic_load(&g_atomic_count);
}

/* ── Functions ───────────────────────────────────────────────── */

static vigil_status_t atomic_new(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t initial = arg_count > 0 ? get_i64_arg(vm, base, 0) : 0;
    vigil_vm_stack_pop_n(vm, arg_count);

    int64_t handle = vigil_atomic_add(&g_atomic_count, 1);
    ensure_capacity((size_t)(handle + 1));
    if ((size_t)(handle + 1) > g_atomic_capacity) {
        vigil_atomic_sub(&g_atomic_count, 1);
        return push_i64(vm, -1, error);
    }
    vigil_atomic_store(&g_atomics[handle], initial);
    return push_i64(vm, handle, error);
}

static vigil_status_t atomic_load_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!valid_handle(handle)) return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_load(&g_atomics[handle]), error);
}

static vigil_status_t atomic_store_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!valid_handle(handle)) return push_bool(vm, 0, error);
    vigil_atomic_store(&g_atomics[handle], val);
    return push_bool(vm, 1, error);
}

static vigil_status_t atomic_add_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!valid_handle(handle)) return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_add(&g_atomics[handle], val), error);
}

static vigil_status_t atomic_sub_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!valid_handle(handle)) return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_sub(&g_atomics[handle], val), error);
}

static vigil_status_t atomic_cas_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t expected = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    int64_t desired = arg_count > 2 ? get_i64_arg(vm, base, 2) : 0;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!valid_handle(handle)) return push_bool(vm, 0, error);
    return push_bool(vm, vigil_atomic_cas(&g_atomics[handle], expected, desired), error);
}

static vigil_status_t atomic_exchange_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!valid_handle(handle)) return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_exchange(&g_atomics[handle], val), error);
}

static vigil_status_t atomic_fetch_or_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!valid_handle(handle)) return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_fetch_or(&g_atomics[handle], val), error);
}

static vigil_status_t atomic_fetch_and_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!valid_handle(handle)) return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_fetch_and(&g_atomics[handle], val), error);
}

static vigil_status_t atomic_fetch_xor_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    int64_t val = arg_count > 1 ? get_i64_arg(vm, base, 1) : 0;
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!valid_handle(handle)) return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_fetch_xor(&g_atomics[handle], val), error);
}

static vigil_status_t atomic_inc_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!valid_handle(handle)) return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_add(&g_atomics[handle], 1), error);
}

static vigil_status_t atomic_dec_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (!valid_handle(handle)) return push_i64(vm, 0, error);
    return push_i64(vm, vigil_atomic_sub(&g_atomics[handle], 1), error);
}

static vigil_status_t atomic_fence_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    (void)error;
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_atomic_fence();
    return VIGIL_STATUS_OK;
}

/* ── Module definition ───────────────────────────────────────── */

static const int i64_param[] = { VIGIL_TYPE_I64 };
static const int i64_i64_param[] = { VIGIL_TYPE_I64, VIGIL_TYPE_I64 };
static const int i64_i64_i64_param[] = { VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I64 };

static const vigil_native_module_function_t atomic_funcs[] = {
    {"new",       3U,  atomic_new,          1U, i64_param,         VIGIL_TYPE_I64,  1U, NULL, 0, NULL, NULL},
    {"load",      4U,  atomic_load_fn,      1U, i64_param,         VIGIL_TYPE_I64,  1U, NULL, 0, NULL, NULL},
    {"store",     5U,  atomic_store_fn,     2U, i64_i64_param,     VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"add",       3U,  atomic_add_fn,       2U, i64_i64_param,     VIGIL_TYPE_I64,  1U, NULL, 0, NULL, NULL},
    {"sub",       3U,  atomic_sub_fn,       2U, i64_i64_param,     VIGIL_TYPE_I64,  1U, NULL, 0, NULL, NULL},
    {"cas",       3U,  atomic_cas_fn,       3U, i64_i64_i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"exchange",  8U,  atomic_exchange_fn,  2U, i64_i64_param,     VIGIL_TYPE_I64,  1U, NULL, 0, NULL, NULL},
    {"fetch_or",  8U,  atomic_fetch_or_fn,  2U, i64_i64_param,     VIGIL_TYPE_I64,  1U, NULL, 0, NULL, NULL},
    {"fetch_and", 9U,  atomic_fetch_and_fn, 2U, i64_i64_param,     VIGIL_TYPE_I64,  1U, NULL, 0, NULL, NULL},
    {"fetch_xor", 9U,  atomic_fetch_xor_fn, 2U, i64_i64_param,     VIGIL_TYPE_I64,  1U, NULL, 0, NULL, NULL},
    {"inc",       3U,  atomic_inc_fn,       1U, i64_param,         VIGIL_TYPE_I64,  1U, NULL, 0, NULL, NULL},
    {"dec",       3U,  atomic_dec_fn,       1U, i64_param,         VIGIL_TYPE_I64,  1U, NULL, 0, NULL, NULL},
    {"fence",     5U,  atomic_fence_fn,     0U, NULL,              VIGIL_TYPE_VOID, 1U, NULL, 0, NULL, NULL},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_atomic = {
    "atomic", 6U,
    atomic_funcs, sizeof(atomic_funcs) / sizeof(atomic_funcs[0]),
    NULL, 0U
};
