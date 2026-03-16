#ifndef BASL_FFI_TRAMPOLINE_H
#define BASL_FFI_TRAMPOLINE_H

/*
 * C cast trampolines for the BASL FFI.
 *
 * Each function takes a void* function pointer (from dlsym) and
 * calls it with the correct C calling convention.
 */

#include <stdint.h>
#include "basl/export.h"

/* Integer */
BASL_API int    basl_ffi_call_void_to_i32(void *fn);
BASL_API int    basl_ffi_call_i32_to_i32(void *fn, int a);
BASL_API int    basl_ffi_call_i32_i32_to_i32(void *fn, int a, int b);
BASL_API void   basl_ffi_call_void_to_void(void *fn);
BASL_API void   basl_ffi_call_i32_to_void(void *fn, int a);

/* Float */
BASL_API double basl_ffi_call_void_to_f64(void *fn);
BASL_API double basl_ffi_call_f64_to_f64(void *fn, double a);
BASL_API double basl_ffi_call_f64_f64_to_f64(void *fn, double a, double b);

/* String */
BASL_API const char *basl_ffi_call_void_to_str(void *fn);
BASL_API const char *basl_ffi_call_str_to_str(void *fn, const char *a);
BASL_API void        basl_ffi_call_str_to_void(void *fn, const char *a);
BASL_API int         basl_ffi_call_str_to_i32(void *fn, const char *a);
BASL_API int         basl_ffi_call_str_str_to_i32(void *fn, const char *a, const char *b);
BASL_API const char *basl_ffi_call_i32_to_str(void *fn, int a);

/* Pointer */
BASL_API void  *basl_ffi_call_void_to_ptr(void *fn);
BASL_API void  *basl_ffi_call_ptr_to_ptr(void *fn, void *a);
BASL_API void  *basl_ffi_call_ptr_ptr_to_ptr(void *fn, void *a, void *b);
BASL_API int    basl_ffi_call_ptr_to_i32(void *fn, void *a);
BASL_API void   basl_ffi_call_ptr_to_void(void *fn, void *a);
BASL_API int    basl_ffi_call_ptr_i32_to_i32(void *fn, void *a, int b);
BASL_API void   basl_ffi_call_ptr_i32_to_void(void *fn, void *a, int b);
BASL_API int    basl_ffi_call_ptr_i32_i32_i32_i32_to_i32(void *fn, void *a, int b, int c, int d, int e);

/* Generic: up to 6 void* args, returns void* */
BASL_API void  *basl_ffi_call_generic(void *fn, int nargs,
           void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);

/* Conversion helpers */
BASL_API void     *basl_ffi_int_to_ptr(uintptr_t v);
BASL_API uintptr_t basl_ffi_ptr_to_int(void *p);

/* ── Callback trampoline pool ──────────────────────────────────── */

#define BASL_FFI_MAX_CALLBACKS 8

/*
 * Callback dispatch function type.
 * The user registers one of these per slot; the C trampoline calls it
 * with the slot index and up to 4 intptr_t arguments.
 */
typedef intptr_t (*basl_ffi_callback_dispatch_fn)(
    int slot, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3);

/*
 * Set the global dispatch function.  Must be called before any
 * callback slot is invoked from C code.
 */
BASL_API void basl_ffi_callback_set_dispatch(basl_ffi_callback_dispatch_fn fn);

/*
 * Allocate a callback slot.  Returns the slot index (0..7) or -1 if
 * all slots are in use.  *out_ptr receives the C function pointer
 * that can be passed to foreign code.
 */
BASL_API int basl_ffi_callback_alloc(void **out_ptr);

/*
 * Free a previously allocated callback slot.
 */
BASL_API void basl_ffi_callback_free(int slot);

#endif
