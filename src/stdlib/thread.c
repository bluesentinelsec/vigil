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
#include "internal/basl_internal.h"

/* ── Handle registry (simple approach without opaque objects) ─── */

#define MAX_HANDLES 1024

static basl_platform_mutex_t *g_mutexes[MAX_HANDLES];
static basl_platform_cond_t *g_conds[MAX_HANDLES];
static basl_platform_rwlock_t *g_rwlocks[MAX_HANDLES];
static volatile int64_t g_mutex_count = 0;
static volatile int64_t g_cond_count = 0;
static volatile int64_t g_rwlock_count = 0;

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

/* ── Spawn support ───────────────────────────────────────────── */

#define MAX_THREADS 1024

typedef struct {
    basl_runtime_t *runtime;
    basl_object_t *function;
    basl_platform_thread_t *thread;
    int in_use;
} thread_slot_t;

static thread_slot_t g_threads[MAX_THREADS];
static volatile int64_t g_thread_count = 0;

static void thread_spawn_entry(void *arg) {
    thread_slot_t *slot = (thread_slot_t *)arg;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    basl_value_t out = {0};

    if (basl_vm_open(&vm, slot->runtime, NULL, &error) == BASL_STATUS_OK) {
        basl_vm_execute_function(vm, slot->function, &out, &error);
        basl_vm_close(&vm);
    }
    basl_object_release(&slot->function);
}

static basl_status_t thread_spawn(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    basl_value_t val = basl_vm_stack_get(vm, base);

    basl_object_t *fn = basl_value_as_object(&val);
    if (fn == NULL ||
        (basl_object_type(fn) != BASL_OBJECT_FUNCTION &&
         basl_object_type(fn) != BASL_OBJECT_CLOSURE)) {
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    int64_t idx = basl_atomic_add(&g_thread_count, 1);
    if (idx >= MAX_THREADS) {
        basl_atomic_sub(&g_thread_count, 1);
        basl_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    /* Retain before popping so the closure isn't freed */
    basl_object_retain(fn);
    basl_vm_stack_pop_n(vm, arg_count);

    g_threads[idx].runtime = basl_vm_runtime(vm);
    g_threads[idx].function = fn;
    g_threads[idx].in_use = 1;

    basl_status_t st = basl_platform_thread_create(
        &g_threads[idx].thread, thread_spawn_entry, &g_threads[idx], error);
    if (st != BASL_STATUS_OK) {
        basl_object_release(&g_threads[idx].function);
        g_threads[idx].in_use = 0;
        basl_atomic_sub(&g_thread_count, 1);
        return push_i64(vm, -1, error);
    }
    return push_i64(vm, idx, error);
}

static basl_status_t thread_join(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);

    int64_t count = basl_atomic_load(&g_thread_count);
    if (handle < 0 || handle >= count || !g_threads[handle].in_use) {
        return push_bool(vm, 0, error);
    }
    basl_platform_thread_join(g_threads[handle].thread, error);
    g_threads[handle].in_use = 0;
    return push_bool(vm, 1, error);
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
    
    /* Atomically allocate a slot */
    int64_t handle = basl_atomic_add(&g_mutex_count, 1);
    if (handle >= MAX_HANDLES) {
        basl_atomic_sub(&g_mutex_count, 1);
        return push_i64(vm, -1, error);
    }
    
    basl_platform_mutex_t *m = NULL;
    basl_status_t s = basl_platform_mutex_create(&m, error);
    if (s != BASL_STATUS_OK) {
        return push_i64(vm, -1, error);
    }
    
    g_mutexes[handle] = m;
    return push_i64(vm, handle, error);
}

static basl_status_t mutex_lock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_mutex_count);
    if (handle < 0 || handle >= count || !g_mutexes[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_mutex_lock(g_mutexes[handle]);
    return push_bool(vm, 1, error);
}

static basl_status_t mutex_unlock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_mutex_count);
    if (handle < 0 || handle >= count || !g_mutexes[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_mutex_unlock(g_mutexes[handle]);
    return push_bool(vm, 1, error);
}

static basl_status_t mutex_try_lock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_mutex_count);
    if (handle < 0 || handle >= count || !g_mutexes[handle]) {
        return push_bool(vm, 0, error);
    }
    
    int acquired = basl_platform_mutex_trylock(g_mutexes[handle]);
    return push_bool(vm, acquired, error);
}

/* ── Condition variable functions ────────────────────────────── */

static basl_status_t thread_cond(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t handle = basl_atomic_add(&g_cond_count, 1);
    if (handle >= MAX_HANDLES) {
        basl_atomic_sub(&g_cond_count, 1);
        return push_i64(vm, -1, error);
    }
    
    basl_platform_cond_t *c = NULL;
    basl_status_t s = basl_platform_cond_create(&c, error);
    if (s != BASL_STATUS_OK) {
        return push_i64(vm, -1, error);
    }
    
    g_conds[handle] = c;
    return push_i64(vm, handle, error);
}

static basl_status_t cond_wait(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t cond_h = get_i64_arg(vm, base, 0);
    int64_t mutex_h = arg_count > 1 ? get_i64_arg(vm, base, 1) : -1;
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t cond_count = basl_atomic_load(&g_cond_count);
    int64_t mutex_count = basl_atomic_load(&g_mutex_count);
    if (cond_h < 0 || cond_h >= cond_count || !g_conds[cond_h] ||
        mutex_h < 0 || mutex_h >= mutex_count || !g_mutexes[mutex_h]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_cond_wait(g_conds[cond_h], g_mutexes[mutex_h]);
    return push_bool(vm, 1, error);
}

static basl_status_t cond_signal(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_cond_count);
    if (handle < 0 || handle >= count || !g_conds[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_cond_signal(g_conds[handle]);
    return push_bool(vm, 1, error);
}

static basl_status_t cond_broadcast(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_cond_count);
    if (handle < 0 || handle >= count || !g_conds[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_cond_broadcast(g_conds[handle]);
    return push_bool(vm, 1, error);
}

/* ── RWLock functions ────────────────────────────────────────── */

static basl_status_t thread_rwlock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t handle = basl_atomic_add(&g_rwlock_count, 1);
    if (handle >= MAX_HANDLES) {
        basl_atomic_sub(&g_rwlock_count, 1);
        return push_i64(vm, -1, error);
    }
    
    basl_platform_rwlock_t *rw = NULL;
    basl_status_t s = basl_platform_rwlock_create(&rw, error);
    if (s != BASL_STATUS_OK) {
        return push_i64(vm, -1, error);
    }
    
    g_rwlocks[handle] = rw;
    return push_i64(vm, handle, error);
}

static basl_status_t rwlock_read_lock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_rwlock_count);
    if (handle < 0 || handle >= count || !g_rwlocks[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_rwlock_rdlock(g_rwlocks[handle]);
    return push_bool(vm, 1, error);
}

static basl_status_t rwlock_write_lock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_rwlock_count);
    if (handle < 0 || handle >= count || !g_rwlocks[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_rwlock_wrlock(g_rwlocks[handle]);
    return push_bool(vm, 1, error);
}

static basl_status_t rwlock_unlock(basl_vm_t *vm, size_t arg_count, basl_error_t *error) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    
    int64_t count = basl_atomic_load(&g_rwlock_count);
    if (handle < 0 || handle >= count || !g_rwlocks[handle]) {
        return push_bool(vm, 0, error);
    }
    
    basl_platform_rwlock_unlock(g_rwlocks[handle]);
    return push_bool(vm, 1, error);
}

/* ── Module definition ───────────────────────────────────────── */

static const int i64_param[] = { BASL_TYPE_I64 };
static const int i64_i64_param[] = { BASL_TYPE_I64, BASL_TYPE_I64 };
static const int object_param[] = { BASL_TYPE_OBJECT };

static const basl_native_module_function_t thread_funcs[] = {
    /* Thread management */
    {"spawn", 5U, thread_spawn, 1U, object_param, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"join", 4U, thread_join, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"current_id", 10U, thread_current_id, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"yield", 5U, thread_yield, 0U, NULL, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"sleep", 5U, thread_sleep, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    
    /* Mutex */
    {"mutex", 5U, thread_mutex, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"lock", 4U, mutex_lock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"unlock", 6U, mutex_unlock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"try_lock", 8U, mutex_try_lock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    
    /* Condition variable */
    {"cond", 4U, thread_cond, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"wait", 4U, cond_wait, 2U, i64_i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"signal", 6U, cond_signal, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"broadcast", 9U, cond_broadcast, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    
    /* RWLock */
    {"rwlock", 6U, thread_rwlock, 0U, NULL, BASL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"read_lock", 9U, rwlock_read_lock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"write_lock", 10U, rwlock_write_lock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"rw_unlock", 9U, rwlock_unlock, 1U, i64_param, BASL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
};

BASL_API const basl_native_module_t basl_stdlib_thread = {
    "thread", 6U,
    thread_funcs, sizeof(thread_funcs) / sizeof(thread_funcs[0]),
    NULL, 0U
};
