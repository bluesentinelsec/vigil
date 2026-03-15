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

#include <math.h>
#include <string.h>


#include "basl/basl.h"
#include "basl/stdlib.h"

/* ── test harness ────────────────────────────────────────────────── */

/*
 * Compile and run a BASL program that imports stdlib modules.
 * The program's main() must return i32.  Returns that value.
 */
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
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_INT);
    output = basl_value_as_int(&result);

    basl_value_release(&result);
    basl_object_release(&function);
    basl_diagnostic_list_free(&diagnostics);
    basl_native_registry_free(&natives);
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    return output;
}

/*
 * Same as RunWithStdlib but captures stdout and returns it.
 * The program's exit code is expected to be 0.
 */
static char *RunAndCaptureStdout(int *basl_test_failed_, const char *source_text) {
    FILE *tmp = tmpfile();
    int saved = dup(fileno(stdout));
    fflush(stdout);
    dup2(fileno(tmp), fileno(stdout));
    int64_t rc = RunWithStdlib(basl_test_failed_, source_text);
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);
    long sz = ftell(tmp);
    rewind(tmp);
    char *buf = (char *)calloc(1, (size_t)sz + 1);
    fread(buf, 1, (size_t)sz, tmp);
    fclose(tmp);
    EXPECT_EQ(rc, 0);
    return buf;
}

static char *RunAndCaptureStderr(int *basl_test_failed_, const char *source_text) {
    FILE *tmp = tmpfile();
    int saved = dup(fileno(stderr));
    fflush(stderr);
    dup2(fileno(tmp), fileno(stderr));
    int64_t rc = RunWithStdlib(basl_test_failed_, source_text);
    fflush(stderr);
    dup2(saved, fileno(stderr));
    close(saved);
    long sz = ftell(tmp);
    rewind(tmp);
    char *buf = (char *)calloc(1, (size_t)sz + 1);
    fread(buf, 1, (size_t)sz, tmp);
    fclose(tmp);
    EXPECT_EQ(rc, 0);
    return buf;
}

/* ── fmt tests ───────────────────────────────────────────────────── */

TEST(BaslStdlibFmtTest, PrintlnOutputsStringWithNewline) {
    char *out = RunAndCaptureStdout(basl_test_failed_, "\n"
        "        import \"fmt\";\n"
        "        fn main() -> i32 {\n"
        "            fmt.println(\"hello\");\n"
        "            return 0;\n"
        "        }\n"
        "    ");
    EXPECT_STREQ(out, "hello\n");
}

TEST(BaslStdlibFmtTest, PrintOutputsStringWithoutNewline) {
    char *out = RunAndCaptureStdout(basl_test_failed_, "\n"
        "        import \"fmt\";\n"
        "        fn main() -> i32 {\n"
        "            fmt.print(\"ab\");\n"
        "            fmt.print(\"cd\");\n"
        "            return 0;\n"
        "        }\n"
        "    ");
    EXPECT_STREQ(out, "abcd");
}

TEST(BaslStdlibFmtTest, EprintlnOutputsToStderr) {
    char *err = RunAndCaptureStderr(basl_test_failed_, "\n"
        "        import \"fmt\";\n"
        "        fn main() -> i32 {\n"
        "            fmt.eprintln(\"oops\");\n"
        "            return 0;\n"
        "        }\n"
        "    ");
    EXPECT_STREQ(err, "oops\n");
}

TEST(BaslStdlibFmtTest, PrintlnEmptyString) {
    char *out = RunAndCaptureStdout(basl_test_failed_, "\n"
        "        import \"fmt\";\n"
        "        fn main() -> i32 {\n"
        "            fmt.println(\"\");\n"
        "            return 0;\n"
        "        }\n"
        "    ");
    EXPECT_STREQ(out, "\n");
}

TEST(BaslStdlibFmtTest, PrintlnWithFString) {
    char *out = RunAndCaptureStdout(basl_test_failed_, "\n"
        "        import \"fmt\";\n"
        "        fn main() -> i32 {\n"
        "            i32 x = 42;\n"
        "            fmt.println(f\"val={x}\");\n"
        "            return 0;\n"
        "        }\n"
        "    ");
    EXPECT_STREQ(out, "val=42\n");
}

TEST(BaslStdlibFmtTest, PrintlnWithVariable) {
    char *out = RunAndCaptureStdout(basl_test_failed_, "\n"
        "        import \"fmt\";\n"
        "        fn main() -> i32 {\n"
        "            string s = \"world\";\n"
        "            fmt.println(s);\n"
        "            return 0;\n"
        "        }\n"
        "    ");
    EXPECT_STREQ(out, "world\n");
}

TEST(BaslStdlibFmtTest, PrintlnInLoop) {
    char *out = RunAndCaptureStdout(basl_test_failed_, "\n"
        "        import \"fmt\";\n"
        "        fn main() -> i32 {\n"
        "            for (i32 i = 0; i < 3; i++) {\n"
        "                fmt.println(string(i));\n"
        "            }\n"
        "            return 0;\n"
        "        }\n"
        "    ");
    EXPECT_STREQ(out, "0\n1\n2\n");
}

/* ── math: constants ─────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, PiReturnsCorrectValue) {
    /*
     * Encode a pass/fail check as an integer return.
     * The BASL program returns 0 if pi matches within tolerance.
     */
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 diff = math.abs(math.pi() - 3.14159265358979323846);\n"
        "            if (diff > 0.000000000000001) { return 1; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, EReturnsCorrectValue) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 diff = math.abs(math.e() - 2.71828182845904523536);\n"
        "            if (diff > 0.000000000000001) { return 1; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── math: rounding / conversion table tests ─────────────────────── */

/*
 * Each rounding function is tested with a table of inputs including
 * positive, negative, zero, and edge cases.  The BASL program
 * encodes the index of the first failing case (1-based) or 0 on
 * success.
 */

TEST(BaslStdlibMathTest, FloorTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.floor(3.7) != 3.0)   { return 1; }\n"
        "            if (math.floor(-3.7) != -4.0)  { return 2; }\n"
        "            if (math.floor(0.0) != 0.0)    { return 3; }\n"
        "            if (math.floor(5.0) != 5.0)    { return 4; }\n"
        "            if (math.floor(-0.1) != -1.0)  { return 5; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, CeilTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.ceil(3.2) != 4.0)    { return 1; }\n"
        "            if (math.ceil(-3.2) != -3.0)   { return 2; }\n"
        "            if (math.ceil(0.0) != 0.0)     { return 3; }\n"
        "            if (math.ceil(5.0) != 5.0)     { return 4; }\n"
        "            if (math.ceil(0.1) != 1.0)     { return 5; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, RoundTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.round(3.5) != 4.0)    { return 1; }\n"
        "            if (math.round(3.4) != 3.0)    { return 2; }\n"
        "            if (math.round(-3.5) != -4.0)  { return 3; }\n"
        "            if (math.round(0.0) != 0.0)    { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, TruncTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.trunc(3.7) != 3.0)    { return 1; }\n"
        "            if (math.trunc(-3.7) != -3.0)  { return 2; }\n"
        "            if (math.trunc(0.0) != 0.0)    { return 3; }\n"
        "            if (math.trunc(5.0) != 5.0)    { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, AbsTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.abs(-42.0) != 42.0)  { return 1; }\n"
        "            if (math.abs(42.0) != 42.0)   { return 2; }\n"
        "            if (math.abs(0.0) != 0.0)     { return 3; }\n"
        "            if (math.abs(-0.0) != 0.0)    { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, SignTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.sign(42.0) != 1.0)   { return 1; }\n"
        "            if (math.sign(-42.0) != -1.0)  { return 2; }\n"
        "            if (math.sign(0.0) != 0.0)    { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── math: trig / exponential ────────────────────────────────────── */

TEST(BaslStdlibMathTest, SqrtTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.sqrt(144.0) != 12.0)  { return 1; }\n"
        "            if (math.sqrt(0.0) != 0.0)     { return 2; }\n"
        "            if (math.sqrt(1.0) != 1.0)     { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, SinCosAtZero) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.sin(0.0) != 0.0)  { return 1; }\n"
        "            if (math.cos(0.0) != 1.0)  { return 2; }\n"
        "            if (math.tan(0.0) != 0.0)  { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, LogTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.log(1.0) != 0.0)      { return 1; }\n"
        "            if (math.log2(1024.0) != 10.0)  { return 2; }\n"
        "            if (math.log10(1000.0) != 3.0)  { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, ExpAtZeroAndOne) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.exp(0.0) != 1.0)  { return 1; }\n"
        "            f64 diff = math.abs(math.exp(1.0) - math.e());\n"
        "            if (diff > 0.000000000000001) { return 2; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── math: two-argument functions ────────────────────────────────── */

TEST(BaslStdlibMathTest, PowTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.pow(2.0, 10.0) != 1024.0)  { return 1; }\n"
        "            if (math.pow(3.0, 0.0) != 1.0)      { return 2; }\n"
        "            if (math.pow(5.0, 1.0) != 5.0)      { return 3; }\n"
        "            if (math.pow(4.0, 0.5) != 2.0)      { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, MinMaxTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.min(3.0, 7.0) != 3.0)    { return 1; }\n"
        "            if (math.min(-1.0, 1.0) != -1.0)   { return 2; }\n"
        "            if (math.min(5.0, 5.0) != 5.0)    { return 3; }\n"
        "            if (math.max(3.0, 7.0) != 7.0)    { return 4; }\n"
        "            if (math.max(-1.0, 1.0) != 1.0)    { return 5; }\n"
        "            if (math.max(5.0, 5.0) != 5.0)    { return 6; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Atan2Table) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.atan2(0.0, 1.0) != 0.0)  { return 1; }\n"
        "            f64 diff = math.abs(math.atan2(1.0, 1.0) - math.pi() / 4.0);\n"
        "            if (diff > 0.000000000000001) { return 2; }\n"
        "            // atan2(1, 0) == pi/2\n"
        "            f64 diff2 = math.abs(math.atan2(1.0, 0.0) - math.pi() / 2.0);\n"
        "            if (diff2 > 0.000000000000001) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, HypotTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.hypot(3.0, 4.0) != 5.0)    { return 1; }\n"
        "            if (math.hypot(0.0, 0.0) != 0.0)    { return 2; }\n"
        "            if (math.hypot(5.0, 0.0) != 5.0)    { return 3; }\n"
        "            if (math.hypot(0.0, 7.0) != 7.0)    { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, FmodTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.fmod(7.5, 3.0) != 1.5)    { return 1; }\n"
        "            if (math.fmod(10.0, 5.0) != 0.0)   { return 2; }\n"
        "            if (math.fmod(-7.5, 3.0) != -1.5)  { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── math: three-argument functions ──────────────────────────────── */

TEST(BaslStdlibMathTest, ClampTable) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.clamp(5.0, 0.0, 10.0) != 5.0)    { return 1; }\n"
        "            if (math.clamp(-5.0, 0.0, 10.0) != 0.0)   { return 2; }\n"
        "            if (math.clamp(15.0, 0.0, 10.0) != 10.0)   { return 3; }\n"
        "            if (math.clamp(0.0, 0.0, 10.0) != 0.0)    { return 4; }\n"
        "            if (math.clamp(10.0, 0.0, 10.0) != 10.0)   { return 5; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── math: composition ───────────────────────────────────────────── */

TEST(BaslStdlibMathTest, ComposedExpressions) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            // pythagorean theorem via composition\n"
        "            f64 hyp = math.sqrt(math.pow(3.0, 2.0) + math.pow(4.0, 2.0));\n"
        "            if (hyp != 5.0) { return 1; }\n"
        "\n"
        "            // clamp via min/max matches clamp()\n"
        "            f64 val = 15.0;\n"
        "            f64 a = math.min(math.max(val, 0.0), 10.0);\n"
        "            f64 b = math.clamp(val, 0.0, 10.0);\n"
        "            if (a != b) { return 2; }\n"
        "\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── native class: Vec2 ──────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec2ConstructionAndFields) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec2 v = math.Vec2(3.0, 4.0);\n"
        "            if (v.x != 3.0) { return 1; }\n"
        "            if (v.y != 4.0) { return 2; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec2FieldMutation) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec2 v = math.Vec2(1.0, 2.0);\n"
        "            v.x = 10.0;\n"
        "            v.y = 20.0;\n"
        "            if (v.x != 10.0) { return 1; }\n"
        "            if (v.y != 20.0) { return 2; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec2Length) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec2 v = math.Vec2(3.0, 4.0);\n"
        "            if (v.length() != 5.0) { return 1; }\n"
        "            math.Vec2 zero = math.Vec2(0.0, 0.0);\n"
        "            if (zero.length() != 0.0) { return 2; }\n"
        "            math.Vec2 unit = math.Vec2(1.0, 0.0);\n"
        "            if (unit.length() != 1.0) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec2Dot) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec2 a = math.Vec2(3.0, 4.0);\n"
        "            math.Vec2 b = math.Vec2(1.0, 0.0);\n"
        "            if (a.dot(b) != 3.0) { return 1; }\n"
        "            // perpendicular vectors: dot == 0\n"
        "            math.Vec2 c = math.Vec2(0.0, 1.0);\n"
        "            if (b.dot(c) != 0.0) { return 2; }\n"
        "            // self dot\n"
        "            if (a.dot(a) != 25.0) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec2WithScalarMath) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec2 v = math.Vec2(3.0, 4.0);\n"
        "            f64 len = v.length();\n"
        "            if (math.sqrt(math.pow(len, 2.0)) != 5.0) { return 1; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── new scalar functions ────────────────────────────────────────── */

TEST(BaslStdlibMathTest, InverseTrig) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            // asin(0) == 0\n"
        "            if (math.abs(math.asin(0.0)) > eps) { return 1; }\n"
        "            // acos(1) == 0\n"
        "            if (math.abs(math.acos(1.0)) > eps) { return 2; }\n"
        "            // atan(0) == 0\n"
        "            if (math.abs(math.atan(0.0)) > eps) { return 3; }\n"
        "            // asin(1) ~= pi/2\n"
        "            if (math.abs(math.asin(1.0) - math.pi() / 2.0) > eps) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Deg2RadRad2Deg) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            if (math.abs(math.deg2rad(180.0) - math.pi()) > eps) { return 1; }\n"
        "            if (math.abs(math.rad2deg(math.pi()) - 180.0) > eps) { return 2; }\n"
        "            if (math.abs(math.deg2rad(0.0)) > eps) { return 3; }\n"
        "            if (math.abs(math.rad2deg(0.0)) > eps) { return 4; }\n"
        "            // roundtrip\n"
        "            if (math.abs(math.rad2deg(math.deg2rad(45.0)) - 45.0) > eps) { return 5; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Lerp) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.lerp(0.0, 10.0, 0.0) != 0.0) { return 1; }\n"
        "            if (math.lerp(0.0, 10.0, 1.0) != 10.0) { return 2; }\n"
        "            if (math.lerp(0.0, 10.0, 0.5) != 5.0) { return 3; }\n"
        "            if (math.lerp(10.0, 20.0, 0.25) != 12.5) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Normalize) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.normalize(5.0, 0.0, 10.0) != 0.5) { return 1; }\n"
        "            if (math.normalize(0.0, 0.0, 10.0) != 0.0) { return 2; }\n"
        "            if (math.normalize(10.0, 0.0, 10.0) != 1.0) { return 3; }\n"
        "            // degenerate range\n"
        "            if (math.normalize(5.0, 5.0, 5.0) != 0.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Wrap) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            if (math.abs(math.wrap(370.0, 0.0, 360.0) - 10.0) > eps) { return 1; }\n"
        "            if (math.abs(math.wrap(-10.0, 0.0, 360.0) - 350.0) > eps) { return 2; }\n"
        "            if (math.abs(math.wrap(5.0, 0.0, 10.0) - 5.0) > eps) { return 3; }\n"
        "            if (math.abs(math.wrap(0.0, 0.0, 360.0)) > eps) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Remap) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.remap(5.0, 0.0, 10.0, 0.0, 100.0) != 50.0) { return 1; }\n"
        "            if (math.remap(0.0, 0.0, 10.0, 0.0, 100.0) != 0.0) { return 2; }\n"
        "            if (math.remap(10.0, 0.0, 10.0, 0.0, 100.0) != 100.0) { return 3; }\n"
        "            if (math.remap(5.0, 0.0, 10.0, 100.0, 200.0) != 150.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Vec2 new methods ────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec2Normalize) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec2 v = math.Vec2(3.0, 4.0);\n"
        "            math.Vec2 n = v.normalize();\n"
        "            if (math.abs(n.length() - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(n.x - 0.6) > eps) { return 2; }\n"
        "            if (math.abs(n.y - 0.8) > eps) { return 3; }\n"
        "            // zero vector normalizes to zero\n"
        "            math.Vec2 z = math.Vec2(0.0, 0.0);\n"
        "            math.Vec2 zn = z.normalize();\n"
        "            if (zn.x != 0.0) { return 4; }\n"
        "            if (zn.y != 0.0) { return 5; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec2AddSubScaleDistance) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec2 a = math.Vec2(1.0, 2.0);\n"
        "            math.Vec2 b = math.Vec2(3.0, 4.0);\n"
        "            math.Vec2 c = a.add(b);\n"
        "            if (c.x != 4.0) { return 1; }\n"
        "            if (c.y != 6.0) { return 2; }\n"
        "            math.Vec2 d = a.sub(b);\n"
        "            if (d.x != -2.0) { return 3; }\n"
        "            if (d.y != -2.0) { return 4; }\n"
        "            math.Vec2 s = a.scale(3.0);\n"
        "            if (s.x != 3.0) { return 5; }\n"
        "            if (s.y != 6.0) { return 6; }\n"
        "            f64 dist = a.distance(b);\n"
        "            if (math.abs(dist - 2.8284271247461903) > eps) { return 7; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Vec3 ────────────────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec3ConstructionAndFields) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec3 v = math.Vec3(1.0, 2.0, 3.0);\n"
        "            if (v.x != 1.0) { return 1; }\n"
        "            if (v.y != 2.0) { return 2; }\n"
        "            if (v.z != 3.0) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec3Length) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec3 v = math.Vec3(1.0, 2.0, 2.0);\n"
        "            if (math.abs(v.length() - 3.0) > eps) { return 1; }\n"
        "            math.Vec3 z = math.Vec3(0.0, 0.0, 0.0);\n"
        "            if (z.length() != 0.0) { return 2; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec3DotAndCross) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec3 x = math.Vec3(1.0, 0.0, 0.0);\n"
        "            math.Vec3 y = math.Vec3(0.0, 1.0, 0.0);\n"
        "            // perpendicular: dot == 0\n"
        "            if (x.dot(y) != 0.0) { return 1; }\n"
        "            // x cross y == z\n"
        "            math.Vec3 z = x.cross(y);\n"
        "            if (z.x != 0.0) { return 2; }\n"
        "            if (z.y != 0.0) { return 3; }\n"
        "            if (z.z != 1.0) { return 4; }\n"
        "            // self dot\n"
        "            math.Vec3 v = math.Vec3(1.0, 2.0, 3.0);\n"
        "            if (v.dot(v) != 14.0) { return 5; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec3NormalizeAddSubScaleDistance) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec3 a = math.Vec3(1.0, 2.0, 3.0);\n"
        "            math.Vec3 b = math.Vec3(4.0, 5.0, 6.0);\n"
        "            // normalize\n"
        "            math.Vec3 n = a.normalize();\n"
        "            if (math.abs(n.length() - 1.0) > eps) { return 1; }\n"
        "            // add\n"
        "            math.Vec3 c = a.add(b);\n"
        "            if (c.x != 5.0) { return 2; }\n"
        "            if (c.y != 7.0) { return 3; }\n"
        "            if (c.z != 9.0) { return 4; }\n"
        "            // sub\n"
        "            math.Vec3 d = a.sub(b);\n"
        "            if (d.x != -3.0) { return 5; }\n"
        "            if (d.y != -3.0) { return 6; }\n"
        "            if (d.z != -3.0) { return 7; }\n"
        "            // scale\n"
        "            math.Vec3 s = a.scale(2.0);\n"
        "            if (s.x != 2.0) { return 8; }\n"
        "            if (s.y != 4.0) { return 9; }\n"
        "            if (s.z != 6.0) { return 10; }\n"
        "            // distance\n"
        "            f64 dist = a.distance(b);\n"
        "            if (math.abs(dist - math.sqrt(27.0)) > eps) { return 11; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec3MethodChaining) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            // physics: pos += vel * dt\n"
        "            math.Vec3 pos = math.Vec3(0.0, 0.0, 0.0);\n"
        "            math.Vec3 vel = math.Vec3(10.0, 20.0, 30.0);\n"
        "            math.Vec3 new_pos = pos.add(vel.scale(0.1));\n"
        "            if (math.abs(new_pos.x - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(new_pos.y - 2.0) > eps) { return 2; }\n"
        "            if (math.abs(new_pos.z - 3.0) > eps) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Tier 1 scalar: step, smoothstep, inverseLerp ────────────────── */

TEST(BaslStdlibMathTest, Step) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.step(0.5, 0.3) != 0.0) { return 1; }\n"
        "            if (math.step(0.5, 0.5) != 1.0) { return 2; }\n"
        "            if (math.step(0.5, 0.7) != 1.0) { return 3; }\n"
        "            if (math.step(0.0, -1.0) != 0.0) { return 4; }\n"
        "            if (math.step(0.0, 0.0) != 1.0) { return 5; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Smoothstep) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            if (math.smoothstep(0.0, 1.0, 0.0) != 0.0) { return 1; }\n"
        "            if (math.smoothstep(0.0, 1.0, 1.0) != 1.0) { return 2; }\n"
        "            if (math.abs(math.smoothstep(0.0, 1.0, 0.5) - 0.5) > eps) { return 3; }\n"
        "            // clamped below\n"
        "            if (math.smoothstep(0.0, 1.0, -1.0) != 0.0) { return 4; }\n"
        "            // clamped above\n"
        "            if (math.smoothstep(0.0, 1.0, 2.0) != 1.0) { return 5; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, InverseLerp) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            if (math.inverseLerp(0.0, 10.0, 0.0) != 0.0) { return 1; }\n"
        "            if (math.inverseLerp(0.0, 10.0, 10.0) != 1.0) { return 2; }\n"
        "            if (math.inverseLerp(0.0, 10.0, 5.0) != 0.5) { return 3; }\n"
        "            // degenerate\n"
        "            if (math.inverseLerp(5.0, 5.0, 5.0) != 0.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Tier 1 Vec2: lengthSqr, negate, lerp, reflect ──────────────── */

TEST(BaslStdlibMathTest, Vec2LengthSqrAndNegate) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec2 v = math.Vec2(3.0, 4.0);\n"
        "            if (v.lengthSqr() != 25.0) { return 1; }\n"
        "            math.Vec2 n = v.negate();\n"
        "            if (n.x != -3.0) { return 2; }\n"
        "            if (n.y != -4.0) { return 3; }\n"
        "            // negate of zero\n"
        "            math.Vec2 z = math.Vec2(0.0, 0.0);\n"
        "            math.Vec2 zn = z.negate();\n"
        "            if (zn.x != 0.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec2LerpAndReflect) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec2 a = math.Vec2(0.0, 0.0);\n"
        "            math.Vec2 b = math.Vec2(10.0, 20.0);\n"
        "            // lerp t=0 -> a\n"
        "            math.Vec2 l0 = a.lerp(b, 0.0);\n"
        "            if (l0.x != 0.0) { return 1; }\n"
        "            // lerp t=1 -> b\n"
        "            math.Vec2 l1 = a.lerp(b, 1.0);\n"
        "            if (l1.x != 10.0) { return 2; }\n"
        "            // lerp t=0.5 -> midpoint\n"
        "            math.Vec2 mid = a.lerp(b, 0.5);\n"
        "            if (mid.x != 5.0) { return 3; }\n"
        "            if (mid.y != 10.0) { return 4; }\n"
        "            // reflect (1,-1) off (0,1) -> (1,1)\n"
        "            math.Vec2 inc = math.Vec2(1.0, -1.0);\n"
        "            math.Vec2 n = math.Vec2(0.0, 1.0);\n"
        "            math.Vec2 r = inc.reflect(n);\n"
        "            if (math.abs(r.x - 1.0) > eps) { return 5; }\n"
        "            if (math.abs(r.y - 1.0) > eps) { return 6; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Tier 1 Vec3: lengthSqr, negate, lerp, reflect, angle ───────── */

TEST(BaslStdlibMathTest, Vec3LengthSqrAndNegate) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec3 v = math.Vec3(1.0, 2.0, 3.0);\n"
        "            if (v.lengthSqr() != 14.0) { return 1; }\n"
        "            math.Vec3 n = v.negate();\n"
        "            if (n.x != -1.0) { return 2; }\n"
        "            if (n.y != -2.0) { return 3; }\n"
        "            if (n.z != -3.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec3LerpAndReflect) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec3 a = math.Vec3(0.0, 0.0, 0.0);\n"
        "            math.Vec3 b = math.Vec3(10.0, 20.0, 30.0);\n"
        "            math.Vec3 mid = a.lerp(b, 0.5);\n"
        "            if (mid.x != 5.0) { return 1; }\n"
        "            if (mid.y != 10.0) { return 2; }\n"
        "            if (mid.z != 15.0) { return 3; }\n"
        "            // reflect downward off ground\n"
        "            math.Vec3 down = math.Vec3(1.0, -1.0, 0.0);\n"
        "            math.Vec3 up = math.Vec3(0.0, 1.0, 0.0);\n"
        "            math.Vec3 bounce = down.reflect(up);\n"
        "            if (math.abs(bounce.x - 1.0) > eps) { return 4; }\n"
        "            if (math.abs(bounce.y - 1.0) > eps) { return 5; }\n"
        "            if (math.abs(bounce.z) > eps) { return 6; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec3Angle) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec3 x = math.Vec3(1.0, 0.0, 0.0);\n"
        "            math.Vec3 y = math.Vec3(0.0, 1.0, 0.0);\n"
        "            // perpendicular -> pi/2\n"
        "            if (math.abs(x.angle(y) - math.pi() / 2.0) > eps) { return 1; }\n"
        "            // parallel -> 0\n"
        "            if (math.abs(x.angle(x)) > eps) { return 2; }\n"
        "            // opposite -> pi\n"
        "            math.Vec3 nx = math.Vec3(-1.0, 0.0, 0.0);\n"
        "            if (math.abs(x.angle(nx) - math.pi()) > eps) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Vec4 ────────────────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec4ConstructionAndFields) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec4 v = math.Vec4(1.0, 2.0, 3.0, 4.0);\n"
        "            if (v.x != 1.0) { return 1; }\n"
        "            if (v.y != 2.0) { return 2; }\n"
        "            if (v.z != 3.0) { return 3; }\n"
        "            if (v.w != 4.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec4LengthAndDot) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec4 v = math.Vec4(1.0, 2.0, 3.0, 4.0);\n"
        "            // length = sqrt(1+4+9+16) = sqrt(30)\n"
        "            if (math.abs(v.length() - math.sqrt(30.0)) > eps) { return 1; }\n"
        "            if (v.lengthSqr() != 30.0) { return 2; }\n"
        "            // perpendicular\n"
        "            math.Vec4 a = math.Vec4(1.0, 0.0, 0.0, 0.0);\n"
        "            math.Vec4 b = math.Vec4(0.0, 1.0, 0.0, 0.0);\n"
        "            if (a.dot(b) != 0.0) { return 3; }\n"
        "            // self dot\n"
        "            if (a.dot(a) != 1.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec4Arithmetic) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec4 a = math.Vec4(1.0, 2.0, 3.0, 4.0);\n"
        "            math.Vec4 b = math.Vec4(5.0, 6.0, 7.0, 8.0);\n"
        "            math.Vec4 c = a.add(b);\n"
        "            if (c.x != 6.0) { return 1; }\n"
        "            if (c.w != 12.0) { return 2; }\n"
        "            math.Vec4 d = a.sub(b);\n"
        "            if (d.x != -4.0) { return 3; }\n"
        "            math.Vec4 s = a.scale(2.0);\n"
        "            if (s.x != 2.0) { return 4; }\n"
        "            if (s.w != 8.0) { return 5; }\n"
        "            math.Vec4 n = a.negate();\n"
        "            if (n.x != -1.0) { return 6; }\n"
        "            if (n.w != -4.0) { return 7; }\n"
        "            // normalize\n"
        "            math.Vec4 u = a.normalize();\n"
        "            if (math.abs(u.length() - 1.0) > eps) { return 8; }\n"
        "            // distance\n"
        "            if (math.abs(a.distance(b) - math.sqrt(64.0)) > eps) { return 9; }\n"
        "            // lerp\n"
        "            math.Vec4 mid = a.lerp(b, 0.5);\n"
        "            if (mid.x != 3.0) { return 10; }\n"
        "            if (mid.w != 6.0) { return 11; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Quaternion ──────────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, QuaternionIdentity) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
        "            if (math.abs(id.length() - 1.0) > eps) { return 1; }\n"
        "            // id * id = id\n"
        "            math.Quaternion r = id.multiply(id);\n"
        "            if (math.abs(r.w - 1.0) > eps) { return 2; }\n"
        "            if (math.abs(r.x) > eps) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, QuaternionConjugateAndInverse) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Quaternion q = math.Quaternion(1.0, 2.0, 3.0, 4.0);\n"
        "            math.Quaternion c = q.conjugate();\n"
        "            if (c.x != -1.0) { return 1; }\n"
        "            if (c.y != -2.0) { return 2; }\n"
        "            if (c.z != -3.0) { return 3; }\n"
        "            if (c.w != 4.0) { return 4; }\n"
        "            // q * inverse(q) ~= identity\n"
        "            math.Quaternion qn = q.normalize();\n"
        "            math.Quaternion inv = qn.inverse();\n"
        "            math.Quaternion id = qn.multiply(inv);\n"
        "            if (math.abs(id.w - 1.0) > eps) { return 5; }\n"
        "            if (math.abs(id.x) > eps) { return 6; }\n"
        "            if (math.abs(id.y) > eps) { return 7; }\n"
        "            if (math.abs(id.z) > eps) { return 8; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, QuaternionMultiply) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            // 90 deg around Y then 90 deg around Y = 180 deg around Y\n"
        "            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
        "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
        "            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));\n"
        "            math.Quaternion r180 = r90.multiply(r90);\n"
        "            // r180 should be (0, 1, 0, 0) or (0, -1, 0, 0)\n"
        "            if (math.abs(math.abs(r180.y) - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(r180.x) > eps) { return 2; }\n"
        "            if (math.abs(r180.z) > eps) { return 3; }\n"
        "            if (math.abs(r180.w) > eps) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, QuaternionFromAxisAngle) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
        "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
        "            // 0 degrees -> identity\n"
        "            math.Quaternion r0 = math.Quaternion.fromAxisAngle(yaxis, 0.0);\n"
        "            if (math.abs(r0.w - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(r0.x) > eps) { return 2; }\n"
        "            // 90 degrees around Y -> (0, sin(45), 0, cos(45))\n"
        "            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));\n"
        "            if (math.abs(r90.length() - 1.0) > eps) { return 3; }\n"
        "            if (math.abs(r90.y - math.sin(math.deg2rad(45.0))) > eps) { return 4; }\n"
        "            if (math.abs(r90.w - math.cos(math.deg2rad(45.0))) > eps) { return 5; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, QuaternionSlerp) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
        "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
        "            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));\n"
        "            // slerp t=0 -> identity\n"
        "            math.Quaternion s0 = id.slerp(r90, 0.0);\n"
        "            if (math.abs(s0.w - 1.0) > eps) { return 1; }\n"
        "            // slerp t=1 -> r90\n"
        "            math.Quaternion s1 = id.slerp(r90, 1.0);\n"
        "            if (math.abs(s1.y - r90.y) > eps) { return 2; }\n"
        "            // slerp t=0.5 -> 45 degrees\n"
        "            math.Quaternion s5 = id.slerp(r90, 0.5);\n"
        "            if (math.abs(s5.length() - 1.0) > eps) { return 3; }\n"
        "            // half of 90 = 45 deg -> y = sin(22.5 deg)\n"
        "            if (math.abs(s5.y - math.sin(math.deg2rad(22.5))) > eps) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, QuaternionToEuler) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.01;\n"
        "            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
        "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
        "            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));\n"
        "            math.Quaternion euler = r90.toEuler();\n"
        "            // yaw (euler.y) should be ~90 degrees\n"
        "            if (math.abs(math.rad2deg(euler.y) - 90.0) > eps) { return 1; }\n"
        "            // pitch and roll should be ~0\n"
        "            if (math.abs(euler.x) > eps) { return 2; }\n"
        "            if (math.abs(euler.z) > eps) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, QuaternionDotAndNormalize) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Quaternion q = math.Quaternion(1.0, 2.0, 3.0, 4.0);\n"
        "            // dot with self = lengthSqr\n"
        "            if (math.abs(q.dot(q) - 30.0) > eps) { return 1; }\n"
        "            // normalize\n"
        "            math.Quaternion n = q.normalize();\n"
        "            if (math.abs(n.length() - 1.0) > eps) { return 2; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Mat4 ────────────────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Mat4ConstructionAndFieldAccess) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            array<f64> d = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0];\n"
        "            math.Mat4 m = math.Mat4(d);\n"
        "            // data field is array<f64>\n"
        "            array<f64> arr = m.data;\n"
        "            if (arr.len() != 16) { return 1; }\n"
        "            if (arr[0] != 1.0) { return 2; }\n"
        "            if (arr[5] != 1.0) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Mat4Identity) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Mat4 id = math.Mat4.identity();\n"
        "            if (id.get(0, 0) != 1.0) { return 1; }\n"
        "            if (id.get(1, 1) != 1.0) { return 2; }\n"
        "            if (id.get(2, 2) != 1.0) { return 3; }\n"
        "            if (id.get(3, 3) != 1.0) { return 4; }\n"
        "            if (id.get(0, 1) != 0.0) { return 5; }\n"
        "            if (id.get(1, 0) != 0.0) { return 6; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Mat4GetSetTranspose) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Mat4 id = math.Mat4.identity();\n"
        "            // set(0,1) = 5 -> get(0,1) = 5\n"
        "            math.Mat4 m = id.set(0, 1, 5.0);\n"
        "            if (m.get(0, 1) != 5.0) { return 1; }\n"
        "            if (m.get(1, 0) != 0.0) { return 2; }\n"
        "            // transpose swaps (0,1) and (1,0)\n"
        "            math.Mat4 t = m.transpose();\n"
        "            if (t.get(1, 0) != 5.0) { return 3; }\n"
        "            if (t.get(0, 1) != 0.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Mat4MultiplyAndDeterminant) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Mat4 id = math.Mat4.identity();\n"
        "            // id * id = id\n"
        "            math.Mat4 r = id.multiply(id);\n"
        "            if (r.get(0, 0) != 1.0) { return 1; }\n"
        "            if (r.get(0, 1) != 0.0) { return 2; }\n"
        "            // det(id) = 1\n"
        "            if (math.abs(id.determinant() - 1.0) > eps) { return 3; }\n"
        "            // scale by 2 -> det = 2^4 = 16\n"
        "            math.Mat4 s = id.scale(2.0);\n"
        "            if (math.abs(s.determinant() - 16.0) > eps) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Mat4AddAndScale) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Mat4 id = math.Mat4.identity();\n"
        "            math.Mat4 sum = id.add(id);\n"
        "            if (sum.get(0, 0) != 2.0) { return 1; }\n"
        "            if (sum.get(0, 1) != 0.0) { return 2; }\n"
        "            math.Mat4 s = id.scale(3.0);\n"
        "            if (s.get(0, 0) != 3.0) { return 3; }\n"
        "            if (s.get(1, 1) != 3.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Static methods ──────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec2StaticZeroAndOne) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec2 z = math.Vec2.zero();\n"
        "            if (z.x != 0.0) { return 1; }\n"
        "            if (z.y != 0.0) { return 2; }\n"
        "            math.Vec2 o = math.Vec2.one();\n"
        "            if (o.x != 1.0) { return 3; }\n"
        "            if (o.y != 1.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec3StaticZeroAndOne) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec3 z = math.Vec3.zero();\n"
        "            if (z.x != 0.0) { return 1; }\n"
        "            if (z.y != 0.0) { return 2; }\n"
        "            if (z.z != 0.0) { return 3; }\n"
        "            math.Vec3 o = math.Vec3.one();\n"
        "            if (o.x != 1.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec4StaticZeroAndOne) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Vec4 z = math.Vec4.zero();\n"
        "            if (z.x != 0.0) { return 1; }\n"
        "            if (z.w != 0.0) { return 2; }\n"
        "            math.Vec4 o = math.Vec4.one();\n"
        "            if (o.x != 1.0) { return 3; }\n"
        "            if (o.w != 1.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Mat4StaticIdentity) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            math.Mat4 id = math.Mat4.identity();\n"
        "            if (id.get(0, 0) != 1.0) { return 1; }\n"
        "            if (id.get(1, 1) != 1.0) { return 2; }\n"
        "            if (id.get(0, 1) != 0.0) { return 3; }\n"
        "            if (id.determinant() != 1.0) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, QuaternionStaticFromAxisAngle) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
        "            math.Quaternion q = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));\n"
        "            if (math.abs(q.length() - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(q.y - math.sin(math.deg2rad(45.0))) > eps) { return 2; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, StaticMethodChaining) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            // Static factory -> instance method chain\n"
        "            math.Vec3 v = math.Vec3.one().scale(3.0);\n"
        "            if (v.x != 3.0) { return 1; }\n"
        "            if (v.y != 3.0) { return 2; }\n"
        "            math.Mat4 m = math.Mat4.identity().scale(2.0);\n"
        "            if (m.get(0, 0) != 2.0) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Vec2 angle/rotate ───────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec2AngleAndRotate) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec2 v = math.Vec2(1.0, 0.0);\n"
        "            if (math.abs(v.angle()) > eps) { return 1; }\n"
        "            math.Vec2 up = math.Vec2(0.0, 1.0);\n"
        "            if (math.abs(up.angle() - math.pi() / 2.0) > eps) { return 2; }\n"
        "            // rotate (1,0) by 90 degrees -> (0,1)\n"
        "            math.Vec2 r = v.rotate(math.pi() / 2.0);\n"
        "            if (math.abs(r.x) > eps) { return 3; }\n"
        "            if (math.abs(r.y - 1.0) > eps) { return 4; }\n"
        "            // rotate by 180 -> (-1, 0)\n"
        "            math.Vec2 r2 = v.rotate(math.pi());\n"
        "            if (math.abs(r2.x + 1.0) > eps) { return 5; }\n"
        "            if (math.abs(r2.y) > eps) { return 6; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Vec3 transform/rotateByQuaternion/unproject ─────────────────── */

TEST(BaslStdlibMathTest, Vec3Transform) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec3 p = math.Vec3(1.0, 2.0, 3.0);\n"
        "            // Identity transform\n"
        "            math.Mat4 id = math.Mat4.identity();\n"
        "            math.Vec3 r = p.transform(id);\n"
        "            if (math.abs(r.x - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(r.y - 2.0) > eps) { return 2; }\n"
        "            if (math.abs(r.z - 3.0) > eps) { return 3; }\n"
        "            // Translation\n"
        "            math.Mat4 t = id.translate(math.Vec3(10.0, 20.0, 30.0));\n"
        "            math.Vec3 r2 = p.transform(t);\n"
        "            if (math.abs(r2.x - 11.0) > eps) { return 4; }\n"
        "            if (math.abs(r2.y - 22.0) > eps) { return 5; }\n"
        "            if (math.abs(r2.z - 33.0) > eps) { return 6; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec3RotateByQuaternion) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec3 fwd = math.Vec3(0.0, 0.0, 1.0);\n"
        "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
        "            // 90 deg around Y: (0,0,1) -> (1,0,0)\n"
        "            math.Quaternion q = math.Quaternion.fromAxisAngle(yaxis, math.pi() / 2.0);\n"
        "            math.Vec3 r = fwd.rotateByQuaternion(q);\n"
        "            if (math.abs(r.x - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(r.y) > eps) { return 2; }\n"
        "            if (math.abs(r.z) > eps) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Vec3Unproject) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.01;\n"
        "            math.Mat4 proj = math.Mat4.perspective(math.deg2rad(90.0), 1.0, 0.1, 100.0);\n"
        "            math.Mat4 view = math.Mat4.identity();\n"
        "            // Center of screen (0.5, 0.5) at near plane (z=0)\n"
        "            math.Vec3 near = math.Vec3(0.5, 0.5, 0.0).unproject(proj, view);\n"
        "            // Should be near origin on near plane\n"
        "            if (math.abs(near.x) > eps) { return 1; }\n"
        "            if (math.abs(near.y) > eps) { return 2; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Quaternion fromEuler/toMat4 ─────────────────────────────────── */

TEST(BaslStdlibMathTest, QuaternionFromEuler) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            // Zero euler -> identity quaternion\n"
        "            math.Quaternion q0 = math.Quaternion.fromEuler(0.0, 0.0, 0.0);\n"
        "            if (math.abs(q0.w - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(q0.x) > eps) { return 2; }\n"
        "            // 90 deg pitch -> unit quaternion\n"
        "            math.Quaternion qp = math.Quaternion.fromEuler(math.pi() / 2.0, 0.0, 0.0);\n"
        "            if (math.abs(qp.length() - 1.0) > eps) { return 3; }\n"
        "            // fromEuler produces unit quaternions\n"
        "            math.Quaternion q = math.Quaternion.fromEuler(0.1, 0.2, 0.3);\n"
        "            if (math.abs(q.length() - 1.0) > eps) { return 4; }\n"
        "            // 180 deg yaw: should be (0, sin(90), 0, cos(90)) = (0, 1, 0, 0)\n"
        "            math.Quaternion qy = math.Quaternion.fromEuler(0.0, math.pi(), 0.0);\n"
        "            if (math.abs(math.abs(qy.y) - 1.0) > eps) { return 5; }\n"
        "            if (math.abs(qy.x) > eps) { return 6; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, QuaternionToMat4) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            // Identity quaternion -> identity matrix\n"
        "            math.Quaternion qi = math.Quaternion(0.0, 0.0, 0.0, 1.0);\n"
        "            math.Mat4 m = qi.toMat4();\n"
        "            if (math.abs(m.get(0, 0) - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(m.get(1, 1) - 1.0) > eps) { return 2; }\n"
        "            if (math.abs(m.get(2, 2) - 1.0) > eps) { return 3; }\n"
        "            if (math.abs(m.get(3, 3) - 1.0) > eps) { return 4; }\n"
        "            if (math.abs(m.get(0, 1)) > eps) { return 5; }\n"
        "            // 90 deg around Y: should rotate (0,0,1) to (1,0,0)\n"
        "            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);\n"
        "            math.Quaternion q90 = math.Quaternion.fromAxisAngle(yaxis, math.pi() / 2.0);\n"
        "            math.Mat4 rm = q90.toMat4();\n"
        "            math.Vec3 fwd = math.Vec3(0.0, 0.0, 1.0);\n"
        "            math.Vec3 r = fwd.transform(rm);\n"
        "            if (math.abs(r.x - 1.0) > eps) { return 6; }\n"
        "            if (math.abs(r.y) > eps) { return 7; }\n"
        "            if (math.abs(r.z) > eps) { return 8; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── Mat4 new methods ────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Mat4Trace) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            if (math.abs(math.Mat4.identity().trace() - 4.0) > eps) { return 1; }\n"
        "            math.Mat4 s = math.Mat4.identity().scale(2.0);\n"
        "            if (math.abs(s.trace() - 8.0) > eps) { return 2; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Mat4Invert) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            // Invert identity = identity\n"
        "            math.Mat4 id = math.Mat4.identity();\n"
        "            math.Mat4 inv = id.invert();\n"
        "            if (math.abs(inv.get(0, 0) - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(inv.get(0, 1)) > eps) { return 2; }\n"
        "            // Invert scale(2) = scale(0.5)\n"
        "            math.Mat4 s = id.scale(2.0);\n"
        "            math.Mat4 si = s.invert();\n"
        "            if (math.abs(si.get(0, 0) - 0.5) > eps) { return 3; }\n"
        "            // M * M^-1 = I\n"
        "            math.Mat4 prod = s.multiply(si);\n"
        "            if (math.abs(prod.get(0, 0) - 1.0) > eps) { return 4; }\n"
        "            if (math.abs(prod.get(0, 1)) > eps) { return 5; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Mat4TranslateAndScaleV) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Mat4 id = math.Mat4.identity();\n"
        "            // Translate\n"
        "            math.Mat4 t = id.translate(math.Vec3(10.0, 20.0, 30.0));\n"
        "            if (math.abs(t.get(0, 3) - 10.0) > eps) { return 1; }\n"
        "            if (math.abs(t.get(1, 3) - 20.0) > eps) { return 2; }\n"
        "            if (math.abs(t.get(2, 3) - 30.0) > eps) { return 3; }\n"
        "            // ScaleV\n"
        "            math.Mat4 s = id.scaleV(math.Vec3(2.0, 3.0, 4.0));\n"
        "            if (math.abs(s.get(0, 0) - 2.0) > eps) { return 4; }\n"
        "            if (math.abs(s.get(1, 1) - 3.0) > eps) { return 5; }\n"
        "            if (math.abs(s.get(2, 2) - 4.0) > eps) { return 6; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Mat4RotateXYZ) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Mat4 id = math.Mat4.identity();\n"
        "            // RotateX 90: (0,1,0) -> (0,0,1)\n"
        "            math.Mat4 rx = id.rotateX(math.pi() / 2.0);\n"
        "            math.Vec3 up = math.Vec3(0.0, 1.0, 0.0);\n"
        "            math.Vec3 r1 = up.transform(rx);\n"
        "            if (math.abs(r1.y) > eps) { return 1; }\n"
        "            if (math.abs(r1.z - 1.0) > eps) { return 2; }\n"
        "            // RotateY 90: (0,0,1) -> (1,0,0)\n"
        "            math.Mat4 ry = id.rotateY(math.pi() / 2.0);\n"
        "            math.Vec3 fwd = math.Vec3(0.0, 0.0, 1.0);\n"
        "            math.Vec3 r2 = fwd.transform(ry);\n"
        "            if (math.abs(r2.x - 1.0) > eps) { return 3; }\n"
        "            if (math.abs(r2.z) > eps) { return 4; }\n"
        "            // RotateZ 90: (1,0,0) -> (0,1,0)\n"
        "            math.Mat4 rz = id.rotateZ(math.pi() / 2.0);\n"
        "            math.Vec3 right = math.Vec3(1.0, 0.0, 0.0);\n"
        "            math.Vec3 r3 = right.transform(rz);\n"
        "            if (math.abs(r3.x) > eps) { return 5; }\n"
        "            if (math.abs(r3.y - 1.0) > eps) { return 6; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Mat4LookAt) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Vec3 eye = math.Vec3(0.0, 0.0, 5.0);\n"
        "            math.Vec3 target = math.Vec3.zero();\n"
        "            math.Vec3 up = math.Vec3(0.0, 1.0, 0.0);\n"
        "            math.Mat4 view = math.Mat4.lookAt(eye, target, up);\n"
        "            // Eye at (0,0,5) looking at origin: the view matrix should\n"
        "            // translate the eye to origin in view space\n"
        "            math.Vec3 eyeView = eye.transform(view);\n"
        "            if (math.abs(eyeView.x) > eps) { return 1; }\n"
        "            if (math.abs(eyeView.y) > eps) { return 2; }\n"
        "            // Target should be at (0,0,-5) in view space\n"
        "            math.Vec3 targetView = target.transform(view);\n"
        "            if (math.abs(targetView.x) > eps) { return 3; }\n"
        "            if (math.abs(targetView.y) > eps) { return 4; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Mat4PerspectiveAndOrtho) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            // Perspective: 90 deg FOV, aspect 1, near 0.1, far 100\n"
        "            math.Mat4 p = math.Mat4.perspective(math.deg2rad(90.0), 1.0, 0.1, 100.0);\n"
        "            // With 90 deg FOV and aspect 1: m[0][0] = m[1][1] = 1.0\n"
        "            if (math.abs(p.get(0, 0) - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(p.get(1, 1) - 1.0) > eps) { return 2; }\n"
        "            if (p.get(3, 3) != 0.0) { return 3; }\n"
        "            // Ortho: symmetric [-1,1] box\n"
        "            math.Mat4 o = math.Mat4.ortho(-1.0, 1.0, -1.0, 1.0, 0.1, 100.0);\n"
        "            if (math.abs(o.get(0, 0) - 1.0) > eps) { return 4; }\n"
        "            if (math.abs(o.get(1, 1) - 1.0) > eps) { return 5; }\n"
        "            if (math.abs(o.get(3, 3) - 1.0) > eps) { return 6; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

TEST(BaslStdlibMathTest, Mat4Frustum) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "        import \"math\";\n"
        "        fn main() -> i32 {\n"
        "            f64 eps = 0.000001;\n"
        "            math.Mat4 f = math.Mat4.frustum(-1.0, 1.0, -1.0, 1.0, 1.0, 100.0);\n"
        "            // Symmetric frustum: m[0][0] = 2*near/(right-left) = 2*1/2 = 1\n"
        "            if (math.abs(f.get(0, 0) - 1.0) > eps) { return 1; }\n"
        "            if (math.abs(f.get(1, 1) - 1.0) > eps) { return 2; }\n"
        "            if (f.get(3, 2) != -1.0) { return 3; }\n"
        "            return 0;\n"
        "        }\n"
        "    "), 0);
}

/* ── String trim_left / trim_right ────────────────────────────────── */

TEST(BaslStdlibStringTest, TrimLeftAndTrimRight) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "fn main() -> i32 {\n"
        "    string s = \"  hello  \";\n"
        "    if (s.trim_left() != \"hello  \") { return 1; }\n"
        "    if (s.trim_right() != \"  hello\") { return 2; }\n"
        "    if (\"nowhitespace\".trim_left() != \"nowhitespace\") { return 3; }\n"
        "    if (\"nowhitespace\".trim_right() != \"nowhitespace\") { return 4; }\n"
        "    if (\"   \".trim_left() != \"\") { return 5; }\n"
        "    if (\"   \".trim_right() != \"\") { return 6; }\n"
        "    return 0;\n"
        "}\n"
        "    "), 0);
}

/* ── String reverse ──────────────────────────────────────────────── */

TEST(BaslStdlibStringTest, Reverse) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "fn main() -> i32 {\n"
        "    if (\"hello\".reverse() != \"olleh\") { return 1; }\n"
        "    if (\"\".reverse() != \"\") { return 2; }\n"
        "    if (\"a\".reverse() != \"a\") { return 3; }\n"
        "    if (\"abcd\".reverse() != \"dcba\") { return 4; }\n"
        "    return 0;\n"
        "}\n"
        "    "), 0);
}

/* ── String is_empty ─────────────────────────────────────────────── */

TEST(BaslStdlibStringTest, IsEmpty) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "fn main() -> i32 {\n"
        "    if (\"\".is_empty() != true) { return 1; }\n"
        "    if (\"x\".is_empty() != false) { return 2; }\n"
        "    if (\" \".is_empty() != false) { return 3; }\n"
        "    return 0;\n"
        "}\n"
        "    "), 0);
}

/* ── String repeat ───────────────────────────────────────────────── */

TEST(BaslStdlibStringTest, Repeat) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "fn main() -> i32 {\n"
        "    if (\"abc\".repeat(i32(3)) != \"abcabcabc\") { return 1; }\n"
        "    if (\"x\".repeat(i32(0)) != \"\") { return 2; }\n"
        "    if (\"hi\".repeat(i32(1)) != \"hi\") { return 3; }\n"
        "    if (\"\".repeat(i32(5)) != \"\") { return 4; }\n"
        "    return 0;\n"
        "}\n"
        "    "), 0);
}

/* ── String count ────────────────────────────────────────────────── */

TEST(BaslStdlibStringTest, Count) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "fn main() -> i32 {\n"
        "    if (\"abcabcabc\".count(\"abc\") != i32(3)) { return 1; }\n"
        "    if (\"hello\".count(\"x\") != i32(0)) { return 2; }\n"
        "    if (\"aaa\".count(\"a\") != i32(3)) { return 3; }\n"
        "    if (\"aaaa\".count(\"aa\") != i32(2)) { return 4; }\n"
        "    return 0;\n"
        "}\n"
        "    "), 0);
}

/* ── String last_index_of ────────────────────────────────────────── */

TEST(BaslStdlibStringTest, LastIndexOf) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "fn main() -> i32 {\n"
        "    i32 idx, bool found = \"hello world\".last_index_of(\"o\");\n"
        "    if (idx != i32(7)) { return 1; }\n"
        "    if (found != true) { return 2; }\n"
        "    i32 idx2, bool found2 = \"hello\".last_index_of(\"xyz\");\n"
        "    if (found2 != false) { return 3; }\n"
        "    i32 idx3, bool found3 = \"abcabc\".last_index_of(\"abc\");\n"
        "    if (idx3 != i32(3)) { return 4; }\n"
        "    if (found3 != true) { return 5; }\n"
        "    return 0;\n"
        "}\n"
        "    "), 0);
}

/* ── String trim_prefix / trim_suffix ────────────────────────────── */

TEST(BaslStdlibStringTest, TrimPrefixAndTrimSuffix) {
    EXPECT_EQ(RunWithStdlib(basl_test_failed_, "\n"
        "fn main() -> i32 {\n"
        "    if (\"hello world\".trim_prefix(\"hello \") != \"world\") { return 1; }\n"
        "    if (\"hello world\".trim_suffix(\" world\") != \"hello\") { return 2; }\n"
        "    if (\"hello\".trim_prefix(\"xyz\") != \"hello\") { return 3; }\n"
        "    if (\"hello\".trim_suffix(\"xyz\") != \"hello\") { return 4; }\n"
        "    if (\"hello\".trim_prefix(\"hello\") != \"\") { return 5; }\n"
        "    if (\"hello\".trim_suffix(\"hello\") != \"\") { return 6; }\n"
        "    return 0;\n"
        "}\n"
        "    "), 0);
}

void register_stdlib_tests(void) {
    REGISTER_TEST(BaslStdlibFmtTest, PrintlnOutputsStringWithNewline);
    REGISTER_TEST(BaslStdlibFmtTest, PrintOutputsStringWithoutNewline);
    REGISTER_TEST(BaslStdlibFmtTest, EprintlnOutputsToStderr);
    REGISTER_TEST(BaslStdlibFmtTest, PrintlnEmptyString);
    REGISTER_TEST(BaslStdlibFmtTest, PrintlnWithFString);
    REGISTER_TEST(BaslStdlibFmtTest, PrintlnWithVariable);
    REGISTER_TEST(BaslStdlibFmtTest, PrintlnInLoop);
    REGISTER_TEST(BaslStdlibMathTest, PiReturnsCorrectValue);
    REGISTER_TEST(BaslStdlibMathTest, EReturnsCorrectValue);
    REGISTER_TEST(BaslStdlibMathTest, FloorTable);
    REGISTER_TEST(BaslStdlibMathTest, CeilTable);
    REGISTER_TEST(BaslStdlibMathTest, RoundTable);
    REGISTER_TEST(BaslStdlibMathTest, TruncTable);
    REGISTER_TEST(BaslStdlibMathTest, AbsTable);
    REGISTER_TEST(BaslStdlibMathTest, SignTable);
    REGISTER_TEST(BaslStdlibMathTest, SqrtTable);
    REGISTER_TEST(BaslStdlibMathTest, SinCosAtZero);
    REGISTER_TEST(BaslStdlibMathTest, LogTable);
    REGISTER_TEST(BaslStdlibMathTest, ExpAtZeroAndOne);
    REGISTER_TEST(BaslStdlibMathTest, PowTable);
    REGISTER_TEST(BaslStdlibMathTest, MinMaxTable);
    REGISTER_TEST(BaslStdlibMathTest, Atan2Table);
    REGISTER_TEST(BaslStdlibMathTest, HypotTable);
    REGISTER_TEST(BaslStdlibMathTest, FmodTable);
    REGISTER_TEST(BaslStdlibMathTest, ClampTable);
    REGISTER_TEST(BaslStdlibMathTest, ComposedExpressions);
    REGISTER_TEST(BaslStdlibMathTest, Vec2ConstructionAndFields);
    REGISTER_TEST(BaslStdlibMathTest, Vec2FieldMutation);
    REGISTER_TEST(BaslStdlibMathTest, Vec2Length);
    REGISTER_TEST(BaslStdlibMathTest, Vec2Dot);
    REGISTER_TEST(BaslStdlibMathTest, Vec2WithScalarMath);
    REGISTER_TEST(BaslStdlibMathTest, InverseTrig);
    REGISTER_TEST(BaslStdlibMathTest, Deg2RadRad2Deg);
    REGISTER_TEST(BaslStdlibMathTest, Lerp);
    REGISTER_TEST(BaslStdlibMathTest, Normalize);
    REGISTER_TEST(BaslStdlibMathTest, Wrap);
    REGISTER_TEST(BaslStdlibMathTest, Remap);
    REGISTER_TEST(BaslStdlibMathTest, Vec2Normalize);
    REGISTER_TEST(BaslStdlibMathTest, Vec2AddSubScaleDistance);
    REGISTER_TEST(BaslStdlibMathTest, Vec3ConstructionAndFields);
    REGISTER_TEST(BaslStdlibMathTest, Vec3Length);
    REGISTER_TEST(BaslStdlibMathTest, Vec3DotAndCross);
    REGISTER_TEST(BaslStdlibMathTest, Vec3NormalizeAddSubScaleDistance);
    REGISTER_TEST(BaslStdlibMathTest, Vec3MethodChaining);
    REGISTER_TEST(BaslStdlibMathTest, Step);
    REGISTER_TEST(BaslStdlibMathTest, Smoothstep);
    REGISTER_TEST(BaslStdlibMathTest, InverseLerp);
    REGISTER_TEST(BaslStdlibMathTest, Vec2LengthSqrAndNegate);
    REGISTER_TEST(BaslStdlibMathTest, Vec2LerpAndReflect);
    REGISTER_TEST(BaslStdlibMathTest, Vec3LengthSqrAndNegate);
    REGISTER_TEST(BaslStdlibMathTest, Vec3LerpAndReflect);
    REGISTER_TEST(BaslStdlibMathTest, Vec3Angle);
    REGISTER_TEST(BaslStdlibMathTest, Vec4ConstructionAndFields);
    REGISTER_TEST(BaslStdlibMathTest, Vec4LengthAndDot);
    REGISTER_TEST(BaslStdlibMathTest, Vec4Arithmetic);
    REGISTER_TEST(BaslStdlibMathTest, QuaternionIdentity);
    REGISTER_TEST(BaslStdlibMathTest, QuaternionConjugateAndInverse);
    REGISTER_TEST(BaslStdlibMathTest, QuaternionMultiply);
    REGISTER_TEST(BaslStdlibMathTest, QuaternionFromAxisAngle);
    REGISTER_TEST(BaslStdlibMathTest, QuaternionSlerp);
    REGISTER_TEST(BaslStdlibMathTest, QuaternionToEuler);
    REGISTER_TEST(BaslStdlibMathTest, QuaternionDotAndNormalize);
    REGISTER_TEST(BaslStdlibMathTest, Mat4ConstructionAndFieldAccess);
    REGISTER_TEST(BaslStdlibMathTest, Mat4Identity);
    REGISTER_TEST(BaslStdlibMathTest, Mat4GetSetTranspose);
    REGISTER_TEST(BaslStdlibMathTest, Mat4MultiplyAndDeterminant);
    REGISTER_TEST(BaslStdlibMathTest, Mat4AddAndScale);
    REGISTER_TEST(BaslStdlibMathTest, Vec2StaticZeroAndOne);
    REGISTER_TEST(BaslStdlibMathTest, Vec3StaticZeroAndOne);
    REGISTER_TEST(BaslStdlibMathTest, Vec4StaticZeroAndOne);
    REGISTER_TEST(BaslStdlibMathTest, Mat4StaticIdentity);
    REGISTER_TEST(BaslStdlibMathTest, QuaternionStaticFromAxisAngle);
    REGISTER_TEST(BaslStdlibMathTest, StaticMethodChaining);
    REGISTER_TEST(BaslStdlibMathTest, Vec2AngleAndRotate);
    REGISTER_TEST(BaslStdlibMathTest, Vec3Transform);
    REGISTER_TEST(BaslStdlibMathTest, Vec3RotateByQuaternion);
    REGISTER_TEST(BaslStdlibMathTest, Vec3Unproject);
    REGISTER_TEST(BaslStdlibMathTest, QuaternionFromEuler);
    REGISTER_TEST(BaslStdlibMathTest, QuaternionToMat4);
    REGISTER_TEST(BaslStdlibMathTest, Mat4Trace);
    REGISTER_TEST(BaslStdlibMathTest, Mat4Invert);
    REGISTER_TEST(BaslStdlibMathTest, Mat4TranslateAndScaleV);
    REGISTER_TEST(BaslStdlibMathTest, Mat4RotateXYZ);
    REGISTER_TEST(BaslStdlibMathTest, Mat4LookAt);
    REGISTER_TEST(BaslStdlibMathTest, Mat4PerspectiveAndOrtho);
    REGISTER_TEST(BaslStdlibMathTest, Mat4Frustum);
    REGISTER_TEST(BaslStdlibStringTest, TrimLeftAndTrimRight);
    REGISTER_TEST(BaslStdlibStringTest, Reverse);
    REGISTER_TEST(BaslStdlibStringTest, IsEmpty);
    REGISTER_TEST(BaslStdlibStringTest, Repeat);
    REGISTER_TEST(BaslStdlibStringTest, Count);
    REGISTER_TEST(BaslStdlibStringTest, LastIndexOf);
    REGISTER_TEST(BaslStdlibStringTest, TrimPrefixAndTrimSuffix);
}
