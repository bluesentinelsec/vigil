/* BASL standard library: ffi module.
 *
 * Provides dynamic loading of C shared libraries and calling of
 * foreign functions via libffi.  All library handles and function
 * pointers are represented as i64 values in BASL.
 *
 * API:
 *   ffi.open(string path) -> i64
 *   ffi.sym(i64 lib, string name) -> i64
 *   ffi.bind(i64 lib, string name, string sig) -> i64
 *   ffi.call(i64 h, i64 a0..a5) -> i64
 *   ffi.call_f(i64 h, f64 a0, f64 a1) -> f64
 *   ffi.call_s(i64 h, i64 a0, i64 a1) -> string
 *   ffi.close(i64 lib)
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
#include "platform/platform.h"

#ifdef BASL_HAS_LIBFFI
#include <ffi.h>
#endif

/* ── helpers ─────────────────────────────────────────────────────── */

static basl_status_t push_i64(basl_vm_t *vm, int64_t v, basl_error_t *error) {
    basl_value_t val = basl_nanbox_encode_int(v);
    return basl_vm_stack_push(vm, &val, error);
}

#ifdef BASL_HAS_LIBFFI
static basl_status_t push_f64(basl_vm_t *vm, double v, basl_error_t *error) {
    basl_value_t val = basl_nanbox_encode_double(v);
    return basl_vm_stack_push(vm, &val, error);
}
#endif

static int64_t pop_i64(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    return basl_nanbox_decode_int(v);
}

static double pop_f64(basl_vm_t *vm, size_t base, size_t idx) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    return basl_nanbox_decode_double(v);
}

/*
 * Extract a C string from a BASL string on the stack into a buffer.
 * Must copy before stack_pop_n, which releases the string object.
 */
static const char *pop_str_buf(basl_vm_t *vm, size_t base, size_t idx,
                                char *buf, size_t bufsz) {
    basl_value_t v = basl_vm_stack_get(vm, base + idx);
    const basl_object_t *obj = (const basl_object_t *)basl_nanbox_decode_ptr(v);
    if (obj && basl_object_type(obj) == BASL_OBJECT_STRING) {
        const char *s = basl_string_object_c_str(obj);
        size_t len = strlen(s);
        if (len >= bufsz) len = bufsz - 1;
        memcpy(buf, s, len);
        buf[len] = '\0';
        return buf;
    }
    buf[0] = '\0';
    return buf;
}

#ifdef BASL_HAS_LIBFFI
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
#endif

/* ── Bind table ──────────────────────────────────────────────────── */

typedef struct {
    void *fn;
    char  sig[64]; /* e.g. "i32(i32,i32)", "f64(f64)", "void()" */
} ffi_bound_t;

#define FFI_MAX_BOUND 256
static ffi_bound_t g_bound[FFI_MAX_BOUND];
static int         g_bound_count;

/* ── ffi.open / ffi.sym / ffi.close ──────────────────────────────── */

static basl_status_t basl_ffi_open(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    char path[512];
    pop_str_buf(vm, base, 0, path, sizeof(path));
    void *handle = NULL;
    basl_vm_stack_pop_n(vm, arg_count);
    basl_status_t s = basl_platform_dlopen(path, &handle, error);
    if (s != BASL_STATUS_OK) return s;
    return push_i64(vm, (int64_t)(intptr_t)handle, error);
}

static basl_status_t basl_ffi_sym(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *handle = (void *)(intptr_t)pop_i64(vm, base, 0);
    char name[256];
    pop_str_buf(vm, base, 1, name, sizeof(name));
    void *sym = NULL;
    basl_vm_stack_pop_n(vm, arg_count);
    basl_status_t s = basl_platform_dlsym(handle, name, &sym, error);
    if (s != BASL_STATUS_OK) return s;
    return push_i64(vm, (int64_t)(intptr_t)sym, error);
}

static basl_status_t basl_ffi_close(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *handle = (void *)(intptr_t)pop_i64(vm, base, 0);
    basl_vm_stack_pop_n(vm, arg_count);
    return basl_platform_dlclose(handle, error);
}

/* ── ffi.bind(i64 lib, string name, string sig) -> i64 ───────────── */

static basl_status_t basl_ffi_bind(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    void *handle = (void *)(intptr_t)pop_i64(vm, base, 0);
    char name[256], sig[64];
    pop_str_buf(vm, base, 1, name, sizeof(name));
    pop_str_buf(vm, base, 2, sig, sizeof(sig));
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

/* ── libffi-based generic call ───────────────────────────────────── */

#ifdef BASL_HAS_LIBFFI

/*
 * Pedantic-safe replacement for libffi's FFI_FN() macro, which does
 * a direct void* -> function-pointer cast that GCC -Wpedantic rejects.
 */
static void (*fn_to_fnptr(void *p))(void) {
    union { void *obj; void (*fn)(void); } u;
    u.obj = p;
    return u.fn;
}

static ffi_type *sig_to_ffi_type(const char *t, size_t len) {
    if (len == 3 && memcmp(t, "i32", 3) == 0) return &ffi_type_sint32;
    if (len == 3 && memcmp(t, "i64", 3) == 0) return &ffi_type_sint64;
    if (len == 3 && memcmp(t, "u32", 3) == 0) return &ffi_type_uint32;
    if (len == 2 && memcmp(t, "u8", 2) == 0)  return &ffi_type_uint8;
    if (len == 3 && memcmp(t, "f64", 3) == 0) return &ffi_type_double;
    if (len == 3 && memcmp(t, "f32", 3) == 0) return &ffi_type_float;
    if (len == 3 && memcmp(t, "ptr", 3) == 0) return &ffi_type_pointer;
    if (len == 4 && memcmp(t, "void", 4) == 0) return &ffi_type_void;
    if (len == 6 && memcmp(t, "string", 6) == 0) return &ffi_type_pointer;
    return &ffi_type_pointer; /* fallback */
}

/* Parse "[stdcall:]ret(p1,p2,...)" and call fn via ffi_call.
 * Returns raw i64 bits.  Prefix "stdcall:" selects FFI_STDCALL on
 * platforms that support it (32-bit x86 Windows); ignored elsewhere. */
static int64_t ffi_call_generic(void *fn, const char *sig,
                                const int64_t *args, int nargs_avail) {
    ffi_abi abi = FFI_DEFAULT_ABI;
    const char *s = sig;
#ifdef FFI_STDCALL
    if (strncmp(s, "stdcall:", 8) == 0) {
        abi = FFI_STDCALL;
        s += 8;
    }
#else
    if (strncmp(s, "stdcall:", 8) == 0)
        s += 8; /* skip prefix, use default ABI */
#endif

    const char *paren = strchr(s, '(');
    if (!paren) return 0;
    size_t ret_len = (size_t)(paren - s);
    ffi_type *rtype = sig_to_ffi_type(s, ret_len);

    const char *p = paren + 1;
    const char *end = strchr(p, ')');
    if (!end) end = p + strlen(p);

    /* Count params in signature. */
    int nargs = 0;
    {
        const char *q = p;
        while (q < end) {
            const char *c = q;
            while (c < end && *c != ',') c++;
            if ((size_t)(c - q) > 0) nargs++;
            q = c + 1;
        }
    }
    if (nargs > nargs_avail) nargs = nargs_avail;

    /* Allocate on the stack for typical sizes, heap for large. */
    ffi_type *atypes_s[16];
    int64_t   args_i_s[16];
    double    args_d_s[16];
    void     *args_p_s[16];
    void     *avalues_s[16];

    ffi_type **atypes  = nargs <= 16 ? atypes_s  : malloc((size_t)nargs * sizeof(*atypes));
    int64_t   *args_i  = nargs <= 16 ? args_i_s  : malloc((size_t)nargs * sizeof(*args_i));
    double    *args_d  = nargs <= 16 ? args_d_s  : malloc((size_t)nargs * sizeof(*args_d));
    void     **args_p  = nargs <= 16 ? args_p_s  : malloc((size_t)nargs * sizeof(*args_p));
    void     **avalues = nargs <= 16 ? avalues_s : malloc((size_t)nargs * sizeof(*avalues));

    if (!atypes || !args_i || !args_d || !args_p || !avalues) {
        if (atypes  != atypes_s)  free(atypes);
        if (args_i  != args_i_s)  free(args_i);
        if (args_d  != args_d_s)  free(args_d);
        if (args_p  != args_p_s)  free(args_p);
        if (avalues != avalues_s) free(avalues);
        return 0;
    }

    /* Parse param types. */
    int idx = 0;
    p = paren + 1;
    while (p < end && idx < nargs) {
        const char *comma = p;
        while (comma < end && *comma != ',') comma++;
        size_t tlen = (size_t)(comma - p);
        if (tlen > 0)
            atypes[idx++] = sig_to_ffi_type(p, tlen);
        p = comma + 1;
    }

    ffi_cif cif;
    int64_t result = 0;
    if (ffi_prep_cif(&cif, abi, (unsigned)nargs,
                     rtype, nargs ? atypes : NULL) != FFI_OK)
        goto done;

    for (int i = 0; i < nargs; i++) {
        args_i[i] = args[i];
        if (atypes[i] == &ffi_type_double) {
            memcpy(&args_d[i], &args_i[i], sizeof(double));
            avalues[i] = &args_d[i];
        } else if (atypes[i] == &ffi_type_pointer) {
            args_p[i] = (void *)(intptr_t)args_i[i];
            avalues[i] = &args_p[i];
        } else {
            avalues[i] = &args_i[i];
        }
    }

    if (rtype == &ffi_type_void) {
        ffi_call(&cif, fn_to_fnptr(fn), NULL, avalues);
    } else if (rtype == &ffi_type_double) {
        double rv;
        ffi_call(&cif, fn_to_fnptr(fn), &rv, avalues);
        memcpy(&result, &rv, sizeof(result));
    } else if (rtype == &ffi_type_pointer) {
        void *rv;
        ffi_call(&cif, fn_to_fnptr(fn), &rv, avalues);
        result = (int64_t)(intptr_t)rv;
    } else {
        ffi_call(&cif, fn_to_fnptr(fn), &result, avalues);
    }

done:
    if (atypes  != atypes_s)  free(atypes);
    if (args_i  != args_i_s)  free(args_i);
    if (args_d  != args_d_s)  free(args_d);
    if (args_p  != args_p_s)  free(args_p);
    if (avalues != avalues_s) free(avalues);
    return result;
}

#endif /* BASL_HAS_LIBFFI */

/* ── ffi.call(i64 h, i64 a0..a5) -> i64 ─────────────────────────── */

static basl_status_t basl_ffi_call(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int idx = (int)pop_i64(vm, base, 0);
    /* Remaining args (after the handle) are the C function args. */
    int nargs = (int)arg_count - 1;
    int64_t args_s[16];
    int64_t *args = nargs <= 16 ? args_s : malloc((size_t)nargs * sizeof(*args));
    if (!args) {
        basl_vm_stack_pop_n(vm, arg_count);
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "ffi.call: alloc failed");
        return BASL_STATUS_INTERNAL;
    }
    for (int i = 0; i < nargs; i++)
        args[i] = pop_i64(vm, base, (size_t)(i + 1));
    basl_vm_stack_pop_n(vm, arg_count);

    if (idx < 0 || idx >= g_bound_count) {
        if (args != args_s) free(args);
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "ffi.call: invalid handle");
        return BASL_STATUS_INTERNAL;
    }

#ifdef BASL_HAS_LIBFFI
    {
        int64_t rv = ffi_call_generic(g_bound[idx].fn, g_bound[idx].sig,
                                       args, nargs);
        if (args != args_s) free(args);
        return push_i64(vm, rv, error);
    }
#else
    if (args != args_s) free(args);
    basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                           "ffi.call: not supported on this platform");
    return BASL_STATUS_INTERNAL;
#endif
}

/* ── ffi.call_f(i64 h, f64 a0, f64 a1) -> f64 ───────────────────── */

static basl_status_t basl_ffi_call_f(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int idx = (int)pop_i64(vm, base, 0);
    int nargs = (int)arg_count - 1;
    int64_t args_s[16];
    int64_t *args = nargs <= 16 ? args_s : malloc((size_t)nargs * sizeof(*args));
    if (!args) {
        basl_vm_stack_pop_n(vm, arg_count);
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "ffi.call_f: alloc failed");
        return BASL_STATUS_INTERNAL;
    }
    for (int i = 0; i < nargs; i++) {
        double d = pop_f64(vm, base, (size_t)(i + 1));
        memcpy(&args[i], &d, sizeof(d));
    }
    basl_vm_stack_pop_n(vm, arg_count);

    if (idx < 0 || idx >= g_bound_count) {
        if (args != args_s) free(args);
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "ffi.call_f: invalid handle");
        return BASL_STATUS_INTERNAL;
    }

#ifdef BASL_HAS_LIBFFI
    {
        int64_t rbits = ffi_call_generic(g_bound[idx].fn, g_bound[idx].sig,
                                          args, nargs);
        if (args != args_s) free(args);
        double rv;
        memcpy(&rv, &rbits, sizeof(rv));
        return push_f64(vm, rv, error);
    }
#else
    if (args != args_s) free(args);
    basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                           "ffi.call_f: not supported on this platform");
    return BASL_STATUS_INTERNAL;
#endif
}

/* ── ffi.call_s(i64 h, i64 a0, i64 a1) -> string ────────────────── */

static basl_status_t basl_ffi_call_s(
    basl_vm_t *vm, size_t arg_count, basl_error_t *error
) {
    size_t base = basl_vm_stack_depth(vm) - arg_count;
    int idx = (int)pop_i64(vm, base, 0);
    int nargs = (int)arg_count - 1;
    int64_t args_s[16];
    int64_t *args = nargs <= 16 ? args_s : malloc((size_t)nargs * sizeof(*args));
    if (!args) {
        basl_vm_stack_pop_n(vm, arg_count);
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "ffi.call_s: alloc failed");
        return BASL_STATUS_INTERNAL;
    }
    for (int i = 0; i < nargs; i++)
        args[i] = pop_i64(vm, base, (size_t)(i + 1));
    basl_vm_stack_pop_n(vm, arg_count);

    if (idx < 0 || idx >= g_bound_count) {
        if (args != args_s) free(args);
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "ffi.call_s: invalid handle");
        return BASL_STATUS_INTERNAL;
    }

#ifdef BASL_HAS_LIBFFI
    {
        int64_t rbits = ffi_call_generic(g_bound[idx].fn, g_bound[idx].sig,
                                          args, nargs);
        if (args != args_s) free(args);
        const char *r = (const char *)(intptr_t)rbits;
        return push_string(vm, r, error);
    }
#else
    if (args != args_s) free(args);
    basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                           "ffi.call_s: not supported on this platform");
    return BASL_STATUS_INTERNAL;
#endif
}

/* ── module descriptor ───────────────────────────────────────────── */

static const int p_str[] = { BASL_TYPE_STRING };
static const int p_i64[] = { BASL_TYPE_I64 };
static const int p_i64_str[] = { BASL_TYPE_I64, BASL_TYPE_STRING };
static const int p_i64_str_str[] = { BASL_TYPE_I64, BASL_TYPE_STRING,
                                      BASL_TYPE_STRING };
static const int p_call[] = { BASL_TYPE_I64, BASL_TYPE_I64, BASL_TYPE_I64,
                               BASL_TYPE_I64, BASL_TYPE_I64, BASL_TYPE_I64,
                               BASL_TYPE_I64 };
static const int p_call_f[] = { BASL_TYPE_I64, BASL_TYPE_F64, BASL_TYPE_F64 };
static const int p_call_s[] = { BASL_TYPE_I64, BASL_TYPE_I64, BASL_TYPE_I64 };

#define F(n, nl, fn, pc, pt, rt) { n, nl, fn, pc, pt, rt, 1, NULL, 0 }
#define FV(n, nl, fn, pc, pt) { n, nl, fn, pc, pt, BASL_TYPE_VOID, 0, NULL, 0 }

static const basl_native_module_function_t basl_ffi_functions[] = {
    F("open",   4U, basl_ffi_open,   1U, p_str,          BASL_TYPE_I64),
    F("sym",    3U, basl_ffi_sym,    2U, p_i64_str,      BASL_TYPE_I64),
    FV("close", 5U, basl_ffi_close,  1U, p_i64),
    F("bind",   4U, basl_ffi_bind,   3U, p_i64_str_str,  BASL_TYPE_I64),
    F("call",   4U, basl_ffi_call,   7U, p_call,         BASL_TYPE_I64),
    F("call_f", 6U, basl_ffi_call_f, 3U, p_call_f,       BASL_TYPE_F64),
    F("call_s", 6U, basl_ffi_call_s, 3U, p_call_s,       BASL_TYPE_STRING),
};

#undef F
#undef FV

BASL_API const basl_native_module_t basl_stdlib_ffi = {
    "ffi", 3U,
    basl_ffi_functions,
    sizeof(basl_ffi_functions) / sizeof(basl_ffi_functions[0]),
    NULL, 0U
};
