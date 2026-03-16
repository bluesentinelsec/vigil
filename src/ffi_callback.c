/* ffi_callback.c — Callback trampoline pool for the BASL FFI.
 *
 * Provides a fixed pool of C function pointers that dispatch through
 * a user-registered callback function.  This allows BASL closures to
 * be passed as C function pointers to foreign code.
 */
#include "internal/ffi_callback.h"

#include <string.h>

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

/*
 * Pedantic-safe conversion: function pointer -> void*.
 * ISO C forbids direct casts; use a union.
 */
static void *fnptr_to_obj(void (*f)(void)) {
    union { void *obj; void (*fn)(void); } u;
    u.fn = f;
    return u.obj;
}

int basl_ffi_callback_alloc(void **out_ptr) {
    for (int i = 0; i < BASL_FFI_MAX_CALLBACKS; i++) {
        if (!g_cb_used[i]) {
            g_cb_used[i] = 1;
            *out_ptr = fnptr_to_obj((void (*)(void))cb_table[i]);
            return i;
        }
    }
    return -1;
}

void basl_ffi_callback_free(int slot) {
    if (slot >= 0 && slot < BASL_FFI_MAX_CALLBACKS)
        g_cb_used[slot] = 0;
}
