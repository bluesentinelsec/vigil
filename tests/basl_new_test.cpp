#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "platform/platform.h"
#include "basl/toml.h"
}

/* Helper: run basl new via the platform layer and verify results. */

class BaslNewTest : public ::testing::Test {
protected:
    basl_error_t error{};

    void TearDown() override {
        basl_error_clear(&error);
    }

    /* Recursively remove a project directory. */
    void remove_project(const char *name) {
        char path[4096];
        /* Remove files first, then dirs deepest-first. */
        const char *files[] = {
            "basl.toml", "main.basl", ".gitignore",
            NULL
        };
        const char *lib_files[] = { NULL }; /* filled per test */
        (void)lib_files;
        for (int i = 0; files[i]; i++) {
            basl_platform_path_join(name, files[i], path, sizeof(path), &error);
            basl_platform_remove(path, &error);
        }
        /* Try lib/ and test/ contents. */
        snprintf(path, sizeof(path), "%s/lib", name);
        basl_platform_remove(path, &error);
        snprintf(path, sizeof(path), "%s/test", name);
        basl_platform_remove(path, &error);
        basl_platform_remove(name, &error);
    }

    void remove_lib_project(const char *name) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/lib/%s.basl", name, name);
        basl_platform_remove(path, &error);
        snprintf(path, sizeof(path), "%s/test/%s_test.basl", name, name);
        basl_platform_remove(path, &error);
        const char *files[] = { "basl.toml", ".gitignore", NULL };
        for (int i = 0; files[i]; i++) {
            basl_platform_path_join(name, files[i], path, sizeof(path), &error);
            basl_platform_remove(path, &error);
        }
        snprintf(path, sizeof(path), "%s/lib", name);
        basl_platform_remove(path, &error);
        snprintf(path, sizeof(path), "%s/test", name);
        basl_platform_remove(path, &error);
        basl_platform_remove(name, &error);
    }
};

/* ── Platform path_join ──────────────────────────────────────────── */

TEST_F(BaslNewTest, PathJoinBasic) {
    char buf[256];
    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_path_join("foo", "bar", buf, sizeof(buf), &error));
    EXPECT_STREQ(buf, "foo/bar");
}

TEST_F(BaslNewTest, PathJoinTrailingSlash) {
    char buf[256];
    ASSERT_EQ(BASL_STATUS_OK,
              basl_platform_path_join("foo/", "bar", buf, sizeof(buf), &error));
    EXPECT_STREQ(buf, "foo/bar");
}

TEST_F(BaslNewTest, PathJoinOverflow) {
    char buf[5];
    EXPECT_NE(BASL_STATUS_OK,
              basl_platform_path_join("long", "path", buf, sizeof(buf), &error));
}

TEST_F(BaslNewTest, PathJoinNull) {
    char buf[256];
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_path_join(NULL, "b", buf, sizeof(buf), &error));
}

/* ── App project scaffolding ─────────────────────────────────────── */

TEST_F(BaslNewTest, AppProjectStructure) {
    const char *name = "test_new_app_proj";
    char path[4096];
    int exists = 0;
    int is_dir = 0;

    /* Clean up from any previous failed run. */
    remove_project(name);

    /* Create project directory and files using platform functions
     * (same logic as cmd_new in main.c). */
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir_p(name, &error));

    basl_platform_path_join(name, "lib", path, sizeof(path), &error);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir(path, &error));

    basl_platform_path_join(name, "test", path, sizeof(path), &error);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir(path, &error));

    /* Write basl.toml via TOML library. */
    {
        basl_toml_value_t *root = nullptr;
        basl_toml_value_t *val = nullptr;
        char *toml_str = nullptr;
        size_t toml_len = 0;

        ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_new(nullptr, &root, &error));
        ASSERT_EQ(BASL_STATUS_OK, basl_toml_string_new(nullptr, name, strlen(name), &val, &error));
        ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_set(root, "name", 4, val, &error));
        val = nullptr;
        ASSERT_EQ(BASL_STATUS_OK, basl_toml_string_new(nullptr, "0.1.0", 5, &val, &error));
        ASSERT_EQ(BASL_STATUS_OK, basl_toml_table_set(root, "version", 7, val, &error));
        val = nullptr;
        ASSERT_EQ(BASL_STATUS_OK, basl_toml_emit(root, &toml_str, &toml_len, &error));

        basl_platform_path_join(name, "basl.toml", path, sizeof(path), &error);
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_write_file(path, toml_str, toml_len, &error));
        free(toml_str);
        basl_toml_free(&root);
    }

    /* Write main.basl. */
    {
        const char *content = "import \"fmt\";\n\nfn main() -> i32 {\n    fmt.println(\"hello\");\n    return 0;\n}\n";
        basl_platform_path_join(name, "main.basl", path, sizeof(path), &error);
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_write_file(path, content, strlen(content), &error));
    }

    /* Write .gitignore. */
    {
        basl_platform_path_join(name, ".gitignore", path, sizeof(path), &error);
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_write_file(path, "deps/\n", 6, &error));
    }

    /* ── Verify structure using platform I/O ─────────────────────── */

    /* Root directory exists. */
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(name, &is_dir));
    EXPECT_EQ(is_dir, 1);

    /* lib/ directory. */
    basl_platform_path_join(name, "lib", path, sizeof(path), &error);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    /* test/ directory. */
    basl_platform_path_join(name, "test", path, sizeof(path), &error);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_is_directory(path, &is_dir));
    EXPECT_EQ(is_dir, 1);

    /* basl.toml exists and parses correctly. */
    basl_platform_path_join(name, "basl.toml", path, sizeof(path), &error);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);
    {
        char *data = nullptr;
        size_t len = 0;
        basl_toml_value_t *parsed = nullptr;
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_read_file(nullptr, path, &data, &len, &error));
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_toml_parse(nullptr, data, len, &parsed, &error));
        EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(parsed, "name")), name);
        EXPECT_STREQ(basl_toml_string_value(basl_toml_table_get(parsed, "version")), "0.1.0");
        basl_toml_free(&parsed);
        free(data);
    }

    /* main.basl exists. */
    basl_platform_path_join(name, "main.basl", path, sizeof(path), &error);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    /* .gitignore exists. */
    basl_platform_path_join(name, ".gitignore", path, sizeof(path), &error);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    /* Clean up. */
    remove_project(name);
}

/* ── Library project scaffolding ─────────────────────────────────── */

TEST_F(BaslNewTest, LibProjectStructure) {
    const char *name = "test_new_lib_proj";
    char path[4096];
    int exists = 0;

    remove_lib_project(name);

    /* Create structure. */
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir_p(name, &error));

    basl_platform_path_join(name, "lib", path, sizeof(path), &error);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir(path, &error));

    basl_platform_path_join(name, "test", path, sizeof(path), &error);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_mkdir(path, &error));

    /* Write lib/name.basl. */
    {
        char content[512];
        snprintf(content, sizeof(content),
            "/// %s library module.\n\npub fn hello() -> string {\n"
            "    return \"hello from %s\";\n}\n", name, name);
        snprintf(path, sizeof(path), "%s/lib/%s.basl", name, name);
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_write_file(path, content, strlen(content), &error));
    }

    /* Write test/name_test.basl. */
    {
        char content[512];
        snprintf(content, sizeof(content),
            "import \"test\";\nimport \"%s\";\n\n"
            "fn test_hello(test.T t) -> void {\n"
            "    t.assert(%s.hello() == \"hello from %s\", \"hello should match\");\n}\n",
            name, name, name);
        snprintf(path, sizeof(path), "%s/test/%s_test.basl", name, name);
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_write_file(path, content, strlen(content), &error));
    }

    /* Verify lib file. */
    snprintf(path, sizeof(path), "%s/lib/%s.basl", name, name);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);
    {
        char *data = nullptr;
        size_t len = 0;
        ASSERT_EQ(BASL_STATUS_OK,
                  basl_platform_read_file(nullptr, path, &data, &len, &error));
        EXPECT_NE(strstr(data, "pub fn hello"), nullptr);
        free(data);
    }

    /* Verify test file. */
    snprintf(path, sizeof(path), "%s/test/%s_test.basl", name, name);
    ASSERT_EQ(BASL_STATUS_OK, basl_platform_file_exists(path, &exists));
    EXPECT_EQ(exists, 1);

    remove_lib_project(name);
}

/* ── Readline ────────────────────────────────────────────────────── */

TEST_F(BaslNewTest, ReadlineNullArgs) {
    EXPECT_EQ(BASL_STATUS_INVALID_ARGUMENT,
              basl_platform_readline(nullptr, nullptr, 0, &error));
}
