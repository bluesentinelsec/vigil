/*
 * ffi_trampoline.c — C cast trampolines for the BASL FFI.
 *
 * These functions cast a void* function pointer to the correct C
 * signature and invoke it.  This avoids the need for libffi while
 * supporting the most common C function signatures.
 *
 * The generic trampoline handles pointer-heavy APIs (SDL, etc.)
 * by casting all arguments to void* and supporting up to 6 args.
 */
#include "internal/ffi_trampoline.h"

#include <stdint.h>
#include <string.h>

/*
 * POSIX guarantees void* and function pointers are the same size,
 * but ISO C forbids direct casts between them.  Use memcpy through
 * a union to satisfy -Wpedantic on GCC while remaining well-defined
 * on all POSIX targets.
 */
typedef union { void *obj; void (*fn)(void); } basl_fn_pun_t;

static void (*to_fnptr(void *p))(void) {
    basl_fn_pun_t u; u.obj = p; return u.fn;
}

static void *from_fnptr(void (*f)(void)) {
    basl_fn_pun_t u; u.fn = f; return u.obj;
}

#define CALL0(ret, fn) \
    ((ret(*)(void))(to_fnptr(fn)))()

#define CALL(ret, sig, fn, ...) \
    ((ret(*)sig)(to_fnptr(fn)))(__VA_ARGS__)

#define CALLV0(fn) \
    ((void(*)(void))(to_fnptr(fn)))()

#define CALLV(sig, fn, ...) \
    ((void(*)sig)(to_fnptr(fn)))(__VA_ARGS__)

/* ── Integer trampolines ─────────────────────────────────────────── */

int basl_ffi_call_void_to_i32(void *fn) {
    return CALL0(int, fn);
}

int basl_ffi_call_i32_to_i32(void *fn, int a) {
    return CALL(int, (int), fn, a);
}

int basl_ffi_call_i32_i32_to_i32(void *fn, int a, int b) {
    return CALL(int, (int, int), fn, a, b);
}

void basl_ffi_call_void_to_void(void *fn) {
    CALLV0(fn);
}

void basl_ffi_call_i32_to_void(void *fn, int a) {
    CALLV((int), fn, a);
}

/* ── Float trampolines ───────────────────────────────────────────── */

double basl_ffi_call_void_to_f64(void *fn) {
    return CALL0(double, fn);
}

double basl_ffi_call_f64_to_f64(void *fn, double a) {
    return CALL(double, (double), fn, a);
}

double basl_ffi_call_f64_f64_to_f64(void *fn, double a, double b) {
    return CALL(double, (double, double), fn, a, b);
}

/* ── String trampolines ──────────────────────────────────────────── */

const char *basl_ffi_call_void_to_str(void *fn) {
    return CALL0(const char *, fn);
}

const char *basl_ffi_call_str_to_str(void *fn, const char *a) {
    return CALL(const char *, (const char *), fn, a);
}

void basl_ffi_call_str_to_void(void *fn, const char *a) {
    CALLV((const char *), fn, a);
}

int basl_ffi_call_str_to_i32(void *fn, const char *a) {
    return CALL(int, (const char *), fn, a);
}

int basl_ffi_call_str_str_to_i32(void *fn, const char *a, const char *b) {
    return CALL(int, (const char *, const char *), fn, a, b);
}

const char *basl_ffi_call_i32_to_str(void *fn, int a) {
    return CALL(const char *, (int), fn, a);
}

/* ── Pointer trampolines ─────────────────────────────────────────── */

void *basl_ffi_call_void_to_ptr(void *fn) {
    return CALL0(void *, fn);
}

void *basl_ffi_call_ptr_to_ptr(void *fn, void *a) {
    return CALL(void *, (void *), fn, a);
}

void *basl_ffi_call_ptr_ptr_to_ptr(void *fn, void *a, void *b) {
    return CALL(void *, (void *, void *), fn, a, b);
}

int basl_ffi_call_ptr_to_i32(void *fn, void *a) {
    return CALL(int, (void *), fn, a);
}

void basl_ffi_call_ptr_to_void(void *fn, void *a) {
    CALLV((void *), fn, a);
}

int basl_ffi_call_ptr_i32_to_i32(void *fn, void *a, int b) {
    return CALL(int, (void *, int), fn, a, b);
}

void basl_ffi_call_ptr_i32_to_void(void *fn, void *a, int b) {
    CALLV((void *, int), fn, a, b);
}

int basl_ffi_call_ptr_i32_i32_i32_i32_to_i32(
    void *fn, void *a, int b, int c, int d, int e
) {
    return CALL(int, (void *, int, int, int, int), fn, a, b, c, d, e);
}

/* ── Generic trampoline (up to 6 void* args) ─────────────────────── */

void *basl_ffi_call_generic(void *fn, int nargs,
    void *a0, void *a1, void *a2, void *a3, void *a4, void *a5
) {
    switch (nargs) {
    case 0: return CALL0(void *, fn);
    case 1: return CALL(void *, (void *), fn, a0);
    case 2: return CALL(void *, (void *, void *), fn, a0, a1);
    case 3: return CALL(void *, (void *, void *, void *), fn, a0, a1, a2);
    case 4: return CALL(void *, (void *, void *, void *, void *), fn, a0, a1, a2, a3);
    case 5: return CALL(void *, (void *, void *, void *, void *, void *), fn, a0, a1, a2, a3, a4);
    case 6: return CALL(void *, (void *, void *, void *, void *, void *, void *), fn, a0, a1, a2, a3, a4, a5);
    default: return (void *)0;
    }
}

/* ── Helpers ──────────────────────────────────────────────────────── */

void *basl_ffi_int_to_ptr(uintptr_t v) { return (void *)v; }
uintptr_t basl_ffi_ptr_to_int(void *p) { return (uintptr_t)p; }

/* ── Callback trampoline pool ────────────────────────────────────── */

static basl_ffi_callback_dispatch_fn g_cb_dispatch;
static int g_cb_used[BASL_FFI_MAX_CALLBACKS];

void basl_ffi_callback_set_dispatch(basl_ffi_callback_dispatch_fn fn) {
    g_cb_dispatch = fn;
}

static intptr_t cb_invoke(int slot, intptr_t a0, intptr_t a1,
                          intptr_t a2, intptr_t a3) {
    if (g_cb_dispatch) return g_cb_dispatch(slot, a0, a1, a2, a3);
    return 0;
}

#define CB_SLOT(N) \
    static intptr_t cb_slot##N(intptr_t a0, intptr_t a1, \
                               intptr_t a2, intptr_t a3) { \
        return cb_invoke(N, a0, a1, a2, a3); \
    }

CB_SLOT(0) CB_SLOT(1) CB_SLOT(2) CB_SLOT(3)
CB_SLOT(4) CB_SLOT(5) CB_SLOT(6) CB_SLOT(7)

#undef CB_SLOT

typedef intptr_t (*cb_fn_t)(intptr_t, intptr_t, intptr_t, intptr_t);

static cb_fn_t cb_table[BASL_FFI_MAX_CALLBACKS] = {
    cb_slot0, cb_slot1, cb_slot2, cb_slot3,
    cb_slot4, cb_slot5, cb_slot6, cb_slot7,
};

int basl_ffi_callback_alloc(void **out_ptr) {
    for (int i = 0; i < BASL_FFI_MAX_CALLBACKS; i++) {
        if (!g_cb_used[i]) {
            g_cb_used[i] = 1;
            *out_ptr = from_fnptr((void (*)(void))cb_table[i]);
            return i;
        }
    }
    return -1;
}

void basl_ffi_callback_free(int slot) {
    if (slot >= 0 && slot < BASL_FFI_MAX_CALLBACKS)
        g_cb_used[slot] = 0;
}
