#include <gtest/gtest.h>

extern "C" {
#include "basl/basl.h"
#include "internal/basl_binding.h"
}

TEST(BaslBindingTest, FunctionTableTracksDeclarationsAndRejectsDuplicates) {
    basl_runtime_t *runtime = nullptr;
    basl_binding_function_table_t table;
    basl_binding_function_t function = {};
    basl_binding_function_t duplicate = {};
    basl_error_t error = {};
    size_t index = 0U;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_binding_function_table_init(&table, runtime);

    basl_binding_function_init(&function);
    function.name = "main";
    function.name_length = 4U;
    function.return_type = basl_binding_type_primitive(BASL_TYPE_I32);

    ASSERT_EQ(
        basl_binding_function_table_append(&table, &function, &index, &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(index, 0U);
    EXPECT_EQ(table.count, 1U);
    EXPECT_TRUE(
        basl_binding_function_table_find(&table, "main", 4U, &index, nullptr)
    );
    EXPECT_EQ(index, 0U);

    basl_binding_function_init(&duplicate);
    duplicate.name = "main";
    duplicate.name_length = 4U;
    duplicate.return_type = basl_binding_type_primitive(BASL_TYPE_I32);
    EXPECT_EQ(
        basl_binding_function_table_append(&table, &duplicate, nullptr, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );

    basl_binding_function_free(runtime, &duplicate);
    basl_binding_function_table_free(&table);
    basl_runtime_close(&runtime);
}

TEST(BaslBindingTest, FunctionParametersRejectDuplicates) {
    basl_runtime_t *runtime = nullptr;
    basl_binding_function_t function = {};
    basl_error_t error = {};
    basl_source_span_t span = {};

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_binding_function_init(&function);
    function.name = "sum";
    function.name_length = 3U;

    ASSERT_EQ(
        basl_binding_function_add_param(
            runtime,
            &function,
            "left",
            4U,
            span,
            basl_binding_type_primitive(BASL_TYPE_I32),
            &error
        ),
        BASL_STATUS_OK
    );
    EXPECT_EQ(
        basl_binding_function_add_param(
            runtime,
            &function,
            "left",
            4U,
            span,
            basl_binding_type_primitive(BASL_TYPE_I32),
            &error
        ),
        BASL_STATUS_INVALID_ARGUMENT
    );

    basl_binding_function_free(runtime, &function);
    basl_runtime_close(&runtime);
}

TEST(BaslBindingTest, ScopeStackTracksDepthShadowingAndPoppedLocals) {
    basl_runtime_t *runtime = nullptr;
    basl_binding_scope_stack_t stack;
    basl_error_t error = {};
    size_t index = 0U;
    size_t popped_count = 0U;
    basl_binding_type_t local_type = basl_binding_type_invalid();

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_binding_scope_stack_init(&stack, runtime);

    ASSERT_EQ(
        basl_binding_scope_stack_declare_local(
            &stack,
            "value",
            5U,
            basl_binding_type_primitive(BASL_TYPE_I32),
            0,
            &index,
            &error
        ),
        BASL_STATUS_OK
    );
    EXPECT_EQ(index, 0U);
    EXPECT_EQ(basl_binding_scope_stack_count(&stack), 1U);

    basl_binding_scope_stack_begin_scope(&stack);
    ASSERT_EQ(
        basl_binding_scope_stack_declare_local(
            &stack,
            "value",
            5U,
            basl_binding_type_primitive(BASL_TYPE_BOOL),
            1,
            &index,
            &error
        ),
        BASL_STATUS_OK
    );
    EXPECT_EQ(basl_binding_scope_stack_depth(&stack), 1U);
    EXPECT_EQ(basl_binding_scope_stack_count_above_depth(&stack, 0U), 1U);
    EXPECT_TRUE(
        basl_binding_scope_stack_find_local(
            &stack,
            "value",
            5U,
            &index,
            &local_type
        )
    );
    EXPECT_TRUE(
        basl_binding_type_equal(local_type, basl_binding_type_primitive(BASL_TYPE_BOOL))
    );
    ASSERT_NE(basl_binding_scope_stack_local_at(&stack, index), nullptr);
    EXPECT_TRUE(basl_binding_scope_stack_local_at(&stack, index)->is_const);

    basl_binding_scope_stack_end_scope(&stack, &popped_count);
    EXPECT_EQ(popped_count, 1U);
    EXPECT_TRUE(
        basl_binding_scope_stack_find_local(
            &stack,
            "value",
            5U,
            &index,
            &local_type
        )
    );
    EXPECT_TRUE(
        basl_binding_type_equal(local_type, basl_binding_type_primitive(BASL_TYPE_I32))
    );

    EXPECT_EQ(
        basl_binding_scope_stack_declare_local(
            &stack,
            "value",
            5U,
            basl_binding_type_primitive(BASL_TYPE_BOOL),
            0,
            &index,
            &error
        ),
        BASL_STATUS_INVALID_ARGUMENT
    );

    basl_binding_scope_stack_free(&stack);
    basl_runtime_close(&runtime);
}
