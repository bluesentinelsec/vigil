#include "vigil_test.h"


#include "vigil/vigil.h"
#include "internal/vigil_binding.h"

TEST(VigilBindingTest, FunctionTableTracksDeclarationsAndRejectsDuplicates) {
    vigil_runtime_t *runtime = NULL;
    vigil_binding_function_table_t table;
    vigil_binding_function_t function = {0};
    vigil_binding_function_t duplicate = {0};
    vigil_error_t error = {0};
    size_t index = 0U;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_binding_function_table_init(&table, runtime);

    vigil_binding_function_init(&function);
    function.name = "main";
    function.name_length = 4U;
    function.return_type = vigil_binding_type_primitive(VIGIL_TYPE_I32);

    ASSERT_EQ(
        vigil_binding_function_table_append(&table, &function, &index, &error),
        VIGIL_STATUS_OK
    );
    EXPECT_EQ(index, 0U);
    EXPECT_EQ(table.count, 1U);
    EXPECT_TRUE(
        vigil_binding_function_table_find(&table, "main", 4U, &index, NULL)
    );
    EXPECT_EQ(index, 0U);

    vigil_binding_function_init(&duplicate);
    duplicate.name = "main";
    duplicate.name_length = 4U;
    duplicate.return_type = vigil_binding_type_primitive(VIGIL_TYPE_I32);
    EXPECT_EQ(
        vigil_binding_function_table_append(&table, &duplicate, NULL, &error),
        VIGIL_STATUS_INVALID_ARGUMENT
    );

    vigil_binding_function_free(runtime, &duplicate);
    vigil_binding_function_table_free(&table);
    vigil_runtime_close(&runtime);
}

TEST(VigilBindingTest, FunctionParametersRejectDuplicates) {
    vigil_runtime_t *runtime = NULL;
    vigil_binding_function_t function = {0};
    vigil_error_t error = {0};
    vigil_source_span_t span = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_binding_function_init(&function);
    function.name = "sum";
    function.name_length = 3U;

    ASSERT_EQ(
        vigil_binding_function_add_param(
            runtime,
            &function,
            "left",
            4U,
            span,
            vigil_binding_type_primitive(VIGIL_TYPE_I32),
            &error
        ),
        VIGIL_STATUS_OK
    );
    EXPECT_EQ(
        vigil_binding_function_add_param(
            runtime,
            &function,
            "left",
            4U,
            span,
            vigil_binding_type_primitive(VIGIL_TYPE_I32),
            &error
        ),
        VIGIL_STATUS_INVALID_ARGUMENT
    );

    vigil_binding_function_free(runtime, &function);
    vigil_runtime_close(&runtime);
}

TEST(VigilBindingTest, ScopeStackTracksDepthShadowingAndPoppedLocals) {
    vigil_runtime_t *runtime = NULL;
    vigil_binding_scope_stack_t stack;
    vigil_error_t error = {0};
    size_t index = 0U;
    size_t popped_count = 0U;
    vigil_binding_type_t local_type = vigil_binding_type_invalid();

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_binding_scope_stack_init(&stack, runtime);

    ASSERT_EQ(
        vigil_binding_scope_stack_declare_local(
            &stack,
            "value",
            5U,
            vigil_binding_type_primitive(VIGIL_TYPE_I32),
            0,
            &index,
            &error
        ),
        VIGIL_STATUS_OK
    );
    EXPECT_EQ(index, 0U);
    EXPECT_EQ(vigil_binding_scope_stack_count(&stack), 1U);

    vigil_binding_scope_stack_begin_scope(&stack);
    ASSERT_EQ(
        vigil_binding_scope_stack_declare_local(
            &stack,
            "value",
            5U,
            vigil_binding_type_primitive(VIGIL_TYPE_BOOL),
            1,
            &index,
            &error
        ),
        VIGIL_STATUS_OK
    );
    EXPECT_EQ(vigil_binding_scope_stack_depth(&stack), 1U);
    EXPECT_EQ(vigil_binding_scope_stack_count_above_depth(&stack, 0U), 1U);
    EXPECT_TRUE(
        vigil_binding_scope_stack_find_local(
            &stack,
            "value",
            5U,
            &index,
            &local_type
        )
    );
    EXPECT_TRUE(
        vigil_binding_type_equal(local_type, vigil_binding_type_primitive(VIGIL_TYPE_BOOL))
    );
    ASSERT_NE(vigil_binding_scope_stack_local_at(&stack, index), NULL);
    EXPECT_TRUE(vigil_binding_scope_stack_local_at(&stack, index)->is_const);

    vigil_binding_scope_stack_end_scope(&stack, &popped_count);
    EXPECT_EQ(popped_count, 1U);
    EXPECT_TRUE(
        vigil_binding_scope_stack_find_local(
            &stack,
            "value",
            5U,
            &index,
            &local_type
        )
    );
    EXPECT_TRUE(
        vigil_binding_type_equal(local_type, vigil_binding_type_primitive(VIGIL_TYPE_I32))
    );

    EXPECT_EQ(
        vigil_binding_scope_stack_declare_local(
            &stack,
            "value",
            5U,
            vigil_binding_type_primitive(VIGIL_TYPE_BOOL),
            0,
            &index,
            &error
        ),
        VIGIL_STATUS_INVALID_ARGUMENT
    );

    vigil_binding_scope_stack_free(&stack);
    vigil_runtime_close(&runtime);
}

void register_binding_tests(void) {
    REGISTER_TEST(VigilBindingTest, FunctionTableTracksDeclarationsAndRejectsDuplicates);
    REGISTER_TEST(VigilBindingTest, FunctionParametersRejectDuplicates);
    REGISTER_TEST(VigilBindingTest, ScopeStackTracksDepthShadowingAndPoppedLocals);
}
