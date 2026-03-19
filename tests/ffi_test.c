/* Tests for FFI callback pool, libffi integration, platform dlopen,
 * and high-level ffi module functions (bind/call/call_f/call_s). */
#include "vigil_test.h"

#include <stdint.h>
#include <string.h>

#include "internal/ffi_callback.h"
#include "platform/platform.h"
#include "vigil/runtime.h"

#ifdef VIGIL_HAS_LIBFFI
#include <ffi.h>

/*
 * Pedantic-safe casts for ffi_call's function pointer argument.
 * Function-to-function cast is allowed in C; void*-to-function is not.
 */
#define VIGIL_FFI_FN(f) ((void (*)(void))(f))

/* For void* from dlsym — use memcpy to avoid pedantic warning. */
static void (*dlsym_to_fnptr(void *p))(void)
{
    void (*f)(void);
    memcpy(&f, &p, sizeof(f));
    return f;
}
#define VIGIL_FFI_FN_P(p) dlsym_to_fnptr(p)
#endif /* VIGIL_HAS_LIBFFI */

/*
 * Pedantic-safe cast: void* -> typed function pointer via memcpy.
 */
typedef intptr_t (*cb_t)(intptr_t, intptr_t, intptr_t, intptr_t);
static cb_t v2fn_cb_t(void *p)
{
    cb_t f;
    memcpy(&f, &p, sizeof(f));
    return f;
}
#define V2FN(type, ptr) v2fn_##type(ptr)

/* ── Test helpers (used by libffi tests) ─────────────────────────── */

#ifdef VIGIL_HAS_LIBFFI
static int fn_i32_i32_to_i32(int a, int b)
{
    return a + b;
}
static double fn_f64_to_f64(double a)
{
    return a / 2.0;
}
static int fn_void_to_i32(void)
{
    return 42;
}
static double fn_void_to_f64(void)
{
    return 3.14159;
}
static void fn_void_to_void(void)
{ /* noop */
}
static const char *fn_void_to_str(void)
{
    return "hello";
}
#endif

/* ── Callback pool ───────────────────────────────────────────────── */

static intptr_t test_dispatch(int slot, intptr_t a0, intptr_t a1, intptr_t a2, intptr_t a3)
{
    (void)a1;
    (void)a2;
    (void)a3;
    return (intptr_t)slot * 100 + a0;
}

TEST(FFICallback, AllocAndFree)
{
    vigil_ffi_callback_set_dispatch(test_dispatch);
    void *ptr = NULL;
    int slot = vigil_ffi_callback_alloc(&ptr);
    EXPECT_TRUE(slot >= 0);
    EXPECT_TRUE(ptr != NULL);
    vigil_ffi_callback_free(slot);
}

TEST(FFICallback, Dispatch)
{
    vigil_ffi_callback_set_dispatch(test_dispatch);
    void *ptr = NULL;
    int slot = vigil_ffi_callback_alloc(&ptr);
    EXPECT_TRUE(slot >= 0);

    cb_t cb = V2FN(cb_t, ptr);
    intptr_t result = cb(5, 0, 0, 0);
    EXPECT_EQ(slot * 100 + 5, (int)result);

    vigil_ffi_callback_free(slot);
}

TEST(FFICallback, AllSlotsExhaust)
{
    vigil_ffi_callback_set_dispatch(test_dispatch);
    void *ptrs[8];
    int slots[8];

    for (int i = 0; i < 8; i++)
    {
        slots[i] = vigil_ffi_callback_alloc(&ptrs[i]);
        EXPECT_TRUE(slots[i] >= 0);
    }

    void *extra = NULL;
    EXPECT_EQ(-1, vigil_ffi_callback_alloc(&extra));

    vigil_ffi_callback_free(slots[3]);
    int s = vigil_ffi_callback_alloc(&extra);
    EXPECT_TRUE(s >= 0);

    for (int i = 0; i < 8; i++)
        vigil_ffi_callback_free(slots[i]);
    vigil_ffi_callback_free(s);
    vigil_ffi_callback_set_dispatch(NULL);
}

TEST(FFICallback, MultipleSlotDispatch)
{
    vigil_ffi_callback_set_dispatch(test_dispatch);
    void *ptrs[4];
    int slots[4];
    for (int i = 0; i < 4; i++)
        slots[i] = vigil_ffi_callback_alloc(&ptrs[i]);

    for (int i = 0; i < 4; i++)
    {
        cb_t cb = V2FN(cb_t, ptrs[i]);
        intptr_t r = cb(1, 0, 0, 0);
        EXPECT_EQ(slots[i] * 100 + 1, (int)r);
    }

    for (int i = 0; i < 4; i++)
        vigil_ffi_callback_free(slots[i]);
    vigil_ffi_callback_set_dispatch(NULL);
}

TEST(FFICallback, FreeAndReuse)
{
    vigil_ffi_callback_set_dispatch(test_dispatch);
    void *p1 = NULL, *p2 = NULL;
    int s1 = vigil_ffi_callback_alloc(&p1);
    vigil_ffi_callback_free(s1);
    int s2 = vigil_ffi_callback_alloc(&p2);
    EXPECT_EQ(s1, s2);
    vigil_ffi_callback_free(s2);
    vigil_ffi_callback_set_dispatch(NULL);
}

TEST(FFICallback, SlotFromPointer)
{
    void *ptr = NULL;
    int slot = -1;

    vigil_ffi_callback_set_dispatch(test_dispatch);
    slot = vigil_ffi_callback_alloc(&ptr);
    EXPECT_TRUE(slot >= 0);
    EXPECT_EQ(slot, vigil_ffi_callback_slot_from_ptr(ptr));
    EXPECT_TRUE(vigil_ffi_callback_is_allocated(slot));

    vigil_ffi_callback_free(slot);
    vigil_ffi_callback_set_dispatch(NULL);
}

TEST(FFICallback, RetainedClosureSurvivesFree)
{
    vigil_runtime_t *runtime = NULL;
    vigil_object_t *closure = NULL;
    vigil_object_t *retained = NULL;
    vigil_error_t error = {0};
    void *ptr = NULL;
    int slot = -1;

    ASSERT_EQ(VIGIL_STATUS_OK, vigil_runtime_open(&runtime, NULL, &error));
    ASSERT_EQ(VIGIL_STATUS_OK, vigil_string_object_new_cstr(runtime, "callback", &closure, &error));

    slot = vigil_ffi_callback_alloc(&ptr);
    ASSERT_TRUE(slot >= 0);
    vigil_ffi_callback_set_closure(slot, closure);

    retained = vigil_ffi_callback_retain_closure(slot);
    EXPECT_TRUE(retained != NULL);

    vigil_ffi_callback_free(slot);
    EXPECT_FALSE(vigil_ffi_callback_is_allocated(slot));

    vigil_object_release(&closure);
    if (retained)
    {
        vigil_object_release(&retained);
    }
    vigil_runtime_close(&runtime);
}

TEST(FFICallback, NullDispatch)
{
    vigil_ffi_callback_set_dispatch(NULL);
    void *ptr = NULL;
    int slot = vigil_ffi_callback_alloc(&ptr);
    EXPECT_TRUE(slot >= 0);

    cb_t cb = V2FN(cb_t, ptr);
    EXPECT_EQ(0, (int)cb(42, 0, 0, 0));

    vigil_ffi_callback_free(slot);
}

/* ── libffi direct tests ─────────────────────────────────────────── */

#ifdef VIGIL_HAS_LIBFFI

TEST(FFILibffi, IntCall)
{
    ffi_cif cif;
    ffi_type *args[2] = {&ffi_type_sint32, &ffi_type_sint32};
    ffi_arg result;
    int a = 3, b = 4;
    void *vals[2] = {&a, &b};

    EXPECT_EQ((int)FFI_OK, (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_sint32, args));
    ffi_call(&cif, VIGIL_FFI_FN(fn_i32_i32_to_i32), &result, vals);
    EXPECT_EQ(7, (int)result);
}

TEST(FFILibffi, FloatCall)
{
    ffi_cif cif;
    ffi_type *args[1] = {&ffi_type_double};
    double result;
    double a = 5.0;
    void *vals[1] = {&a};

    EXPECT_EQ((int)FFI_OK, (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 1, &ffi_type_double, args));
    ffi_call(&cif, VIGIL_FFI_FN(fn_f64_to_f64), &result, vals);
    EXPECT_NEAR(2.5, result, 0.001);
}

TEST(FFILibffi, VoidCall)
{
    ffi_cif cif;
    EXPECT_EQ((int)FFI_OK, (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_void, NULL));
    ffi_call(&cif, VIGIL_FFI_FN(fn_void_to_void), NULL, NULL);
    EXPECT_TRUE(1);
}

TEST(FFILibffi, NoArgsIntReturn)
{
    ffi_cif cif;
    ffi_arg result;
    EXPECT_EQ((int)FFI_OK, (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_sint32, NULL));
    ffi_call(&cif, VIGIL_FFI_FN(fn_void_to_i32), &result, NULL);
    EXPECT_EQ(42, (int)result);
}

TEST(FFILibffi, NoArgsDoubleReturn)
{
    ffi_cif cif;
    double result;
    EXPECT_EQ((int)FFI_OK, (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_double, NULL));
    ffi_call(&cif, VIGIL_FFI_FN(fn_void_to_f64), &result, NULL);
    EXPECT_NEAR(3.14159, result, 0.001);
}

TEST(FFILibffi, PointerReturn)
{
    ffi_cif cif;
    void *result;
    EXPECT_EQ((int)FFI_OK, (int)ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_pointer, NULL));
    ffi_call(&cif, VIGIL_FFI_FN(fn_void_to_str), &result, NULL);
    EXPECT_STREQ("hello", (const char *)result);
}

#endif /* VIGIL_HAS_LIBFFI */

/* ── Platform dlopen tests ───────────────────────────────────────── */

#ifdef FFI_TESTLIB_PATH

TEST(FFIDlopen, OpenAndClose)
{
    void *handle = NULL;
    vigil_error_t error = {0};
    vigil_status_t s = vigil_platform_dlopen(FFI_TESTLIB_PATH, &handle, &error);
    EXPECT_EQ((int)VIGIL_STATUS_OK, (int)s);
    EXPECT_TRUE(handle != NULL);
    s = vigil_platform_dlclose(handle, &error);
    EXPECT_EQ((int)VIGIL_STATUS_OK, (int)s);
}

TEST(FFIDlopen, SymLookup)
{
    void *handle = NULL;
    vigil_error_t error = {0};
    vigil_platform_dlopen(FFI_TESTLIB_PATH, &handle, &error);
    EXPECT_TRUE(handle != NULL);

    void *sym = NULL;
    vigil_status_t s = vigil_platform_dlsym(handle, "test_add", &sym, &error);
    EXPECT_EQ((int)VIGIL_STATUS_OK, (int)s);
    EXPECT_TRUE(sym != NULL);

    vigil_platform_dlclose(handle, &error);
}

TEST(FFIDlopen, SymNotFound)
{
    void *handle = NULL;
    vigil_error_t error = {0};
    vigil_platform_dlopen(FFI_TESTLIB_PATH, &handle, &error);

    void *sym = NULL;
    vigil_status_t s = vigil_platform_dlsym(handle, "nonexistent_symbol_xyz", &sym, &error);
    EXPECT_TRUE(s != VIGIL_STATUS_OK);

    vigil_platform_dlclose(handle, &error);
}

TEST(FFIDlopen, OpenBadPath)
{
    void *handle = NULL;
    vigil_error_t error = {0};
    vigil_status_t s = vigil_platform_dlopen("/no/such/library.so", &handle, &error);
    EXPECT_TRUE(s != VIGIL_STATUS_OK);
}

#ifdef VIGIL_HAS_LIBFFI

static void (*dlsym_to_fnptr(void *p))(void);
#define VIGIL_FFI_FN_P(p) dlsym_to_fnptr(p)

TEST(FFIDlopen, CallViaLibffi)
{
    void *handle = NULL;
    vigil_error_t error = {0};
    vigil_platform_dlopen(FFI_TESTLIB_PATH, &handle, &error);

    /* test_add(3, 4) -> 7 */
    void *add_fn = NULL;
    vigil_platform_dlsym(handle, "test_add", &add_fn, &error);
    {
        ffi_cif cif;
        ffi_type *args[2] = {&ffi_type_sint32, &ffi_type_sint32};
        ffi_arg result;
        int a = 3, b = 4;
        void *vals[2] = {&a, &b};
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_sint32, args);
        ffi_call(&cif, VIGIL_FFI_FN_P(add_fn), &result, vals);
        EXPECT_EQ(7, (int)result);
    }

    /* test_half(5.0) -> 2.5 */
    void *half_fn = NULL;
    vigil_platform_dlsym(handle, "test_half", &half_fn, &error);
    {
        ffi_cif cif;
        ffi_type *args[1] = {&ffi_type_double};
        double result;
        double a = 5.0;
        void *vals[1] = {&a};
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 1, &ffi_type_double, args);
        ffi_call(&cif, VIGIL_FFI_FN_P(half_fn), &result, vals);
        EXPECT_NEAR(2.5, result, 0.001);
    }

    /* test_answer() -> 42 */
    void *answer_fn = NULL;
    vigil_platform_dlsym(handle, "test_answer", &answer_fn, &error);
    {
        ffi_cif cif;
        ffi_arg result;
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_sint32, NULL);
        ffi_call(&cif, VIGIL_FFI_FN_P(answer_fn), &result, NULL);
        EXPECT_EQ(42, (int)result);
    }

    /* test_greeting() -> "hello from C" */
    void *greet_fn = NULL;
    vigil_platform_dlsym(handle, "test_greeting", &greet_fn, &error);
    {
        ffi_cif cif;
        void *result;
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_pointer, NULL);
        ffi_call(&cif, VIGIL_FFI_FN_P(greet_fn), &result, NULL);
        EXPECT_STREQ("hello from C", (const char *)result);
    }

    /* test_noop() — void return */
    void *noop_fn = NULL;
    vigil_platform_dlsym(handle, "test_noop", &noop_fn, &error);
    {
        ffi_cif cif;
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_void, NULL);
        ffi_call(&cif, VIGIL_FFI_FN_P(noop_fn), NULL, NULL);
        EXPECT_TRUE(1);
    }

    /* test_pi() -> 3.14159 */
    void *pi_fn = NULL;
    vigil_platform_dlsym(handle, "test_pi", &pi_fn, &error);
    {
        ffi_cif cif;
        double result;
        ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, &ffi_type_double, NULL);
        ffi_call(&cif, VIGIL_FFI_FN_P(pi_fn), &result, NULL);
        EXPECT_NEAR(3.14159, result, 0.001);
    }

    vigil_platform_dlclose(handle, &error);
}

#endif /* VIGIL_HAS_LIBFFI */
#endif /* FFI_TESTLIB_PATH */

/* ── High-level FFI module tests (via VM) ────────────────────────── */
/* These test ffi.open/bind/call/call_f/call_s through the actual
 * native module interface with a real VM and stack. */

#if defined(FFI_TESTLIB_PATH) && defined(VIGIL_HAS_LIBFFI) && defined(VIGIL_HAS_STDLIB_FFI)

#include "internal/vigil_nanbox.h"
#include "vigil/runtime.h"
#include "vigil/stdlib.h"
#include "vigil/vm.h"

/* Helper: set up a runtime + VM for FFI tests. */
static void ffi_vm_setup(vigil_runtime_t **rt, vigil_vm_t **vm, vigil_error_t *error)
{
    vigil_runtime_open(rt, NULL, error);
    vigil_vm_open(vm, *rt, NULL, error);
}

static void ffi_vm_teardown(vigil_runtime_t **rt, vigil_vm_t **vm)
{
    vigil_vm_close(vm);
    vigil_runtime_close(rt);
}

/* Helper: push an i64 onto the VM stack. */
static void push_i64(vigil_vm_t *vm, int64_t v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_int(v);
    vigil_vm_stack_push(vm, &val, error);
}

/* Helper: push a string onto the VM stack. */
static void push_str(vigil_vm_t *vm, vigil_runtime_t *rt, const char *s, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_string_object_new_cstr(rt, s, &obj, error);
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
}

/* Helper: push an f64 onto the VM stack. */
static void push_f64(vigil_vm_t *vm, double v, vigil_error_t *error)
{
    vigil_value_t val = vigil_nanbox_encode_double(v);
    vigil_vm_stack_push(vm, &val, error);
}

/* Look up a native function from the ffi module by name. */
static vigil_native_fn_t find_ffi_fn(const char *name)
{
    for (size_t i = 0; i < vigil_stdlib_ffi.function_count; i++)
    {
        if (strcmp(vigil_stdlib_ffi.functions[i].name, name) == 0)
            return vigil_stdlib_ffi.functions[i].native_fn;
    }
    return NULL;
}

TEST(FFIModule, OpenBindCallClose)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error;
    memset(&error, 0, sizeof(error));
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_open = find_ffi_fn("open");
    vigil_native_fn_t fn_bind = find_ffi_fn("bind");
    vigil_native_fn_t fn_call = find_ffi_fn("call");
    vigil_native_fn_t fn_close = find_ffi_fn("close");
    ASSERT_NE(fn_open, NULL);
    ASSERT_NE(fn_bind, NULL);
    ASSERT_NE(fn_call, NULL);
    ASSERT_NE(fn_close, NULL);

    /* ffi.open(path) -> lib handle */
    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    ASSERT_EQ((int)fn_open(vm, 1, &error), (int)VIGIL_STATUS_OK);
    vigil_value_t lib_val = vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1);
    int64_t lib = vigil_nanbox_decode_int(lib_val);
    vigil_vm_stack_pop_n(vm, 1);
    EXPECT_TRUE(lib != 0);

    /* ffi.bind(lib, "test_add", "i32(i32,i32)") -> handle */
    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_add", &error);
    push_str(vm, rt, "i32(i32,i32)", &error);
    ASSERT_EQ((int)fn_bind(vm, 3, &error), (int)VIGIL_STATUS_OK);
    vigil_value_t h_val = vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1);
    int64_t h = vigil_nanbox_decode_int(h_val);
    vigil_vm_stack_pop_n(vm, 1);
    EXPECT_TRUE(h >= 0);

    /* ffi.call(h, 3, 4, 0, 0, 0, 0) -> 7 */
    push_i64(vm, h, &error);
    push_i64(vm, 3, &error);
    push_i64(vm, 4, &error);
    push_i64(vm, 0, &error);
    push_i64(vm, 0, &error);
    push_i64(vm, 0, &error);
    push_i64(vm, 0, &error);
    ASSERT_EQ((int)fn_call(vm, 7, &error), (int)VIGIL_STATUS_OK);
    vigil_value_t r_val = vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1);
    int64_t result = vigil_nanbox_decode_int(r_val);
    vigil_vm_stack_pop_n(vm, 1);
    EXPECT_EQ(7, (int)result);

    /* ffi.close(lib) */
    push_i64(vm, lib, &error);
    ASSERT_EQ((int)fn_close(vm, 1, &error), (int)VIGIL_STATUS_OK);

    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallVoidReturn)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_open = find_ffi_fn("open");
    vigil_native_fn_t fn_bind = find_ffi_fn("bind");
    vigil_native_fn_t fn_call = find_ffi_fn("call");
    vigil_native_fn_t fn_close = find_ffi_fn("close");

    /* Open and bind test_noop with void() signature */
    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_noop", &error);
    push_str(vm, rt, "void()", &error);
    fn_bind(vm, 3, &error);
    int64_t h = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    /* ffi.call should return 0 for void */
    push_i64(vm, h, &error);
    for (int i = 0; i < 6; i++)
        push_i64(vm, 0, &error);
    ASSERT_EQ((int)fn_call(vm, 7, &error), (int)VIGIL_STATUS_OK);
    int64_t result = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);
    EXPECT_EQ(0, (int)result);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallF_Float)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_open = find_ffi_fn("open");
    vigil_native_fn_t fn_bind = find_ffi_fn("bind");
    vigil_native_fn_t fn_call_f = find_ffi_fn("call_f");
    vigil_native_fn_t fn_close = find_ffi_fn("close");

    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    /* bind test_half: f64(f64) */
    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_half", &error);
    push_str(vm, rt, "f64(f64)", &error);
    fn_bind(vm, 3, &error);
    int64_t h = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    /* ffi.call_f(h, 10.0, 0.0) -> 5.0 */
    push_i64(vm, h, &error);
    push_f64(vm, 10.0, &error);
    push_f64(vm, 0.0, &error);
    ASSERT_EQ((int)fn_call_f(vm, 3, &error), (int)VIGIL_STATUS_OK);
    double result = vigil_nanbox_decode_double(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);
    EXPECT_NEAR(5.0, result, 0.001);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallS_String)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_open = find_ffi_fn("open");
    vigil_native_fn_t fn_bind = find_ffi_fn("bind");
    vigil_native_fn_t fn_call_s = find_ffi_fn("call_s");
    vigil_native_fn_t fn_close = find_ffi_fn("close");

    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    /* bind test_greeting: string() */
    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_greeting", &error);
    push_str(vm, rt, "string()", &error);
    fn_bind(vm, 3, &error);
    int64_t h = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    /* ffi.call_s(h, 0, 0) -> "hello from C" */
    push_i64(vm, h, &error);
    push_i64(vm, 0, &error);
    push_i64(vm, 0, &error);
    ASSERT_EQ((int)fn_call_s(vm, 3, &error), (int)VIGIL_STATUS_OK);
    vigil_value_t r_val = vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1);
    const vigil_object_t *obj = (const vigil_object_t *)vigil_nanbox_decode_ptr(r_val);
    EXPECT_STREQ("hello from C", vigil_string_object_c_str(obj));
    vigil_vm_stack_pop_n(vm, 1);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallInvalidHandle)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_call = find_ffi_fn("call");

    /* Call with handle -1 (invalid) */
    push_i64(vm, -1, &error);
    for (int i = 0; i < 6; i++)
        push_i64(vm, 0, &error);
    vigil_status_t s = fn_call(vm, 7, &error);
    EXPECT_TRUE(s != VIGIL_STATUS_OK);

    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallF_InvalidHandle)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_call_f = find_ffi_fn("call_f");

    push_i64(vm, 9999, &error);
    push_f64(vm, 0.0, &error);
    push_f64(vm, 0.0, &error);
    vigil_status_t s = fn_call_f(vm, 3, &error);
    EXPECT_TRUE(s != VIGIL_STATUS_OK);

    ffi_vm_teardown(&rt, &vm);
}

TEST(FFIModule, CallNoArgsIntReturn)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_open = find_ffi_fn("open");
    vigil_native_fn_t fn_bind = find_ffi_fn("bind");
    vigil_native_fn_t fn_call = find_ffi_fn("call");
    vigil_native_fn_t fn_close = find_ffi_fn("close");

    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    /* bind test_answer: i32() */
    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_answer", &error);
    push_str(vm, rt, "i32()", &error);
    fn_bind(vm, 3, &error);
    int64_t h = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    push_i64(vm, h, &error);
    for (int i = 0; i < 6; i++)
        push_i64(vm, 0, &error);
    ASSERT_EQ((int)fn_call(vm, 7, &error), (int)VIGIL_STATUS_OK);
    int64_t result = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);
    EXPECT_EQ(42, (int)result);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}

/* ── Multi-arg test (8 args, exercises dynamic dispatch) ─────────── */

TEST(FFIModule, CallManyArgs)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_open = find_ffi_fn("open");
    vigil_native_fn_t fn_bind = find_ffi_fn("bind");
    vigil_native_fn_t fn_call = find_ffi_fn("call");
    vigil_native_fn_t fn_close = find_ffi_fn("close");

    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    /* bind test_sum8: i32(i32,i32,i32,i32,i32,i32,i32,i32) */
    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_sum8", &error);
    push_str(vm, rt, "i32(i32,i32,i32,i32,i32,i32,i32,i32)", &error);
    fn_bind(vm, 3, &error);
    int64_t h = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    /* call with 8 args: 1+2+3+4+5+6+7+8 = 36 */
    push_i64(vm, h, &error);
    for (int i = 1; i <= 8; i++)
        push_i64(vm, i, &error);
    ASSERT_EQ((int)fn_call(vm, 9, &error), (int)VIGIL_STATUS_OK);
    int64_t result = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);
    EXPECT_EQ(36, (int)result);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}

/* ── ffi.sym direct lookup ───────────────────────────────────────── */

TEST(FFIModule, SymLookup)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_open = find_ffi_fn("open");
    vigil_native_fn_t fn_sym = find_ffi_fn("sym");
    vigil_native_fn_t fn_close = find_ffi_fn("close");

    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    /* ffi.sym(lib, "test_add") -> non-zero pointer */
    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_add", &error);
    ASSERT_EQ((int)fn_sym(vm, 2, &error), (int)VIGIL_STATUS_OK);
    int64_t sym = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);
    EXPECT_TRUE(sym != 0);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}

/* ── ffi.call_s with invalid handle ──────────────────────────────── */

TEST(FFIModule, CallS_InvalidHandle)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_call_s = find_ffi_fn("call_s");
    push_i64(vm, -1, &error);
    push_i64(vm, 0, &error);
    push_i64(vm, 0, &error);
    vigil_status_t s = fn_call_s(vm, 3, &error);
    EXPECT_TRUE(s != VIGIL_STATUS_OK);

    ffi_vm_teardown(&rt, &vm);
}

/* ── Pointer passing: call C function with buffer pointer ────────── */

TEST(FFIModule, CallWithPointerArg)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_open = find_ffi_fn("open");
    vigil_native_fn_t fn_bind = find_ffi_fn("bind");
    vigil_native_fn_t fn_call = find_ffi_fn("call");
    vigil_native_fn_t fn_close = find_ffi_fn("close");

    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    /* bind test_strlen_ptr: i32(ptr) */
    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_strlen_ptr", &error);
    push_str(vm, rt, "i32(ptr)", &error);
    fn_bind(vm, 3, &error);
    int64_t h = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    /* Pass a string pointer */
    const char *test_str = "hello";
    push_i64(vm, h, &error);
    push_i64(vm, (int64_t)(intptr_t)test_str, &error);
    ASSERT_EQ((int)fn_call(vm, 2, &error), (int)VIGIL_STATUS_OK);
    int64_t result = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);
    EXPECT_EQ(5, (int)result);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}

/* ── Negate (single i32 arg) ─────────────────────────────────────── */

TEST(FFIModule, CallNegate)
{
    vigil_runtime_t *rt = NULL;
    vigil_vm_t *vm = NULL;
    vigil_error_t error = {0};
    ffi_vm_setup(&rt, &vm, &error);

    vigil_native_fn_t fn_open = find_ffi_fn("open");
    vigil_native_fn_t fn_bind = find_ffi_fn("bind");
    vigil_native_fn_t fn_call = find_ffi_fn("call");
    vigil_native_fn_t fn_close = find_ffi_fn("close");

    push_str(vm, rt, FFI_TESTLIB_PATH, &error);
    fn_open(vm, 1, &error);
    int64_t lib = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    push_i64(vm, lib, &error);
    push_str(vm, rt, "test_negate", &error);
    push_str(vm, rt, "i32(i32)", &error);
    fn_bind(vm, 3, &error);
    int64_t h = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);

    push_i64(vm, h, &error);
    push_i64(vm, 42, &error);
    ASSERT_EQ((int)fn_call(vm, 2, &error), (int)VIGIL_STATUS_OK);
    int64_t result = vigil_nanbox_decode_int(vigil_vm_stack_get(vm, vigil_vm_stack_depth(vm) - 1));
    vigil_vm_stack_pop_n(vm, 1);
    EXPECT_EQ(-42, (int)result);

    push_i64(vm, lib, &error);
    fn_close(vm, 1, &error);
    ffi_vm_teardown(&rt, &vm);
}
#endif /* FFI_TESTLIB_PATH && VIGIL_HAS_LIBFFI && VIGIL_HAS_STDLIB_FFI */

/* ── Registration ────────────────────────────────────────────────── */

void register_ffi_tests(void)
{
    /* Callback pool */
    REGISTER_TEST(FFICallback, AllocAndFree);
    REGISTER_TEST(FFICallback, Dispatch);
    REGISTER_TEST(FFICallback, AllSlotsExhaust);
    REGISTER_TEST(FFICallback, MultipleSlotDispatch);
    REGISTER_TEST(FFICallback, FreeAndReuse);
    REGISTER_TEST(FFICallback, SlotFromPointer);
    REGISTER_TEST(FFICallback, RetainedClosureSurvivesFree);
    REGISTER_TEST(FFICallback, NullDispatch);
#ifdef VIGIL_HAS_LIBFFI
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
#ifdef VIGIL_HAS_LIBFFI
    REGISTER_TEST(FFIDlopen, CallViaLibffi);
#endif
#if defined(VIGIL_HAS_LIBFFI) && defined(VIGIL_HAS_STDLIB_FFI)
    /* High-level module tests */
    REGISTER_TEST(FFIModule, OpenBindCallClose);
    REGISTER_TEST(FFIModule, CallVoidReturn);
    REGISTER_TEST(FFIModule, CallF_Float);
    REGISTER_TEST(FFIModule, CallS_String);
    REGISTER_TEST(FFIModule, CallInvalidHandle);
    REGISTER_TEST(FFIModule, CallF_InvalidHandle);
    REGISTER_TEST(FFIModule, CallNoArgsIntReturn);
    REGISTER_TEST(FFIModule, CallManyArgs);
    REGISTER_TEST(FFIModule, SymLookup);
    REGISTER_TEST(FFIModule, CallS_InvalidHandle);
    REGISTER_TEST(FFIModule, CallWithPointerArg);
    REGISTER_TEST(FFIModule, CallNegate);
#endif
#endif
}
