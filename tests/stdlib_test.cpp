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

/* ── Tier 1 scalar: step, smoothstep, inverseLerp ────────────────── */

TEST(BaslStdlibMathTest, Step) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.step(0.5, 0.3) != 0.0) { return 1; }
            if (math.step(0.5, 0.5) != 1.0) { return 2; }
            if (math.step(0.5, 0.7) != 1.0) { return 3; }
            if (math.step(0.0, -1.0) != 0.0) { return 4; }
            if (math.step(0.0, 0.0) != 1.0) { return 5; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Smoothstep) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            if (math.smoothstep(0.0, 1.0, 0.0) != 0.0) { return 1; }
            if (math.smoothstep(0.0, 1.0, 1.0) != 1.0) { return 2; }
            if (math.abs(math.smoothstep(0.0, 1.0, 0.5) - 0.5) > eps) { return 3; }
            // clamped below
            if (math.smoothstep(0.0, 1.0, -1.0) != 0.0) { return 4; }
            // clamped above
            if (math.smoothstep(0.0, 1.0, 2.0) != 1.0) { return 5; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, InverseLerp) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            if (math.inverseLerp(0.0, 10.0, 0.0) != 0.0) { return 1; }
            if (math.inverseLerp(0.0, 10.0, 10.0) != 1.0) { return 2; }
            if (math.inverseLerp(0.0, 10.0, 5.0) != 0.5) { return 3; }
            // degenerate
            if (math.inverseLerp(5.0, 5.0, 5.0) != 0.0) { return 4; }
            return 0;
        }
    )"), 0);
}

/* ── Tier 1 Vec2: lengthSqr, negate, lerp, reflect ──────────────── */

TEST(BaslStdlibMathTest, Vec2LengthSqrAndNegate) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec2 v = math.Vec2(3.0, 4.0);
            if (v.lengthSqr() != 25.0) { return 1; }
            math.Vec2 n = v.negate();
            if (n.x != -3.0) { return 2; }
            if (n.y != -4.0) { return 3; }
            // negate of zero
            math.Vec2 z = math.Vec2(0.0, 0.0);
            math.Vec2 zn = z.negate();
            if (zn.x != 0.0) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec2LerpAndReflect) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec2 a = math.Vec2(0.0, 0.0);
            math.Vec2 b = math.Vec2(10.0, 20.0);
            // lerp t=0 -> a
            math.Vec2 l0 = a.lerp(b, 0.0);
            if (l0.x != 0.0) { return 1; }
            // lerp t=1 -> b
            math.Vec2 l1 = a.lerp(b, 1.0);
            if (l1.x != 10.0) { return 2; }
            // lerp t=0.5 -> midpoint
            math.Vec2 mid = a.lerp(b, 0.5);
            if (mid.x != 5.0) { return 3; }
            if (mid.y != 10.0) { return 4; }
            // reflect (1,-1) off (0,1) -> (1,1)
            math.Vec2 inc = math.Vec2(1.0, -1.0);
            math.Vec2 n = math.Vec2(0.0, 1.0);
            math.Vec2 r = inc.reflect(n);
            if (math.abs(r.x - 1.0) > eps) { return 5; }
            if (math.abs(r.y - 1.0) > eps) { return 6; }
            return 0;
        }
    )"), 0);
}

/* ── Tier 1 Vec3: lengthSqr, negate, lerp, reflect, angle ───────── */

TEST(BaslStdlibMathTest, Vec3LengthSqrAndNegate) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec3 v = math.Vec3(1.0, 2.0, 3.0);
            if (v.lengthSqr() != 14.0) { return 1; }
            math.Vec3 n = v.negate();
            if (n.x != -1.0) { return 2; }
            if (n.y != -2.0) { return 3; }
            if (n.z != -3.0) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec3LerpAndReflect) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec3 a = math.Vec3(0.0, 0.0, 0.0);
            math.Vec3 b = math.Vec3(10.0, 20.0, 30.0);
            math.Vec3 mid = a.lerp(b, 0.5);
            if (mid.x != 5.0) { return 1; }
            if (mid.y != 10.0) { return 2; }
            if (mid.z != 15.0) { return 3; }
            // reflect downward off ground
            math.Vec3 down = math.Vec3(1.0, -1.0, 0.0);
            math.Vec3 up = math.Vec3(0.0, 1.0, 0.0);
            math.Vec3 bounce = down.reflect(up);
            if (math.abs(bounce.x - 1.0) > eps) { return 4; }
            if (math.abs(bounce.y - 1.0) > eps) { return 5; }
            if (math.abs(bounce.z) > eps) { return 6; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec3Angle) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec3 x = math.Vec3(1.0, 0.0, 0.0);
            math.Vec3 y = math.Vec3(0.0, 1.0, 0.0);
            // perpendicular -> pi/2
            if (math.abs(x.angle(y) - math.pi() / 2.0) > eps) { return 1; }
            // parallel -> 0
            if (math.abs(x.angle(x)) > eps) { return 2; }
            // opposite -> pi
            math.Vec3 nx = math.Vec3(-1.0, 0.0, 0.0);
            if (math.abs(x.angle(nx) - math.pi()) > eps) { return 3; }
            return 0;
        }
    )"), 0);
}

/* ── Vec4 ────────────────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec4ConstructionAndFields) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec4 v = math.Vec4(1.0, 2.0, 3.0, 4.0);
            if (v.x != 1.0) { return 1; }
            if (v.y != 2.0) { return 2; }
            if (v.z != 3.0) { return 3; }
            if (v.w != 4.0) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec4LengthAndDot) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec4 v = math.Vec4(1.0, 2.0, 3.0, 4.0);
            // length = sqrt(1+4+9+16) = sqrt(30)
            if (math.abs(v.length() - math.sqrt(30.0)) > eps) { return 1; }
            if (v.lengthSqr() != 30.0) { return 2; }
            // perpendicular
            math.Vec4 a = math.Vec4(1.0, 0.0, 0.0, 0.0);
            math.Vec4 b = math.Vec4(0.0, 1.0, 0.0, 0.0);
            if (a.dot(b) != 0.0) { return 3; }
            // self dot
            if (a.dot(a) != 1.0) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec4Arithmetic) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec4 a = math.Vec4(1.0, 2.0, 3.0, 4.0);
            math.Vec4 b = math.Vec4(5.0, 6.0, 7.0, 8.0);
            math.Vec4 c = a.add(b);
            if (c.x != 6.0) { return 1; }
            if (c.w != 12.0) { return 2; }
            math.Vec4 d = a.sub(b);
            if (d.x != -4.0) { return 3; }
            math.Vec4 s = a.scale(2.0);
            if (s.x != 2.0) { return 4; }
            if (s.w != 8.0) { return 5; }
            math.Vec4 n = a.negate();
            if (n.x != -1.0) { return 6; }
            if (n.w != -4.0) { return 7; }
            // normalize
            math.Vec4 u = a.normalize();
            if (math.abs(u.length() - 1.0) > eps) { return 8; }
            // distance
            if (math.abs(a.distance(b) - math.sqrt(64.0)) > eps) { return 9; }
            // lerp
            math.Vec4 mid = a.lerp(b, 0.5);
            if (mid.x != 3.0) { return 10; }
            if (mid.w != 6.0) { return 11; }
            return 0;
        }
    )"), 0);
}

/* ── Quaternion ──────────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, QuaternionIdentity) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);
            if (math.abs(id.length() - 1.0) > eps) { return 1; }
            // id * id = id
            math.Quaternion r = id.multiply(id);
            if (math.abs(r.w - 1.0) > eps) { return 2; }
            if (math.abs(r.x) > eps) { return 3; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, QuaternionConjugateAndInverse) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Quaternion q = math.Quaternion(1.0, 2.0, 3.0, 4.0);
            math.Quaternion c = q.conjugate();
            if (c.x != -1.0) { return 1; }
            if (c.y != -2.0) { return 2; }
            if (c.z != -3.0) { return 3; }
            if (c.w != 4.0) { return 4; }
            // q * inverse(q) ~= identity
            math.Quaternion qn = q.normalize();
            math.Quaternion inv = qn.inverse();
            math.Quaternion id = qn.multiply(inv);
            if (math.abs(id.w - 1.0) > eps) { return 5; }
            if (math.abs(id.x) > eps) { return 6; }
            if (math.abs(id.y) > eps) { return 7; }
            if (math.abs(id.z) > eps) { return 8; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, QuaternionMultiply) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            // 90 deg around Y then 90 deg around Y = 180 deg around Y
            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);
            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);
            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));
            math.Quaternion r180 = r90.multiply(r90);
            // r180 should be (0, 1, 0, 0) or (0, -1, 0, 0)
            if (math.abs(math.abs(r180.y) - 1.0) > eps) { return 1; }
            if (math.abs(r180.x) > eps) { return 2; }
            if (math.abs(r180.z) > eps) { return 3; }
            if (math.abs(r180.w) > eps) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, QuaternionFromAxisAngle) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);
            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);
            // 0 degrees -> identity
            math.Quaternion r0 = math.Quaternion.fromAxisAngle(yaxis, 0.0);
            if (math.abs(r0.w - 1.0) > eps) { return 1; }
            if (math.abs(r0.x) > eps) { return 2; }
            // 90 degrees around Y -> (0, sin(45), 0, cos(45))
            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));
            if (math.abs(r90.length() - 1.0) > eps) { return 3; }
            if (math.abs(r90.y - math.sin(math.deg2rad(45.0))) > eps) { return 4; }
            if (math.abs(r90.w - math.cos(math.deg2rad(45.0))) > eps) { return 5; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, QuaternionSlerp) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);
            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);
            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));
            // slerp t=0 -> identity
            math.Quaternion s0 = id.slerp(r90, 0.0);
            if (math.abs(s0.w - 1.0) > eps) { return 1; }
            // slerp t=1 -> r90
            math.Quaternion s1 = id.slerp(r90, 1.0);
            if (math.abs(s1.y - r90.y) > eps) { return 2; }
            // slerp t=0.5 -> 45 degrees
            math.Quaternion s5 = id.slerp(r90, 0.5);
            if (math.abs(s5.length() - 1.0) > eps) { return 3; }
            // half of 90 = 45 deg -> y = sin(22.5 deg)
            if (math.abs(s5.y - math.sin(math.deg2rad(22.5))) > eps) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, QuaternionToEuler) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.01;
            math.Quaternion id = math.Quaternion(0.0, 0.0, 0.0, 1.0);
            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);
            math.Quaternion r90 = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));
            math.Quaternion euler = r90.toEuler();
            // yaw (euler.y) should be ~90 degrees
            if (math.abs(math.rad2deg(euler.y) - 90.0) > eps) { return 1; }
            // pitch and roll should be ~0
            if (math.abs(euler.x) > eps) { return 2; }
            if (math.abs(euler.z) > eps) { return 3; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, QuaternionDotAndNormalize) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Quaternion q = math.Quaternion(1.0, 2.0, 3.0, 4.0);
            // dot with self = lengthSqr
            if (math.abs(q.dot(q) - 30.0) > eps) { return 1; }
            // normalize
            math.Quaternion n = q.normalize();
            if (math.abs(n.length() - 1.0) > eps) { return 2; }
            return 0;
        }
    )"), 0);
}

/* ── Mat4 ────────────────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Mat4ConstructionAndFieldAccess) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            array<f64> d = [1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0];
            math.Mat4 m = math.Mat4(d);
            // data field is array<f64>
            array<f64> arr = m.data;
            if (arr.len() != 16) { return 1; }
            if (arr[0] != 1.0) { return 2; }
            if (arr[5] != 1.0) { return 3; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Mat4Identity) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Mat4 id = math.Mat4.identity();
            if (id.get(0, 0) != 1.0) { return 1; }
            if (id.get(1, 1) != 1.0) { return 2; }
            if (id.get(2, 2) != 1.0) { return 3; }
            if (id.get(3, 3) != 1.0) { return 4; }
            if (id.get(0, 1) != 0.0) { return 5; }
            if (id.get(1, 0) != 0.0) { return 6; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Mat4GetSetTranspose) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Mat4 id = math.Mat4.identity();
            // set(0,1) = 5 -> get(0,1) = 5
            math.Mat4 m = id.set(0, 1, 5.0);
            if (m.get(0, 1) != 5.0) { return 1; }
            if (m.get(1, 0) != 0.0) { return 2; }
            // transpose swaps (0,1) and (1,0)
            math.Mat4 t = m.transpose();
            if (t.get(1, 0) != 5.0) { return 3; }
            if (t.get(0, 1) != 0.0) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Mat4MultiplyAndDeterminant) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Mat4 id = math.Mat4.identity();
            // id * id = id
            math.Mat4 r = id.multiply(id);
            if (r.get(0, 0) != 1.0) { return 1; }
            if (r.get(0, 1) != 0.0) { return 2; }
            // det(id) = 1
            if (math.abs(id.determinant() - 1.0) > eps) { return 3; }
            // scale by 2 -> det = 2^4 = 16
            math.Mat4 s = id.scale(2.0);
            if (math.abs(s.determinant() - 16.0) > eps) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Mat4AddAndScale) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Mat4 id = math.Mat4.identity();
            math.Mat4 sum = id.add(id);
            if (sum.get(0, 0) != 2.0) { return 1; }
            if (sum.get(0, 1) != 0.0) { return 2; }
            math.Mat4 s = id.scale(3.0);
            if (s.get(0, 0) != 3.0) { return 3; }
            if (s.get(1, 1) != 3.0) { return 4; }
            return 0;
        }
    )"), 0);
}

/* ── Static methods ──────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec2StaticZeroAndOne) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec2 z = math.Vec2.zero();
            if (z.x != 0.0) { return 1; }
            if (z.y != 0.0) { return 2; }
            math.Vec2 o = math.Vec2.one();
            if (o.x != 1.0) { return 3; }
            if (o.y != 1.0) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec3StaticZeroAndOne) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec3 z = math.Vec3.zero();
            if (z.x != 0.0) { return 1; }
            if (z.y != 0.0) { return 2; }
            if (z.z != 0.0) { return 3; }
            math.Vec3 o = math.Vec3.one();
            if (o.x != 1.0) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec4StaticZeroAndOne) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Vec4 z = math.Vec4.zero();
            if (z.x != 0.0) { return 1; }
            if (z.w != 0.0) { return 2; }
            math.Vec4 o = math.Vec4.one();
            if (o.x != 1.0) { return 3; }
            if (o.w != 1.0) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Mat4StaticIdentity) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            math.Mat4 id = math.Mat4.identity();
            if (id.get(0, 0) != 1.0) { return 1; }
            if (id.get(1, 1) != 1.0) { return 2; }
            if (id.get(0, 1) != 0.0) { return 3; }
            if (id.determinant() != 1.0) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, QuaternionStaticFromAxisAngle) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);
            math.Quaternion q = math.Quaternion.fromAxisAngle(yaxis, math.deg2rad(90.0));
            if (math.abs(q.length() - 1.0) > eps) { return 1; }
            if (math.abs(q.y - math.sin(math.deg2rad(45.0))) > eps) { return 2; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, StaticMethodChaining) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            // Static factory -> instance method chain
            math.Vec3 v = math.Vec3.one().scale(3.0);
            if (v.x != 3.0) { return 1; }
            if (v.y != 3.0) { return 2; }
            math.Mat4 m = math.Mat4.identity().scale(2.0);
            if (m.get(0, 0) != 2.0) { return 3; }
            return 0;
        }
    )"), 0);
}

/* ── Vec2 angle/rotate ───────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Vec2AngleAndRotate) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec2 v = math.Vec2(1.0, 0.0);
            if (math.abs(v.angle()) > eps) { return 1; }
            math.Vec2 up = math.Vec2(0.0, 1.0);
            if (math.abs(up.angle() - math.pi() / 2.0) > eps) { return 2; }
            // rotate (1,0) by 90 degrees -> (0,1)
            math.Vec2 r = v.rotate(math.pi() / 2.0);
            if (math.abs(r.x) > eps) { return 3; }
            if (math.abs(r.y - 1.0) > eps) { return 4; }
            // rotate by 180 -> (-1, 0)
            math.Vec2 r2 = v.rotate(math.pi());
            if (math.abs(r2.x + 1.0) > eps) { return 5; }
            if (math.abs(r2.y) > eps) { return 6; }
            return 0;
        }
    )"), 0);
}

/* ── Vec3 transform/rotateByQuaternion/unproject ─────────────────── */

TEST(BaslStdlibMathTest, Vec3Transform) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec3 p = math.Vec3(1.0, 2.0, 3.0);
            // Identity transform
            math.Mat4 id = math.Mat4.identity();
            math.Vec3 r = p.transform(id);
            if (math.abs(r.x - 1.0) > eps) { return 1; }
            if (math.abs(r.y - 2.0) > eps) { return 2; }
            if (math.abs(r.z - 3.0) > eps) { return 3; }
            // Translation
            math.Mat4 t = id.translate(math.Vec3(10.0, 20.0, 30.0));
            math.Vec3 r2 = p.transform(t);
            if (math.abs(r2.x - 11.0) > eps) { return 4; }
            if (math.abs(r2.y - 22.0) > eps) { return 5; }
            if (math.abs(r2.z - 33.0) > eps) { return 6; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec3RotateByQuaternion) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec3 fwd = math.Vec3(0.0, 0.0, 1.0);
            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);
            // 90 deg around Y: (0,0,1) -> (1,0,0)
            math.Quaternion q = math.Quaternion.fromAxisAngle(yaxis, math.pi() / 2.0);
            math.Vec3 r = fwd.rotateByQuaternion(q);
            if (math.abs(r.x - 1.0) > eps) { return 1; }
            if (math.abs(r.y) > eps) { return 2; }
            if (math.abs(r.z) > eps) { return 3; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Vec3Unproject) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.01;
            math.Mat4 proj = math.Mat4.perspective(math.deg2rad(90.0), 1.0, 0.1, 100.0);
            math.Mat4 view = math.Mat4.identity();
            // Center of screen (0.5, 0.5) at near plane (z=0)
            math.Vec3 near = math.Vec3(0.5, 0.5, 0.0).unproject(proj, view);
            // Should be near origin on near plane
            if (math.abs(near.x) > eps) { return 1; }
            if (math.abs(near.y) > eps) { return 2; }
            return 0;
        }
    )"), 0);
}

/* ── Quaternion fromEuler/toMat4 ─────────────────────────────────── */

TEST(BaslStdlibMathTest, QuaternionFromEuler) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            // Zero euler -> identity quaternion
            math.Quaternion q0 = math.Quaternion.fromEuler(0.0, 0.0, 0.0);
            if (math.abs(q0.w - 1.0) > eps) { return 1; }
            if (math.abs(q0.x) > eps) { return 2; }
            // 90 deg pitch -> unit quaternion
            math.Quaternion qp = math.Quaternion.fromEuler(math.pi() / 2.0, 0.0, 0.0);
            if (math.abs(qp.length() - 1.0) > eps) { return 3; }
            // fromEuler produces unit quaternions
            math.Quaternion q = math.Quaternion.fromEuler(0.1, 0.2, 0.3);
            if (math.abs(q.length() - 1.0) > eps) { return 4; }
            // 180 deg yaw: should be (0, sin(90), 0, cos(90)) = (0, 1, 0, 0)
            math.Quaternion qy = math.Quaternion.fromEuler(0.0, math.pi(), 0.0);
            if (math.abs(math.abs(qy.y) - 1.0) > eps) { return 5; }
            if (math.abs(qy.x) > eps) { return 6; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, QuaternionToMat4) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            // Identity quaternion -> identity matrix
            math.Quaternion qi = math.Quaternion(0.0, 0.0, 0.0, 1.0);
            math.Mat4 m = qi.toMat4();
            if (math.abs(m.get(0, 0) - 1.0) > eps) { return 1; }
            if (math.abs(m.get(1, 1) - 1.0) > eps) { return 2; }
            if (math.abs(m.get(2, 2) - 1.0) > eps) { return 3; }
            if (math.abs(m.get(3, 3) - 1.0) > eps) { return 4; }
            if (math.abs(m.get(0, 1)) > eps) { return 5; }
            // 90 deg around Y: should rotate (0,0,1) to (1,0,0)
            math.Vec3 yaxis = math.Vec3(0.0, 1.0, 0.0);
            math.Quaternion q90 = math.Quaternion.fromAxisAngle(yaxis, math.pi() / 2.0);
            math.Mat4 rm = q90.toMat4();
            math.Vec3 fwd = math.Vec3(0.0, 0.0, 1.0);
            math.Vec3 r = fwd.transform(rm);
            if (math.abs(r.x - 1.0) > eps) { return 6; }
            if (math.abs(r.y) > eps) { return 7; }
            if (math.abs(r.z) > eps) { return 8; }
            return 0;
        }
    )"), 0);
}

/* ── Mat4 new methods ────────────────────────────────────────────── */

TEST(BaslStdlibMathTest, Mat4Trace) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            if (math.abs(math.Mat4.identity().trace() - 4.0) > eps) { return 1; }
            math.Mat4 s = math.Mat4.identity().scale(2.0);
            if (math.abs(s.trace() - 8.0) > eps) { return 2; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Mat4Invert) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            // Invert identity = identity
            math.Mat4 id = math.Mat4.identity();
            math.Mat4 inv = id.invert();
            if (math.abs(inv.get(0, 0) - 1.0) > eps) { return 1; }
            if (math.abs(inv.get(0, 1)) > eps) { return 2; }
            // Invert scale(2) = scale(0.5)
            math.Mat4 s = id.scale(2.0);
            math.Mat4 si = s.invert();
            if (math.abs(si.get(0, 0) - 0.5) > eps) { return 3; }
            // M * M^-1 = I
            math.Mat4 prod = s.multiply(si);
            if (math.abs(prod.get(0, 0) - 1.0) > eps) { return 4; }
            if (math.abs(prod.get(0, 1)) > eps) { return 5; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Mat4TranslateAndScaleV) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Mat4 id = math.Mat4.identity();
            // Translate
            math.Mat4 t = id.translate(math.Vec3(10.0, 20.0, 30.0));
            if (math.abs(t.get(0, 3) - 10.0) > eps) { return 1; }
            if (math.abs(t.get(1, 3) - 20.0) > eps) { return 2; }
            if (math.abs(t.get(2, 3) - 30.0) > eps) { return 3; }
            // ScaleV
            math.Mat4 s = id.scaleV(math.Vec3(2.0, 3.0, 4.0));
            if (math.abs(s.get(0, 0) - 2.0) > eps) { return 4; }
            if (math.abs(s.get(1, 1) - 3.0) > eps) { return 5; }
            if (math.abs(s.get(2, 2) - 4.0) > eps) { return 6; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Mat4RotateXYZ) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Mat4 id = math.Mat4.identity();
            // RotateX 90: (0,1,0) -> (0,0,1)
            math.Mat4 rx = id.rotateX(math.pi() / 2.0);
            math.Vec3 up = math.Vec3(0.0, 1.0, 0.0);
            math.Vec3 r1 = up.transform(rx);
            if (math.abs(r1.y) > eps) { return 1; }
            if (math.abs(r1.z - 1.0) > eps) { return 2; }
            // RotateY 90: (0,0,1) -> (1,0,0)
            math.Mat4 ry = id.rotateY(math.pi() / 2.0);
            math.Vec3 fwd = math.Vec3(0.0, 0.0, 1.0);
            math.Vec3 r2 = fwd.transform(ry);
            if (math.abs(r2.x - 1.0) > eps) { return 3; }
            if (math.abs(r2.z) > eps) { return 4; }
            // RotateZ 90: (1,0,0) -> (0,1,0)
            math.Mat4 rz = id.rotateZ(math.pi() / 2.0);
            math.Vec3 right = math.Vec3(1.0, 0.0, 0.0);
            math.Vec3 r3 = right.transform(rz);
            if (math.abs(r3.x) > eps) { return 5; }
            if (math.abs(r3.y - 1.0) > eps) { return 6; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Mat4LookAt) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Vec3 eye = math.Vec3(0.0, 0.0, 5.0);
            math.Vec3 target = math.Vec3.zero();
            math.Vec3 up = math.Vec3(0.0, 1.0, 0.0);
            math.Mat4 view = math.Mat4.lookAt(eye, target, up);
            // Eye at (0,0,5) looking at origin: the view matrix should
            // translate the eye to origin in view space
            math.Vec3 eyeView = eye.transform(view);
            if (math.abs(eyeView.x) > eps) { return 1; }
            if (math.abs(eyeView.y) > eps) { return 2; }
            // Target should be at (0,0,-5) in view space
            math.Vec3 targetView = target.transform(view);
            if (math.abs(targetView.x) > eps) { return 3; }
            if (math.abs(targetView.y) > eps) { return 4; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Mat4PerspectiveAndOrtho) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            // Perspective: 90 deg FOV, aspect 1, near 0.1, far 100
            math.Mat4 p = math.Mat4.perspective(math.deg2rad(90.0), 1.0, 0.1, 100.0);
            // With 90 deg FOV and aspect 1: m[0][0] = m[1][1] = 1.0
            if (math.abs(p.get(0, 0) - 1.0) > eps) { return 1; }
            if (math.abs(p.get(1, 1) - 1.0) > eps) { return 2; }
            if (p.get(3, 3) != 0.0) { return 3; }
            // Ortho: symmetric [-1,1] box
            math.Mat4 o = math.Mat4.ortho(-1.0, 1.0, -1.0, 1.0, 0.1, 100.0);
            if (math.abs(o.get(0, 0) - 1.0) > eps) { return 4; }
            if (math.abs(o.get(1, 1) - 1.0) > eps) { return 5; }
            if (math.abs(o.get(3, 3) - 1.0) > eps) { return 6; }
            return 0;
        }
    )"), 0);
}

TEST(BaslStdlibMathTest, Mat4Frustum) {
    EXPECT_EQ(RunWithStdlib(R"(
        import "math";
        fn main() -> i32 {
            f64 eps = 0.000001;
            math.Mat4 f = math.Mat4.frustum(-1.0, 1.0, -1.0, 1.0, 1.0, 100.0);
            // Symmetric frustum: m[0][0] = 2*near/(right-left) = 2*1/2 = 1
            if (math.abs(f.get(0, 0) - 1.0) > eps) { return 1; }
            if (math.abs(f.get(1, 1) - 1.0) > eps) { return 2; }
            if (f.get(3, 2) != -1.0) { return 3; }
            return 0;
        }
    )"), 0);
}

/* ── String trim_left / trim_right ────────────────────────────────── */

TEST(BaslStdlibStringTest, TrimLeftAndTrimRight) {
    EXPECT_EQ(RunWithStdlib(R"(
fn main() -> i32 {
    string s = "  hello  ";
    if (s.trim_left() != "hello  ") { return 1; }
    if (s.trim_right() != "  hello") { return 2; }
    if ("nowhitespace".trim_left() != "nowhitespace") { return 3; }
    if ("nowhitespace".trim_right() != "nowhitespace") { return 4; }
    if ("   ".trim_left() != "") { return 5; }
    if ("   ".trim_right() != "") { return 6; }
    return 0;
}
    )"), 0);
}

/* ── String reverse ──────────────────────────────────────────────── */

TEST(BaslStdlibStringTest, Reverse) {
    EXPECT_EQ(RunWithStdlib(R"(
fn main() -> i32 {
    if ("hello".reverse() != "olleh") { return 1; }
    if ("".reverse() != "") { return 2; }
    if ("a".reverse() != "a") { return 3; }
    if ("abcd".reverse() != "dcba") { return 4; }
    return 0;
}
    )"), 0);
}

/* ── String is_empty ─────────────────────────────────────────────── */

TEST(BaslStdlibStringTest, IsEmpty) {
    EXPECT_EQ(RunWithStdlib(R"(
fn main() -> i32 {
    if ("".is_empty() != true) { return 1; }
    if ("x".is_empty() != false) { return 2; }
    if (" ".is_empty() != false) { return 3; }
    return 0;
}
    )"), 0);
}

/* ── String repeat ───────────────────────────────────────────────── */

TEST(BaslStdlibStringTest, Repeat) {
    EXPECT_EQ(RunWithStdlib(R"(
fn main() -> i32 {
    if ("abc".repeat(i32(3)) != "abcabcabc") { return 1; }
    if ("x".repeat(i32(0)) != "") { return 2; }
    if ("hi".repeat(i32(1)) != "hi") { return 3; }
    if ("".repeat(i32(5)) != "") { return 4; }
    return 0;
}
    )"), 0);
}

/* ── String count ────────────────────────────────────────────────── */

TEST(BaslStdlibStringTest, Count) {
    EXPECT_EQ(RunWithStdlib(R"(
fn main() -> i32 {
    if ("abcabcabc".count("abc") != i32(3)) { return 1; }
    if ("hello".count("x") != i32(0)) { return 2; }
    if ("aaa".count("a") != i32(3)) { return 3; }
    if ("aaaa".count("aa") != i32(2)) { return 4; }
    return 0;
}
    )"), 0);
}

/* ── String last_index_of ────────────────────────────────────────── */

TEST(BaslStdlibStringTest, LastIndexOf) {
    EXPECT_EQ(RunWithStdlib(R"(
fn main() -> i32 {
    i32 idx, bool found = "hello world".last_index_of("o");
    if (idx != i32(7)) { return 1; }
    if (found != true) { return 2; }
    i32 idx2, bool found2 = "hello".last_index_of("xyz");
    if (found2 != false) { return 3; }
    i32 idx3, bool found3 = "abcabc".last_index_of("abc");
    if (idx3 != i32(3)) { return 4; }
    if (found3 != true) { return 5; }
    return 0;
}
    )"), 0);
}

/* ── String trim_prefix / trim_suffix ────────────────────────────── */

TEST(BaslStdlibStringTest, TrimPrefixAndTrimSuffix) {
    EXPECT_EQ(RunWithStdlib(R"(
fn main() -> i32 {
    if ("hello world".trim_prefix("hello ") != "world") { return 1; }
    if ("hello world".trim_suffix(" world") != "hello") { return 2; }
    if ("hello".trim_prefix("xyz") != "hello") { return 3; }
    if ("hello".trim_suffix("xyz") != "hello") { return 4; }
    if ("hello".trim_prefix("hello") != "") { return 5; }
    if ("hello".trim_suffix("hello") != "") { return 6; }
    return 0;
}
    )"), 0);
}

}  // namespace
