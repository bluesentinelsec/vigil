/* VIGIL standard library: thread module.
 *
 * Provides threading primitives: mutexes, condition variables,
 * read-write locks, and utilities.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"
#include "vigil/runtime.h"
#include "platform/platform.h"

#include "internal/vigil_nanbox.h"
#include "internal/vigil_internal.h"

/* ── Dynamic handle registry ─────────────────────────────────── */

#define INITIAL_CAPACITY 256

typedef struct {
    void **items;
    volatile int64_t count;
    int64_t capacity;
    vigil_platform_mutex_t *lock;
} handle_registry_t;

static void registry_init(handle_registry_t *r) {
    r->capacity = INITIAL_CAPACITY;
    r->items = calloc((size_t)r->capacity, sizeof(void *));
    r->count = 0;
    vigil_platform_mutex_create(&r->lock, NULL);
}

static int64_t registry_alloc(handle_registry_t *r) {
    vigil_platform_mutex_lock(r->lock);
    int64_t idx = vigil_atomic_add(&r->count, 1);
    if (idx >= r->capacity) {
        int64_t new_cap = r->capacity * 2;
        while (new_cap <= idx) new_cap *= 2;
        void **new_items = calloc((size_t)new_cap, sizeof(void *));
        if (new_items) {
            memcpy(new_items, r->items, (size_t)r->capacity * sizeof(void *));
            free(r->items);
            r->items = new_items;
            r->capacity = new_cap;
        }
    }
    vigil_platform_mutex_unlock(r->lock);
    return idx;
}

static void *registry_get(handle_registry_t *r, int64_t handle) {
    int64_t count = vigil_atomic_load(&r->count);
    if (handle < 0 || handle >= count) return NULL;
    return r->items[handle];
}

static void registry_set(handle_registry_t *r, int64_t handle, void *val) {
    if (handle >= 0 && handle < r->capacity) {
        r->items[handle] = val;
    }
}

static handle_registry_t g_mutexes;
static handle_registry_t g_conds;
static handle_registry_t g_rwlocks;

static int g_registries_inited = 0;

static void ensure_registries(void) {
    if (!g_registries_inited) {
        registry_init(&g_mutexes);
        registry_init(&g_conds);
        registry_init(&g_rwlocks);
        g_registries_inited = 1;
    }
}

/* ── Helpers ─────────────────────────────────────────────────── */

static vigil_status_t push_bool(vigil_vm_t *vm, int val, vigil_error_t *error) {
    vigil_value_t v;
    vigil_value_init_bool(&v, val);
    return vigil_vm_stack_push(vm, &v, error);
}

static vigil_status_t push_i64(vigil_vm_t *vm, int64_t val, vigil_error_t *error) {
    vigil_value_t v;
    vigil_value_init_int(&v, val);
    return vigil_vm_stack_push(vm, &v, error);
}

static int64_t get_i64_arg(vigil_vm_t *vm, size_t base, size_t idx) {
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (vigil_nanbox_is_int(v)) return vigil_nanbox_decode_int(v);
    return 0;
}

/* ── Spawn support ───────────────────────────────────────────── */

#define INITIAL_THREAD_CAPACITY 256

typedef struct {
    vigil_runtime_t *runtime;
    vigil_object_t *function;
    vigil_platform_thread_t *thread;
    int in_use;
    volatile int64_t result; /* return value from thread function */
} thread_slot_t;

static thread_slot_t *g_threads = NULL;
static volatile int64_t g_thread_count = 0;
static int64_t g_thread_capacity = 0;
static vigil_platform_mutex_t *g_thread_lock = NULL;

static void ensure_threads(void) {
    if (!g_threads) {
        g_thread_capacity = INITIAL_THREAD_CAPACITY;
        g_threads = calloc((size_t)g_thread_capacity, sizeof(thread_slot_t));
        vigil_platform_mutex_create(&g_thread_lock, NULL);
    }
}

static int64_t alloc_thread_slot(void) {
    vigil_platform_mutex_lock(g_thread_lock);
    int64_t idx = vigil_atomic_add(&g_thread_count, 1);
    if (idx >= g_thread_capacity) {
        int64_t new_cap = g_thread_capacity * 2;
        while (new_cap <= idx) new_cap *= 2;
        thread_slot_t *new_slots = calloc((size_t)new_cap, sizeof(thread_slot_t));
        if (new_slots) {
            memcpy(new_slots, g_threads, (size_t)g_thread_capacity * sizeof(thread_slot_t));
            free(g_threads);
            g_threads = new_slots;
            g_thread_capacity = new_cap;
        }
    }
    vigil_platform_mutex_unlock(g_thread_lock);
    return idx;
}

static void thread_spawn_entry(void *arg) {
    thread_slot_t *slot = (thread_slot_t *)arg;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    vigil_value_t out = {0};

    if (vigil_vm_open(&vm, slot->runtime, NULL, &error) == VIGIL_STATUS_OK) {
        vigil_vm_execute_function(vm, slot->function, &out, &error);
        /* Capture return value if it's an integer */
        if (vigil_nanbox_is_int(out)) {
            vigil_atomic_store(&slot->result, vigil_nanbox_decode_int(out));
        }
        vigil_vm_close(&vm);
    }
    vigil_object_release(&slot->function);
}

static vigil_status_t thread_spawn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    ensure_threads();
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t val = vigil_vm_stack_get(vm, base);

    vigil_object_t *fn = vigil_value_as_object(&val);
    if (fn == NULL ||
        (vigil_object_type(fn) != VIGIL_OBJECT_FUNCTION &&
         vigil_object_type(fn) != VIGIL_OBJECT_CLOSURE)) {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_i64(vm, -1, error);
    }

    int64_t idx = alloc_thread_slot();

    /* Retain before popping so the closure isn't freed */
    vigil_object_retain(fn);
    vigil_vm_stack_pop_n(vm, arg_count);

    g_threads[idx].runtime = vigil_vm_runtime(vm);
    g_threads[idx].function = fn;
    g_threads[idx].in_use = 1;
    g_threads[idx].result = 0;

    vigil_status_t st = vigil_platform_thread_create(
        &g_threads[idx].thread, thread_spawn_entry, &g_threads[idx], error);
    if (st != VIGIL_STATUS_OK) {
        vigil_object_release(&g_threads[idx].function);
        g_threads[idx].in_use = 0;
        return push_i64(vm, -1, error);
    }
    return push_i64(vm, idx, error);
}

/* thread.join(handle) -> i64 (returns thread's return value) */
static vigil_status_t thread_join(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    int64_t count = vigil_atomic_load(&g_thread_count);
    if (handle < 0 || handle >= count || !g_threads[handle].in_use) {
        return push_i64(vm, INT64_MIN, error);
    }
    vigil_platform_thread_join(g_threads[handle].thread, error);
    int64_t result = vigil_atomic_load(&g_threads[handle].result);
    g_threads[handle].in_use = 0;
    return push_i64(vm, result, error);
}

/* thread.detach(handle) -> bool */
static vigil_status_t thread_detach(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    int64_t count = vigil_atomic_load(&g_thread_count);
    if (handle < 0 || handle >= count || !g_threads[handle].in_use) {
        return push_bool(vm, 0, error);
    }
    vigil_platform_thread_detach(g_threads[handle].thread, error);
    g_threads[handle].in_use = 0;
    return push_bool(vm, 1, error);
}

/* ── Thread functions ────────────────────────────────────────── */

static vigil_status_t thread_current_id(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    vigil_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, (int64_t)vigil_platform_thread_current_id(), error);
}

static vigil_status_t thread_yield(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_platform_thread_yield();
    return push_bool(vm, 1, error);
}

static vigil_status_t thread_sleep(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t ms = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (ms > 0) vigil_platform_thread_sleep((uint64_t)ms);
    return push_bool(vm, 1, error);
}

/* ── Mutex functions ─────────────────────────────────────────── */

static vigil_status_t thread_mutex(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    ensure_registries();
    vigil_vm_stack_pop_n(vm, arg_count);
    
    int64_t handle = registry_alloc(&g_mutexes);
    
    vigil_platform_mutex_t *m = NULL;
    vigil_status_t s = vigil_platform_mutex_create(&m, error);
    if (s != VIGIL_STATUS_OK) {
        return push_i64(vm, -1, error);
    }
    
    registry_set(&g_mutexes, handle, m);
    return push_i64(vm, handle, error);
}

static vigil_status_t mutex_lock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_mutex_t *m = registry_get(&g_mutexes, handle);
    if (!m) return push_bool(vm, 0, error);
    
    vigil_platform_mutex_lock(m);
    return push_bool(vm, 1, error);
}

static vigil_status_t mutex_unlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_mutex_t *m = registry_get(&g_mutexes, handle);
    if (!m) return push_bool(vm, 0, error);
    
    vigil_platform_mutex_unlock(m);
    return push_bool(vm, 1, error);
}

static vigil_status_t mutex_try_lock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_mutex_t *m = registry_get(&g_mutexes, handle);
    if (!m) return push_bool(vm, 0, error);
    
    int acquired = vigil_platform_mutex_trylock(m);
    return push_bool(vm, acquired, error);
}

static vigil_status_t mutex_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_mutex_t *m = registry_get(&g_mutexes, handle);
    if (!m) return push_bool(vm, 0, error);
    
    vigil_platform_mutex_destroy(m);
    registry_set(&g_mutexes, handle, NULL);
    return push_bool(vm, 1, error);
}

/* ── Condition variable functions ────────────────────────────── */

static vigil_status_t thread_cond(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    ensure_registries();
    vigil_vm_stack_pop_n(vm, arg_count);
    
    int64_t handle = registry_alloc(&g_conds);
    
    vigil_platform_cond_t *c = NULL;
    vigil_status_t s = vigil_platform_cond_create(&c, error);
    if (s != VIGIL_STATUS_OK) {
        return push_i64(vm, -1, error);
    }
    
    registry_set(&g_conds, handle, c);
    return push_i64(vm, handle, error);
}

static vigil_status_t cond_wait(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cond_h = get_i64_arg(vm, base, 0);
    int64_t mutex_h = arg_count > 1 ? get_i64_arg(vm, base, 1) : -1;
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_cond_t *c = registry_get(&g_conds, cond_h);
    vigil_platform_mutex_t *m = registry_get(&g_mutexes, mutex_h);
    if (!c || !m) return push_bool(vm, 0, error);
    
    vigil_platform_cond_wait(c, m);
    return push_bool(vm, 1, error);
}

/* thread.wait_timeout(cond, mutex, ms) -> bool (true if signalled, false on timeout) */
static vigil_status_t cond_wait_timeout(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t cond_h = get_i64_arg(vm, base, 0);
    int64_t mutex_h = arg_count > 1 ? get_i64_arg(vm, base, 1) : -1;
    int64_t ms = arg_count > 2 ? get_i64_arg(vm, base, 2) : 0;
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_cond_t *c = registry_get(&g_conds, cond_h);
    vigil_platform_mutex_t *m = registry_get(&g_mutexes, mutex_h);
    if (!c || !m) return push_bool(vm, 0, error);
    
    int signalled = vigil_platform_cond_timedwait(c, m, (uint64_t)(ms > 0 ? ms : 0));
    return push_bool(vm, signalled, error);
}

static vigil_status_t cond_signal(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_cond_t *c = registry_get(&g_conds, handle);
    if (!c) return push_bool(vm, 0, error);
    
    vigil_platform_cond_signal(c);
    return push_bool(vm, 1, error);
}

static vigil_status_t cond_broadcast(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_cond_t *c = registry_get(&g_conds, handle);
    if (!c) return push_bool(vm, 0, error);
    
    vigil_platform_cond_broadcast(c);
    return push_bool(vm, 1, error);
}

static vigil_status_t cond_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_cond_t *c = registry_get(&g_conds, handle);
    if (!c) return push_bool(vm, 0, error);
    
    vigil_platform_cond_destroy(c);
    registry_set(&g_conds, handle, NULL);
    return push_bool(vm, 1, error);
}

/* ── RWLock functions ────────────────────────────────────────── */

static vigil_status_t thread_rwlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    ensure_registries();
    vigil_vm_stack_pop_n(vm, arg_count);
    
    int64_t handle = registry_alloc(&g_rwlocks);
    
    vigil_platform_rwlock_t *rw = NULL;
    vigil_status_t s = vigil_platform_rwlock_create(&rw, error);
    if (s != VIGIL_STATUS_OK) {
        return push_i64(vm, -1, error);
    }
    
    registry_set(&g_rwlocks, handle, rw);
    return push_i64(vm, handle, error);
}

static vigil_status_t rwlock_read_lock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_rwlock_t *rw = registry_get(&g_rwlocks, handle);
    if (!rw) return push_bool(vm, 0, error);
    
    vigil_platform_rwlock_rdlock(rw);
    return push_bool(vm, 1, error);
}

static vigil_status_t rwlock_write_lock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_rwlock_t *rw = registry_get(&g_rwlocks, handle);
    if (!rw) return push_bool(vm, 0, error);
    
    vigil_platform_rwlock_wrlock(rw);
    return push_bool(vm, 1, error);
}

static vigil_status_t rwlock_unlock(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_rwlock_t *rw = registry_get(&g_rwlocks, handle);
    if (!rw) return push_bool(vm, 0, error);
    
    vigil_platform_rwlock_unlock(rw);
    return push_bool(vm, 1, error);
}

static vigil_status_t rwlock_destroy(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error) {
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int64_t handle = get_i64_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    
    vigil_platform_rwlock_t *rw = registry_get(&g_rwlocks, handle);
    if (!rw) return push_bool(vm, 0, error);
    
    vigil_platform_rwlock_destroy(rw);
    registry_set(&g_rwlocks, handle, NULL);
    return push_bool(vm, 1, error);
}

/* ── Module definition ───────────────────────────────────────── */

static const int i64_param[] = { VIGIL_TYPE_I64 };
static const int i64_i64_param[] = { VIGIL_TYPE_I64, VIGIL_TYPE_I64 };
static const int i64_i64_i64_param[] = { VIGIL_TYPE_I64, VIGIL_TYPE_I64, VIGIL_TYPE_I64 };
static const int object_param[] = { VIGIL_TYPE_OBJECT };

static const vigil_native_module_function_t thread_funcs[] = {
    /* Thread management */
    {"spawn", 5U, thread_spawn, 1U, object_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"join", 4U, thread_join, 1U, i64_param, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"detach", 6U, thread_detach, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"current_id", 10U, thread_current_id, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"yield", 5U, thread_yield, 0U, NULL, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"sleep", 5U, thread_sleep, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    
    /* Mutex */
    {"mutex", 5U, thread_mutex, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"lock", 4U, mutex_lock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"unlock", 6U, mutex_unlock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"try_lock", 8U, mutex_try_lock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"mutex_destroy", 13U, mutex_destroy, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    
    /* Condition variable */
    {"cond", 4U, thread_cond, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"wait", 4U, cond_wait, 2U, i64_i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"wait_timeout", 12U, cond_wait_timeout, 3U, i64_i64_i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"signal", 6U, cond_signal, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"broadcast", 9U, cond_broadcast, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"cond_destroy", 12U, cond_destroy, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    
    /* RWLock */
    {"rwlock", 6U, thread_rwlock, 0U, NULL, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL},
    {"read_lock", 9U, rwlock_read_lock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"write_lock", 10U, rwlock_write_lock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"rw_unlock", 9U, rwlock_unlock, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
    {"rwlock_destroy", 14U, rwlock_destroy, 1U, i64_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_thread = {
    "thread", 6U,
    thread_funcs, sizeof(thread_funcs) / sizeof(thread_funcs[0]),
    NULL, 0U
};
