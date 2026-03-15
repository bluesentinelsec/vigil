#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "basl/cli_lib.h"
}

/* Helper to build argv from a string literal list. */
#define ARGV(...) \
    (char *[]){__VA_ARGS__}
#define ARGC(...) \
    (int)(sizeof(ARGV(__VA_ARGS__)) / sizeof(char *))

/* ── Basic lifecycle ─────────────────────────────────────────────── */

TEST(BaslCliLibTest, InitAndFree) {
    basl_cli_t cli;
    basl_cli_init(&cli, "test", "A test program");
    EXPECT_STREQ(cli.program_name, "test");
    basl_cli_free(&cli);
}

/* ── Subcommand matching ─────────────────────────────────────────── */

TEST(BaslCliLibTest, MatchesSubcommand) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_add_command(&cli, "run", "Run a script");
    basl_cli_add_command(&cli, "check", "Check a script");

    char *argv[] = {(char *)"basl", (char *)"run"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 2, argv, &error), BASL_STATUS_OK);
    ASSERT_NE(basl_cli_matched_command(&cli), nullptr);
    EXPECT_STREQ(basl_cli_matched_command(&cli)->name, "run");
    basl_cli_free(&cli);
}

TEST(BaslCliLibTest, UnknownCommandFails) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_add_command(&cli, "run", "Run");

    char *argv[] = {(char *)"basl", (char *)"bogus"};
    basl_error_t error = {};
    EXPECT_EQ(basl_cli_parse(&cli, 2, argv, &error), BASL_STATUS_INVALID_ARGUMENT);
    basl_cli_free(&cli);
}

/* ── Boolean flags ───────────────────────────────────────────────── */

TEST(BaslCliLibTest, BoolFlagLong) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run");
    int verbose = 0;
    basl_cli_add_bool_flag(run, "verbose", 'v', "Verbose", &verbose);

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"--verbose"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 3, argv, &error), BASL_STATUS_OK);
    EXPECT_EQ(verbose, 1);
    basl_cli_free(&cli);
}

TEST(BaslCliLibTest, BoolFlagShort) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run");
    int verbose = 0;
    basl_cli_add_bool_flag(run, "verbose", 'v', "Verbose", &verbose);

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"-v"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 3, argv, &error), BASL_STATUS_OK);
    EXPECT_EQ(verbose, 1);
    basl_cli_free(&cli);
}

/* ── String flags ────────────────────────────────────────────────── */

TEST(BaslCliLibTest, StringFlagWithEquals) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run");
    const char *output = NULL;
    basl_cli_add_string_flag(run, "output", 'o', "Output file", &output);

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"--output=foo.txt"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 3, argv, &error), BASL_STATUS_OK);
    ASSERT_NE(output, nullptr);
    EXPECT_STREQ(output, "foo.txt");
    basl_cli_free(&cli);
}

TEST(BaslCliLibTest, StringFlagWithSpace) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run");
    const char *output = NULL;
    basl_cli_add_string_flag(run, "output", 'o', "Output file", &output);

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"--output", (char *)"bar.txt"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 4, argv, &error), BASL_STATUS_OK);
    EXPECT_STREQ(output, "bar.txt");
    basl_cli_free(&cli);
}

TEST(BaslCliLibTest, ShortStringFlagAttached) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run");
    const char *output = NULL;
    basl_cli_add_string_flag(run, "output", 'o', "Output file", &output);

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"-ofoo.txt"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 3, argv, &error), BASL_STATUS_OK);
    EXPECT_STREQ(output, "foo.txt");
    basl_cli_free(&cli);
}

TEST(BaslCliLibTest, ShortStringFlagSeparate) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run");
    const char *output = NULL;
    basl_cli_add_string_flag(run, "output", 'o', "Output file", &output);

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"-o", (char *)"bar.txt"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 4, argv, &error), BASL_STATUS_OK);
    EXPECT_STREQ(output, "bar.txt");
    basl_cli_free(&cli);
}

/* ── Positional args ─────────────────────────────────────────────── */

TEST(BaslCliLibTest, PositionalArg) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run");
    const char *file = NULL;
    basl_cli_add_positional(run, "file", "Script file", &file);

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"main.basl"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 3, argv, &error), BASL_STATUS_OK);
    EXPECT_STREQ(file, "main.basl");
    basl_cli_free(&cli);
}

TEST(BaslCliLibTest, PositionalAndFlags) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run");
    const char *file = NULL;
    int verbose = 0;
    basl_cli_add_positional(run, "file", "Script file", &file);
    basl_cli_add_bool_flag(run, "verbose", 'v', "Verbose", &verbose);

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"-v", (char *)"main.basl"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 4, argv, &error), BASL_STATUS_OK);
    EXPECT_STREQ(file, "main.basl");
    EXPECT_EQ(verbose, 1);
    basl_cli_free(&cli);
}

/* ── Global flags ────────────────────────────────────────────────── */

TEST(BaslCliLibTest, GlobalFlag) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    int verbose = 0;
    basl_cli_add_global_bool_flag(&cli, "verbose", 'v', "Verbose", &verbose);
    basl_cli_add_command(&cli, "run", "Run");

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"--verbose"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 3, argv, &error), BASL_STATUS_OK);
    EXPECT_EQ(verbose, 1);
    basl_cli_free(&cli);
}

/* ── Global positionals (no subcommands) ─────────────────────────── */

TEST(BaslCliLibTest, GlobalPositionalNoSubcommands) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    const char *file = NULL;
    basl_cli_add_global_positional(&cli, "file", "Script file", &file);

    char *argv[] = {(char *)"basl", (char *)"main.basl"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 2, argv, &error), BASL_STATUS_OK);
    EXPECT_STREQ(file, "main.basl");
    basl_cli_free(&cli);
}

/* ── Double dash stops flag parsing ──────────────────────────────── */

TEST(BaslCliLibTest, DoubleDashStopsFlagParsing) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run");
    const char *file = NULL;
    basl_cli_add_positional(run, "file", "Script file", &file);

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"--", (char *)"--weird-file"};
    basl_error_t error = {};
    ASSERT_EQ(basl_cli_parse(&cli, 4, argv, &error), BASL_STATUS_OK);
    EXPECT_STREQ(file, "--weird-file");
    basl_cli_free(&cli);
}

/* ── Error cases ─────────────────────────────────────────────────── */

TEST(BaslCliLibTest, UnknownFlagFails) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_add_command(&cli, "run", "Run");

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"--bogus"};
    basl_error_t error = {};
    EXPECT_EQ(basl_cli_parse(&cli, 3, argv, &error), BASL_STATUS_INVALID_ARGUMENT);
    basl_cli_free(&cli);
}

TEST(BaslCliLibTest, MissingFlagValueFails) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run");
    const char *output = NULL;
    basl_cli_add_string_flag(run, "output", 'o', "Output", &output);

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"--output"};
    basl_error_t error = {};
    EXPECT_EQ(basl_cli_parse(&cli, 3, argv, &error), BASL_STATUS_INVALID_ARGUMENT);
    basl_cli_free(&cli);
}

TEST(BaslCliLibTest, ExtraPositionalFails) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_command_t *run = basl_cli_add_command(&cli, "run", "Run");
    const char *file = NULL;
    basl_cli_add_positional(run, "file", "Script file", &file);

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"a.basl", (char *)"b.basl"};
    basl_error_t error = {};
    EXPECT_EQ(basl_cli_parse(&cli, 4, argv, &error), BASL_STATUS_INVALID_ARGUMENT);
    basl_cli_free(&cli);
}

/* ── Help flag doesn't error ─────────────────────────────────────── */

TEST(BaslCliLibTest, HelpFlagReturnsOk) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_add_command(&cli, "run", "Run");

    char *argv[] = {(char *)"basl", (char *)"--help"};
    basl_error_t error = {};
    EXPECT_EQ(basl_cli_parse(&cli, 2, argv, &error), BASL_STATUS_OK);
    basl_cli_free(&cli);
}

TEST(BaslCliLibTest, CommandHelpFlagReturnsOk) {
    basl_cli_t cli;
    basl_cli_init(&cli, "basl", "test");
    basl_cli_add_command(&cli, "run", "Run a script");

    char *argv[] = {(char *)"basl", (char *)"run", (char *)"-h"};
    basl_error_t error = {};
    EXPECT_EQ(basl_cli_parse(&cli, 3, argv, &error), BASL_STATUS_OK);
    basl_cli_free(&cli);
}

/* ── Custom allocator ────────────────────────────────────────────── */

static size_t g_cli_allocs = 0;
static void *tracking_cli_alloc(void *, size_t s) { g_cli_allocs++; return calloc(1, s); }
static void *tracking_cli_realloc(void *, void *p, size_t s) { g_cli_allocs++; return realloc(p, s); }
static void tracking_cli_dealloc(void *, void *p) { free(p); }

TEST(BaslCliLibTest, CustomAllocator) {
    g_cli_allocs = 0;
    basl_allocator_t a = {NULL, tracking_cli_alloc, tracking_cli_realloc, tracking_cli_dealloc};
    basl_cli_t cli;
    basl_cli_init_with_allocator(&cli, "basl", "test", &a);
    basl_cli_add_command(&cli, "run", "Run");
    basl_cli_add_command(&cli, "check", "Check");

    EXPECT_GT(g_cli_allocs, 0U);
    basl_cli_free(&cli);
}
