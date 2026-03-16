#include "basl_test.h"
#include "basl/semantic.h"
#include "basl/runtime.h"
#include "basl/source.h"

/* ── Type utilities ───────────────────────────────────────── */

TEST(SemanticTest, TypeInvalid) {
    basl_semantic_type_t type = basl_semantic_type_invalid();
    ASSERT_TRUE(!basl_semantic_type_is_valid(type));
}

TEST(SemanticTest, TypePrimitive) {
    basl_semantic_type_t i32_type = basl_semantic_type_primitive(BASL_TYPE_I32);
    basl_semantic_type_t str_type = basl_semantic_type_primitive(BASL_TYPE_STRING);

    ASSERT_TRUE(basl_semantic_type_is_valid(i32_type));
    ASSERT_TRUE(basl_semantic_type_is_valid(str_type));
    ASSERT_EQ(i32_type.kind, BASL_TYPE_I32);
    ASSERT_EQ(str_type.kind, BASL_TYPE_STRING);
}

TEST(SemanticTest, TypeEqual) {
    basl_semantic_type_t a = basl_semantic_type_primitive(BASL_TYPE_I32);
    basl_semantic_type_t b = basl_semantic_type_primitive(BASL_TYPE_I32);
    basl_semantic_type_t c = basl_semantic_type_primitive(BASL_TYPE_STRING);

    ASSERT_TRUE(basl_semantic_type_equal(a, b));
    ASSERT_TRUE(!basl_semantic_type_equal(a, c));
}

/* ── File creation/destruction ────────────────────────────── */

TEST(SemanticTest, FileCreateDestroy) {
    basl_runtime_t *runtime = NULL;
    basl_semantic_file_t *file = NULL;
    basl_error_t error = {0};

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);

    ASSERT_EQ(basl_semantic_file_create(&file, runtime, 0, &error), BASL_STATUS_OK);
    ASSERT_NE(file, NULL);
    ASSERT_EQ(file->source_id, 0);

    basl_semantic_file_destroy(&file);
    ASSERT_EQ(file, NULL);

    basl_runtime_close(&runtime);
}

TEST(SemanticTest, FileNodeAtEmpty) {
    basl_runtime_t *runtime = NULL;
    basl_semantic_file_t *file = NULL;
    basl_error_t error = {0};

    basl_runtime_open(&runtime, NULL, &error);
    basl_semantic_file_create(&file, runtime, 0, &error);

    /* Empty file should return NULL for any position */
    ASSERT_EQ(basl_semantic_file_node_at(file, 0), NULL);
    ASSERT_EQ(basl_semantic_file_node_at(file, 100), NULL);

    basl_semantic_file_destroy(&file);
    basl_runtime_close(&runtime);
}

/* ── Index creation/destruction ───────────────────────────── */

TEST(SemanticTest, IndexCreateDestroy) {
    basl_runtime_t *runtime = NULL;
    basl_source_registry_t registry;
    basl_semantic_index_t *index = NULL;
    basl_error_t error = {0};

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);

    basl_source_registry_init(&registry, runtime);

    ASSERT_EQ(basl_semantic_index_create(&index, runtime, &registry, &error), BASL_STATUS_OK);
    ASSERT_NE(index, NULL);

    basl_semantic_index_destroy(&index);
    ASSERT_EQ(index, NULL);

    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(SemanticTest, IndexGetFileNotFound) {
    basl_runtime_t *runtime = NULL;
    basl_source_registry_t registry;
    basl_semantic_index_t *index = NULL;
    basl_error_t error = {0};

    basl_runtime_open(&runtime, NULL, &error);
    basl_source_registry_init(&registry, runtime);
    basl_semantic_index_create(&index, runtime, &registry, &error);

    /* Non-existent file should return NULL */
    ASSERT_EQ(basl_semantic_index_get_file(index, 0), NULL);
    ASSERT_EQ(basl_semantic_index_get_file(index, 999), NULL);

    basl_semantic_index_destroy(&index);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

/* ── Type to string ───────────────────────────────────────── */

TEST(SemanticTest, TypeToString) {
    basl_semantic_type_t i32_type = basl_semantic_type_primitive(BASL_TYPE_I32);
    char *str = basl_semantic_type_to_string(NULL, i32_type);

    ASSERT_NE(str, NULL);
    EXPECT_STREQ(str, "i32");

    free(str);
}

TEST(SemanticTest, TypeToStringInvalid) {
    basl_semantic_type_t invalid = basl_semantic_type_invalid();
    char *str = basl_semantic_type_to_string(NULL, invalid);

    ASSERT_NE(str, NULL);
    EXPECT_STREQ(str, "<invalid>");

    free(str);
}

/* ── Index analysis ───────────────────────────────────────── */

TEST(SemanticTest, AnalyzeValidSource) {
    basl_runtime_t *runtime = NULL;
    basl_source_registry_t registry;
    basl_semantic_index_t *index = NULL;
    basl_source_id_t source_id = 0;
    basl_error_t error = {0};
    const basl_semantic_file_t *file;

    basl_runtime_open(&runtime, NULL, &error);
    basl_source_registry_init(&registry, runtime);

    /* Register a simple valid source. */
    basl_source_registry_register_cstr(
        &registry, "test.basl",
        "fn main() -> i32 { return 0; }",
        &source_id, &error
    );

    basl_semantic_index_create(&index, runtime, &registry, &error);

    /* Analyze should succeed. */
    ASSERT_EQ(basl_semantic_index_analyze(index, source_id, &error), BASL_STATUS_OK);

    /* File should be in index. */
    file = basl_semantic_index_get_file(index, source_id);
    ASSERT_NE(file, NULL);
    ASSERT_EQ(file->source_id, source_id);

    /* Should have no diagnostics for valid source. */
    ASSERT_EQ(basl_diagnostic_list_count(&file->diagnostics), 0);

    /* Index should have symbols (at least 'main'). */
    ASSERT_TRUE(basl_debug_symbol_table_count(&index->symbols) >= 1);

    basl_semantic_index_destroy(&index);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(SemanticTest, AnalyzeInvalidSource) {
    basl_runtime_t *runtime = NULL;
    basl_source_registry_t registry;
    basl_semantic_index_t *index = NULL;
    basl_source_id_t source_id = 0;
    basl_error_t error = {0};
    const basl_semantic_file_t *file;

    basl_runtime_open(&runtime, NULL, &error);
    basl_source_registry_init(&registry, runtime);

    /* Register source with syntax error. */
    basl_source_registry_register_cstr(
        &registry, "test.basl",
        "fn main() -> i32 { return }",  /* missing expression */
        &source_id, &error
    );

    basl_semantic_index_create(&index, runtime, &registry, &error);

    /* Analyze should still succeed (captures diagnostics). */
    ASSERT_EQ(basl_semantic_index_analyze(index, source_id, &error), BASL_STATUS_OK);

    /* File should be in index. */
    file = basl_semantic_index_get_file(index, source_id);
    ASSERT_NE(file, NULL);

    /* Should have diagnostics for invalid source. */
    ASSERT_TRUE(basl_diagnostic_list_count(&file->diagnostics) > 0);

    basl_semantic_index_destroy(&index);
    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

/* ── Test registration ────────────────────────────────────── */

void register_semantic_tests(void) {
    REGISTER_TEST(SemanticTest, TypeInvalid);
    REGISTER_TEST(SemanticTest, TypePrimitive);
    REGISTER_TEST(SemanticTest, TypeEqual);
    REGISTER_TEST(SemanticTest, FileCreateDestroy);
    REGISTER_TEST(SemanticTest, FileNodeAtEmpty);
    REGISTER_TEST(SemanticTest, IndexCreateDestroy);
    REGISTER_TEST(SemanticTest, IndexGetFileNotFound);
    REGISTER_TEST(SemanticTest, TypeToString);
    REGISTER_TEST(SemanticTest, TypeToStringInvalid);
    REGISTER_TEST(SemanticTest, AnalyzeValidSource);
    REGISTER_TEST(SemanticTest, AnalyzeInvalidSource);
}
