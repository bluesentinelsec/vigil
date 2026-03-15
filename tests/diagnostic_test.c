#include "basl_test.h"

#include <stdlib.h>
#include <string.h>


#include "basl/basl.h"

struct AllocatorStats {
    int allocate_calls;
    int reallocate_calls;
    int deallocate_calls;
};

static void *CountedAllocate(void *user_data, size_t size) {
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->allocate_calls += 1;
    return calloc(1U, size);
}

static void *CountedReallocate(void *user_data, void *memory, size_t size) {
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->reallocate_calls += 1;
    return realloc(memory, size);
}

static void CountedDeallocate(void *user_data, void *memory) {
    struct AllocatorStats *stats = (struct AllocatorStats *)(user_data);

    stats->deallocate_calls += 1;
    free(memory);
}


TEST(BaslDiagnosticTest, InitStartsEmpty) {
    basl_diagnostic_list_t list;

    basl_diagnostic_list_init(&list, NULL);

    EXPECT_EQ(list.runtime, NULL);
    EXPECT_EQ(list.items, NULL);
    EXPECT_EQ(list.count, 0U);
    EXPECT_EQ(list.capacity, 0U);
}

TEST(BaslDiagnosticTest, AppendCopiesMessageAndSpan) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_diagnostic_list_t list;
    basl_source_span_t span = {7U, 2U, 5U};
    const basl_diagnostic_t *diagnostic;
    char message[] = "unexpected token";

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_diagnostic_list_init(&list, runtime);

    ASSERT_EQ(
        basl_diagnostic_list_append(
            &list,
            BASL_DIAGNOSTIC_ERROR,
            span,
            message,
            strlen(message),
            &error
        ),
        BASL_STATUS_OK
    );

    message[0] = 'X';
    diagnostic = basl_diagnostic_list_get(&list, 0U);
    ASSERT_NE(diagnostic, NULL);
    EXPECT_EQ(diagnostic->severity, BASL_DIAGNOSTIC_ERROR);
    EXPECT_EQ(diagnostic->span.source_id, 7U);
    EXPECT_EQ(diagnostic->span.start_offset, 2U);
    EXPECT_EQ(diagnostic->span.end_offset, 5U);
    EXPECT_STREQ(basl_string_c_str(&diagnostic->message), "unexpected token");

    basl_diagnostic_list_free(&list);
    basl_runtime_close(&runtime);
}

TEST(BaslDiagnosticTest, ClearDropsItemsButKeepsListUsable) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_diagnostic_list_t list;
    size_t capacity;
    basl_source_span_t warning_span = {1U, 0U, 1U};
    basl_source_span_t note_span = {1U, 1U, 2U};

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_diagnostic_list_init(&list, runtime);

    ASSERT_EQ(
        basl_diagnostic_list_append_cstr(
            &list,
            BASL_DIAGNOSTIC_WARNING,
            warning_span,
            "warning",
            &error
        ),
        BASL_STATUS_OK
    );
    capacity = list.capacity;

    basl_diagnostic_list_clear(&list);

    EXPECT_EQ(basl_diagnostic_list_count(&list), 0U);
    EXPECT_EQ(list.capacity, capacity);
    ASSERT_EQ(
        basl_diagnostic_list_append_cstr(
            &list,
            BASL_DIAGNOSTIC_NOTE,
            note_span,
            "note",
            &error
        ),
        BASL_STATUS_OK
    );
    EXPECT_EQ(basl_diagnostic_list_count(&list), 1U);

    basl_diagnostic_list_free(&list);
    basl_runtime_close(&runtime);
}

TEST(BaslDiagnosticTest, RejectsMissingRuntime) {
    basl_diagnostic_list_t list;
    basl_error_t error = {0};
    basl_source_span_t span = {0U, 0U, 0U};

    basl_diagnostic_list_init(&list, NULL);

    EXPECT_EQ(
        basl_diagnostic_list_append_cstr(
            &list,
            BASL_DIAGNOSTIC_ERROR,
            span,
            "bad",
            &error
        ),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "diagnostic list runtime must not be null"), 0);
}

TEST(BaslDiagnosticTest, UsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_diagnostic_list_t list;
    struct AllocatorStats stats = {0};
    basl_allocator_t allocator = {0};
    basl_runtime_options_t options = {0};
    basl_source_span_t error_span = {1U, 0U, 1U};
    basl_source_span_t note_span = {1U, 2U, 3U};
    basl_source_span_t warning_span = {1U, 4U, 5U};

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    basl_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(basl_runtime_open(&runtime, &options, &error), BASL_STATUS_OK);
    basl_diagnostic_list_init(&list, runtime);

    ASSERT_EQ(
        basl_diagnostic_list_append_cstr(
            &list,
            BASL_DIAGNOSTIC_ERROR,
            error_span,
            "error",
            &error
        ),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_diagnostic_list_append_cstr(
            &list,
            BASL_DIAGNOSTIC_NOTE,
            note_span,
            "note",
            &error
        ),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_diagnostic_list_append_cstr(
            &list,
            BASL_DIAGNOSTIC_WARNING,
            warning_span,
            "warning",
            &error
        ),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_diagnostic_list_append_cstr(
            &list,
            BASL_DIAGNOSTIC_NOTE,
            note_span,
            "note 2",
            &error
        ),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_diagnostic_list_append_cstr(
            &list,
            BASL_DIAGNOSTIC_ERROR,
            error_span,
            "error 2",
            &error
        ),
        BASL_STATUS_OK
    );

    EXPECT_GE(stats.allocate_calls, 3);

    basl_diagnostic_list_free(&list);
    EXPECT_GE(stats.deallocate_calls, 3);
    basl_runtime_close(&runtime);
}

TEST(BaslDiagnosticTest, FreeResetsWholeList) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_diagnostic_list_t list;
    basl_source_span_t span = {1U, 0U, 1U};

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_diagnostic_list_init(&list, runtime);
    ASSERT_EQ(
        basl_diagnostic_list_append_cstr(
            &list,
            BASL_DIAGNOSTIC_ERROR,
            span,
            "error",
            &error
        ),
        BASL_STATUS_OK
    );

    basl_diagnostic_list_free(&list);

    EXPECT_EQ(list.runtime, NULL);
    EXPECT_EQ(list.items, NULL);
    EXPECT_EQ(list.count, 0U);
    EXPECT_EQ(list.capacity, 0U);
    basl_runtime_close(&runtime);
}

TEST(BaslDiagnosticTest, SeverityNamesAreStable) {
    EXPECT_STREQ(basl_diagnostic_severity_name(BASL_DIAGNOSTIC_ERROR), "error");
    EXPECT_STREQ(basl_diagnostic_severity_name(BASL_DIAGNOSTIC_WARNING), "warning");
    EXPECT_STREQ(basl_diagnostic_severity_name(BASL_DIAGNOSTIC_NOTE), "note");
}

TEST(BaslDiagnosticTest, FormatIncludesPathLineColumnSeverityAndMessage) {
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};
    basl_source_registry_t registry;
    basl_source_id_t source_id = 0U;
    basl_diagnostic_list_t list;
    basl_string_t output;
    basl_source_span_t span;

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    basl_diagnostic_list_init(&list, runtime);
    basl_string_init(&output, runtime);

    ASSERT_EQ(
        basl_source_registry_register_cstr(
            &registry,
            "main.basl",
            "fn main() -> i32 {\n    return 1;\n}\n",
            &source_id,
            &error
        ),
        BASL_STATUS_OK
    );

    span.source_id = source_id;
    span.start_offset = 23U;
    span.end_offset = 29U;
    ASSERT_EQ(
        basl_diagnostic_list_append_cstr(
            &list,
            BASL_DIAGNOSTIC_ERROR,
            span,
            "unexpected token",
            &error
        ),
        BASL_STATUS_OK
    );

    ASSERT_EQ(
        basl_diagnostic_format(
            &registry,
            basl_diagnostic_list_get(&list, 0U),
            &output,
            &error
        ),
        BASL_STATUS_OK
    );
    EXPECT_STREQ(
        basl_string_c_str(&output),
        "main.basl:2:5: error: unexpected token"
    );

    basl_string_free(&output);
    basl_diagnostic_list_free(&list);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

void register_diagnostic_tests(void) {
    REGISTER_TEST(BaslDiagnosticTest, InitStartsEmpty);
    REGISTER_TEST(BaslDiagnosticTest, AppendCopiesMessageAndSpan);
    REGISTER_TEST(BaslDiagnosticTest, ClearDropsItemsButKeepsListUsable);
    REGISTER_TEST(BaslDiagnosticTest, RejectsMissingRuntime);
    REGISTER_TEST(BaslDiagnosticTest, UsesRuntimeAllocatorHooks);
    REGISTER_TEST(BaslDiagnosticTest, FreeResetsWholeList);
    REGISTER_TEST(BaslDiagnosticTest, SeverityNamesAreStable);
    REGISTER_TEST(BaslDiagnosticTest, FormatIncludesPathLineColumnSeverityAndMessage);
}
