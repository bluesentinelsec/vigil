/* Tests for thread.spawn with arguments and closures. */
#include "basl_test.h"

#ifdef _WIN32
#include <io.h>
#define dup    _dup
#define dup2   _dup2
#define fileno _fileno
#define close  _close
#else
#include <unistd.h>
#endif

#include <string.h>

#include "basl/basl.h"
#include "basl/stdlib.h"
#include "internal/basl_nanbox.h"

/* ── test harness ────────────────────────────────────────────────── */

static int64_t RunWithStdlib(int *basl_test_failed_, const char *source_text) {
    basl_runtime_t *runtime = NULL;
    basl_vm_t *vm = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_native_registry_t natives;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = NULL;
    basl_value_t result;
    basl_source_id_t source_id = 0U;
    int64_t output = 0;

    EXPECT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_vm_open(&vm, runtime, NULL, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    basl_native_registry_init(&natives);
    EXPECT_EQ(basl_stdlib_register_all(&natives, &error), BASL_STATUS_OK);

    EXPECT_EQ(
        basl_source_registry_register_cstr(
            &registry, "main.basl", source_text, &source_id, &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(
        basl_compile_source_with_natives(
            &registry, source_id, &natives, &function, &diagnostics, &error),
        BASL_STATUS_OK
    );
    EXPECT_NE(function, NULL);
    EXPECT_EQ(basl_diagnostic_list_count(&diagnostics), 0U);

    basl_value_init_nil(&result);
    EXPECT_EQ(
        basl_vm_execute_function(vm, function, &result, &error),
        BASL_STATUS_OK
    );

    if (basl_nanbox_is_int(result)) {
        output = basl_nanbox_decode_int(result);
    }

    basl_value_release(&result);
    if (function) basl_object_release(&function);
    basl_native_registry_free(&natives);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    return output;
}

/* ── Tests ───────────────────────────────────────────────────────── */

TEST(BaslThreadTest, SpawnZeroArityFunction) {
    int64_t r = RunWithStdlib(basl_test_failed_,
        "import \"atomic\";\n"
        "import \"thread\";\n"
        "fn main() -> i32 {\n"
        "    i64 a = atomic.new(i64(0));\n"
        "    i64 t = thread.spawn(fn() -> void {\n"
        "        atomic.store(a, i64(42));\n"
        "    });\n"
        "    thread.join(t);\n"
        "    return i32(atomic.load(a));\n"
        "}\n"
    );
    EXPECT_EQ(r, 42);
}

TEST(BaslThreadTest, SpawnWithClosureCapture) {
    int64_t r = RunWithStdlib(basl_test_failed_,
        "import \"atomic\";\n"
        "import \"thread\";\n"
        "fn main() -> i32 {\n"
        "    i64 a = atomic.new(i64(0));\n"
        "    i64 val = i64(99);\n"
        "    i64 t = thread.spawn(fn() -> void {\n"
        "        atomic.store(a, val);\n"
        "    });\n"
        "    thread.join(t);\n"
        "    return i32(atomic.load(a));\n"
        "}\n"
    );
    EXPECT_EQ(r, 99);
}

TEST(BaslThreadTest, SpawnClosureWithMultipleCaptures) {
    int64_t r = RunWithStdlib(basl_test_failed_,
        "import \"atomic\";\n"
        "import \"thread\";\n"
        "fn main() -> i32 {\n"
        "    i64 result = atomic.new(i64(0));\n"
        "    i64 a = i64(30);\n"
        "    i64 b = i64(12);\n"
        "    i64 t = thread.spawn(fn() -> void {\n"
        "        atomic.store(result, a + b);\n"
        "    });\n"
        "    thread.join(t);\n"
        "    return i32(atomic.load(result));\n"
        "}\n"
    );
    EXPECT_EQ(r, 42);
}

TEST(BaslThreadTest, SpawnMultipleThreadsWithClosures) {
    int64_t r = RunWithStdlib(basl_test_failed_,
        "import \"atomic\";\n"
        "import \"thread\";\n"
        "fn main() -> i32 {\n"
        "    i64 sum = atomic.new(i64(0));\n"
        "    i64 t1 = thread.spawn(fn() -> void { atomic.add(sum, i64(10)); });\n"
        "    i64 t2 = thread.spawn(fn() -> void { atomic.add(sum, i64(20)); });\n"
        "    i64 t3 = thread.spawn(fn() -> void { atomic.add(sum, i64(30)); });\n"
        "    thread.join(t1);\n"
        "    thread.join(t2);\n"
        "    thread.join(t3);\n"
        "    return i32(atomic.load(sum));\n"
        "}\n"
    );
    EXPECT_EQ(r, 60);
}

TEST(BaslThreadTest, SpawnWithMutexCoordination) {
    int64_t r = RunWithStdlib(basl_test_failed_,
        "import \"atomic\";\n"
        "import \"thread\";\n"
        "fn increment(i64 mtx, i64 counter, i64 times) -> void {\n"
        "    for (i64 i = i64(0); i < times; i = i + i64(1)) {\n"
        "        thread.lock(mtx);\n"
        "        i64 cur = atomic.load(counter);\n"
        "        atomic.store(counter, cur + i64(1));\n"
        "        thread.unlock(mtx);\n"
        "    }\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    i64 mtx = thread.mutex();\n"
        "    i64 counter = atomic.new(i64(0));\n"
        "    i64 t1 = thread.spawn(fn() -> void { increment(mtx, counter, i64(100)); });\n"
        "    i64 t2 = thread.spawn(fn() -> void { increment(mtx, counter, i64(100)); });\n"
        "    thread.join(t1);\n"
        "    thread.join(t2);\n"
        "    return i32(atomic.load(counter));\n"
        "}\n"
    );
    EXPECT_EQ(r, 200);
}

/* ── Registration ────────────────────────────────────────────────── */

void register_thread_tests(void) {
    REGISTER_TEST(BaslThreadTest, SpawnZeroArityFunction);
    REGISTER_TEST(BaslThreadTest, SpawnWithClosureCapture);
    REGISTER_TEST(BaslThreadTest, SpawnClosureWithMultipleCaptures);
    REGISTER_TEST(BaslThreadTest, SpawnMultipleThreadsWithClosures);
    REGISTER_TEST(BaslThreadTest, SpawnWithMutexCoordination);
}
