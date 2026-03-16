/* Tests for FFI callback pool, libffi integration, platform dlopen,
 * and high-level ffi module functions (bind/call/call_f/call_s). */
#include "basl_test.h"

#include <stdint.h>
#include <string.h>

#include "internal/ffi_callback.h"
#include "platform/platform.h"

#ifdef BASL_HAS_LIBFFI
#include <ffi.h>

/*
 * Pedantic-safe casts for ffi_call's function pointer argument.
 * Function-to-function cast is allowed in C; void*-to-function is not.
 */
#define BASL_FFI_FN(f) ((void (*)(void))(f))

/* For void* from dlsym — use memcpy to avoid pedantic warning. */
static void (*dlsym_to_fnptr(void *p))(void) {
    void (*f)(void);
    memcpy(&f, &p, sizeof(f));
    return f;
}
#define BASL_FFI_FN_P(p) dlsym_to_fnptr(p)
#endif /* BASL_HAS_LIBFFI */

/*
 * Pedantic-safe cast: void* -> typed function pointer via memcpy.
 */
typedef intptr_t (*cb_t)(intptr_t, intptr_t, intptr_t, intptr_t);
static cb_t v2fn_cb_t(void *p) {
    cb_t f; memcpy(&f, &p, sizeof(f)); return f;
}
#define V2FN(type, ptr) v2fn_##type(ptr)

/* ── Test helpers (used by libffi tests) ─────────────────────────── */

#ifdef BASL_HAS_LIBFFI
static int    fn_i32_i32_to_i32(int a, int b) { return a + b; }
static double fn_f64_to_f64(double a)         { return a / 2.0; }
static int    fn_void_to_i32(void)            { return 42; }
static double fn_void_to_f64(void)            { return 3.14159; }
static void   fn_void_to_void(void)           { /* noop */ }
static const char *fn_void_to_str(void)       { return "hello"; }
#endif

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

    cb_t cb = V2FN(cb_t, ptr);
    intptr_t result = cb(5, 0, 0, 0);
    EXPECT_EQ(slot * 100 + 5, (int)result);

    basl_ffi_callback_free(slot);
}

TEST(FFICallback, AllSlotsExhaust) {
    basl_ffi_callback_set_dispatch(test_dispatch);
    void *ptrs[8];
    int slots[8];

    for (int i = 0; i < 8; i++) {
        slots[i] = basl_ffi_callback_alloc(&ptrs[i]);
        EXPECT_TRUE(slots[i] >= 0);
    }

    void *extra = NULL;
    EXPECT_EQ(-1, basl_ffi_callback_alloc(&extra));

    basl_ffi_callback_free(slots[3]);
    int s = basl_ffi_callback_alloc(&extra);
    EXPECT_TRUE(s >= 0);

    for (int i = 0; i < 8; i++)
        basl_ffi_callback_free(slots[i]);
    basl_ffi_callback_free(s);
    basl_ffi_callback_set_dispatch(NULL);
}

TEST(FFICallback, MultipleSlotDispatch) {
    basl_ffi_callback_set_dispatch(test_dispatch);
    void *ptrs[4];
    int slots[4];
    for (int i = 0; i < 4; i++)
        slots[i] = basl_ffi_callback_alloc(&ptrs[i]);

    for (int i = 0; i < 4; i++) {
        cb_t cb = V2FN(cb_t, ptrs[i]);
        intptr_t r = cb(1, 0, 0, 0);
        EXPECT_EQ(slots[i] * 100 + 1, (int)r);
    }

    for (int i = 0; i < 4; i++)
        basl_ffi_callback_free(slots[i]);
    basl_ffi_callback_set_dispatch(NULL);
}

TEST(FFICallback, FreeAndReuse) {
    basl_ffi_callback_set_dispatch(test_dispatch);
    void *p1 = NULL, *p2 = NULL;
    int s1 = basl_ffi_callback_alloc(&p1);
    basl_ffi_callback_free(s1);
    int s2 = basl_ffi_callback_alloc(&p2);
    EXPECT_EQ(s1, s2);
    basl_ffi_callback_free(s2);
    basl_ffi_callback_set_dispatch(NULL);
}

TEST(FFICallback, NullDispatch) {
    basl_ffi_callback_set_dispatch(NULL);
    void *ptr = NULL;
    int slot = basl_ffi_callback_alloc(&ptr);
    EXPECT_TRUE(slot >= 0);

    cb_t cb = V2FN(cb_t, ptr);
    EXPECT_EQ(0, (int)cb(42, 0, 0, 0));

    basl_ffi_callback_free(slot);
}

/* ── libffi direct tests ─────────────────────────────────────────── */

#ifdef BASL_HAS_LIBFFI

TEST(FFILibffi, IntCall) {
    ffi_cif cif;
    ffi_type *args[2] = { &ffi_type_sint32, &ffi_type_sint32 };
    ffi_arg result;
    int a = 3, b = 4;
    void *vals[2] = { &a, &b };

    EXPECT_EQ((int)FFI_OK,
              (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2,
                                &ffi_type_sint32, args));
    ffi_call(&cif, BASL_FFI_FN(fn_i32_i32_to_i32), &result, vals);
    EXPECT_EQ(7, (int)result);
}

TEST(FFILibffi, FloatCall) {
    ffi_cif cif;
    ffi_type *args[1] = { &ffi_type_double };
    double result;
    double a = 5.0;
    void *vals[1] = { &a };

    EXPECT_EQ((int)FFI_OK,
              (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 1,
                                &ffi_type_double, args));
    ffi_call(&cif, BASL_FFI_FN(fn_f64_to_f64), &result, vals);
    EXPECT_NEAR(2.5, result, 0.001);
}

TEST(FFILibffi, VoidCall) {
    ffi_cif cif;
    EXPECT_EQ((int)FFI_OK,
              (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0,
                                &ffi_type_void, NULL));
    ffi_call(&cif, BASL_FFI_FN(fn_void_to_void), NULL, NULL);
    EXPECT_TRUE(1);
}

TEST(FFILibffi, NoArgsIntReturn) {
    ffi_cif cif;
    ffi_arg result;
    EXPECT_EQ((int)FFI_OK,
              (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0,
                                &ffi_type_sint32, NULL));
    ffi_call(&cif, BASL_FFI_FN(fn_void_to_i32), &result, NULL);
    EXPECT_EQ(42, (int)result);
}

TEST(FFILibffi, NoArgsDoubleReturn) {
    ffi_cif cif;
    double result;
    EXPECT_EQ((int)FFI_OK,
              (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0,
                                &ffi_type_double, NULL));
    ffi_call(&cif, BASL_FFI_FN(fn_void_to_f64), &result, NULL);
    EXPECT_NEAR(3.14159, result, 0.001);
}

TEST(FFILibffi, PointerReturn) {
    ffi_cif cif;
    void *result;
    EXPECT_EQ((int)FFI_OK,
              (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0,
                                &ffi_type_pointer, NULL));
    ffi_call(&cif, BASL_FFI_FN(fn_void_to_str), &result, NULL);
    EXPECT_STREQ("hello", (const char *)result);
}

#endif /* BASL_HAS_LIBFFI */

/* ── Platform dlopen tests ───────────────────────────────────────── */

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
    basl_status_t s = basl_platform_dlsym(handle, "nonexistent_symbol_xyz",
                                           &sym, &error);
    EXPECT_TRUE(s != BASL_STATUS_OK);

    basl_platform_dlclose(handle, &error);
}

TEST(FFIDlopen, OpenBadPath) {
    void *handle = NULL;
    basl_error_t error = {0};
    basl_status_t s = basl_platform_dlopen("/no/such/library.so",
                                            &handle, &error);
    EXPECT_TRUE(s != BASL_STATUS_OK);
}

#ifdef BASL_HAS_LIBFFI

static void (*dlsym_to_fnptr(void *p))(void);
#define BASL_FFI_FN_P(p) dlsym_to_fnptr(p)

TEST(FFIDlopen, CallViaLibffi) {
    void *handle = NULL;
    basl_error_t error = {0};
    basl_platform_dlopen(FFI_TESTLIB_PATH, &handle, &error);

    /* test_add(3, 4) -> 7 */
    void *add_fn = NULL;
    basl_platform_dlsym(handle, "test_add", &add_fn, &error);
    {
        ffi_cif cif;
        ffi_type *args[2] = { &ffi_type_sint32, &ffi_type_sint32 };
        ffi_arg result;
        int a = 3, b = 4;
        void *vals[2] = { &a, &b };
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_sint32, args);
        ffi_call(&cif, BASL_FFI_FN_P(add_fn), &result, vals);
        EXPECT_EQ(7, (int)result);
    }

    /* test_half(5.0) -> 2.5 */
    void *half_fn = NULL;
    basl_platform_dlsym(handle, "test_half", &half_fn, &error);
    {
        ffi_cif cif;
        ffi_type *args[1] = { &ffi_type_double };
        double result;
        double a = 5.0;
        void *vals[1] = { &a };
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 1, &ffi_type_double, args);
        ffi_call(&cif, BASL_FFI_FN_P(half_fn), &result, vals);
        EXPECT_NEAR(2.5, result, 0.001);
    }

    /* test_answer() -> 42 */
    void *answer_fn = NULL;
    basl_platform_dlsym(handle, "test_answer", &answer_fn, &error);
    {
        ffi_cif cif;
        ffi_arg result;
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_sint32, NULL);
        ffi_call(&cif, BASL_FFI_FN_P(answer_fn), &result, NULL);
        EXPECT_EQ(42, (int)result);
    }

    /* test_greeting() -> "hello from C" */
    void *greet_fn = NULL;
    basl_platform_dlsym(handle, "test_greeting", &greet_fn, &error);
    {
        ffi_cif cif;
        void *result;
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_pointer, NULL);
        ffi_call(&cif, BASL_FFI_FN_P(greet_fn), &result, NULL);
        EXPECT_STREQ("hello from C", (const char *)result);
    }

    /* test_noop() — void return */
    void *noop_fn = NULL;
    basl_platform_dlsym(handle, "test_noop", &noop_fn, &error);
    {
        ffi_cif cif;
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_void, NULL);
        ffi_call(&cif, BASL_FFI_FN_P(noop_fn), NULL, NULL);
        EXPECT_TRUE(1);
    }

    /* test_pi() -> 3.14159 */
    void *pi_fn = NULL;
    basl_platform_dlsym(handle, "test_pi", &pi_fn, &error);
    {
        ffi_cif cif;
        double result;
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_double, NULL);
        ffi_call(&cif, BASL_FFI_FN_P(pi_fn), &result, NULL);
        EXPECT_NEAR(3.14159, result, 0.001);
    }

    basl_platform_dlclose(handle, &error);
}

#endif /* BASL_HAS_LIBFFI */
#endif /* FFI_TESTLIB_PATH */

/* ── High-level FFI module tests (via VM) ────────────────────────── */
/* These test ffi.open/bind/call/call_f/call_s through the actual
 * native module interface with a real VM and stack. */

#if defined(FFI_TESTLIB_PATH) && defined(BASL_HAS_LIBFFI)

#include "basl/vm.h"
#include "basl/runtime.h"
#include "basl/stdlib.h"
#include "internal/basl_nanbox.h"

/* Helper: set up a runtime + VM for FFI tests. */
static void ffi_vm_setup(basl_runtime_t **rt, basl_vm_t **vm,
                          basl_error_t *error) {
    basl_runtime_open(rt, NULL, error);
    basl_vm_open(vm, *rt, NULL, error);
}

static void ffi_vm_teardown(basl_runtime_t **rt, basl_vm_t **vm) {
    basl_vm_close(vm);
    basl_runtime_close(rt);
}

/* Helper: push an i64 onto the VM stack. */
static void push_i64(basl_vm_t *vm, int64_t v, basl_error_t *error) {
    basl_value_t val = basl_nanbox_encode_int(v);
    basl_vm_stack_push(vm, &val, error);
}

/* Helper: push a string onto the VM stack. */
static void push_str(basl_vm_t *vm, basl_runtime_t *rt, const char *s,
                      basl_error_t *error) {
    basl_object_t *obj = NULL;
    basl_string_object_new_cstr(rt, s, &obj, error);
    basl_value_t val;
    basl_value_init_object(&val, &obj);
    basl_vm_stack_push(vm, &val, error);
    basl_value_release(&val);
}

/* Helper: push an f64 onto the VM stack. */
static void push_f64(basl_vm_t *vm, double v, basl_error_t *error) {
    basl_value_t val = basl_nanbox_encode_double(v);
    basl_vm_stack_push(vm, &val, error);
}

/* Look up a native function from the ffi module by name. */
static basl_native_fn_t find_ffi_fn(const char *name) {
    for (size_t i = 0; i < basl_stdlib_ffi.function_count; i++) {
        if (strcmp(basl_stdlib_ffi.functions[i].name, name) == 0)
            return basl_stdlib_ffi.functions[i].native_fn;
    }
    return NULL;
}

TEST(FFIModule, OpenBindCallClose) {
    basl_runtime_t *rt = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error;
    memset(&error, 0, sizeof(error));
    ffi_vm_setup(&rt, &vm, &error);

    basl_native_fn_t fn_open  = find_ffi_fn("open");
    basl_native_fn_t fn_bind  = find_ffi_fn("bind");
    basl_native_fn_t fn_call  = find_ffi_fn("call");
    basl_native_fn_t fn_close = find_ffi_fn("close");
    ASSERT_NE(fn_open, NULL);
    ASSERT_NE(fn_bind, NULL);
    ASSERT_NE(fn_call, NULL);
    ASSERT_NE(fn_close, NULL);

    /* ffi.open(path) -> lib handle */
    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    ASSERT_EQ((int)fn_open(vm, 1, &error), (int)BASL_STATUS_OK);
    basl_value_t lib_val = basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1);
    int64_t lib = basl_nanbox_decode_int(lib_val);
    basl_vm_stack_pop_n(vm, 1);
    EXPECT_TRUE(lib != 0);

    /* ffi.bind(lib, "test_add", "i32(i32,i32)") -> handle */
    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_add", &error);
    push_str(vm, rt, "i32(i32,i32)", &error);
    ASSERT_EQ((int)fn_bind(vm, 3, &error), (int)BASL_STATUS_OK);
    basl_value_t h_val = basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1);
    int64_t h = basl_nanbox_decode_int(h_val);
    basl_vm_stack_pop_n(vm, 1);
    EXPECT_TRUE(h >= 0);

    /* ffi.call(h, 3, 4, 0, 0, 0, 0) -> 7 */
    push_i64(vm, h, &error);
    push_i64(vm, 3, &error);
    push_i64(vm, 4, &error);
    push_i64(vm, 0, &error);
    push_i64(vm, 0, &error);
    push_i64(vm, 0, &error);
    push_i64(vm, 0, &error);
    ASSERT_EQ((int)fn_call(vm, 7, &error), (int)BASL_STATUS_OK);
    basl_value_t r_val = basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1);
    int64_t result = basl_nanbox_decode_int(r_val);
    basl_vm_stack_pop_n(vm, 1);
    EXPECT_EQ(7, (int)result);

    /* ffi.close(lib) */
    push_i64(vm, lib, &error);
    ASSERT_EQ((int)fn_close(vm, 1, &error), (int)BASL_STATUS_OK);

    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallVoidReturn) {
    basl_runtime_t *rt = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    basl_native_fn_t fn_open  = find_ffi_fn("open");
    basl_native_fn_t fn_bind  = find_ffi_fn("bind");
    basl_native_fn_t fn_call  = find_ffi_fn("call");
    basl_native_fn_t fn_close = find_ffi_fn("close");

    /* Open and bind test_noop with void() signature */
    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = basl_nanbox_decode_int(
        basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1));
    basl_vm_stack_pop_n(vm, 1);

    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_noop", &error);
    push_str(vm, rt, "void()", &error);
    fn_bind(vm, 3, &error);
    int64_t h = basl_nanbox_decode_int(
        basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1));
    basl_vm_stack_pop_n(vm, 1);

    /* ffi.call should return 0 for void */
    push_i64(vm, h, &error);
    for (int i = 0; i < 6; i++) push_i64(vm, 0, &error);
    ASSERT_EQ((int)fn_call(vm, 7, &error), (int)BASL_STATUS_OK);
    int64_t result = basl_nanbox_decode_int(
        basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1));
    basl_vm_stack_pop_n(vm, 1);
    EXPECT_EQ(0, (int)result);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallF_Float) {
    basl_runtime_t *rt = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    basl_native_fn_t fn_open   = find_ffi_fn("open");
    basl_native_fn_t fn_bind   = find_ffi_fn("bind");
    basl_native_fn_t fn_call_f = find_ffi_fn("call_f");
    basl_native_fn_t fn_close  = find_ffi_fn("close");

    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = basl_nanbox_decode_int(
        basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1));
    basl_vm_stack_pop_n(vm, 1);

    /* bind test_half: f64(f64) */
    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_half", &error);
    push_str(vm, rt, "f64(f64)", &error);
    fn_bind(vm, 3, &error);
    int64_t h = basl_nanbox_decode_int(
        basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1));
    basl_vm_stack_pop_n(vm, 1);

    /* ffi.call_f(h, 10.0, 0.0) -> 5.0 */
    push_i64(vm, h, &error);
    push_f64(vm, 10.0, &error);
    push_f64(vm, 0.0, &error);
    ASSERT_EQ((int)fn_call_f(vm, 3, &error), (int)BASL_STATUS_OK);
    double result = basl_nanbox_decode_double(
        basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1));
    basl_vm_stack_pop_n(vm, 1);
    EXPECT_NEAR(5.0, result, 0.001);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallS_String) {
    basl_runtime_t *rt = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    basl_native_fn_t fn_open   = find_ffi_fn("open");
    basl_native_fn_t fn_bind   = find_ffi_fn("bind");
    basl_native_fn_t fn_call_s = find_ffi_fn("call_s");
    basl_native_fn_t fn_close  = find_ffi_fn("close");

    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = basl_nanbox_decode_int(
        basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1));
    basl_vm_stack_pop_n(vm, 1);

    /* bind test_greeting: string() */
    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_greeting", &error);
    push_str(vm, rt, "string()", &error);
    fn_bind(vm, 3, &error);
    int64_t h = basl_nanbox_decode_int(
        basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1));
    basl_vm_stack_pop_n(vm, 1);

    /* ffi.call_s(h, 0, 0) -> "hello from C" */
    push_i64(vm, h, &error);
    push_i64(vm, 0, &error);
    push_i64(vm, 0, &error);
    ASSERT_EQ((int)fn_call_s(vm, 3, &error), (int)BASL_STATUS_OK);
    basl_value_t r_val = basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1);
    const basl_object_t *obj =
        (const basl_object_t *)basl_nanbox_decode_ptr(r_val);
    EXPECT_STREQ("hello from C", basl_string_object_c_str(obj));
    basl_vm_stack_pop_n(vm, 1);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallInvalidHandle) {
    basl_runtime_t *rt = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    basl_native_fn_t fn_call = find_ffi_fn("call");

    /* Call with handle -1 (invalid) */
    push_i64(vm, -1, &error);
    for (int i = 0; i < 6; i++) push_i64(vm, 0, &error);
    basl_status_t s = fn_call(vm, 7, &error);
    EXPECT_TRUE(s != BASL_STATUS_OK);

    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallF_InvalidHandle) {
    basl_runtime_t *rt = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    basl_native_fn_t fn_call_f = find_ffi_fn("call_f");

    push_i64(vm, 9999, &error);
    push_f64(vm, 0.0, &error);
    push_f64(vm, 0.0, &error);
    basl_status_t s = fn_call_f(vm, 3, &error);
    EXPECT_TRUE(s != BASL_STATUS_OK);

    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallNoArgsIntReturn) {
    basl_runtime_t *rt = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    basl_native_fn_t fn_open  = find_ffi_fn("open");
    basl_native_fn_t fn_bind  = find_ffi_fn("bind");
    basl_native_fn_t fn_call  = find_ffi_fn("call");
    basl_native_fn_t fn_close = find_ffi_fn("close");

    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = basl_nanbox_decode_int(
        basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1));
    basl_vm_stack_pop_n(vm, 1);

    /* bind test_answer: i32() */
    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_answer", &error);
    push_str(vm, rt, "i32()", &error);
    fn_bind(vm, 3, &error);
    int64_t h = basl_nanbox_decode_int(
        basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1));
    basl_vm_stack_pop_n(vm, 1);

    push_i64(vm, h, &error);
    for (int i = 0; i < 6; i++) push_i64(vm, 0, &error);
    ASSERT_EQ((int)fn_call(vm, 7, &error), (int)BASL_STATUS_OK);
    int64_t result = basl_nanbox_decode_int(
        basl_vm_stack_get(vm, basl_vm_stack_depth(vm) - 1));
    basl_vm_stack_pop_n(vm, 1);
    EXPECT_EQ(42, (int)result);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}

#endif /* FFI_TESTLIB_PATH && BASL_HAS_LIBFFI */

/* ── Registration ────────────────────────────────────────────────── */

void register_ffi_tests(void) {
    /* Callback pool */
    REGISTER_TEST(FFICallback, AllocAndFree);
    REGISTER_TEST(FFICallback, Dispatch);
    REGISTER_TEST(FFICallback, AllSlotsExhaust);
    REGISTER_TEST(FFICallback, MultipleSlotDispatch);
    REGISTER_TEST(FFICallback, FreeAndReuse);
    REGISTER_TEST(FFICallback, NullDispatch);
#ifdef BASL_HAS_LIBFFI
    /* libffi direct */
    REGISTER_TEST(FFILibffi, IntCall);
    REGISTER_TEST(FFILibffi, FloatCall);
    REGISTER_TEST(FFILibffi, VoidCall);
    REGISTER_TEST(FFILibffi, NoArgsIntReturn);
    REGISTER_TEST(FFILibffi, NoArgsDoubleReturn);
    REGISTER_TEST(FFILibffi, PointerReturn);
#endif
#ifdef FFI_TESTLIB_PATH
    /* Platform dlopen */
    REGISTER_TEST(FFIDlopen, OpenAndClose);
    REGISTER_TEST(FFIDlopen, SymLookup);
    REGISTER_TEST(FFIDlopen, SymNotFound);
    REGISTER_TEST(FFIDlopen, OpenBadPath);
#ifdef BASL_HAS_LIBFFI
    REGISTER_TEST(FFIDlopen, CallViaLibffi);
    /* High-level module tests */
    REGISTER_TEST(FFIModule, OpenBindCallClose);
    REGISTER_TEST(FFIModule, CallVoidReturn);
    REGISTER_TEST(FFIModule, CallF_Float);
    REGISTER_TEST(FFIModule, CallS_String);
    REGISTER_TEST(FFIModule, CallInvalidHandle);
    REGISTER_TEST(FFIModule, CallF_InvalidHandle);
    REGISTER_TEST(FFIModule, CallNoArgsIntReturn);
#endif
#endif
}
