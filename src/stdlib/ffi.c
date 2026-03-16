/* BASL standard library: ffi module.
 *
 * Provides dynamic loading of C shared libraries and calling of
 * foreign functions.  All library handles and function pointers are
 * represented as i64 values in BASL.
 *
 * Functions:
 *   ffi.open(string path) -> i64          load a shared library
 *   ffi.sym(i64 lib, string name) -> i64  look up a symbol
 *   ffi.close(i64 lib)                    close a library
 *
 * Typed call functions (signature encoded in name):
 *   ffi.call_vi(i64 fn) -> i32                    void -> i32
 *   ffi.call_ii(i64 fn, i64 a) -> i32             i32 -> i32
 *   ffi.call_iii(i64 fn, i64 a, i64 b) -> i32     (i32,i32) -> i32
 *   ffi.call_vv(i64 fn)                            void -> void
 *   ffi.call_iv(i64 fn, i64 a)                     i32 -> void
 *   ffi.call_vd(i64 fn) -> f64                     void -> f64
 *   ffi.call_dd(i64 fn, f64 a) -> f64              f64 -> f64
 *   ffi.call_ddd(i64 fn, f64 a, f64 b) -> f64      (f64,f64) -> f64
 *   ffi.call_vp(i64 fn) -> i64                     void -> ptr
 *   ffi.call_pp(i64 fn, i64 a) -> i64              ptr -> ptr
 *   ffi.call_ppp(i64 fn, i64 a, i64 b) -> i64      (ptr,ptr) -> ptr
 *   ffi.call_pi(i64 fn, i64 a) -> i32              ptr -> i32
 *   ffi.call_pv(i64 fn, i64 a)                     ptr -> void
 *   ffi.call_si(i64 fn, string a) -> i32           string -> i32
 *   ffi.call_ssi(i64 fn, string a, string b) -> i32 (str,str) -> i32
 */
#include <stdint.h>
#include <string.h>

#include "basl/native_module.h"
#include "basl/type.h"
#include "basl/value.h"
#include "basl/vm.h"

#include "internal/basl_nanbox.h"
#include "internal/ffi_trampoline.h"
#include "platform/platform.h"

/* ── helpers ─────────────────────────────────────────────────────── */

static basl_status_t push_i64(basl_vm_t *vm, int64_t v, basl_error_t *error) {
    basl_value_t val = basl_nanbox_encode_int(v);
    return basl_vm_stack_push(vm, &val, error);
}

static basl_status_t push_i32(basl_vm_t *vm, int32_t v, basl_error_t *error) {
    basl_value_t val = basl_nanbox_encode_i32(v);
    return basl_vm_stack_push(vm, &val, error);
}

static basl_status_t push_f64(basl_vm_t *vm, double v, basl_error_t *error) {
    basl_value_t val = basl_nanbox_encode_double(v);
    return basl_vm_stack_push(vm, &val, error);
}

static int64_t pop_i64(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    return basl_nanbox_decode_int(v);
}

static double pop_f64(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    return basl_nanbox_decode_double(v);
}

/* Extract a C string from a BASL string object on the stack. */
static const char *pop_str(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    const basl_object_t *obj = (const basl_object_t *)basl_nanbox_decode_ptr(v);
    if (obj && basl_object_type(obj) == BASL_OBJECT_STRING)
        return basl_string_object_c_str(obj);
    return "";
}

/* ── ffi.open(string path) -> i64 ────────────────────────────────── */

static basl_status_t basl_ffi_open(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    const char *path = pop_str(vm, base, 0);
    void *handle = NULL;
    basl_status_t s;

    basl_vm_stack_pop_n(vm, arg_count);
    s = basl_platform_dlopen(path, &handle, error);
    if (s != BASL_STATUS_OK) return s;
    return push_i64(vm, (int64_t)(intptr_t)handle, error);
}

/* ── ffi.sym(i64 lib, string name) -> i64 ────────────────────────── */

static basl_status_t basl_ffi_sym(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *handle = (void *)(intptr_t)pop_i64(vm, base, 0);
    const char *name = pop_str(vm, base, 1);
    void *sym = NULL;
    basl_status_t s;

    basl_vm_stack_pop_n(vm, arg_count);
    s = basl_platform_dlsym(handle, name, &sym, error);
    if (s != BASL_STATUS_OK) return s;
    return push_i64(vm, (int64_t)(intptr_t)sym, error);
}

/* ── ffi.close(i64 lib) ──────────────────────────────────────────── */

static basl_status_t basl_ffi_close(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *handle = (void *)(intptr_t)pop_i64(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_platform_dlclose(handle, error);
}

/* ── Typed call functions ────────────────────────────────────────── */

/* void -> i32 */
static basl_status_t basl_ffi_call_vi(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i32(vm, basl_ffi_call_void_to_i32(fn), error);
}

/* i32 -> i32 */
static basl_status_t basl_ffi_call_ii(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    int a = (int)pop_i64(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i32(vm, basl_ffi_call_i32_to_i32(fn, a), error);
}

/* (i32, i32) -> i32 */
static basl_status_t basl_ffi_call_iii(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    int a = (int)pop_i64(vm, base, 1);
    int b = (int)pop_i64(vm, base, 2);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i32(vm, basl_ffi_call_i32_i32_to_i32(fn, a, b), error);
}

/* void -> void */
static basl_status_t basl_ffi_call_vv(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    (void)error;
    basl_vm_stack_pop_n(vm, arg_count);
    basl_ffi_call_void_to_void(fn);
    return BASL_STATUS_OK;
}

/* i32 -> void */
static basl_status_t basl_ffi_call_iv(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    int a = (int)pop_i64(vm, base, 1);
    (void)error;
    basl_vm_stack_pop_n(vm, arg_count);
    basl_ffi_call_i32_to_void(fn, a);
    return BASL_STATUS_OK;
}

/* void -> f64 */
static basl_status_t basl_ffi_call_vd(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_f64(vm, basl_ffi_call_void_to_f64(fn), error);
}

/* f64 -> f64 */
static basl_status_t basl_ffi_call_dd(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    double a = pop_f64(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_f64(vm, basl_ffi_call_f64_to_f64(fn, a), error);
}

/* (f64, f64) -> f64 */
static basl_status_t basl_ffi_call_ddd(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    double a = pop_f64(vm, base, 1);
    double b = pop_f64(vm, base, 2);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_f64(vm, basl_ffi_call_f64_f64_to_f64(fn, a, b), error);
}

/* void -> ptr (as i64) */
static basl_status_t basl_ffi_call_vp(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, (int64_t)(intptr_t)basl_ffi_call_void_to_ptr(fn), error);
}

/* ptr -> ptr */
static basl_status_t basl_ffi_call_pp(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    void *a = (void *)(intptr_t)pop_i64(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, (int64_t)(intptr_t)basl_ffi_call_ptr_to_ptr(fn, a), error);
}

/* (ptr, ptr) -> ptr */
static basl_status_t basl_ffi_call_ppp(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    void *a = (void *)(intptr_t)pop_i64(vm, base, 1);
    void *b = (void *)(intptr_t)pop_i64(vm, base, 2);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i64(vm, (int64_t)(intptr_t)basl_ffi_call_ptr_ptr_to_ptr(fn, a, b), error);
}

/* ptr -> i32 */
static basl_status_t basl_ffi_call_pi(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    void *a = (void *)(intptr_t)pop_i64(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i32(vm, basl_ffi_call_ptr_to_i32(fn, a), error);
}

/* ptr -> void */
static basl_status_t basl_ffi_call_pv(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    void *a = (void *)(intptr_t)pop_i64(vm, base, 1);
    (void)error;
    basl_vm_stack_pop_n(vm, arg_count);
    basl_ffi_call_ptr_to_void(fn, a);
    return BASL_STATUS_OK;
}

/* string -> i32 */
static basl_status_t basl_ffi_call_si(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    const char *a = pop_str(vm, base, 1);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i32(vm, basl_ffi_call_str_to_i32(fn, a), error);
}

/* (string, string) -> i32 */
static basl_status_t basl_ffi_call_ssi(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    const char *a = pop_str(vm, base, 1);
    const char *b = pop_str(vm, base, 2);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i32(vm, basl_ffi_call_str_str_to_i32(fn, a, b), error);
}

/* (ptr, i32) -> i32 */
static basl_status_t basl_ffi_call_pii(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *fn = (void *)(intptr_t)pop_i64(vm, base, 0);
    void *a = (void *)(intptr_t)pop_i64(vm, base, 1);
    int b = (int)pop_i64(vm, base, 2);
    basl_vm_stack_pop_n(vm, arg_count);
    return push_i32(vm, basl_ffi_call_ptr_i32_to_i32(fn, a, b), error);
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int p_str[] = { BASL_TYPE_STRING };
static const int p_i64[] = { BASL_TYPE_I64 };
static const int p_i64_str[] = { BASL_TYPE_I64, BASL_TYPE_STRING };
static const int p_i64_i64[] = { BASL_TYPE_I64, BASL_TYPE_I64 };
static const int p_i64_i64_i64[] = { BASL_TYPE_I64, BASL_TYPE_I64, BASL_TYPE_I64 };
static const int p_i64_f64[] = { BASL_TYPE_I64, BASL_TYPE_F64 };
static const int p_i64_f64_f64[] = { BASL_TYPE_I64, BASL_TYPE_F64, BASL_TYPE_F64 };
static const int p_i64_str2[] = { BASL_TYPE_I64, BASL_TYPE_STRING };
static const int p_i64_str_str[] = { BASL_TYPE_I64, BASL_TYPE_STRING, BASL_TYPE_STRING };

#define F(n, nl, fn, pc, pt, rt) { n, nl, fn, pc, pt, rt, 1, NULL }
#define FV(n, nl, fn, pc, pt) { n, nl, fn, pc, pt, BASL_TYPE_VOID, 0, NULL }

static const basl_native_module_function_t basl_ffi_functions[] = {
    /* Library management */
    F("open",     4U, basl_ffi_open,     1U, p_str,           BASL_TYPE_I64),
    F("sym",      3U, basl_ffi_sym,      2U, p_i64_str,       BASL_TYPE_I64),
    FV("close",   5U, basl_ffi_close,    1U, p_i64),

    /* Typed call functions: name encodes signature */
    F("call_vi",  7U, basl_ffi_call_vi,  1U, p_i64,           BASL_TYPE_I32),
    F("call_ii",  7U, basl_ffi_call_ii,  2U, p_i64_i64,       BASL_TYPE_I32),
    F("call_iii", 8U, basl_ffi_call_iii, 3U, p_i64_i64_i64,   BASL_TYPE_I32),
    FV("call_vv", 7U, basl_ffi_call_vv,  1U, p_i64),
    FV("call_iv", 7U, basl_ffi_call_iv,  2U, p_i64_i64),
    F("call_vd",  7U, basl_ffi_call_vd,  1U, p_i64,           BASL_TYPE_F64),
    F("call_dd",  7U, basl_ffi_call_dd,  2U, p_i64_f64,       BASL_TYPE_F64),
    F("call_ddd", 8U, basl_ffi_call_ddd, 3U, p_i64_f64_f64,   BASL_TYPE_F64),
    F("call_vp",  7U, basl_ffi_call_vp,  1U, p_i64,           BASL_TYPE_I64),
    F("call_pp",  7U, basl_ffi_call_pp,  2U, p_i64_i64,       BASL_TYPE_I64),
    F("call_ppp", 8U, basl_ffi_call_ppp, 3U, p_i64_i64_i64,   BASL_TYPE_I64),
    F("call_pi",  7U, basl_ffi_call_pi,  2U, p_i64_i64,       BASL_TYPE_I32),
    FV("call_pv", 7U, basl_ffi_call_pv,  2U, p_i64_i64),
    F("call_si",  7U, basl_ffi_call_si,  2U, p_i64_str2,      BASL_TYPE_I32),
    F("call_ssi", 8U, basl_ffi_call_ssi, 3U, p_i64_str_str,   BASL_TYPE_I32),
    F("call_pii", 8U, basl_ffi_call_pii, 3U, p_i64_i64_i64,   BASL_TYPE_I32),
};

#undef F
#undef FV

BASL_API const basl_native_module_t basl_stdlib_ffi = {
    "ffi", 3U,
    basl_ffi_functions,
    sizeof(basl_ffi_functions) / sizeof(basl_ffi_functions[0]),
    NULL, 0U
};
