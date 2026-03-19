#include "vigil_test.h"
#include "vigil/doc_registry.h"
#include "vigil/stdlib.h"

#include <stdio.h>
#include <string.h>

TEST(DocRegistryTest, LookupBuiltin) {
    const vigil_doc_entry_t *entry = vigil_doc_lookup("len");
    ASSERT_NE(entry, NULL);
    EXPECT_STREQ(entry->name, "len");
    ASSERT_NE(entry->signature, NULL);
    ASSERT_NE(entry->summary, NULL);
}

TEST(DocRegistryTest, LookupModule) {
    const vigil_doc_entry_t *entry = vigil_doc_lookup("math");
    ASSERT_NE(entry, NULL);
    EXPECT_STREQ(entry->name, "math");
    EXPECT_EQ(entry->signature, NULL);  /* Modules have no signature */
    ASSERT_NE(entry->summary, NULL);
}

TEST(DocRegistryTest, LookupQualified) {
    const vigil_doc_entry_t *entry = vigil_doc_lookup("math.sqrt");
    ASSERT_NE(entry, NULL);
    EXPECT_STREQ(entry->name, "math.sqrt");
    ASSERT_NE(entry->signature, NULL);
}

TEST(DocRegistryTest, LookupNotFound) {
    const vigil_doc_entry_t *entry = vigil_doc_lookup("nonexistent");
    EXPECT_EQ(entry, NULL);
}

TEST(DocRegistryTest, ListModules) {
    size_t count = 0;
    const char **modules = vigil_doc_list_modules(&count);
    ASSERT_NE(modules, NULL);
    EXPECT_GT(count, 0u);
}

TEST(DocRegistryTest, ListModuleContents) {
    size_t count = 0;
    const vigil_doc_entry_t *entries = vigil_doc_list_module("math", &count);
    ASSERT_NE(entries, NULL);
    EXPECT_GT(count, 1u);  /* Module entry + functions */
}

TEST(DocRegistryTest, RenderEntry) {
    const vigil_doc_entry_t *entry = vigil_doc_lookup("len");
    char *text = NULL;
    size_t len = 0;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_doc_entry_render(entry, &text, &len, &error), VIGIL_STATUS_OK);
    ASSERT_NE(text, NULL);
    EXPECT_GT(len, 0u);
    free(text);
}

static int module_name_in_list(const char *name, const char **modules, size_t module_count) {
    size_t i;

    for (i = 0U; i < module_count; i += 1U) {
        if (strcmp(name, modules[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

TEST(DocRegistryTest, CoversAllStdlibModulesAndFunctions) {
    const vigil_native_module_t *modules[] = {
        &vigil_stdlib_args,
        &vigil_stdlib_atomic,
        &vigil_stdlib_compress,
        &vigil_stdlib_crypto,
        &vigil_stdlib_csv,
        &vigil_stdlib_ffi,
        &vigil_stdlib_fmt,
        &vigil_stdlib_fs,
        &vigil_stdlib_http,
        &vigil_stdlib_log,
        &vigil_stdlib_math,
        &vigil_stdlib_net,
        &vigil_stdlib_parse,
        &vigil_stdlib_random,
        &vigil_stdlib_readline,
        &vigil_stdlib_regex,
        &vigil_stdlib_test,
        &vigil_stdlib_thread,
        &vigil_stdlib_time,
        &vigil_stdlib_unsafe,
        &vigil_stdlib_url,
        &vigil_stdlib_yaml,
    };
    size_t module_count = 0U;
    const char **listed_modules = vigil_doc_list_modules(&module_count);
    size_t module_index;

    ASSERT_NE(listed_modules, NULL);

    for (module_index = 0U; module_index < sizeof(modules) / sizeof(modules[0]); module_index += 1U) {
        const vigil_native_module_t *module = modules[module_index];
        const vigil_doc_entry_t *module_entry = NULL;
        const vigil_doc_entry_t *module_entries = NULL;
        size_t entry_count = 0U;
        size_t function_index;

        ASSERT_NE(module, NULL);
        EXPECT_TRUE(module_name_in_list(module->name, listed_modules, module_count));

        module_entry = vigil_doc_lookup(module->name);
        ASSERT_NE(module_entry, NULL);
        EXPECT_EQ(module_entry->signature, NULL);

        module_entries = vigil_doc_list_module(module->name, &entry_count);
        ASSERT_NE(module_entries, NULL);
        EXPECT_GE(entry_count, module->function_count + 1U);

        for (function_index = 0U; function_index < module->function_count; function_index += 1U) {
            const vigil_native_module_function_t *function = &module->functions[function_index];
            const vigil_doc_entry_t *entry = NULL;
            char qualified_name[128];
            int written;

            written = snprintf(
                qualified_name,
                sizeof(qualified_name),
                "%s.%s",
                module->name,
                function->name
            );
            ASSERT_TRUE(written > 0);
            ASSERT_TRUE((size_t)written < sizeof(qualified_name));

            entry = vigil_doc_lookup(qualified_name);
            ASSERT_NE(entry, NULL);
            EXPECT_STREQ(entry->name, qualified_name);
            ASSERT_NE(entry->signature, NULL);
            ASSERT_NE(entry->summary, NULL);
        }
    }
}

void register_doc_registry_tests(void) {
    REGISTER_TEST(DocRegistryTest, LookupBuiltin);
    REGISTER_TEST(DocRegistryTest, LookupModule);
    REGISTER_TEST(DocRegistryTest, LookupQualified);
    REGISTER_TEST(DocRegistryTest, LookupNotFound);
    REGISTER_TEST(DocRegistryTest, ListModules);
    REGISTER_TEST(DocRegistryTest, ListModuleContents);
    REGISTER_TEST(DocRegistryTest, RenderEntry);
    REGISTER_TEST(DocRegistryTest, CoversAllStdlibModulesAndFunctions);
}
