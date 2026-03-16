/* Tests for FFI trampolines, callback pool, and unsafe buffer management. */
#include "basl_test.h"

#include <stdint.h>
#include <string.h>

#include "internal/ffi_trampoline.h"
#include "platform/platform.h"

/* ── Test helpers: known C functions to call through trampolines ── */

static int  fn_void_to_i32(void)          { return 42; }
static int  fn_i32_to_i32(int a)          { return a * 2; }
static int  fn_i32_i32_to_i32(int a, int b) { return a + b; }
static void fn_void_to_void(void)         { /* noop */ }
static int  g_side_effect;
static void fn_i32_to_void(int a)         { g_side_effect = a; }

static double fn_void_to_f64(void)              { return 3.14; }
static double fn_f64_to_f64(double a)           { return a * 2.0; }
static double fn_f64_f64_to_f64(double a, double b) { return a + b; }

static void  *fn_void_to_ptr(void)              { return (void *)(intptr_t)0xBEEF; }
static void  *fn_ptr_to_ptr(void *a)            { return (void *)((intptr_t)a + 1); }
static void  *fn_ptr_ptr_to_ptr(void *a, void *b) {
    return (void *)((intptr_t)a + (intptr_t)b);
}
static int    fn_ptr_to_i32(void *a)            { return (int)(intptr_t)a; }
static void   fn_ptr_to_void(void *a)           { g_side_effect = (int)(intptr_t)a; }
static int    fn_ptr_i32_to_i32(void *a, int b) { return (int)(intptr_t)a + b; }

static int    fn_str_to_i32(const char *s)      { return (int)strlen(s); }
static int    fn_str_str_to_i32(const char *a, const char *b) {
    return strcmp(a, b);
}

/* ── Trampoline: integer ─────────────────────────────────────────── */

TEST(FFITrampoline, VoidToI32) {
    EXPECT_EQ(42, basl_ffi_call_void_to_i32((void *)fn_void_to_i32));
}

TEST(FFITrampoline, I32ToI32) {
    EXPECT_EQ(10, basl_ffi_call_i32_to_i32((void *)fn_i32_to_i32, 5));
}

TEST(FFITrampoline, I32I32ToI32) {
    EXPECT_EQ(7, basl_ffi_call_i32_i32_to_i32((void *)fn_i32_i32_to_i32, 3, 4));
}

TEST(FFITrampoline, VoidToVoid) {
    basl_ffi_call_void_to_void((void *)fn_void_to_void);
    EXPECT_TRUE(1); /* no crash = pass */
}

TEST(FFITrampoline, I32ToVoid) {
    g_side_effect = 0;
    basl_ffi_call_i32_to_void((void *)fn_i32_to_void, 99);
    EXPECT_EQ(99, g_side_effect);
}

/* ── Trampoline: float ───────────────────────────────────────────── */

TEST(FFITrampoline, VoidToF64) {
    EXPECT_NEAR(3.14, basl_ffi_call_void_to_f64((void *)fn_void_to_f64), 0.001);
}

TEST(FFITrampoline, F64ToF64) {
    EXPECT_NEAR(6.0, basl_ffi_call_f64_to_f64((void *)fn_f64_to_f64, 3.0), 0.001);
}

TEST(FFITrampoline, F64F64ToF64) {
    EXPECT_NEAR(5.5, basl_ffi_call_f64_f64_to_f64((void *)fn_f64_f64_to_f64, 2.5, 3.0), 0.001);
}

/* ── Trampoline: pointer ─────────────────────────────────────────── */

TEST(FFITrampoline, VoidToPtr) {
    void *r = basl_ffi_call_void_to_ptr((void *)fn_void_to_ptr);
    EXPECT_EQ((intptr_t)0xBEEF, (intptr_t)r);
}

TEST(FFITrampoline, PtrToPtr) {
    void *r = basl_ffi_call_ptr_to_ptr((void *)fn_ptr_to_ptr, (void *)(intptr_t)10);
    EXPECT_EQ(11, (int)(intptr_t)r);
}

TEST(FFITrampoline, PtrPtrToPtr) {
    void *r = basl_ffi_call_ptr_ptr_to_ptr(
        (void *)fn_ptr_ptr_to_ptr,
        (void *)(intptr_t)3, (void *)(intptr_t)7);
    EXPECT_EQ(10, (int)(intptr_t)r);
}

TEST(FFITrampoline, PtrToI32) {
    EXPECT_EQ(42, basl_ffi_call_ptr_to_i32((void *)fn_ptr_to_i32, (void *)(intptr_t)42));
}

TEST(FFITrampoline, PtrToVoid) {
    g_side_effect = 0;
    basl_ffi_call_ptr_to_void((void *)fn_ptr_to_void, (void *)(intptr_t)77);
    EXPECT_EQ(77, g_side_effect);
}

TEST(FFITrampoline, PtrI32ToI32) {
    EXPECT_EQ(15, basl_ffi_call_ptr_i32_to_i32(
        (void *)fn_ptr_i32_to_i32, (void *)(intptr_t)10, 5));
}

/* ── Trampoline: string ──────────────────────────────────────────── */

TEST(FFITrampoline, StrToI32) {
    EXPECT_EQ(5, basl_ffi_call_str_to_i32((void *)fn_str_to_i32, "hello"));
}

TEST(FFITrampoline, StrStrToI32) {
    EXPECT_EQ(0, basl_ffi_call_str_str_to_i32((void *)fn_str_str_to_i32, "abc", "abc"));
    EXPECT_TRUE(basl_ffi_call_str_str_to_i32((void *)fn_str_str_to_i32, "a", "b") < 0);
}

/* ── Trampoline: generic ─────────────────────────────────────────── */

TEST(FFITrampoline, Generic0) {
    void *r = basl_ffi_call_generic((void *)fn_void_to_ptr, 0,
                                     NULL, NULL, NULL, NULL, NULL, NULL);
    EXPECT_EQ((intptr_t)0xBEEF, (intptr_t)r);
}

TEST(FFITrampoline, Generic2) {
    void *r = basl_ffi_call_generic((void *)fn_ptr_ptr_to_ptr, 2,
                                     (void *)(intptr_t)3, (void *)(intptr_t)7,
                                     NULL, NULL, NULL, NULL);
    EXPECT_EQ(10, (int)(intptr_t)r);
}

/* ── Trampoline: conversion helpers ──────────────────────────────── */

TEST(FFITrampoline, IntToPtr) {
    void *p = basl_ffi_int_to_ptr(0x1234);
    EXPECT_EQ((uintptr_t)0x1234, (uintptr_t)p);
}

TEST(FFITrampoline, PtrToInt) {
    uintptr_t v = basl_ffi_ptr_to_int((void *)(uintptr_t)0x5678);
    EXPECT_EQ((uintptr_t)0x5678, v);
}

/* ── Callback pool ───────────────────────────────────────────────── */

static intptr_t test_dispatch(int slot, intptr_t a0, intptr_t a1,
                              intptr_t a2, intptr_t a3) {
    (void)a1; (void)a2; (void)a3;
    return (intptr_t)slot * 100 + a0;
}

TEST(FFICallback, AllocAndFree) {
    basl_ffi_callback_set_dispatch(test_dispatch);
    void *ptr = NULL;
    int slot = basl_ffi_callback_alloc(&ptr);
    EXPECT_TRUE(slot >= 0);
    EXPECT_TRUE(ptr != NULL);
    basl_ffi_callback_free(slot);
}

TEST(FFICallback, Dispatch) {
    basl_ffi_callback_set_dispatch(test_dispatch);
    void *ptr = NULL;
    int slot = basl_ffi_callback_alloc(&ptr);
    EXPECT_TRUE(slot >= 0);

    /* Call the C function pointer — it should dispatch through our function */
    typedef intptr_t (*cb_t)(intptr_t, intptr_t, intptr_t, intptr_t);
    cb_t cb = (cb_t)ptr;
    intptr_t result = cb(5, 0, 0, 0);
    EXPECT_EQ(slot * 100 + 5, (int)result);

    basl_ffi_callback_free(slot);
}

TEST(FFICallback, AllSlotsExhaust) {
    basl_ffi_callback_set_dispatch(test_dispatch);
    void *ptrs[8];
    int slots[8];

    /* Allocate all 8 */
    for (int i = 0; i < 8; i++) {
        slots[i] = basl_ffi_callback_alloc(&ptrs[i]);
        EXPECT_TRUE(slots[i] >= 0);
    }

    /* 9th should fail */
    void *extra = NULL;
    EXPECT_EQ(-1, basl_ffi_callback_alloc(&extra));

    /* Free one, allocate again */
    basl_ffi_callback_free(slots[3]);
    int s = basl_ffi_callback_alloc(&extra);
    EXPECT_TRUE(s >= 0);

    /* Cleanup */
    for (int i = 0; i < 8; i++)
        basl_ffi_callback_free(slots[i]);
    basl_ffi_callback_free(s);
    basl_ffi_callback_set_dispatch(NULL);
}

/* ── Platform dlopen tests (require test shared library) ──────────── */

#ifdef FFI_TESTLIB_PATH

TEST(FFIDlopen, OpenAndClose) {
    void *handle = NULL;
    basl_error_t error = {0};
    basl_status_t s = basl_platform_dlopen(FFI_TESTLIB_PATH, &handle, &error);
    EXPECT_EQ((int)BASL_STATUS_OK, (int)s);
    EXPECT_TRUE(handle != NULL);
    s = basl_platform_dlclose(handle, &error);
    EXPECT_EQ((int)BASL_STATUS_OK, (int)s);
}

TEST(FFIDlopen, SymLookup) {
    void *handle = NULL;
    basl_error_t error = {0};
    basl_platform_dlopen(FFI_TESTLIB_PATH, &handle, &error);
    EXPECT_TRUE(handle != NULL);

    void *sym = NULL;
    basl_status_t s = basl_platform_dlsym(handle, "test_add", &sym, &error);
    EXPECT_EQ((int)BASL_STATUS_OK, (int)s);
    EXPECT_TRUE(sym != NULL);

    basl_platform_dlclose(handle, &error);
}

TEST(FFIDlopen, SymNotFound) {
    void *handle = NULL;
    basl_error_t error = {0};
    basl_platform_dlopen(FFI_TESTLIB_PATH, &handle, &error);

    void *sym = NULL;
    basl_status_t s = basl_platform_dlsym(handle, "nonexistent_symbol_xyz", &sym, &error);
    EXPECT_TRUE(s != BASL_STATUS_OK);

    basl_platform_dlclose(handle, &error);
}

TEST(FFIDlopen, OpenBadPath) {
    void *handle = NULL;
    basl_error_t error = {0};
    basl_status_t s = basl_platform_dlopen("/no/such/library.so", &handle, &error);
    EXPECT_TRUE(s != BASL_STATUS_OK);
}

TEST(FFIDlopen, CallThroughTrampoline) {
    void *handle = NULL;
    basl_error_t error = {0};
    basl_platform_dlopen(FFI_TESTLIB_PATH, &handle, &error);

    void *add_fn = NULL;
    basl_platform_dlsym(handle, "test_add", &add_fn, &error);
    EXPECT_EQ(7, basl_ffi_call_i32_i32_to_i32(add_fn, 3, 4));

    void *neg_fn = NULL;
    basl_platform_dlsym(handle, "test_negate", &neg_fn, &error);
    EXPECT_EQ(-5, basl_ffi_call_i32_to_i32(neg_fn, 5));

    void *half_fn = NULL;
    basl_platform_dlsym(handle, "test_half", &half_fn, &error);
    EXPECT_NEAR(2.5, basl_ffi_call_f64_to_f64(half_fn, 5.0), 0.001);

    void *answer_fn = NULL;
    basl_platform_dlsym(handle, "test_answer", &answer_fn, &error);
    EXPECT_EQ(42, basl_ffi_call_void_to_i32(answer_fn));

    basl_platform_dlclose(handle, &error);
}

#endif /* FFI_TESTLIB_PATH */

/* ── Registration ────────────────────────────────────────────────── */

void register_ffi_tests(void) {
    /* Trampoline: integer */
    REGISTER_TEST(FFITrampoline, VoidToI32);
    REGISTER_TEST(FFITrampoline, I32ToI32);
    REGISTER_TEST(FFITrampoline, I32I32ToI32);
    REGISTER_TEST(FFITrampoline, VoidToVoid);
    REGISTER_TEST(FFITrampoline, I32ToVoid);
    /* Trampoline: float */
    REGISTER_TEST(FFITrampoline, VoidToF64);
    REGISTER_TEST(FFITrampoline, F64ToF64);
    REGISTER_TEST(FFITrampoline, F64F64ToF64);
    /* Trampoline: pointer */
    REGISTER_TEST(FFITrampoline, VoidToPtr);
    REGISTER_TEST(FFITrampoline, PtrToPtr);
    REGISTER_TEST(FFITrampoline, PtrPtrToPtr);
    REGISTER_TEST(FFITrampoline, PtrToI32);
    REGISTER_TEST(FFITrampoline, PtrToVoid);
    REGISTER_TEST(FFITrampoline, PtrI32ToI32);
    /* Trampoline: string */
    REGISTER_TEST(FFITrampoline, StrToI32);
    REGISTER_TEST(FFITrampoline, StrStrToI32);
    /* Trampoline: generic */
    REGISTER_TEST(FFITrampoline, Generic0);
    REGISTER_TEST(FFITrampoline, Generic2);
    /* Trampoline: conversion */
    REGISTER_TEST(FFITrampoline, IntToPtr);
    REGISTER_TEST(FFITrampoline, PtrToInt);
    /* Callback pool */
    REGISTER_TEST(FFICallback, AllocAndFree);
    REGISTER_TEST(FFICallback, Dispatch);
    REGISTER_TEST(FFICallback, AllSlotsExhaust);
#ifdef FFI_TESTLIB_PATH
    /* Platform dlopen */
    REGISTER_TEST(FFIDlopen, OpenAndClose);
    REGISTER_TEST(FFIDlopen, SymLookup);
    REGISTER_TEST(FFIDlopen, SymNotFound);
    REGISTER_TEST(FFIDlopen, OpenBadPath);
    REGISTER_TEST(FFIDlopen, CallThroughTrampoline);
#endif
}
