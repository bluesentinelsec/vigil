/* Unit tests for BASL fs module platform functions. */
#include "basl_test.h"
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper to build temp file path */
static void make_temp_path(char *buf, size_t bufsize, const char *name) {
    char *tmp = NULL;
    basl_platform_temp_dir(&tmp, NULL);
    snprintf(buf, bufsize, "%s/%s", tmp ? tmp : ".", name);
    free(tmp);
}

/* ── Path operations (tested via platform layer) ─────────────────── */

TEST(BaslFsTest, PathJoin) {
    char buf[256];
    basl_status_t s = basl_platform_path_join("a", "b", buf, sizeof(buf), NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_STREQ(buf, "a/b");
}

TEST(BaslFsTest, PathJoinTrailingSlash) {
    char buf[256];
    basl_status_t s = basl_platform_path_join("a/", "b", buf, sizeof(buf), NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_STREQ(buf, "a/b");
}

/* ── File operations ─────────────────────────────────────────────── */

TEST(BaslFsTest, WriteAndRead) {
    char path[512];
    make_temp_path(path, sizeof(path), "basl_fs_test.txt");
    const char *data = "hello world";
    
    basl_status_t s = basl_platform_write_file(path, data, strlen(data), NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    
    char *read_data = NULL;
    size_t read_len = 0;
    s = basl_platform_read_file(NULL, path, &read_data, &read_len, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    EXPECT_STREQ(read_data, data);
    free(read_data);
    
    basl_platform_remove(path, NULL);
}

TEST(BaslFsTest, FileExists) {
    char path[512];
    make_temp_path(path, sizeof(path), "basl_fs_exists_test.txt");
    basl_platform_write_file(path, "x", 1, NULL);
    
    int exists = 0;
    basl_platform_file_exists(path, &exists);
    EXPECT_EQ(exists, 1);
    
    basl_platform_remove(path, NULL);
    basl_platform_file_exists(path, &exists);
    EXPECT_EQ(exists, 0);
}

TEST(BaslFsTest, IsDirectory) {
    char *tmp = NULL;
    basl_platform_temp_dir(&tmp, NULL);
    int is_dir = 0;
    basl_platform_is_directory(tmp, &is_dir);
    EXPECT_EQ(is_dir, 1);
    free(tmp);
    
    char path[512];
    make_temp_path(path, sizeof(path), "basl_fs_isdir_test.txt");
    basl_platform_write_file(path, "x", 1, NULL);
    basl_platform_is_directory(path, &is_dir);
    EXPECT_EQ(is_dir, 0);
    basl_platform_remove(path, NULL);
}

TEST(BaslFsTest, IsFile) {
    char *tmp = NULL;
    basl_platform_temp_dir(&tmp, NULL);
    int is_file = 0;
    basl_platform_is_file(tmp, &is_file);
    EXPECT_EQ(is_file, 0);
    free(tmp);
    
    char path[512];
    make_temp_path(path, sizeof(path), "basl_fs_isfile_test.txt");
    basl_platform_write_file(path, "x", 1, NULL);
    basl_platform_is_file(path, &is_file);
    EXPECT_EQ(is_file, 1);
    basl_platform_remove(path, NULL);
}

TEST(BaslFsTest, CopyFile) {
    char src[512], dst[512];
    make_temp_path(src, sizeof(src), "basl_fs_copy_src.txt");
    make_temp_path(dst, sizeof(dst), "basl_fs_copy_dst.txt");
    
    basl_platform_write_file(src, "copy me", 7, NULL);
    basl_status_t s = basl_platform_copy_file(src, dst, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    
    char *data = NULL;
    size_t len = 0;
    basl_platform_read_file(NULL, dst, &data, &len, NULL);
    EXPECT_STREQ(data, "copy me");
    free(data);
    
    basl_platform_remove(src, NULL);
    basl_platform_remove(dst, NULL);
}

TEST(BaslFsTest, RenameFile) {
    char src[512], dst[512];
    make_temp_path(src, sizeof(src), "basl_fs_rename_src.txt");
    make_temp_path(dst, sizeof(dst), "basl_fs_rename_dst.txt");
    
    basl_platform_write_file(src, "move me", 7, NULL);
    basl_status_t s = basl_platform_rename(src, dst, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    
    int exists = 0;
    basl_platform_file_exists(src, &exists);
    EXPECT_EQ(exists, 0);
    basl_platform_file_exists(dst, &exists);
    EXPECT_EQ(exists, 1);
    
    basl_platform_remove(dst, NULL);
}

/* ── Directory operations ────────────────────────────────────────── */

TEST(BaslFsTest, MkdirAndRemove) {
    char path[512];
    make_temp_path(path, sizeof(path), "basl_fs_mkdir_test");
    
    basl_status_t s = basl_platform_mkdir(path, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    
    int is_dir = 0;
    basl_platform_is_directory(path, &is_dir);
    EXPECT_EQ(is_dir, 1);
    
    basl_platform_remove(path, NULL);
    basl_platform_is_directory(path, &is_dir);
    EXPECT_EQ(is_dir, 0);
}

TEST(BaslFsTest, MkdirP) {
    char path[512], base[512];
    make_temp_path(base, sizeof(base), "basl_fs_mkdirp_test");
    snprintf(path, sizeof(path), "%s/a/b/c", base);
    
    basl_status_t s = basl_platform_mkdir_p(path, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    
    int is_dir = 0;
    basl_platform_is_directory(path, &is_dir);
    EXPECT_EQ(is_dir, 1);
    
    /* Cleanup */
    char cleanup[512];
    snprintf(cleanup, sizeof(cleanup), "%s/a/b/c", base);
    basl_platform_remove(cleanup, NULL);
    snprintf(cleanup, sizeof(cleanup), "%s/a/b", base);
    basl_platform_remove(cleanup, NULL);
    snprintf(cleanup, sizeof(cleanup), "%s/a", base);
    basl_platform_remove(cleanup, NULL);
    basl_platform_remove(base, NULL);
}

/* ── Metadata ────────────────────────────────────────────────────── */

TEST(BaslFsTest, FileSize) {
    char path[512];
    make_temp_path(path, sizeof(path), "basl_fs_size_test.txt");
    basl_platform_write_file(path, "12345", 5, NULL);
    
    int64_t size = basl_platform_file_size(path);
    EXPECT_EQ(size, 5);
    
    basl_platform_remove(path, NULL);
}

TEST(BaslFsTest, FileMtime) {
    char path[512];
    make_temp_path(path, sizeof(path), "basl_fs_mtime_test.txt");
    basl_platform_write_file(path, "x", 1, NULL);
    
    int64_t mtime = basl_platform_file_mtime(path);
    EXPECT_TRUE(mtime > 0);
    
    basl_platform_remove(path, NULL);
}

/* ── Standard locations ──────────────────────────────────────────── */

TEST(BaslFsTest, TempDir) {
    char *path = NULL;
    basl_status_t s = basl_platform_temp_dir(&path, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    ASSERT_NE(path, NULL);
    EXPECT_TRUE(strlen(path) > 0);
    free(path);
}

TEST(BaslFsTest, HomeDir) {
    char *path = NULL;
    basl_status_t s = basl_platform_home_dir(&path, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    ASSERT_NE(path, NULL);
    EXPECT_TRUE(strlen(path) > 0);
    free(path);
}

TEST(BaslFsTest, ConfigDir) {
    char *path = NULL;
    basl_status_t s = basl_platform_config_dir(&path, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    ASSERT_NE(path, NULL);
    EXPECT_TRUE(strlen(path) > 0);
    free(path);
}

TEST(BaslFsTest, TempFile) {
    char *path = NULL;
    basl_status_t s = basl_platform_temp_file("basl", &path, NULL);
    ASSERT_EQ(s, BASL_STATUS_OK);
    ASSERT_NE(path, NULL);
    
    int exists = 0;
    basl_platform_file_exists(path, &exists);
    EXPECT_EQ(exists, 1);
    
    basl_platform_remove(path, NULL);
    free(path);
}

/* ── Test Registration ───────────────────────────────────────────── */

void register_fs_tests(void) {
    REGISTER_TEST(BaslFsTest, PathJoin);
    REGISTER_TEST(BaslFsTest, PathJoinTrailingSlash);
    REGISTER_TEST(BaslFsTest, WriteAndRead);
    REGISTER_TEST(BaslFsTest, FileExists);
    REGISTER_TEST(BaslFsTest, IsDirectory);
    REGISTER_TEST(BaslFsTest, IsFile);
    REGISTER_TEST(BaslFsTest, CopyFile);
    REGISTER_TEST(BaslFsTest, RenameFile);
    REGISTER_TEST(BaslFsTest, MkdirAndRemove);
    REGISTER_TEST(BaslFsTest, MkdirP);
    REGISTER_TEST(BaslFsTest, FileSize);
    REGISTER_TEST(BaslFsTest, FileMtime);
    REGISTER_TEST(BaslFsTest, TempDir);
    REGISTER_TEST(BaslFsTest, HomeDir);
    REGISTER_TEST(BaslFsTest, ConfigDir);
    REGISTER_TEST(BaslFsTest, TempFile);
}
