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

/* ── Integer trampolines ─────────────────────────────────────────── */

int basl_ffi_call_void_to_i32(void *fn) {
    return ((int (*)(void))fn)();
}

int basl_ffi_call_i32_to_i32(void *fn, int a) {
    return ((int (*)(int))fn)(a);
}

int basl_ffi_call_i32_i32_to_i32(void *fn, int a, int b) {
    return ((int (*)(int, int))fn)(a, b);
}

void basl_ffi_call_void_to_void(void *fn) {
    ((void (*)(void))fn)();
}

void basl_ffi_call_i32_to_void(void *fn, int a) {
    ((void (*)(int))fn)(a);
}

/* ── Float trampolines ───────────────────────────────────────────── */

double basl_ffi_call_void_to_f64(void *fn) {
    return ((double (*)(void))fn)();
}

double basl_ffi_call_f64_to_f64(void *fn, double a) {
    return ((double (*)(double))fn)(a);
}

double basl_ffi_call_f64_f64_to_f64(void *fn, double a, double b) {
    return ((double (*)(double, double))fn)(a, b);
}

/* ── String trampolines ──────────────────────────────────────────── */

const char *basl_ffi_call_void_to_str(void *fn) {
    return ((const char *(*)(void))fn)();
}

const char *basl_ffi_call_str_to_str(void *fn, const char *a) {
    return ((const char *(*)(const char *))fn)(a);
}

void basl_ffi_call_str_to_void(void *fn, const char *a) {
    ((void (*)(const char *))fn)(a);
}

int basl_ffi_call_str_to_i32(void *fn, const char *a) {
    return ((int (*)(const char *))fn)(a);
}

int basl_ffi_call_str_str_to_i32(void *fn, const char *a, const char *b) {
    return ((int (*)(const char *, const char *))fn)(a, b);
}

const char *basl_ffi_call_i32_to_str(void *fn, int a) {
    return ((const char *(*)(int))fn)(a);
}

/* ── Pointer trampolines ─────────────────────────────────────────── */

void *basl_ffi_call_void_to_ptr(void *fn) {
    return ((void *(*)(void))fn)();
}

void *basl_ffi_call_ptr_to_ptr(void *fn, void *a) {
    return ((void *(*)(void *))fn)(a);
}

void *basl_ffi_call_ptr_ptr_to_ptr(void *fn, void *a, void *b) {
    return ((void *(*)(void *, void *))fn)(a, b);
}

int basl_ffi_call_ptr_to_i32(void *fn, void *a) {
    return ((int (*)(void *))fn)(a);
}

void basl_ffi_call_ptr_to_void(void *fn, void *a) {
    ((void (*)(void *))fn)(a);
}

int basl_ffi_call_ptr_i32_to_i32(void *fn, void *a, int b) {
    return ((int (*)(void *, int))fn)(a, b);
}

void basl_ffi_call_ptr_i32_to_void(void *fn, void *a, int b) {
    ((void (*)(void *, int))fn)(a, b);
}

int basl_ffi_call_ptr_i32_i32_i32_i32_to_i32(
    void *fn, void *a, int b, int c, int d, int e
) {
    return ((int (*)(void *, int, int, int, int))fn)(a, b, c, d, e);
}

/* ── Generic trampoline (up to 6 void* args) ─────────────────────── */

void *basl_ffi_call_generic(void *fn, int nargs,
    void *a0, void *a1, void *a2, void *a3, void *a4, void *a5
) {
    switch (nargs) {
    case 0: return ((void *(*)(void))fn)();
    case 1: return ((void *(*)(void *))fn)(a0);
    case 2: return ((void *(*)(void *, void *))fn)(a0, a1);
    case 3: return ((void *(*)(void *, void *, void *))fn)(a0, a1, a2);
    case 4: return ((void *(*)(void *, void *, void *, void *))fn)(a0, a1, a2, a3);
    case 5: return ((void *(*)(void *, void *, void *, void *, void *))fn)(a0, a1, a2, a3, a4);
    case 6: return ((void *(*)(void *, void *, void *, void *, void *, void *))fn)(a0, a1, a2, a3, a4, a5);
    default: return (void *)0;
    }
}

/* ── Helpers ──────────────────────────────────────────────────────── */

void *basl_ffi_int_to_ptr(uintptr_t v) { return (void *)v; }
uintptr_t basl_ffi_ptr_to_int(void *p) { return (uintptr_t)p; }
