#include "basl_test.h"
#include "basl/doc_registry.h"

TEST(DocRegistryTest, LookupBuiltin) {
    const basl_doc_entry_t *entry = basl_doc_lookup("len");
    ASSERT_NE(entry, NULL);
    EXPECT_STREQ(entry->name, "len");
    ASSERT_NE(entry->signature, NULL);
    ASSERT_NE(entry->summary, NULL);
}

TEST(DocRegistryTest, LookupModule) {
    const basl_doc_entry_t *entry = basl_doc_lookup("math");
    ASSERT_NE(entry, NULL);
    EXPECT_STREQ(entry->name, "math");
    EXPECT_EQ(entry->signature, NULL);  /* Modules have no signature */
    ASSERT_NE(entry->summary, NULL);
}

TEST(DocRegistryTest, LookupQualified) {
    const basl_doc_entry_t *entry = basl_doc_lookup("math.sqrt");
    ASSERT_NE(entry, NULL);
    EXPECT_STREQ(entry->name, "math.sqrt");
    ASSERT_NE(entry->signature, NULL);
}

TEST(DocRegistryTest, LookupNotFound) {
    const basl_doc_entry_t *entry = basl_doc_lookup("nonexistent");
    EXPECT_EQ(entry, NULL);
}

TEST(DocRegistryTest, ListModules) {
    size_t count = 0;
    const char **modules = basl_doc_list_modules(&count);
    ASSERT_NE(modules, NULL);
    EXPECT_GT(count, 0u);
}

TEST(DocRegistryTest, ListModuleContents) {
    size_t count = 0;
    const basl_doc_entry_t *entries = basl_doc_list_module("math", &count);
    ASSERT_NE(entries, NULL);
    EXPECT_GT(count, 1u);  /* Module entry + functions */
}

TEST(DocRegistryTest, RenderEntry) {
    const basl_doc_entry_t *entry = basl_doc_lookup("len");
    char *text = NULL;
    size_t len = 0;
    basl_error_t error = {0};

    ASSERT_EQ(basl_doc_entry_render(entry, &text, &len, &error), BASL_STATUS_OK);
    ASSERT_NE(text, NULL);
    EXPECT_GT(len, 0u);
    free(text);
}

void register_doc_registry_tests(void) {
    REGISTER_TEST(DocRegistryTest, LookupBuiltin);
    REGISTER_TEST(DocRegistryTest, LookupModule);
    REGISTER_TEST(DocRegistryTest, LookupQualified);
    REGISTER_TEST(DocRegistryTest, LookupNotFound);
    REGISTER_TEST(DocRegistryTest, ListModules);
    REGISTER_TEST(DocRegistryTest, ListModuleContents);
    REGISTER_TEST(DocRegistryTest, RenderEntry);
}
