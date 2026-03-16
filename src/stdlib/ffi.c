/* BASL standard library: ffi module.
 *
 * Provides dynamic loading of C shared libraries and calling of
 * foreign functions.  All library handles and function pointers are
 * represented as i64 values in BASL.
 *
 * High-level API (signature-based dispatch):
 *   ffi.open(string path) -> i64
 *   ffi.bind(i64 lib, string name, string sig) -> i64
 *   ffi.call(i64 h, i64 a0..a5) -> i64
 *   ffi.call_f(i64 h, f64 a0, f64 a1) -> f64
 *   ffi.close(i64 lib)
 *
 * Low-level typed calls also available (call_vi, call_ii, etc.)
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
#include "platform/platform.h"

#ifdef BASL_HAS_LIBFFI
#include <ffi.h>
#endif

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

/* Push a BASL string from a C string. */
static basl_status_t push_string(basl_vm_t *vm, const char *s,
                                  basl_error_t *error) {
    basl_runtime_t *rt = basl_vm_runtime(vm);
    basl_object_t *obj = NULL;
    basl_status_t st = basl_string_object_new_cstr(rt, s ? s : "", &obj, error);
    if (st != BASL_STATUS_OK) return st;
    basl_value_t val;
    basl_value_init_object(&val, &obj);
    st = basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
    return st;
}

/* ── Bind table ──────────────────────────────────────────────────── */
/* Maps integer handles to {fn_ptr, signature_string}.               */

typedef struct {
    void *fn;
    char  sig[64]; /* e.g. "i32(i32,i32)", "f64(f64)", "void()" */
} ffi_bound_t;

#define FFI_MAX_BOUND 256
static ffi_bound_t g_bound[FFI_MAX_BOUND];
static int         g_bound_count;

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

/* ── ffi.bind(i64 lib, string name, string sig) -> i64 ───────────── */

static basl_status_t basl_ffi_bind(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *handle = (void *)(intptr_t)pop_i64(vm, base, 0);
    const char *name = pop_str(vm, base, 1);
    const char *sig  = pop_str(vm, base, 2);
    basl_vm_stack_pop_n(vm, arg_count);

    if (g_bound_count >= FFI_MAX_BOUND) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "ffi.bind: too many bound functions");
        return BASL_STATUS_INTERNAL;
    }

    void *sym = NULL;
    basl_status_t s = basl_platform_dlsym(handle, name, &sym, error);
    if (s != BASL_STATUS_OK) return s;

    int idx = g_bound_count++;
    g_bound[idx].fn = sym;
    size_t len = strlen(sig);
    if (len >= sizeof(g_bound[idx].sig)) len = sizeof(g_bound[idx].sig) - 1;
    memcpy(g_bound[idx].sig, sig, len);
    g_bound[idx].sig[len] = '\0';

    return push_i64(vm, (int64_t)idx, error);
}

/* ── ffi.call(i64 h, i64 a0..a5) -> i64 — signature-based dispatch ─ */

#ifdef BASL_HAS_LIBFFI
/* Parse a signature like "i32(i32,i32)" and call via libffi. */
static ffi_type *sig_to_ffi_type(const char *t, size_t len) {
    if (len == 3 && memcmp(t, "i32", 3) == 0) return &ffi_type_sint32;
    if (len == 3 && memcmp(t, "i64", 3) == 0) return &ffi_type_sint64;
    if (len == 3 && memcmp(t, "u32", 3) == 0) return &ffi_type_uint32;
    if (len == 2 && memcmp(t, "u8", 2) == 0)  return &ffi_type_uint8;
    if (len == 3 && memcmp(t, "f64", 3) == 0) return &ffi_type_double;
    if (len == 3 && memcmp(t, "f32", 3) == 0) return &ffi_type_float;
    if (len == 3 && memcmp(t, "ptr", 3) == 0) return &ffi_type_pointer;
    if (len == 4 && memcmp(t, "void", 4) == 0) return &ffi_type_void;
    return &ffi_type_pointer; /* fallback */
}

static int64_t ffi_call_libffi(void *fn, const char *sig,
                                int64_t a0, int64_t a1, int64_t a2,
                                int64_t a3, int64_t a4, int64_t a5) {
    /* Parse return type: everything before '(' */
    const char *paren = strchr(sig, '(');
    if (!paren) return 0;
    size_t ret_len = (size_t)(paren - sig);
    ffi_type *rtype = sig_to_ffi_type(sig, ret_len);

    /* Parse param types between '(' and ')' */
    const char *p = paren + 1;
    const char *end = strchr(p, ')');
    if (!end) end = p + strlen(p);

    ffi_type *atypes[6];
    int nargs = 0;
    while (p < end && nargs < 6) {
        const char *comma = p;
        while (comma < end && *comma != ',') comma++;
        size_t tlen = (size_t)(comma - p);
        if (tlen > 0)
            atypes[nargs++] = sig_to_ffi_type(p, tlen);
        p = comma + 1;
    }

    ffi_cif cif;
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, (unsigned)nargs,
                     rtype, nargs ? atypes : NULL) != FFI_OK)
        return 0;

    /* Set up argument storage and pointers */
    int64_t args_i[6] = { a0, a1, a2, a3, a4, a5 };
    double  args_d[6];
    void   *args_p[6];
    void   *avalues[6];

    for (int i = 0; i < nargs; i++) {
        if (atypes[i] == &ffi_type_double) {
            /* Reinterpret the i64 bits as double */
            memcpy(&args_d[i], &args_i[i], sizeof(double));
            avalues[i] = &args_d[i];
        } else if (atypes[i] == &ffi_type_pointer) {
            args_p[i] = (void *)(intptr_t)args_i[i];
            avalues[i] = &args_p[i];
        } else {
            avalues[i] = &args_i[i];
        }
    }

    /* Call and return */
    if (rtype == &ffi_type_void) {
        ffi_call(&cif, FFI_FN(fn), NULL, avalues);
        return 0;
    } else if (rtype == &ffi_type_double) {
        double rv;
        ffi_call(&cif, FFI_FN(fn), &rv, avalues);
        int64_t bits;
        memcpy(&bits, &rv, sizeof(bits));
        return bits;
    } else if (rtype == &ffi_type_pointer) {
        void *rv;
        ffi_call(&cif, FFI_FN(fn), &rv, avalues);
        return (int64_t)(intptr_t)rv;
    } else {
        int64_t rv = 0;
        ffi_call(&cif, FFI_FN(fn), &rv, avalues);
        return rv;
    }
}
#endif /* BASL_HAS_LIBFFI */

static basl_status_t basl_ffi_call(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int idx   = (int)pop_i64(vm, base, 0);
    int64_t a0 = pop_i64(vm, base, 1);
    int64_t a1 = pop_i64(vm, base, 2);
    int64_t a2 = pop_i64(vm, base, 3);
    int64_t a3 = pop_i64(vm, base, 4);
    int64_t a4 = pop_i64(vm, base, 5);
    int64_t a5 = pop_i64(vm, base, 6);
    basl_vm_stack_pop_n(vm, arg_count);

    if (idx < 0 || idx >= g_bound_count) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "ffi.call: invalid handle");
        return BASL_STATUS_INTERNAL;
    }

    void *fn = g_bound[idx].fn;
    const char *sig = g_bound[idx].sig;

    /* Dispatch based on signature string */
    if (strcmp(sig, "void()") == 0) {
        basl_ffi_call_void_to_void(fn);
        return push_i64(vm, 0, error);
    }
    if (strcmp(sig, "i32()") == 0)
        return push_i64(vm, basl_ffi_call_void_to_i32(fn), error);
    if (strcmp(sig, "i32(i32)") == 0)
        return push_i64(vm, basl_ffi_call_i32_to_i32(fn, (int)a0), error);
    if (strcmp(sig, "i32(i32,i32)") == 0)
        return push_i64(vm, basl_ffi_call_i32_i32_to_i32(fn, (int)a0, (int)a1), error);
    if (strcmp(sig, "void(i32)") == 0) {
        basl_ffi_call_i32_to_void(fn, (int)a0);
        return push_i64(vm, 0, error);
    }
    if (strcmp(sig, "ptr()") == 0)
        return push_i64(vm, (int64_t)(intptr_t)basl_ffi_call_void_to_ptr(fn), error);
    if (strcmp(sig, "ptr(ptr)") == 0)
        return push_i64(vm, (int64_t)(intptr_t)basl_ffi_call_ptr_to_ptr(fn, (void *)(intptr_t)a0), error);
    if (strcmp(sig, "ptr(ptr,ptr)") == 0)
        return push_i64(vm, (int64_t)(intptr_t)basl_ffi_call_ptr_ptr_to_ptr(fn, (void *)(intptr_t)a0, (void *)(intptr_t)a1), error);
    if (strcmp(sig, "i32(ptr)") == 0)
        return push_i64(vm, basl_ffi_call_ptr_to_i32(fn, (void *)(intptr_t)a0), error);
    if (strcmp(sig, "void(ptr)") == 0) {
        basl_ffi_call_ptr_to_void(fn, (void *)(intptr_t)a0);
        return push_i64(vm, 0, error);
    }
    if (strcmp(sig, "i32(ptr,i32)") == 0)
        return push_i64(vm, basl_ffi_call_ptr_i32_to_i32(fn, (void *)(intptr_t)a0, (int)a1), error);
    if (strcmp(sig, "void(ptr,i32)") == 0) {
        basl_ffi_call_ptr_i32_to_void(fn, (void *)(intptr_t)a0, (int)a1);
        return push_i64(vm, 0, error);
    }
    if (strcmp(sig, "i32(ptr,i32,i32,i32,i32)") == 0)
        return push_i64(vm, basl_ffi_call_ptr_i32_i32_i32_i32_to_i32(fn, (void *)(intptr_t)a0, (int)a1, (int)a2, (int)a3, (int)a4), error);

    /* Fallback: use libffi if available, else generic trampoline */
#ifdef BASL_HAS_LIBFFI
    return push_i64(vm, ffi_call_libffi(fn, sig, a0, a1, a2, a3, a4, a5), error);
#else
    void *r = basl_ffi_call_generic(fn, 6,
        (void *)(intptr_t)a0, (void *)(intptr_t)a1,
        (void *)(intptr_t)a2, (void *)(intptr_t)a3,
        (void *)(intptr_t)a4, (void *)(intptr_t)a5);
    return push_i64(vm, (int64_t)(intptr_t)r, error);
#endif
}

/* ── ffi.call_f(i64 h, f64 a0, f64 a1) -> f64 — float dispatch ──── */

static basl_status_t basl_ffi_call_f(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int idx   = (int)pop_i64(vm, base, 0);
    double a0 = pop_f64(vm, base, 1);
    double a1 = pop_f64(vm, base, 2);
    basl_vm_stack_pop_n(vm, arg_count);

    if (idx < 0 || idx >= g_bound_count) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "ffi.call_f: invalid handle");
        return BASL_STATUS_INTERNAL;
    }

    void *fn = g_bound[idx].fn;
    const char *sig = g_bound[idx].sig;

    if (strcmp(sig, "f64()") == 0)
        return push_f64(vm, basl_ffi_call_void_to_f64(fn), error);
    if (strcmp(sig, "f64(f64)") == 0)
        return push_f64(vm, basl_ffi_call_f64_to_f64(fn, a0), error);
    if (strcmp(sig, "f64(f64,f64)") == 0)
        return push_f64(vm, basl_ffi_call_f64_f64_to_f64(fn, a0, a1), error);

#ifdef BASL_HAS_LIBFFI
    {
        /* Use libffi for unknown float signatures */
        int64_t a0_bits, a1_bits;
        memcpy(&a0_bits, &a0, sizeof(a0_bits));
        memcpy(&a1_bits, &a1, sizeof(a1_bits));
        int64_t rbits = ffi_call_libffi(fn, sig, a0_bits, a1_bits, 0, 0, 0, 0);
        double rv;
        memcpy(&rv, &rbits, sizeof(rv));
        return push_f64(vm, rv, error);
    }
#else
    basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                           "ffi.call_f: signature is not a float function");
    return BASL_STATUS_INTERNAL;
#endif
}

/* ── ffi.call_s(i64 h, i64 a0, i64 a1) -> string — string-return ── */

static basl_status_t basl_ffi_call_s(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int idx   = (int)pop_i64(vm, base, 0);
    int64_t a0 = pop_i64(vm, base, 1);
    int64_t a1 = pop_i64(vm, base, 2);
    (void)a1;
    basl_vm_stack_pop_n(vm, arg_count);

    if (idx < 0 || idx >= g_bound_count) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "ffi.call_s: invalid handle");
        return BASL_STATUS_INTERNAL;
    }

    void *fn = g_bound[idx].fn;
    const char *sig = g_bound[idx].sig;
    const char *r = NULL;

    if (strcmp(sig, "string()") == 0)
        r = basl_ffi_call_void_to_str(fn);
    else if (strcmp(sig, "string(string)") == 0)
        r = basl_ffi_call_str_to_str(fn, (const char *)(intptr_t)a0);
    else if (strcmp(sig, "string(i32)") == 0)
        r = basl_ffi_call_i32_to_str(fn, (int)a0);
    else {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "ffi.call_s: signature is not a string function");
        return BASL_STATUS_INTERNAL;
    }

    return push_string(vm, r, error);
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int p_str[] = { BASL_TYPE_STRING };
static const int p_i64[] = { BASL_TYPE_I64 };
static const int p_i64_str[] = { BASL_TYPE_I64, BASL_TYPE_STRING };
static const int p_i64_str_str[] = { BASL_TYPE_I64, BASL_TYPE_STRING, BASL_TYPE_STRING };
static const int p_i64_i64[] = { BASL_TYPE_I64, BASL_TYPE_I64 };
static const int p_i64_i64_i64[] = { BASL_TYPE_I64, BASL_TYPE_I64, BASL_TYPE_I64 };
static const int p_i64_f64[] = { BASL_TYPE_I64, BASL_TYPE_F64 };
static const int p_i64_f64_f64[] = { BASL_TYPE_I64, BASL_TYPE_F64, BASL_TYPE_F64 };
static const int p_i64_str2[] = { BASL_TYPE_I64, BASL_TYPE_STRING };

/* bind: (i64 lib, string name, string sig) */
/* call: (i64 h, i64 a0..a5) — 7 params */
static const int p_call[] = { BASL_TYPE_I64, BASL_TYPE_I64, BASL_TYPE_I64,
                               BASL_TYPE_I64, BASL_TYPE_I64, BASL_TYPE_I64,
                               BASL_TYPE_I64 };
/* call_f: (i64 h, f64 a0, f64 a1) */
static const int p_call_f[] = { BASL_TYPE_I64, BASL_TYPE_F64, BASL_TYPE_F64 };
/* call_s: (i64 h, i64 a0, i64 a1) */
static const int p_call_s[] = { BASL_TYPE_I64, BASL_TYPE_I64, BASL_TYPE_I64 };

#define F(n, nl, fn, pc, pt, rt) { n, nl, fn, pc, pt, rt, 1, NULL }
#define FV(n, nl, fn, pc, pt) { n, nl, fn, pc, pt, BASL_TYPE_VOID, 0, NULL }

static const basl_native_module_function_t basl_ffi_functions[] = {
    /* Library management */
    F("open",     4U, basl_ffi_open,     1U, p_str,           BASL_TYPE_I64),
    F("sym",      3U, basl_ffi_sym,      2U, p_i64_str,       BASL_TYPE_I64),
    FV("close",   5U, basl_ffi_close,    1U, p_i64),

    /* High-level: bind + signature-dispatched call */
    F("bind",     4U, basl_ffi_bind,     3U, p_i64_str_str,          BASL_TYPE_I64),
    F("call",     4U, basl_ffi_call,     7U, p_call,          BASL_TYPE_I64),
    F("call_f",   6U, basl_ffi_call_f,   3U, p_call_f,        BASL_TYPE_F64),
    F("call_s",   6U, basl_ffi_call_s,   3U, p_call_s,        BASL_TYPE_STRING),

    /* Low-level typed calls */
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
    F("call_ssi", 8U, basl_ffi_call_ssi, 3U, p_i64_str_str,  BASL_TYPE_I32),
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
