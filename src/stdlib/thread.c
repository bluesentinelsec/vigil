/* BASL standard library: thread module.
 *
 * Provides threading primitives: mutexes, condition variables,
 * read-write locks, and utilities.
 */
#include <stdlib.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"
#include "basl/runtime.h"
#include "platform/platform.h"

#include "internal/basl_nanbox.h"

/* ── Handle registry (simple approach without opaque objects) ─── */

#define MAX_HANDLES 1024

static basl_platform_mutex_t *g_mutexes[MAX_HANDLES];
static basl_platform_cond_t *g_conds[MAX_HANDLES];
static basl_platform_rwlock_t *g_rwlocks[MAX_HANDLES];
static size_t g_mutex_count = 0;
static size_t g_cond_count = 0;
static size_t g_rwlock_count = 0;

/* ── Helpers ─────────────────────────────────────────────────── */

static basl_status_t push_bool(basl_vm_t *vm, int val, basl_error_t *error) {
    basl_value_t v;
    basl_value_init_bool(&v, val);
    return basl_vm_stack_push(vm, &v, error);
}

static basl_status_t push_i64(basl_vm_t *vm, int64_t val, basl_error_t *error) {
    basl_value_t v;
    basl_value_init_int(&v, val);
    return basl_vm_stack_push(vm, &v, error);
}

static int64_t get_i64_arg(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    if (basl_nanbox_is_int(v)) return basl_nanbox_decode_int(v);
    return 0;
}

/* ── Thread functions ────────────────────────────────────────── */

static basl_status_t thread_current_id(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, (int64_t)basl_platform_thread_current_id(), error);
}

static basl_status_t thread_yield(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    basl_platform_thread_yield();
    return push_bool(vm, 1, error);
}

static basl_status_t thread_sleep(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t ms = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    if (ms > 0) basl_platform_thread_sleep((uint64_t)ms);
    return push_bool(vm, 1, error);
}

/* ── Mutex functions ─────────────────────────────────────────── */

static basl_status_t thread_mutex(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (g_mutex_count >= MAX_HANDLES) {
        return push_i64(vm, -1, error);
    }
    
    basl_platform_mutex_t *m = NULL;
    basl_status_t s = basl_platform_mutex_create(&m, error);
    if (s != BASL_STATUS_OK) {
        return push_i64(vm, -1, error);
    }
    
    int64_t handle = (int64_t)g_mutex_count;
    g_mutexes[g_mutex_count++] = m;
    return push_i64(vm, handle, error);
}

static basl_status_t mutex_lock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (handle < 0 || (size_t)handle >= g_mutex_count || !g_mutexes[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_mutex_lock(g_mutexes[handle]);
    return push_bool(vm, 1, error);
}

static basl_status_t mutex_unlock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (handle < 0 || (size_t)handle >= g_mutex_count || !g_mutexes[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_mutex_unlock(g_mutexes[handle]);
    return push_bool(vm, 1, error);
}

static basl_status_t mutex_try_lock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (handle < 0 || (size_t)handle >= g_mutex_count || !g_mutexes[handle]) {
        return push_bool(vm, 0, error);
    }
    
    int acquired = basl_platform_mutex_trylock(g_mutexes[handle]);
    return push_bool(vm, acquired, error);
}

/* ── Condition variable functions ────────────────────────────── */

static basl_status_t thread_cond(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (g_cond_count >= MAX_HANDLES) {
        return push_i64(vm, -1, error);
    }
    
    basl_platform_cond_t *c = NULL;
    basl_status_t s = basl_platform_cond_create(&c, error);
    if (s != BASL_STATUS_OK) {
        return push_i64(vm, -1, error);
    }
    
    int64_t handle = (int64_t)g_cond_count;
    g_conds[g_cond_count++] = c;
    return push_i64(vm, handle, error);
}

static basl_status_t cond_wait(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t cond_h = get_i64_arg(vm, base, 0);
    int64_t mutex_h = arg_count > 1 ? get_i64_arg(vm, base, 1) : -1;
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (cond_h < 0 || (size_t)cond_h >= g_cond_count || !g_conds[cond_h] ||
        mutex_h < 0 || (size_t)mutex_h >= g_mutex_count || !g_mutexes[mutex_h]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_cond_wait(g_conds[cond_h], g_mutexes[mutex_h]);
    return push_bool(vm, 1, error);
}

static basl_status_t cond_signal(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (handle < 0 || (size_t)handle >= g_cond_count || !g_conds[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_cond_signal(g_conds[handle]);
    return push_bool(vm, 1, error);
}

static basl_status_t cond_broadcast(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (handle < 0 || (size_t)handle >= g_cond_count || !g_conds[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_cond_broadcast(g_conds[handle]);
    return push_bool(vm, 1, error);
}

/* ── RWLock functions ────────────────────────────────────────── */

static basl_status_t thread_rwlock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (g_rwlock_count >= MAX_HANDLES) {
        return push_i64(vm, -1, error);
    }
    
    basl_platform_rwlock_t *rw = NULL;
    basl_status_t s = basl_platform_rwlock_create(&rw, error);
    if (s != BASL_STATUS_OK) {
        return push_i64(vm, -1, error);
    }
    
    int64_t handle = (int64_t)g_rwlock_count;
    g_rwlocks[g_rwlock_count++] = rw;
    return push_i64(vm, handle, error);
}

static basl_status_t rwlock_read_lock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (handle < 0 || (size_t)handle >= g_rwlock_count || !g_rwlocks[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_rwlock_rdlock(g_rwlocks[handle]);
    return push_bool(vm, 1, error);
}

static basl_status_t rwlock_write_lock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (handle < 0 || (size_t)handle >= g_rwlock_count || !g_rwlocks[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_rwlock_wrlock(g_rwlocks[handle]);
    return push_bool(vm, 1, error);
}

static basl_status_t rwlock_unlock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    if (handle < 0 || (size_t)handle >= g_rwlock_count || !g_rwlocks[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_rwlock_unlock(g_rwlocks[handle]);
    return push_bool(vm, 1, error);
}

/* ── Module definition ───────────────────────────────────────── */

static const int i64_param[] = { BASL_TYPE_I64 };
static const int i64_i64_param[] = { BASL_TYPE_I64, BASL_TYPE_I64 };

static const basl_native_module_function_t thread_funcs[] = {
    /* Thread management */
    {"current_id", 10U, thread_current_id, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0},
    {"yield", 5U, thread_yield, 0U, NULL, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"sleep", 5U, thread_sleep, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    
    /* Mutex */
    {"mutex", 5U, thread_mutex, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0},
    {"lock", 4U, mutex_lock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"unlock", 6U, mutex_unlock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"try_lock", 8U, mutex_try_lock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    
    /* Condition variable */
    {"cond", 4U, thread_cond, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0},
    {"wait", 4U, cond_wait, 2U, i64_i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"signal", 6U, cond_signal, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"broadcast", 9U, cond_broadcast, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    
    /* RWLock */
    {"rwlock", 6U, thread_rwlock, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0},
    {"read_lock", 9U, rwlock_read_lock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"write_lock", 10U, rwlock_write_lock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
    {"rw_unlock", 9U, rwlock_unlock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0},
};

BASL_API const basl_native_module_t basl_stdlib_thread = {
    "thread", 6U,
    thread_funcs, sizeof(thread_funcs) / sizeof(thread_funcs[0]),
    NULL, 0U
};
