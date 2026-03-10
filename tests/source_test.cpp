#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

extern "C" {
#include "basl/basl.h"
}

namespace {

struct AllocatorStats {
    int allocate_calls;
    int reallocate_calls;
    int deallocate_calls;
};

void *CountedAllocate(void *user_data, size_t size) {
    AllocatorStats *stats = static_cast<AllocatorStats *>(user_data);

    stats->allocate_calls += 1;
    return std::calloc(1U, size);
}

void *CountedReallocate(void *user_data, void *memory, size_t size) {
    AllocatorStats *stats = static_cast<AllocatorStats *>(user_data);

    stats->reallocate_calls += 1;
    return std::realloc(memory, size);
}

void CountedDeallocate(void *user_data, void *memory) {
    AllocatorStats *stats = static_cast<AllocatorStats *>(user_data);

    stats->deallocate_calls += 1;
    std::free(memory);
}

}  // namespace

TEST(BaslSourceTest, InitStartsEmpty) {
    basl_source_registry_t registry;

    basl_source_registry_init(&registry, nullptr);

    EXPECT_EQ(registry.runtime, nullptr);
    EXPECT_EQ(registry.files, nullptr);
    EXPECT_EQ(registry.count, 0U);
    EXPECT_EQ(registry.capacity, 0U);
}

TEST(BaslSourceTest, RegisterCopiesOwnedPathAndText) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_source_id_t source_id = 0U;
    char path[] = "main.basl";
    char text[] = "print(\"hi\")";
    const basl_source_file_t *file;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);

    ASSERT_EQ(
        basl_source_registry_register(
            &registry,
            path,
            std::strlen(path),
            text,
            std::strlen(text),
            &source_id,
            &error
        ),
        BASL_STATUS_OK
    );
    ASSERT_EQ(source_id, 1U);

    path[0] = 'x';
    text[0] = 'X';
    file = basl_source_registry_get(&registry, source_id);
    ASSERT_NE(file, nullptr);
    EXPECT_STREQ(basl_string_c_str(&file->path), "main.basl");
    EXPECT_STREQ(basl_string_c_str(&file->text), "print(\"hi\")");

    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslSourceTest, MultipleRegistrationsUseStableSourceIds) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_source_id_t first = 0U;
    basl_source_id_t second = 0U;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);

    ASSERT_EQ(
        basl_source_registry_register_cstr(&registry, "a.basl", "a", &first, &error),
        BASL_STATUS_OK
    );
    ASSERT_EQ(
        basl_source_registry_register_cstr(&registry, "b.basl", "b", &second, &error),
        BASL_STATUS_OK
    );

    EXPECT_EQ(first, 1U);
    EXPECT_EQ(second, 2U);
    EXPECT_EQ(basl_source_registry_count(&registry), 2U);
    ASSERT_NE(basl_source_registry_get(&registry, second), nullptr);
    EXPECT_STREQ(
        basl_string_c_str(&basl_source_registry_get(&registry, second)->path),
        "b.basl"
    );

    basl_source_registry_free(&registry);
    basl_runtime_close(&runtime);
}

TEST(BaslSourceTest, RejectsMissingRuntime) {
    basl_source_registry_t registry;
    basl_source_id_t source_id = 0U;
    basl_error_t error = {};

    basl_source_registry_init(&registry, nullptr);

    EXPECT_EQ(
        basl_source_registry_register_cstr(&registry, "main.basl", "", &source_id, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, nullptr);
    EXPECT_EQ(std::strcmp(error.value, "source registry runtime must not be null"), 0);
}

TEST(BaslSourceTest, UsesRuntimeAllocatorHooks) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    AllocatorStats stats = {};
    basl_allocator_t allocator = {};
    basl_runtime_options_t options = {};
    basl_source_id_t source_id = 0U;

    allocator.user_data = &stats;
    allocator.allocate = CountedAllocate;
    allocator.reallocate = CountedReallocate;
    allocator.deallocate = CountedDeallocate;
    basl_runtime_options_init(&options);
    options.allocator = &allocator;

    ASSERT_EQ(basl_runtime_open(&runtime, &options, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);

    ASSERT_EQ(
        basl_source_registry_register_cstr(
            &registry,
            "main.basl",
            "let value = 1",
            &source_id,
            &error
        ),
        BASL_STATUS_OK
    );

    EXPECT_GE(stats.allocate_calls, 3);
    EXPECT_EQ(source_id, 1U);

    basl_source_registry_free(&registry);
    EXPECT_GE(stats.deallocate_calls, 3);
    basl_runtime_close(&runtime);
}

TEST(BaslSourceTest, FreeResetsWholeRegistry) {
    basl_runtime_t *runtime = nullptr;
    basl_error_t error = {};
    basl_source_registry_t registry;
    basl_source_id_t source_id = 0U;

    ASSERT_EQ(basl_runtime_open(&runtime, nullptr, &error), BASL_STATUS_OK);
    basl_source_registry_init(&registry, runtime);
    ASSERT_EQ(
        basl_source_registry_register_cstr(&registry, "main.basl", "", &source_id, &error),
        BASL_STATUS_OK
    );

    basl_source_registry_free(&registry);

    EXPECT_EQ(registry.runtime, nullptr);
    EXPECT_EQ(registry.files, nullptr);
    EXPECT_EQ(registry.count, 0U);
    EXPECT_EQ(registry.capacity, 0U);
    basl_runtime_close(&runtime);
}
