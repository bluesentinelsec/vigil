#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "platform/platform.h"
}

/* ── Stub tests (always run — verify the stub API contract) ──────── */

/* Link the real platform on native, so we test actual I/O there.
 * For stub coverage, we call the stub functions directly via a
 * separate compilation unit.  Since we can't link both, we test
 * the stub contract by verifying the real platform returns OK
 * (not UNSUPPORTED) on native, proving the dispatch works. */

class PlatformTest : public ::testing::Test {
protected:
    basl_error_t error{};

    void TearDown() override {
        basl_error_clear(&error);
    }
};

/* ── File read/write round-trip ──────────────────────────────────── */

#ifndef __EMSCRIPTEN__

TEST_F(PlatformTest, WriteAndReadFile) {
    const char *path = "test_platform_rw.tmp";
    const char *content = "hello platform";
    char *data = nullptr;
    size_t len = 0;

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_write_file(path, content, strlen(content), &error))
        << basl_error_message(&error);

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_read_file(nullptr, path, &data, &len, &error))
        << basl_error_message(&error);

    ASSERT_NE(data, nullptr);
    EXPECT_EQ(len, strlen(content));
    EXPECT_STREQ(data, content);
    free(data);

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_remove(path, &error));
}

TEST_F(PlatformTest, ReadNonexistent) {
    char *data = nullptr;
    size_t len = 0;
    EXPECT_NE(BASL_STATUS_OK,
              basl_platform_read_file(nullptr, "nonexistent_file_xyz.tmp",
                                      &data, &len, &error));
}

/* ── File exists ─────────────────────────────────────────────────── */

TEST_F(PlatformTest, FileExists) {
    const char *path = "test_platform_exists.tmp";
    int exists = 0;

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 0);

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_write_file(path, "x", 1, &error));

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    basl_platform_remove(path, &error);
}

/* ── Directory operations ────────────────────────────────────────── */

TEST_F(PlatformTest, MkdirAndIsDirectory) {
    const char *path = "test_platform_dir.tmp";
    int is_dir = 0;

    /* Clean up from any previous failed run. */
    basl_platform_remove(path, &error);

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir(path, &error))
        << basl_error_message(&error);

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_remove(path, &error));
}

TEST_F(PlatformTest, MkdirP) {
    const char *path = "test_platform_deep.tmp/a/b/c";
    int is_dir = 0;

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir_p(path, &error))
        << basl_error_message(&error);

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    /* Clean up (deepest first). */
    basl_platform_remove("test_platform_deep.tmp/a/b/c", &error);
    basl_platform_remove("test_platform_deep.tmp/a/b", &error);
    basl_platform_remove("test_platform_deep.tmp/a", &error);
    basl_platform_remove("test_platform_deep.tmp", &error);
}

TEST_F(PlatformTest, IsDirectoryOnFile) {
    const char *path = "test_platform_notdir.tmp";
    int is_dir = 1;

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_write_file(path, "x", 1, &error));

    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 0);

    basl_platform_remove(path, &error);
}

/* ── Custom allocator ────────────────────────────────────────────── */

static size_t test_alloc_count = 0;
static void *counting_alloc(void *, size_t s) { test_alloc_count++; return malloc(s); }
static void *counting_realloc(void *, void *p, size_t s) { return realloc(p, s); }
static void counting_dealloc(void *, void *p) { free(p); }

TEST_F(PlatformTest, ReadFileCustomAllocator) {
    const char *path = "test_platform_alloc.tmp";
    char *data = nullptr;
    size_t len = 0;
    basl_allocator_t alloc = {nullptr, counting_alloc, counting_realloc, counting_dealloc};

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_write_file(path, "test", 4, &error));

    test_alloc_count = 0;
    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_read_file(&alloc, path, &data, &len, &error));
    EXPECT_GT(test_alloc_count, 0u);
    EXPECT_STREQ(data, "test");
    free(data);

    basl_platform_remove(path, &error);
}

/* ── NULL argument handling ──────────────────────────────────────── */

TEST_F(PlatformTest, NullArgs) {
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_read_file(nullptr, nullptr, nullptr, nullptr, &error));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_write_file(nullptr, nullptr, 0, &error));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_file_exists(nullptr, nullptr));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_is_directory(nullptr, nullptr));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_mkdir(nullptr, &error));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_mkdir_p(nullptr, &error));
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_remove(nullptr, &error));
}

/* ── Write empty file ────────────────────────────────────────────── */

TEST_F(PlatformTest, WriteEmptyFile) {
    const char *path = "test_platform_empty.tmp";
    char *data = nullptr;
    size_t len = 99;

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_write_file(path, nullptr, 0, &error));

    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_read_file(nullptr, path, &data, &len, &error));
    EXPECT_EQ(len, 0u);
    free(data);

    basl_platform_remove(path, &error);
}

#endif /* !__EMSCRIPTEN__ */

/* ── Stub contract tests (always compiled) ───────────────────────── */

/* These verify the stub returns UNSUPPORTED.  On native builds we
 * link the real platform, so we test that it returns OK instead.
 * This ensures the CMake dispatch is correct for each platform. */

TEST_F(PlatformTest, FileExistsReturnsValidStatus) {
    int exists = 0;
    basl_status_t s = basl_platform_file_exists(".", &exists);
#ifdef __EMSCRIPTEN__
    EXPECT_EQ(s, BASL_STATUS_UNSUPPORTED);
#else
    EXPECT_EQ(s, BASL_STATUS_OK);
    EXPECT_EQ(exists, 1);  /* "." always exists on native */
#endif
}
