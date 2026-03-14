#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

extern "C" {
#include "basl/basl.h"
#include "basl/stdlib.h"
}

namespace {

/* ── test harness ────────────────────────────────────────────────── */

/*
 * Compile and run a BASL program that imports stdlib modules.
 * The program's main() must return i32.  Returns that value.
 */
int64_t RunWithStdlib(const char *source_text) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_native_registry_t natives;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_value_t result;
    basl_source_id_t source_id = 0U;
    int64_t output = 0;

    EXPECT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
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
    EXPECT_NE(function, nullptr);
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
std::string RunAndCaptureStdout(const char *source_text) {
    testing::internal::CaptureStdout();
    int64_t rc = RunWithStdlib(source_text);
    std::string captured = testing::internal::GetCapturedStdout();
    EXPECT_EQ(rc, 0);
    return captured;
}

std::string RunAndCaptureStderr(const char *source_text) {
    testing::internal::CaptureStderr();
    int64_t rc = RunWithStdlib(source_text);
    std::string captured = testing::internal::GetCapturedStderr();
    EXPECT_EQ(rc, 0);
    return captured;
}

/* ── fmt tests ───────────────────────────────────────────────────── */

TEST(BaslStdlibFmtTest, PrintlnOutputsStringWithNewline) {
    std::string out = RunAndCaptureStdout(R"(
        import "fmt";
        fn main() -> i32 {
            fmt.println("hello");
            return 0;
        }
    )");
    EXPECT_EQ(out, "hello\n");
}

TEST(BaslStdlibFmtTest, PrintOutputsStringWithoutNewline) {
    std::string out = RunAndCaptureStdout(R"(
        import "fmt";
        fn main() -> i32 {
            fmt.print("ab");
            fmt.print("cd");
            return 0;
        }
    )");
    EXPECT_EQ(out, "abcd");
}

TEST(BaslStdlibFmtTest, EprintlnOutputsToStderr) {
    std::string err = RunAndCaptureStderr(R"(
        import "fmt";
        fn main() -> i32 {
            fmt.eprintln("oops");
            return 0;
        }
    )");
    EXPECT_EQ(err, "oops\n");
}

TEST(BaslStdlibFmtTest, PrintlnEmptyString) {
    std::string out = RunAndCaptureStdout(R"(
        import "fmt";
        fn main() -> i32 {
            fmt.println("");
            return 0;
        }
    )");
    EXPECT_EQ(out, "\n");
}

TEST(BaslStdlibFmtTest, PrintlnWithFString) {
    std::string out = RunAndCaptureStdout(R"(
        import "fmt";
        fn main() -> i32 {
            i32 x = 42;
            fmt.println(f"val={x}");
            return 0;
        }
    )");
    EXPECT_EQ(out, "val=42\n");
}

TEST(BaslStdlibFmtTest, PrintlnWithVariable) {
    std::string out = RunAndCaptureStdout(R"(
        import "fmt";
        fn main() -> i32 {
            string s = "world";
            fmt.println(s);
            return 0;
        }
    )");
    EXPECT_EQ(out, "world\n");
}

TEST(BaslStdlibFmtTest, PrintlnInLoop) {
    std::string out = RunAndCaptureStdout(R"(
        import "fmt";
        fn main() -> i32 {
            for (i32 i = 0; i < 3; i++) {
                fmt.println(string(i));
            }
            return 0;
        }
    )");
    EXPECT_EQ(out, "0\n1\n2\n");
}

/* ── math: constants ─────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, PiReturnsCorrectValue) {
    /*
     * Encode a pass/fail check as an integer return.
     * The BASL program returns 0 if pi matches within tolerance.
     */
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 diff = math.abs(math.pi() - 3.14159265358979323846);
            if (diff > 0.000000000000001) { return 1; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, EReturnsCorrectValue) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 diff = math.abs(math.e() - 2.71828182845904523536);
            if (diff > 0.000000000000001) { return 1; }
            return 0;
        }
    )"), 0);
}

/* ── math: rounding / conversion table tests ─────────────────────── */

/*
 * Each rounding function is tested with a table of inputs including
 * positive, negative, zero, and edge cases.  The BASL program
 * encodes the index of the first failing case (1-based) or 0 on
 * success.
 */

TEST(BaslStdlibMathTest, FloorTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.floor(3.7) != 3.0)   { return 1; }
            if (math.floor(-3.7) != -4.0)  { return 2; }
            if (math.floor(0.0) != 0.0)    { return 3; }
            if (math.floor(5.0) != 5.0)    { return 4; }
            if (math.floor(-0.1) != -1.0)  { return 5; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, CeilTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.ceil(3.2) != 4.0)    { return 1; }
            if (math.ceil(-3.2) != -3.0)   { return 2; }
            if (math.ceil(0.0) != 0.0)     { return 3; }
            if (math.ceil(5.0) != 5.0)     { return 4; }
            if (math.ceil(0.1) != 1.0)     { return 5; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, RoundTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.round(3.5) != 4.0)    { return 1; }
            if (math.round(3.4) != 3.0)    { return 2; }
            if (math.round(-3.5) != -4.0)  { return 3; }
            if (math.round(0.0) != 0.0)    { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, TruncTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.trunc(3.7) != 3.0)    { return 1; }
            if (math.trunc(-3.7) != -3.0)  { return 2; }
            if (math.trunc(0.0) != 0.0)    { return 3; }
            if (math.trunc(5.0) != 5.0)    { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, AbsTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.abs(-42.0) != 42.0)  { return 1; }
            if (math.abs(42.0) != 42.0)   { return 2; }
            if (math.abs(0.0) != 0.0)     { return 3; }
            if (math.abs(-0.0) != 0.0)    { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, SignTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.sign(42.0) != 1.0)   { return 1; }
            if (math.sign(-42.0) != -1.0)  { return 2; }
            if (math.sign(0.0) != 0.0)    { return 3; }
            return 0;
        }
    )"), 0);
}

/* ── math: trig / exponential ────────────────────────────────────── */

TEST(BaslStdlibMathTest, SqrtTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.sqrt(144.0) != 12.0)  { return 1; }
            if (math.sqrt(0.0) != 0.0)     { return 2; }
            if (math.sqrt(1.0) != 1.0)     { return 3; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, SinCosAtZero) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.sin(0.0) != 0.0)  { return 1; }
            if (math.cos(0.0) != 1.0)  { return 2; }
            if (math.tan(0.0) != 0.0)  { return 3; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, LogTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.log(1.0) != 0.0)      { return 1; }
            if (math.log2(1024.0) != 10.0)  { return 2; }
            if (math.log10(1000.0) != 3.0)  { return 3; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, ExpAtZeroAndOne) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.exp(0.0) != 1.0)  { return 1; }
            f64 diff = math.abs(math.exp(1.0) - math.e());
            if (diff > 0.000000000000001) { return 2; }
            return 0;
        }
    )"), 0);
}

/* ── math: two-argument functions ────────────────────────────────── */

TEST(BaslStdlibMathTest, PowTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.pow(2.0, 10.0) != 1024.0)  { return 1; }
            if (math.pow(3.0, 0.0) != 1.0)      { return 2; }
            if (math.pow(5.0, 1.0) != 5.0)      { return 3; }
            if (math.pow(4.0, 0.5) != 2.0)      { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, MinMaxTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.min(3.0, 7.0) != 3.0)    { return 1; }
            if (math.min(-1.0, 1.0) != -1.0)   { return 2; }
            if (math.min(5.0, 5.0) != 5.0)    { return 3; }
            if (math.max(3.0, 7.0) != 7.0)    { return 4; }
            if (math.max(-1.0, 1.0) != 1.0)    { return 5; }
            if (math.max(5.0, 5.0) != 5.0)    { return 6; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Atan2Table) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.atan2(0.0, 1.0) != 0.0)  { return 1; }
            f64 diff = math.abs(math.atan2(1.0, 1.0) - math.pi() / 4.0);
            if (diff > 0.000000000000001) { return 2; }
            // atan2(1, 0) == pi/2
            f64 diff2 = math.abs(math.atan2(1.0, 0.0) - math.pi() / 2.0);
            if (diff2 > 0.000000000000001) { return 3; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, HypotTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.hypot(3.0, 4.0) != 5.0)    { return 1; }
            if (math.hypot(0.0, 0.0) != 0.0)    { return 2; }
            if (math.hypot(5.0, 0.0) != 5.0)    { return 3; }
            if (math.hypot(0.0, 7.0) != 7.0)    { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, FmodTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.fmod(7.5, 3.0) != 1.5)    { return 1; }
            if (math.fmod(10.0, 5.0) != 0.0)   { return 2; }
            if (math.fmod(-7.5, 3.0) != -1.5)  { return 3; }
            return 0;
        }
    )"), 0);
}

/* ── math: three-argument functions ──────────────────────────────── */

TEST(BaslStdlibMathTest, ClampTable) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.clamp(5.0, 0.0, 10.0) != 5.0)    { return 1; }
            if (math.clamp(-5.0, 0.0, 10.0) != 0.0)   { return 2; }
            if (math.clamp(15.0, 0.0, 10.0) != 10.0)   { return 3; }
            if (math.clamp(0.0, 0.0, 10.0) != 0.0)    { return 4; }
            if (math.clamp(10.0, 0.0, 10.0) != 10.0)   { return 5; }
            return 0;
        }
    )"), 0);
}

/* ── math: composition ───────────────────────────────────────────── */

TEST(BaslStdlibMathTest, ComposedExpressions) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            // pythagorean theorem via composition
            f64 hyp = math.sqrt(math.pow(3.0, 2.0) + math.pow(4.0, 2.0));
            if (hyp != 5.0) { return 1; }

            // clamp via min/max matches clamp()
            f64 val = 15.0;
            f64 a = math.min(math.max(val, 0.0), 10.0);
            f64 b = math.clamp(val, 0.0, 10.0);
            if (a != b) { return 2; }

            return 0;
        }
    )"), 0);
}

/* ── native class: Vec2 ──────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec2ConstructionAndFields) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec2 v = math.Vec2(3.0, 4.0);
            if (v.x != 3.0) { return 1; }
            if (v.y != 4.0) { return 2; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec2FieldMutation) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec2 v = math.Vec2(1.0, 2.0);
            v.x = 10.0;
            v.y = 20.0;
            if (v.x != 10.0) { return 1; }
            if (v.y != 20.0) { return 2; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec2Length) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec2 v = math.Vec2(3.0, 4.0);
            if (v.length() != 5.0) { return 1; }
            math.Vec2 zero = math.Vec2(0.0, 0.0);
            if (zero.length() != 0.0) { return 2; }
            math.Vec2 unit = math.Vec2(1.0, 0.0);
            if (unit.length() != 1.0) { return 3; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec2Dot) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec2 a = math.Vec2(3.0, 4.0);
            math.Vec2 b = math.Vec2(1.0, 0.0);
            if (a.dot(b) != 3.0) { return 1; }
            // perpendicular vectors: dot == 0
            math.Vec2 c = math.Vec2(0.0, 1.0);
            if (b.dot(c) != 0.0) { return 2; }
            // self dot
            if (a.dot(a) != 25.0) { return 3; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec2WithScalarMath) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec2 v = math.Vec2(3.0, 4.0);
            f64 len = v.length();
            if (math.sqrt(math.pow(len, 2.0)) != 5.0) { return 1; }
            return 0;
        }
    )"), 0);
}

/* ── new scalar functions ────────────────────────────────────────── */

TEST(BaslStdlibMathTest, InverseTrig) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            // asin(0) == 0
            if (math.abs(math.asin(0.0)) > eps) { return 1; }
            // acos(1) == 0
            if (math.abs(math.acos(1.0)) > eps) { return 2; }
            // atan(0) == 0
            if (math.abs(math.atan(0.0)) > eps) { return 3; }
            // asin(1) ~= pi/2
            if (math.abs(math.asin(1.0) - math.pi() / 2.0) > eps) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Deg2RadRad2Deg) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            if (math.abs(math.deg2rad(180.0) - math.pi()) > eps) { return 1; }
            if (math.abs(math.rad2deg(math.pi()) - 180.0) > eps) { return 2; }
            if (math.abs(math.deg2rad(0.0)) > eps) { return 3; }
            if (math.abs(math.rad2deg(0.0)) > eps) { return 4; }
            // roundtrip
            if (math.abs(math.rad2deg(math.deg2rad(45.0)) - 45.0) > eps) { return 5; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Lerp) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.lerp(0.0, 10.0, 0.0) != 0.0) { return 1; }
            if (math.lerp(0.0, 10.0, 1.0) != 10.0) { return 2; }
            if (math.lerp(0.0, 10.0, 0.5) != 5.0) { return 3; }
            if (math.lerp(10.0, 20.0, 0.25) != 12.5) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Normalize) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.normalize(5.0, 0.0, 10.0) != 0.5) { return 1; }
            if (math.normalize(0.0, 0.0, 10.0) != 0.0) { return 2; }
            if (math.normalize(10.0, 0.0, 10.0) != 1.0) { return 3; }
            // degenerate range
            if (math.normalize(5.0, 5.0, 5.0) != 0.0) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Wrap) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            if (math.abs(math.wrap(370.0, 0.0, 360.0) - 10.0) > eps) { return 1; }
            if (math.abs(math.wrap(-10.0, 0.0, 360.0) - 350.0) > eps) { return 2; }
            if (math.abs(math.wrap(5.0, 0.0, 10.0) - 5.0) > eps) { return 3; }
            if (math.abs(math.wrap(0.0, 0.0, 360.0)) > eps) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Remap) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.remap(5.0, 0.0, 10.0, 0.0, 100.0) != 50.0) { return 1; }
            if (math.remap(0.0, 0.0, 10.0, 0.0, 100.0) != 0.0) { return 2; }
            if (math.remap(10.0, 0.0, 10.0, 0.0, 100.0) != 100.0) { return 3; }
            if (math.remap(5.0, 0.0, 10.0, 100.0, 200.0) != 150.0) { return 4; }
            return 0;
        }
    )"), 0);
}

/* ── Vec2 new methods ────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec2Normalize) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec2 v = math.Vec2(3.0, 4.0);
            math.Vec2 n = v.normalize();
            if (math.abs(n.length() - 1.0) > eps) { return 1; }
            if (math.abs(n.x - 0.6) > eps) { return 2; }
            if (math.abs(n.y - 0.8) > eps) { return 3; }
            // zero vector normalizes to zero
            math.Vec2 z = math.Vec2(0.0, 0.0);
            math.Vec2 zn = z.normalize();
            if (zn.x != 0.0) { return 4; }
            if (zn.y != 0.0) { return 5; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec2AddSubScaleDistance) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec2 a = math.Vec2(1.0, 2.0);
            math.Vec2 b = math.Vec2(3.0, 4.0);
            math.Vec2 c = a.add(b);
            if (c.x != 4.0) { return 1; }
            if (c.y != 6.0) { return 2; }
            math.Vec2 d = a.sub(b);
            if (d.x != -2.0) { return 3; }
            if (d.y != -2.0) { return 4; }
            math.Vec2 s = a.scale(3.0);
            if (s.x != 3.0) { return 5; }
            if (s.y != 6.0) { return 6; }
            f64 dist = a.distance(b);
            if (math.abs(dist - 2.8284271247461903) > eps) { return 7; }
            return 0;
        }
    )"), 0);
}

/* ── Vec3 ────────────────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec3ConstructionAndFields) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec3 v = math.Vec3(1.0, 2.0, 3.0);
            if (v.x != 1.0) { return 1; }
            if (v.y != 2.0) { return 2; }
            if (v.z != 3.0) { return 3; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec3Length) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec3 v = math.Vec3(1.0, 2.0, 2.0);
            if (math.abs(v.length() - 3.0) > eps) { return 1; }
            math.Vec3 z = math.Vec3(0.0, 0.0, 0.0);
            if (z.length() != 0.0) { return 2; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec3DotAndCross) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec3 x = math.Vec3(1.0, 0.0, 0.0);
            math.Vec3 y = math.Vec3(0.0, 1.0, 0.0);
            // perpendicular: dot == 0
            if (x.dot(y) != 0.0) { return 1; }
            // x cross y == z
            math.Vec3 z = x.cross(y);
            if (z.x != 0.0) { return 2; }
            if (z.y != 0.0) { return 3; }
            if (z.z != 1.0) { return 4; }
            // self dot
            math.Vec3 v = math.Vec3(1.0, 2.0, 3.0);
            if (v.dot(v) != 14.0) { return 5; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec3NormalizeAddSubScaleDistance) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec3 a = math.Vec3(1.0, 2.0, 3.0);
            math.Vec3 b = math.Vec3(4.0, 5.0, 6.0);
            // normalize
            math.Vec3 n = a.normalize();
            if (math.abs(n.length() - 1.0) > eps) { return 1; }
            // add
            math.Vec3 c = a.add(b);
            if (c.x != 5.0) { return 2; }
            if (c.y != 7.0) { return 3; }
            if (c.z != 9.0) { return 4; }
            // sub
            math.Vec3 d = a.sub(b);
            if (d.x != -3.0) { return 5; }
            if (d.y != -3.0) { return 6; }
            if (d.z != -3.0) { return 7; }
            // scale
            math.Vec3 s = a.scale(2.0);
            if (s.x != 2.0) { return 8; }
            if (s.y != 4.0) { return 9; }
            if (s.z != 6.0) { return 10; }
            // distance
            f64 dist = a.distance(b);
            if (math.abs(dist - math.sqrt(27.0)) > eps) { return 11; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec3MethodChaining) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            // physics: pos += vel * dt
            math.Vec3 pos = math.Vec3(0.0, 0.0, 0.0);
            math.Vec3 vel = math.Vec3(10.0, 20.0, 30.0);
            math.Vec3 new_pos = pos.add(vel.scale(0.1));
            if (math.abs(new_pos.x - 1.0) > eps) { return 1; }
            if (math.abs(new_pos.y - 2.0) > eps) { return 2; }
            if (math.abs(new_pos.z - 3.0) > eps) { return 3; }
            return 0;
        }
    )"), 0);
}

}  // namespace
