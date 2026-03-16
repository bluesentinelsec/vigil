/* BASL standard library: unsafe module.
 *
 * Low-level memory operations for FFI interop.
 *
 * Functions:
 *   unsafe.alloc(i32 size) -> i64          allocate a byte buffer
 *   unsafe.free(i64 buf)                   free a buffer
 *   unsafe.get(i64 buf, i32 index) -> i32  read byte at index
 *   unsafe.set(i64 buf, i32 index, i32 v)  write byte at index
 *   unsafe.get_i32(i64 buf, i32 off) -> i32   read little-endian i32
 *   unsafe.set_i32(i64 buf, i32 off, i32 v)   write little-endian i32
 *   unsafe.get_i64(i64 buf, i32 off) -> i64   read little-endian i64
 *   unsafe.set_i64(i64 buf, i32 off, i64 v)   write little-endian i64
 *   unsafe.ptr(i64 buf) -> i64             raw pointer to buffer data
 *   unsafe.null() -> i64                   null pointer constant
 *   unsafe.cb_alloc() -> i64               allocate a callback slot (ptr)
 *   unsafe.cb_free(i32 slot)               free a callback slot
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_internal.h"
#include "internal/basl_nanbox.h"
#include "internal/ffi_trampoline.h"

/* ── helpers ─────────────────────────────────────────────────────── */

static basl_status_t push_i64(basl_vm_t *vm, int64_t v, basl_error_t *error) {
    basl_value_t val = basl_nanbox_encode_int(v);
    return basl_vm_stack_push(vm, &val, error);
}

static basl_status_t push_i32(basl_vm_t *vm, int32_t v, basl_error_t *error) {
    basl_value_t val = basl_nanbox_encode_i32(v);
    return basl_vm_stack_push(vm, &val, error);
}

static int64_t arg_i64(basl_vm_t *vm, size_t base, size_t idx) {
    return basl_nanbox_decode_int(basl_vm_stack_get(vm, base + idx));
}

static int32_t arg_i32(basl_vm_t *vm, size_t base, size_t idx) {
    return basl_nanbox_decode_i32(basl_vm_stack_get(vm, base + idx));
}

/* ── Buffer tracking ─────────────────────────────────────────────── */
/* Simple table so we can bounds-check and prevent use-after-free.    */

typedef struct {
    uint8_t *data;
    int32_t  size;
} buf_entry_t;

#define MAX_BUFS 256
static buf_entry_t g_bufs[MAX_BUFS];

static int buf_find_slot(void) {
    for (int i = 0; i < MAX_BUFS; i++)
        if (!g_bufs[i].data) return i;
    return -1;
}

/* ── unsafe.alloc(i32 size) -> i64 ───────────────────────────────── */

static basl_status_t basl_unsafe_alloc(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int32_t size = arg_i32(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);

    if (size <= 0) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "unsafe.alloc: size must be > 0");
        return BASL_STATUS_INTERNAL;
    }
    int slot = buf_find_slot();
    if (slot < 0) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "unsafe.alloc: too many buffers");
        return BASL_STATUS_INTERNAL;
    }
    g_bufs[slot].data = (uint8_t *)calloc((size_t)size, 1);
    if (!g_bufs[slot].data) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "unsafe.alloc: out of memory");
        return BASL_STATUS_INTERNAL;
    }
    g_bufs[slot].size = size;
    return push_i64(vm, (int64_t)slot, error);
}

/* ── unsafe.free(i64 buf) ────────────────────────────────────────── */

static basl_status_t basl_unsafe_free(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    (void)error;
    basl_vm_stack_pop_n(vm, arg_count);
    if (slot >= 0 && slot < MAX_BUFS && g_bufs[slot].data) {
        free(g_bufs[slot].data);
        g_bufs[slot].data = NULL;
        g_bufs[slot].size = 0;
    }
    return BASL_STATUS_OK;
}

/* ── unsafe.get(i64 buf, i32 index) -> i32 ───────────────────────── */

static basl_status_t basl_unsafe_get(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t idx = arg_i32(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data ||
        idx < 0 || idx >= g_bufs[slot].size) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "unsafe.get: index out of bounds");
        return BASL_STATUS_INTERNAL;
    }
    return push_i32(vm, (int32_t)g_bufs[slot].data[idx], error);
}

/* ── unsafe.set(i64 buf, i32 index, i32 value) ──────────────────── */

static basl_status_t basl_unsafe_set(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t idx = arg_i32(vm, base, 1);
    int32_t val = arg_i32(vm, base, 2);
    basl_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data ||
        idx < 0 || idx >= g_bufs[slot].size) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "unsafe.set: index out of bounds");
        return BASL_STATUS_INTERNAL;
    }
    g_bufs[slot].data[idx] = (uint8_t)val;
    return BASL_STATUS_OK;
}

/* ── unsafe.get_i32(i64 buf, i32 offset) -> i32 ─────────────────── */

static basl_status_t basl_unsafe_get_i32(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data ||
        off < 0 || off + 4 > g_bufs[slot].size) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "unsafe.get_i32: out of bounds");
        return BASL_STATUS_INTERNAL;
    }
    int32_t v;
    memcpy(&v, g_bufs[slot].data + off, 4);
    return push_i32(vm, v, error);
}

/* ── unsafe.set_i32(i64 buf, i32 offset, i32 value) ─────────────── */

static basl_status_t basl_unsafe_set_i32(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    int32_t val = arg_i32(vm, base, 2);
    basl_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data ||
        off < 0 || off + 4 > g_bufs[slot].size) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "unsafe.set_i32: out of bounds");
        return BASL_STATUS_INTERNAL;
    }
    memcpy(g_bufs[slot].data + off, &val, 4);
    return BASL_STATUS_OK;
}

/* ── unsafe.get_i64(i64 buf, i32 offset) -> i64 ─────────────────── */

static basl_status_t basl_unsafe_get_i64(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data ||
        off < 0 || off + 8 > g_bufs[slot].size) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "unsafe.get_i64: out of bounds");
        return BASL_STATUS_INTERNAL;
    }
    int64_t v;
    memcpy(&v, g_bufs[slot].data + off, 8);
    return push_i64(vm, v, error);
}

/* ── unsafe.set_i64(i64 buf, i32 offset, i64 value) ─────────────── */

static basl_status_t basl_unsafe_set_i64(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    int32_t off = arg_i32(vm, base, 1);
    int64_t val = arg_i64(vm, base, 2);
    basl_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data ||
        off < 0 || off + 8 > g_bufs[slot].size) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "unsafe.set_i64: out of bounds");
        return BASL_STATUS_INTERNAL;
    }
    memcpy(g_bufs[slot].data + off, &val, 8);
    return BASL_STATUS_OK;
}

/* ── unsafe.ptr(i64 buf) -> i64 ──────────────────────────────────── */

static basl_status_t basl_unsafe_ptr(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int slot = (int)arg_i64(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);

    if (slot < 0 || slot >= MAX_BUFS || !g_bufs[slot].data) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "unsafe.ptr: invalid buffer");
        return BASL_STATUS_INTERNAL;
    }
    return push_i64(vm, (int64_t)(intptr_t)g_bufs[slot].data, error);
}

/* ── unsafe.null() -> i64 ────────────────────────────────────────── */

static basl_status_t basl_unsafe_null(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, 0, error);
}

/* ── unsafe.cb_alloc() -> i64 ────────────────────────────────────── */

static basl_status_t basl_unsafe_cb_alloc(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    basl_vm_stack_pop_n(vm, arg_count);
    void *ptr = NULL;
    int slot = basl_ffi_callback_alloc(&ptr);
    if (slot < 0) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "unsafe.cb_alloc: all callback slots in use");
        return BASL_STATUS_INTERNAL;
    }
    return push_i64(vm, (int64_t)(intptr_t)ptr, error);
}

/* ── unsafe.cb_free(i32 slot) ────────────────────────────────────── */

static basl_status_t basl_unsafe_cb_free(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int32_t slot = arg_i32(vm, base, 0);
    (void)error;
    basl_vm_stack_pop_n(vm, arg_count);
    basl_ffi_callback_free(slot);
    return BASL_STATUS_OK;
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int p_i32[] = { BASL_TYPE_I32 };
static const int p_i64[] = { BASL_TYPE_I64 };
static const int p_i64_i32[] = { BASL_TYPE_I64, BASL_TYPE_I32 };
static const int p_i64_i32_i32[] = { BASL_TYPE_I64, BASL_TYPE_I32, BASL_TYPE_I32 };
static const int p_i64_i32_i64[] = { BASL_TYPE_I64, BASL_TYPE_I32, BASL_TYPE_I64 };

#define F(n, nl, fn, pc, pt, rt) { n, nl, fn, pc, pt, rt, 1, NULL }
#define FV(n, nl, fn, pc, pt) { n, nl, fn, pc, pt, BASL_TYPE_VOID, 0, NULL }

static const basl_native_module_function_t basl_unsafe_functions[] = {
    F("alloc",    5U, basl_unsafe_alloc,    1U, p_i32,         BASL_TYPE_I64),
    FV("free",    4U, basl_unsafe_free,     1U, p_i64),
    F("get",      3U, basl_unsafe_get,      2U, p_i64_i32,     BASL_TYPE_I32),
    FV("set",     3U, basl_unsafe_set,      3U, p_i64_i32_i32),
    F("get_i32",  7U, basl_unsafe_get_i32,  2U, p_i64_i32,     BASL_TYPE_I32),
    FV("set_i32", 7U, basl_unsafe_set_i32,  3U, p_i64_i32_i32),
    F("get_i64",  7U, basl_unsafe_get_i64,  2U, p_i64_i32,     BASL_TYPE_I64),
    FV("set_i64", 7U, basl_unsafe_set_i64,  3U, p_i64_i32_i64),
    F("ptr",      3U, basl_unsafe_ptr,      1U, p_i64,         BASL_TYPE_I64),
    F("null",     4U, basl_unsafe_null,     0U, NULL,           BASL_TYPE_I64),
    F("cb_alloc", 8U, basl_unsafe_cb_alloc, 0U, NULL,           BASL_TYPE_I64),
    FV("cb_free", 7U, basl_unsafe_cb_free,  1U, p_i32),
};

#undef F
#undef FV

BASL_API const basl_native_module_t basl_stdlib_unsafe = {
    "unsafe", 6U,
    basl_unsafe_functions,
    sizeof(basl_unsafe_functions) / sizeof(basl_unsafe_functions[0]),
    NULL, 0U
};
