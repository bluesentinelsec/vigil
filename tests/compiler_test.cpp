#include <gtest/gtest.h>

extern "C" {
#include "basl/basl.h"
}

namespace {

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
        "float expressions are not yet supported"
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
