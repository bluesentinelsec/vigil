#include "basl_test.h"


#include "basl/basl.h"

static basl_source_id_t RegisterSource(int *basl_test_failed_,
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


TEST(BaslCheckerTest, ValidatesWellTypedProgramWithoutDiagnostics) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_source_id_t source_id;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        basl_test_failed_, &registry,
        "main.basl",
        "fn add(i32 left, i32 right) -> i32 {"
        "    return left + right;"
        "}"
        "fn main() -> i32 {"
        "    i32 total = add(3, 4);"
        "    while (total > 4) {"
        "        total = total - 1;"
        "        if (total == 5) {"
        "            break;"
        "        }"
        "    }"
        "    return total;"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_check_source(&registry, source_id, &diagnostics, &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(basl_diagnostic_list_count(&diagnostics), 0U);

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCheckerTest, ReportsSemanticErrorsWithoutProducingEntrypoint) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_source_id_t source_id;
    const basl_diagnostic_t *diagnostic;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        basl_test_failed_, &registry,
        "bad.basl",
        "fn is_ready() -> bool {"
        "    return true;"
        "}"
        "fn main() -> i32 {"
        "    i32 value = 1;"
        "    value = is_ready();"
        "    return value;"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_check_source(&registry, source_id, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    diagnostic = basl_diagnostic_list_get(&diagnostics, 0U);
    ASSERT_NE(diagnostic, NULL);
    EXPECT_STREQ(
        basl_string_c_str(&diagnostic->message),
        "assigned expression type does not match local variable type"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCheckerTest, ReportsMissingReturnOnSomePaths) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_source_id_t source_id;
    const basl_diagnostic_t *diagnostic;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);

    source_id = RegisterSource(
        basl_test_failed_, &registry,
        "missing_return.basl",
        "fn choose(bool ready) -> i32 {"
        "    if (ready) {"
        "        return 1;"
        "    }"
        "}"
        "fn main() -> i32 {"
        "    return choose(false);"
        "}",
        &error
    );

    EXPECT_EQ(
        basl_check_source(&registry, source_id, &diagnostics, &error),
        BASL_STATUS_SYNTAX_ERROR
    );
    ASSERT_EQ(basl_diagnostic_list_count(&diagnostics), 1U);
    diagnostic = basl_diagnostic_list_get(&diagnostics, 0U);
    ASSERT_NE(diagnostic, NULL);
    EXPECT_STREQ(
        basl_string_c_str(&diagnostic->message),
        "function must return a value on all paths"
    );

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslCheckerTest, ValidatesArguments) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_diagnostic_list_t diagnostics;
    basl_source_id_t source_id = 0U;

    ASSERT_EQ(
        basl_check_source(NULL, source_id, &diagnostics, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_STREQ(basl_error_message(&error), "source registry must not be null");

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&diagnostics, runtime);
    source_id = RegisterSource(
        basl_test_failed_, &registry,
        "main.basl",
        "fn main() -> i32 { return 0; }",
        &error
    );

    EXPECT_EQ(
        basl_check_source(&registry, source_id, NULL, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_STREQ(basl_error_message(&error), "diagnostic list must not be null");

    basl_diagnostic_list_free(&diagnostics);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}
