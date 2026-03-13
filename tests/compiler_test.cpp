#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "basl/basl.h"
}

namespace {

struct TestSource {
    const char *path;
    const char *text;
};

basl_source_id_t RegisterSource(
    basl_source_registry_t *registry,
    const char *path,
    const char *text,
    basl_error_t *error
) {
    basl_source_id_t source_id = 0U;

    EXPECT_EQ(
        basl_source_registry_register_cstr(registry, path, text, &source_id, error),
        BASL_STATUS_OK
    );
    return source_id;
}

int64_t CompileAndRun(const char *source_text) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_value_t result;
    basl_source_id_t source_id;
    int64_t output = 0;

    EXPECT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    source_id = RegisterSource(&registry, "main.basl", source_text, &error);

    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
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
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    return output;
}

int64_t CompileAndRun(
    const TestSource *sources,
    size_t source_count,
    const char *entry_path
) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_value_t result;
    basl_source_id_t source_id = 0U;
    int64_t output = 0;
    size_t index;

    EXPECT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    EXPECT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    for (index = 0U; index < source_count; index += 1U) {
        RegisterSource(&registry, sources[index].path, sources[index].text, &error);
    }

    for (index = 1U; index <= basl_source_registry_count(&registry); index += 1U) {
        const basl_source_file_t *source =
            basl_source_registry_get(&registry, (basl_source_id_t)index);

        if (source != nullptr && std::strcmp(basl_string_c_str(&source->path), entry_path) == 0) {
            source_id = source->id;
            break;
        }
    }

    EXPECT_NE(source_id, 0U);
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
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
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
    return output;
}

}  // namespace

TEST(BaslCompilerTest, CompilesAndExecutesArithmeticAndLocals) {
    EXPECT_EQ(
        CompileAndRun(
            "fn main() -> i32 {"
            "    i32 x = 1 + 2 * 3;"
            "    x = (x + 4) / 2;"
            "    return x;"
            "}"
        ),
        5
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesFloatArithmeticAndComparison) {
    EXPECT_EQ(
        CompileAndRun(
            "fn scale(f64 value) -> f64 {"
            "    value *= 2.0;"
            "    value++;"
            "    return value;"
            "}"
            "fn main() -> i32 {"
            "    f64 total = scale(2.5);"
            "    if (total == 6.0 && total >= 5.5) {"
            "        return 6;"
            "    }"
            "    return 0;"
            "}"
        ),
        6
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesIfElseAndWhile) {
    EXPECT_EQ(
        CompileAndRun(
            "fn main() -> i32 {"
            "    i32 sum = 0;"
            "    i32 i = 0;"
            "    while (i < 5) {"
            "        sum = sum + i;"
            "        i = i + 1;"
            "    }"
            "    if (sum > 9) {"
            "        return sum;"
            "    } else {"
            "        return 0;"
            "    }"
            "}"
        ),
        10
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesBoolLocalsAndEquality) {
    EXPECT_EQ(
        CompileAndRun(
            "fn main() -> i32 {"
            "    bool ready = 1 + 1 == 2;"
            "    if (ready != false) {"
            "        return 7;"
            "    }"
            "    return 0;"
            "}"
        ),
        7
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesDirectFunctionCalls) {
    EXPECT_EQ(
        CompileAndRun(
            "fn add(i32 left, i32 right) -> i32 {"
            "    return left + right;"
            "}"
            "fn is_ten(i32 value) -> bool {"
            "    return value == 10;"
            "}"
            "fn main() -> i32 {"
            "    i32 result = add(4, 6);"
            "    if (is_ten(result)) {"
            "        return result;"
            "    }"
            "    return 0;"
            "}"
        ),
        10
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesRecursiveFunctionCalls) {
    EXPECT_EQ(
        CompileAndRun(
            "fn sum_to(i32 value) -> i32 {"
            "    if (value == 0) {"
            "        return 0;"
            "    }"
            "    return value + sum_to(value - 1);"
            "}"
            "fn main() -> i32 {"
            "    return sum_to(5);"
            "}"
        ),
        15
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesShortCircuitLogicalOperators) {
    EXPECT_EQ(
        CompileAndRun(
            "fn panic_bool() -> bool {"
            "    i32 x = 1 / 0;"
            "    return x == 0;"
            "}"
            "fn main() -> i32 {"
            "    if (true || panic_bool()) {"
            "        if (!(false && panic_bool())) {"
            "            return 7;"
            "        }"
            "    }"
            "    return 0;"
            "}"
        ),
        7
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesBitwiseShiftAndTernaryExpressions) {
    EXPECT_EQ(
        CompileAndRun(
            "const i32 MASK = (1 << 3) | 1;"
            "const i32 PICK = true ? MASK : 0;"
            "fn main() -> i32 {"
            "    i32 bits = PICK ^ 2;"
            "    return bits > 7 ? bits & 14 : 0;"
            "}"
        ),
        10
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesBreakAndContinue) {
    EXPECT_EQ(
        CompileAndRun(
            "fn main() -> i32 {"
            "    i32 sum = 0;"
            "    i32 i = 0;"
            "    while (i < 8) {"
            "        i = i + 1;"
            "        {"
            "            i32 current = i;"
            "            if (current == 3) {"
            "                continue;"
            "            }"
            "            if (current == 6) {"
            "                break;"
            "            }"
            "        }"
            "        sum = sum + i;"
            "    }"
            "    return sum;"
            "}"
        ),
        12
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesForLoopsAndIncrementClauses) {
    EXPECT_EQ(
        CompileAndRun(
            "fn main() -> i32 {"
            "    i32 sum = 0;"
            "    for (i32 i = 0; i < 5; i++) {"
            "        sum += i;"
            "    }"
            "    return sum;"
            "}"
        ),
        10
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesForLoopContinueAndBreak) {
    EXPECT_EQ(
        CompileAndRun(
            "fn main() -> i32 {"
            "    i32 sum = 0;"
            "    for (i32 i = 0; i < 6; i++) {"
            "        if (i == 2) {"
            "            continue;"
            "        }"
            "        if (i == 5) {"
            "            break;"
            "        }"
            "        sum += i;"
            "    }"
            "    return sum;"
            "}"
        ),
        8
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesImportedFunctionsAcrossFiles) {
    const TestSource sources[] = {
        {
            "/project/math.basl",
            "fn add(i32 left, i32 right) -> i32 {"
            "    return left + right;"
            "}"
        },
        {
            "/project/main.basl",
            "import \"math\";"
            "fn main() -> i32 {"
            "    return add(2, 5);"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 7);
}

TEST(BaslCompilerTest, CompilesAndExecutesNestedImportsAcrossFiles) {
    const TestSource sources[] = {
        {
            "/project/lib/math.basl",
            "fn inc(i32 value) -> i32 {"
            "    return value + 1;"
            "}"
        },
        {
            "/project/lib/logic.basl",
            "import \"math\";"
            "fn bump_twice(i32 value) -> i32 {"
            "    return inc(inc(value));"
            "}"
        },
        {
            "/project/main.basl",
            "import \"lib/logic\";"
            "fn main() -> i32 {"
            "    return bump_twice(5);"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 3U, "/project/main.basl"), 7);
}

TEST(BaslCompilerTest, RejectsUnregisteredImportedSource) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "/project/main.basl",
        "import \"missing\";"
        "fn main() -> i32 {"
        "    return 0;"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "imported source is not registered"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, CompilesAndExecutesModuleConstantsAcrossFiles) {
    const TestSource sources[] = {
        {
            "/project/config.basl",
            "const i32 LIMIT = 7;"
        },
        {
            "/project/main.basl",
            "import \"config\";"
            "fn main() -> i32 {"
            "    return LIMIT;"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 7);
}

TEST(BaslCompilerTest, CompilesAndExecutesConstantExpressions) {
    const TestSource sources[] = {
        {
            "/project/config.basl",
            "const i32 BASE = 2 + 3 * 4;"
            "const bool READY = BASE == 14;"
        },
        {
            "/project/main.basl",
            "import \"config\";"
            "fn main() -> i32 {"
            "    if (READY) {"
            "        return BASE;"
            "    }"
            "    return 0;"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 14);
}

TEST(BaslCompilerTest, CompilesAndExecutesClassesFieldAccessAndFieldAssignment) {
    EXPECT_EQ(
        CompileAndRun(
            "class Pair {"
            "    i32 left;"
            "    i32 right;"
            "}"
            "fn sum(Pair pair) -> i32 {"
            "    return pair.left + pair.right;"
            "}"
            "fn main() -> i32 {"
            "    Pair pair = Pair(3, 4);"
            "    pair.left = pair.left + 1;"
            "    return sum(pair);"
            "}"
        ),
        8
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesClassesAcrossFiles) {
    const TestSource sources[] = {
        {
            "/project/model.basl",
            "class Counter {"
            "    i32 value;"
            "}"
            "fn make_counter(i32 value) -> Counter {"
            "    return Counter(value);"
            "}"
            "fn read_counter(Counter counter) -> i32 {"
            "    return counter.value;"
            "}"
        },
        {
            "/project/main.basl",
            "import \"model\";"
            "fn main() -> i32 {"
            "    Counter counter = make_counter(7);"
            "    return read_counter(counter);"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 7);
}

TEST(BaslCompilerTest, CompilesAndExecutesClassMethodsAndSelf) {
    EXPECT_EQ(
        CompileAndRun(
            "class Counter {"
            "    i32 value;"
            "    fn bump(i32 delta) -> i32 {"
            "        self.value = self.value + delta;"
            "        return self.value;"
            "    }"
            "    fn read() -> i32 {"
            "        return self.value;"
            "    }"
            "}"
            "fn main() -> i32 {"
            "    Counter counter = Counter(5);"
            "    counter.bump(2);"
            "    return counter.read();"
            "}"
        ),
        7
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesFloatFieldsAndGlobals) {
    EXPECT_EQ(
        CompileAndRun(
            "f64 scale = 1.5;"
            "class Counter {"
            "    f64 value;"
            "    fn init(f64 value) -> void {"
            "        self.value = value * scale;"
            "    }"
            "    fn grow(f64 delta) -> void {"
            "        self.value += delta;"
            "    }"
            "}"
            "fn main() -> i32 {"
            "    Counter counter = Counter(4.0);"
            "    counter.grow(1.0);"
            "    if (counter.value > 6.5) {"
            "        return 7;"
            "    }"
            "    return 0;"
            "}"
        ),
        7
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesInitBasedConstructors) {
    EXPECT_EQ(
        CompileAndRun(
            "class Counter {"
            "    i32 value;"
            "    i32 doubled;"
            "    fn init(i32 value) -> void {"
            "        self.value = value;"
            "        self.doubled = value * 2;"
            "    }"
            "}"
            "fn main() -> i32 {"
            "    Counter counter = Counter(5);"
            "    return counter.value + counter.doubled;"
            "}"
        ),
        15
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesMethodsAcrossFiles) {
    const TestSource sources[] = {
        {
            "/project/model.basl",
            "class Counter {"
            "    i32 value;"
            "    fn bump(i32 delta) -> i32 {"
            "        self.value = self.value + delta;"
            "        return self.value;"
            "    }"
            "}"
            "fn build(i32 value) -> Counter {"
            "    return Counter(value);"
            "}"
        },
        {
            "/project/main.basl",
            "import \"model\";"
            "fn main() -> i32 {"
            "    Counter counter = build(8);"
            "    return counter.bump(3);"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 11);
}

TEST(BaslCompilerTest, CompilesAndExecutesPublicInitConstructorsAcrossFiles) {
    const TestSource sources[] = {
        {
            "/project/models.basl",
            "pub class Counter {"
            "    pub i32 value;"
            "    fn init(i32 value) -> void {"
            "        self.value = value + 1;"
            "    }"
            "}"
        },
        {
            "/project/main.basl",
            "import \"models\";"
            "fn main() -> i32 {"
            "    models.Counter counter = models.Counter(6);"
            "    return counter.value;"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 7);
}

TEST(BaslCompilerTest, CompilesAndExecutesInterfacePolymorphismAcrossFiles) {
    const TestSource sources[] = {
        {
            "/project/model.basl",
            "interface Reader {"
            "    fn read() -> i32;"
            "}"
            "class Counter implements Reader {"
            "    i32 value;"
            "    fn bump(i32 delta) -> i32 {"
            "        self.value = self.value + delta;"
            "        return self.value;"
            "    }"
            "    fn read() -> i32 {"
            "        return self.value;"
            "    }"
            "}"
            "fn make_reader(i32 value) -> Reader {"
            "    Counter counter = Counter(value);"
            "    counter.bump(1);"
            "    return counter;"
            "}"
        },
        {
            "/project/main.basl",
            "import \"model\";"
            "fn use_reader(Reader reader) -> i32 {"
            "    return reader.read();"
            "}"
            "fn main() -> i32 {"
            "    Reader reader = make_reader(6);"
            "    return use_reader(reader);"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 7);
}

TEST(BaslCompilerTest, CompilesAndExecutesQualifiedModuleSymbolsAcrossFiles) {
    const TestSource sources[] = {
        {
            "/project/model.basl",
            "pub const i32 OFFSET = 2;"
            "pub class Counter {"
            "    i32 value;"
            "    fn bump(i32 delta) -> i32 {"
            "        self.value = self.value + delta;"
            "        return self.value;"
            "    }"
            "}"
            "pub fn make_counter(i32 value) -> Counter {"
            "    return Counter(value + OFFSET);"
            "}"
        },
        {
            "/project/main.basl",
            "import \"model\" as models;"
            "fn read(models.Counter counter) -> i32 {"
            "    return counter.value;"
            "}"
            "fn main() -> i32 {"
            "    models.Counter counter = models.make_counter(models.OFFSET + 5);"
            "    return read(counter) + counter.bump(1);"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 19);
}

TEST(BaslCompilerTest, CompilesAndExecutesQualifiedImportedInterfacesAcrossFiles) {
    const TestSource sources[] = {
        {
            "/project/contracts.basl",
            "pub interface Reader {"
            "    fn read() -> i32;"
            "}"
        },
        {
            "/project/model.basl",
            "import \"contracts\";"
            "pub class Counter implements contracts.Reader {"
            "    i32 value;"
            "    fn read() -> i32 {"
            "        return self.value;"
            "    }"
            "}"
            "pub fn make_reader(i32 value) -> contracts.Reader {"
            "    return Counter(value);"
            "}"
        },
        {
            "/project/main.basl",
            "import \"contracts\";"
            "import \"model\";"
            "fn use_reader(contracts.Reader reader) -> i32 {"
            "    return reader.read();"
            "}"
            "fn main() -> i32 {"
            "    return use_reader(model.make_reader(9));"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 3U, "/project/main.basl"), 9);
}

TEST(BaslCompilerTest, CompilesAndExecutesQualifiedConstantsInConstantExpressions) {
    const TestSource sources[] = {
        {
            "/project/config.basl",
            "pub const i32 BASE = 7;"
        },
        {
            "/project/main.basl",
            "import \"config\" as cfg;"
            "const i32 LIMIT = cfg.BASE + 1;"
            "fn main() -> i32 {"
            "    return LIMIT;"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 8);
}

TEST(BaslCompilerTest, CompilesAndExecutesEnumsAndSwitch) {
    EXPECT_EQ(
        CompileAndRun(
            "enum Color {"
            "    Red,"
            "    Green = 3,"
            "    Blue"
            "}"
            "fn main() -> i32 {"
            "    Color color = Color.Blue;"
            "    switch (color) {"
            "        case Color.Red:"
            "            return 1;"
            "        case Color.Green, Color.Blue:"
            "            return 7;"
            "        default:"
            "            return 0;"
            "    }"
            "}"
        ),
        7
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesStringsAndStringConstants) {
    const char *source = R"(
const string PREFIX = "he" + 'l';

fn make_message() -> string {
    return PREFIX + `lo`;
}

fn main() -> i32 {
    string message = make_message();
    if (message == "hello" && message != "world") {
        return 12;
    }
    return 0;
}
)";

    EXPECT_EQ(CompileAndRun(source), 12);
}

TEST(BaslCompilerTest, CompilesAndExecutesImportedStringConstantsAcrossFiles) {
    const TestSource sources[] = {
        {
            "labels.basl",
            R"(
pub const string GREETING = "ba" + "sl";

pub fn render(string suffix) -> string {
    return GREETING + suffix;
}
)"
        },
        {
            "main.basl",
            R"(
import "labels";

fn main() -> i32 {
    string value = labels.render(" vm");
    if (value == "basl vm") {
        return 15;
    }
    return 0;
}
)"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, sizeof(sources) / sizeof(sources[0]), "main.basl"), 15);
}

TEST(BaslCompilerTest, CompilesAndExecutesVoidFunctionsAndMethods) {
    const char *source = R"(
class Counter {
    i32 value;

    fn bump() -> void {
        self.value++;
        return;
    }
}

fn touch(Counter counter) -> void {
    counter.bump();
}

fn reset(bool flag) -> void {
    if (flag) {
        return;
    }
}

fn main() -> i32 {
    Counter counter = Counter(4);
    touch(counter);
    reset(false);
    counter.bump();
    return counter.value;
}
)";

    EXPECT_EQ(CompileAndRun(source), 6);
}

TEST(BaslCompilerTest, CompilesAndExecutesDeferredCallsInLifoOrderWithEagerArguments) {
    const char *source = R"(
i32 state = 0;

fn push(i32 value) -> void {
    state = state * 10 + value;
}

fn next() -> i32 {
    state = state + 1;
    return state;
}

fn run() -> void {
    defer push(next());
    defer push(next());
}

fn main() -> i32 {
    run();
    return state;
}
)";

    EXPECT_EQ(CompileAndRun(source), 221);
}

TEST(BaslCompilerTest, CompilesAndExecutesDeferredMethodsAndReturnsAfterDrain) {
    const char *source = R"(
class Counter {
    i32 value;

    fn bump(i32 delta) -> void {
        self.value += delta;
    }
}

fn run(Counter counter) -> i32 {
    defer counter.bump(2);
    defer counter.bump(3);
    return 7;
}

fn main() -> i32 {
    Counter counter = Counter(4);
    i32 result = run(counter);
    return counter.value * 10 + result;
}
)";

    EXPECT_EQ(CompileAndRun(source), 97);
}

TEST(BaslCompilerTest, CompilesAndExecutesDeferredInterfaceCalls) {
    const char *source = R"(
interface Reader {
    fn read() -> i32;
}

i32 state = 0;

class Counter implements Reader {
    i32 value;

    fn read() -> i32 {
        state = state * 10 + self.value;
        return self.value;
    }
}

fn run(Reader reader) -> void {
    defer reader.read();
}

fn main() -> i32 {
    run(Counter(8));
    return state;
}
)";

    EXPECT_EQ(CompileAndRun(source), 8);
}

TEST(BaslCompilerTest, CompilesAndExecutesDeferredInitConstructors) {
    const char *source = R"(
i32 state = 0;

class Counter {
    i32 value;

    fn init(i32 value) -> void {
        self.value = value;
        state = state * 10 + value;
    }
}

fn run() -> void {
    defer Counter(2);
    defer Counter(3);
}

fn main() -> i32 {
    run();
    return state;
}
)";

    EXPECT_EQ(CompileAndRun(source), 32);
}

TEST(BaslCompilerTest, CompilesAndExecutesPublicGlobalsAcrossFiles) {
    const TestSource sources[] = {
        {
            "/project/lib.basl",
            "pub i32 counter = 3;"
            "pub fn bump() -> i32 {"
            "    counter = counter + 1;"
            "    return counter;"
            "}"
        },
        {
            "/project/main.basl",
            "import \"lib\";"
            "fn main() -> i32 {"
            "    return lib.bump() + lib.counter;"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 8);
}

TEST(BaslCompilerTest, CompilesAndExecutesCompoundAssignmentsForGlobalsAndFields) {
    EXPECT_EQ(
        CompileAndRun(
            "class Counter {"
            "    i32 value;"
            "}"
            "i32 total = 2;"
            "fn main() -> i32 {"
            "    Counter counter = Counter(3);"
            "    total += 4;"
            "    counter.value *= 2;"
            "    counter.value--;"
            "    return total + counter.value;"
            "}"
        ),
        11
    );
}

TEST(BaslCompilerTest, CompilesAndExecutesPublicClassesInterfacesAndGlobals) {
    const TestSource sources[] = {
        {
            "/project/contracts.basl",
            "pub interface Reader {"
            "    fn read() -> i32;"
            "}"
        },
        {
            "/project/model.basl",
            "import \"contracts\";"
            "pub i32 seed = 4;"
            "pub class Counter implements contracts.Reader {"
            "    pub i32 value;"
            "    pub fn read() -> i32 {"
            "        return self.value;"
            "    }"
            "}"
            "pub fn make_reader(i32 delta) -> contracts.Reader {"
            "    return Counter(seed + delta);"
            "}"
        },
        {
            "/project/main.basl",
            "import \"contracts\";"
            "import \"model\";"
            "fn main() -> i32 {"
            "    contracts.Reader reader = model.make_reader(5);"
            "    model.Counter counter = model.Counter(model.seed);"
            "    return reader.read() + counter.value;"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 3U, "/project/main.basl"), 13);
}

TEST(BaslCompilerTest, CompilesAndExecutesQualifiedImportedEnumsAcrossFiles) {
    const TestSource sources[] = {
        {
            "/project/colors.basl",
            "pub enum Color {"
            "    Red,"
            "    Green,"
            "    Blue"
            "}"
            "pub fn pick() -> Color {"
            "    return Color.Green;"
            "}"
        },
        {
            "/project/main.basl",
            "import \"colors\";"
            "fn main() -> i32 {"
            "    colors.Color color = colors.pick();"
            "    switch (color) {"
            "        case colors.Color.Red:"
            "            return 1;"
            "        case colors.Color.Green:"
            "            return 2;"
            "        default:"
            "            return 0;"
            "    }"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 2);
}

TEST(BaslCompilerTest, CompilesAndExecutesQualifiedConstantsWithBitwiseExpressions) {
    const TestSource sources[] = {
        {
            "/project/config.basl",
            "pub const i32 BASE = 1 << 4;"
            "pub const i32 FLAG = BASE | 3;"
        },
        {
            "/project/main.basl",
            "import \"config\" as cfg;"
            "const i32 LIMIT = true ? cfg.FLAG : 0;"
            "fn main() -> i32 {"
            "    return LIMIT ^ 2;"
            "}"
        }
    };

    EXPECT_EQ(CompileAndRun(sources, 2U, "/project/main.basl"), 17);
}

TEST(BaslCompilerTest, RejectsDuplicateGlobalConstantNames) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "/project/main.basl",
        "const i32 LIMIT = 1;"
        "const i32 LIMIT = 2;"
        "fn main() -> i32 {"
        "    return LIMIT;"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "global constant is already declared"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsAssigningRawI32ToEnumVariable) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "/project/main.basl",
        "enum Color { Red, Blue }"
        "fn main() -> i32 {"
        "    Color color = 1;"
        "    return 0;"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "initializer type does not match local variable type"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsQualifiedAccessToNonPublicModuleMembers) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    RegisterSource(
        &registry,
        "/project/lib.basl",
        "fn hidden() -> i32 {"
        "    return 7;"
        "}"
        "i32 value = 3;",
        &error
    );
    source_id = RegisterSource(
        &registry,
        "/project/main.basl",
        "import \"lib\";"
        "fn main() -> i32 {"
        "    return lib.hidden() + lib.value;"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "module member is not public"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsClassesMissingInterfaceMethods) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "missing_interface_method.basl",
        "interface Reader { fn read() -> i32; }"
        "class Counter implements Reader {"
        "    i32 value;"
        "}"
        "fn main() -> i32 {"
        "    return 0;"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "class does not implement required interface method"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsNonVoidInitMethods) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "bad_init.basl",
        "class Counter {"
        "    fn init(i32 value) -> i32 {"
        "        return value;"
        "    }"
        "}"
        "fn main() -> i32 {"
        "    Counter(1);"
        "    return 0;"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "init methods must return void"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsInterfaceMethodsWithWrongSignature) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "wrong_interface_signature.basl",
        "interface Reader { fn read() -> i32; }"
        "class Counter implements Reader {"
        "    i32 value;"
        "    fn read(i32 delta) -> i32 {"
        "        return self.value + delta;"
        "    }"
        "}"
        "fn main() -> i32 {"
        "    return 0;"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "class method signature does not match interface"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsUnknownClassFields) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "missing_field.basl",
        "class Pair { i32 left; }"
        "fn main() -> i32 {"
        "    Pair pair = Pair(1);"
        "    return pair.right;"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "unknown class field"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsUnknownClassMethods) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "missing_method.basl",
        "class Pair { i32 left; }"
        "fn main() -> i32 {"
        "    Pair pair = Pair(1);"
        "    return pair.right();"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "unknown class method"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsNonI32MainReturnTypesAndUnsupportedReturnExpressions) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;
    const basl_diagnostic_t *diagnostic;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "string_type.basl",
        "fn main() -> string { return 1; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    diagnostic = basl_diagnostic_list_get(&diagnostics, 0U);
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_STREQ(
        basl_string_c_str(&diagnostic->message),
        "main entrypoint must declare return type i32"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "void_return_value.basl",
        "fn helper() -> void { return 1; }"
        "fn main() -> i32 { helper(); return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "void functions cannot return a value"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "bool_return.basl",
        "fn main() -> i32 { return true; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "main entrypoint must return an i32 expression"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "nil_return.basl",
        "fn main() -> i32 { return nil; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "main entrypoint must return an i32 expression"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "float_return.basl",
        "fn main() -> i32 { return 3.14; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "main entrypoint must return an i32 expression"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsMixedI32AndF64Arithmetic) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "mixed_numeric.basl",
        "fn main() -> i32 { f64 x = 1.0 + 2; return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "'+' requires matching i32, f64, or string operands"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsInvalidLocalsAndConditions) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "uninit.basl",
        "fn main() -> i32 { i32 x; return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "variables must be initialized at declaration"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "unknown.basl",
        "fn main() -> i32 { x = 1; return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "unknown local variable"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "condition.basl",
        "fn main() -> i32 { if (1) { return 1; } return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "if condition must be bool"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "type.basl",
        "fn main() -> i32 { bool ready = 1; return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "initializer type does not match local variable type"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsInvalidFunctionSignaturesAndCalls) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "main_params.basl",
        "fn main(i32 value) -> i32 { return value; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    EXPECT_EQ(error.location.source_id, source_id);
    EXPECT_EQ(error.location.line, 1U);
    EXPECT_EQ(error.location.column, 4U);
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "main entrypoint must not declare parameters"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "arg_count.basl",
        "fn add(i32 left, i32 right) -> i32 { return left + right; }"
        "fn main() -> i32 { return add(1); }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "call argument count does not match function signature"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "arg_type.basl",
        "fn truthy(bool ready) -> i32 {"
        "    if (ready) { return 1; }"
        "    return 0;"
        "}"
        "fn main() -> i32 { return truthy(1); }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "call argument type does not match parameter type"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "unknown_call.basl",
        "fn main() -> i32 { return missing(1); }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "unknown function"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsInvalidLogicalOperandsAndLoopControlOutsideLoops) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "logical_type.basl",
        "fn main() -> i32 { if (1 && true) { return 1; } return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "logical '&&' requires bool operands"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "break_outside.basl",
        "fn main() -> i32 { break; return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "'break' is only valid inside a loop"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "continue_outside.basl",
        "fn main() -> i32 { continue; return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "'continue' is only valid inside a loop"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RequiresGuaranteedReturnAndPreservesNestedScopeShadowing) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "missing_return.basl",
        "fn choose(bool ready) -> i32 {"
        "    if (ready) {"
        "        return 1;"
        "    }"
        "}"
        "fn main() -> i32 {"
        "    return choose(true);"
        "}",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "function must return a value on all paths"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "missing_main_return.basl",
        "fn main() -> i32 {"
        "    if (true) {"
        "        return 1;"
        "    }"
        "}",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "main entrypoint must return an i32 value on all paths"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "void_missing_value.basl",
        "fn helper(bool ready) -> void {"
        "    if (ready) {"
        "        return;"
        "    }"
        "}"
        "fn main() -> i32 { helper(false); return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(basl_diagnostic_list_count(&diagnostics), 0U);
    basl_object_release(&function);
    function = nullptr;

    basl_diagnostic_list_clear(&diagnostics);
    EXPECT_EQ(
        CompileAndRun(
            "fn choose(bool ready) -> i32 {"
            "    if (ready) {"
                "        return 1;"
            "    } else {"
                "        return 2;"
            "    }"
            "}"
            "fn main() -> i32 {"
            "    return choose(false);"
            "}"
        ),
        2
    );

    EXPECT_EQ(
        CompileAndRun(
            "fn main() -> i32 {"
            "    i32 value = 7;"
            "    {"
            "        i32 value = 2;"
            "    }"
            "    return value;"
            "}"
        ),
        7
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsVoidInNonReturnTypePositions) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "void_local.basl",
        "fn helper() -> void {}"
        "fn main() -> i32 { void x = helper(); return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "local variables cannot use type void"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "void_param.basl",
        "fn bad(void value) -> i32 { return 0; }"
        "fn main() -> i32 { return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "function parameters cannot use type void"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "void_global.basl",
        "void state = nil;"
        "fn main() -> i32 { return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "global variables cannot use type void"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "void_field.basl",
        "class Bad { void value; }"
        "fn main() -> i32 { return 0; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "class fields cannot use type void"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsDeferWithoutCallExpression) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "bad_defer.basl",
        "fn main() -> i32 {"
        "    i32 value = 1;"
        "    defer value;"
        "    return 0;"
        "}",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "defer requires a call expression"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, ReportsSyntaxErrorsForUnsupportedShape) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_source_id_t source_id;
    const basl_diagnostic_t *diagnostic;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        &registry,
        "bad.basl",
        "fn helper() -> i32 { return 1; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    diagnostic = basl_diagnostic_list_get(&diagnostics, 0U);
    ASSERT_NE(diagnostic, nullptr);
    EXPECT_STREQ(
        basl_string_c_str(&diagnostic->message),
        "expected top-level function 'main'"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "fstring.basl",
        "fn main() -> i32 { return f\"hi\"; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "f-strings are not yet supported"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}
