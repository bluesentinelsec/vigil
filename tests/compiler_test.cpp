#include <gtest/gtest.h>

#include <cstring>

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

}  // namespace

TEST(BaslCompilerTest, CompilesAndExecutesIntegerMain) {
    basl_runtime_t *runtime = nullptr;
    basl_vm_t *vm = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_object_t *function = nullptr;
    basl_value_t result;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    ASSERT_EQ(basl_vm_open(&vm, runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    source_id = RegisterSource(
        &registry,
        "main.basl",
        "fn main() -> i32 { return 42; }",
        &error
    );

    ASSERT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_OK
    );
    ASSERT_NE(function, nullptr);
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 0U);

    basl_value_init_nil(&result);
    ASSERT_EQ(
        basl_vm_execute_function(vm, function, &result, &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(basl_value_kind(&result), BASL_VALUE_INT);
    EXPECT_EQ(basl_value_as_int(&result), 42);

    basl_value_release(&result);
    basl_object_release(&function);
    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_vm_close(&vm);
    basl_runtime_close(&runtime);
}

TEST(BaslCompilerTest, RejectsNonI32MainReturnTypesAndValues) {
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
        "string.basl",
        "fn main() -> string { return \"hello\"; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    EXPECT_EQ(function, nullptr);
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
        "bool.basl",
        "fn main() -> bool { return true; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    EXPECT_EQ(function, nullptr);
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "main entrypoint must declare return type i32"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "nil.basl",
        "fn main() -> i32 { return nil; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    EXPECT_EQ(function, nullptr);
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "main entrypoint must return an integer literal"
    );

    basl_diagnostic_list_clear(&diagnostics);
    source_id = RegisterSource(
        &registry,
        "float.basl",
        "fn main() -> i32 { return 3.14; }",
        &error
    );
    EXPECT_EQ(
        basl_compile_source(&registry, source_id, &function, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    EXPECT_EQ(function, nullptr);
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    EXPECT_STREQ(
        basl_string_c_str(&basl_diagnostic_list_get(&diagnostics, 0U)->message),
        "main entrypoint must return an integer literal"
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
    EXPECT_EQ(function, nullptr);
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
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
        "main entrypoint must return an integer literal"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}
