#ifndef BASL_FFI_TRAMPOLINE_H
#define BASL_FFI_TRAMPOLINE_H

/*
 * C cast trampolines for the BASL FFI.
 *
 * Each function takes a void* function pointer (from dlsym) and
 * calls it with the correct C calling convention.
 */

#include <stdint.h>

/* Integer */
int    basl_ffi_call_void_to_i32(void *fn);
int    basl_ffi_call_i32_to_i32(void *fn, int a);
int    basl_ffi_call_i32_i32_to_i32(void *fn, int a, int b);
void   basl_ffi_call_void_to_void(void *fn);
void   basl_ffi_call_i32_to_void(void *fn, int a);

/* Float */
double basl_ffi_call_void_to_f64(void *fn);
double basl_ffi_call_f64_to_f64(void *fn, double a);
double basl_ffi_call_f64_f64_to_f64(void *fn, double a, double b);

/* String */
const char *basl_ffi_call_void_to_str(void *fn);
const char *basl_ffi_call_str_to_str(void *fn, const char *a);
void        basl_ffi_call_str_to_void(void *fn, const char *a);
int         basl_ffi_call_str_to_i32(void *fn, const char *a);
int         basl_ffi_call_str_str_to_i32(void *fn, const char *a, const char *b);
const char *basl_ffi_call_i32_to_str(void *fn, int a);

/* Pointer */
void  *basl_ffi_call_void_to_ptr(void *fn);
void  *basl_ffi_call_ptr_to_ptr(void *fn, void *a);
void  *basl_ffi_call_ptr_ptr_to_ptr(void *fn, void *a, void *b);
int    basl_ffi_call_ptr_to_i32(void *fn, void *a);
void   basl_ffi_call_ptr_to_void(void *fn, void *a);
int    basl_ffi_call_ptr_i32_to_i32(void *fn, void *a, int b);
void   basl_ffi_call_ptr_i32_to_void(void *fn, void *a, int b);
int    basl_ffi_call_ptr_i32_i32_i32_i32_to_i32(void *fn, void *a, int b, int c, int d, int e);

/* Generic: up to 6 void* args, returns void* */
void  *basl_ffi_call_generic(void *fn, int nargs,
           void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);

/* Conversion helpers */
void     *basl_ffi_int_to_ptr(uintptr_t v);
uintptr_t basl_ffi_ptr_to_int(void *p);

#endif
