#ifndef BASL_FFI_CALLBACK_H
#define BASL_FFI_CALLBACK_H

/* Callback trampoline pool for the BASL FFI.
 *
 * Provides a fixed pool of C function pointers that dispatch through
 * a user-registered callback function, allowing BASL closures to be
 * passed as C function pointers to foreign code.
 */

#include <stdint.h>
#include "basl/export.h"

#define BASL_FFI_MAX_CALLBACKS 8

typedef intptr_t (*basl_ffi_callback_dispatch_fn)(
    int slot, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3);

BASL_API void basl_ffi_callback_set_dispatch(basl_ffi_callback_dispatch_fn fn);
BASL_API int  basl_ffi_callback_alloc(void **out_ptr);
BASL_API void basl_ffi_callback_free(int slot);

#endif
