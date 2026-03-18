#include "vigil_test.h"
#include <string.h>


#include "vigil/cli_lib.h"

/* Helper to build argv from a string literal list. */
#define ARGV(...) \
    (char *[]){__VA_ARGS__}
#define ARGC(...) \
    (int)(sizeof(ARGV(__VA_ARGS__)) / sizeof(char *))

/* ── Basic lifecycle ─────────────────────────────────────────────── */

TEST(VigilCliLibTest, InitAndFree) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "test", "A test program");
    EXPECT_STREQ(cli.program_name, "test");
    vigil_cli_free(&cli);
}

/* ── Subcommand matching ─────────────────────────────────────────── */

TEST(VigilCliLibTest, MatchesSubcommand) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_add_command(&cli, "run", "Run a script");
    vigil_cli_add_command(&cli, "check", "Check a script");

    char *argv[] = {(char *)"vigil", (char *)"run"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 2, argv, &error), VIGIL_STATUS_OK);
    ASSERT_NE(vigil_cli_matched_command(&cli), NULL);
    EXPECT_STREQ(vigil_cli_matched_command(&cli)->name, "run");
    vigil_cli_free(&cli);
}

TEST(VigilCliLibTest, UnknownCommandFails) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_add_command(&cli, "run", "Run");

    char *argv[] = {(char *)"vigil", (char *)"bogus"};
    vigil_error_t error = {0};
    EXPECT_EQ(vigil_cli_parse(&cli, 2, argv, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    vigil_cli_free(&cli);
}

/* ── Boolean flags ───────────────────────────────────────────────── */

TEST(VigilCliLibTest, BoolFlagLong) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run");
    int verbose = 0;
    vigil_cli_add_bool_flag(run, "verbose", 'v', "Verbose", &verbose);

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"--verbose"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 3, argv, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(verbose, 1);
    vigil_cli_free(&cli);
}

TEST(VigilCliLibTest, BoolFlagShort) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run");
    int verbose = 0;
    vigil_cli_add_bool_flag(run, "verbose", 'v', "Verbose", &verbose);

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"-v"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 3, argv, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(verbose, 1);
    vigil_cli_free(&cli);
}

/* ── String flags ────────────────────────────────────────────────── */

TEST(VigilCliLibTest, StringFlagWithEquals) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run");
    const char *output = NULL;
    vigil_cli_add_string_flag(run, "output", 'o', "Output file", &output);

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"--output=foo.txt"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 3, argv, &error), VIGIL_STATUS_OK);
    ASSERT_NE(output, NULL);
    EXPECT_STREQ(output, "foo.txt");
    vigil_cli_free(&cli);
}

TEST(VigilCliLibTest, StringFlagWithSpace) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run");
    const char *output = NULL;
    vigil_cli_add_string_flag(run, "output", 'o', "Output file", &output);

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"--output", (char *)"bar.txt"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 4, argv, &error), VIGIL_STATUS_OK);
    EXPECT_STREQ(output, "bar.txt");
    vigil_cli_free(&cli);
}

TEST(VigilCliLibTest, ShortStringFlagAttached) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run");
    const char *output = NULL;
    vigil_cli_add_string_flag(run, "output", 'o', "Output file", &output);

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"-ofoo.txt"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 3, argv, &error), VIGIL_STATUS_OK);
    EXPECT_STREQ(output, "foo.txt");
    vigil_cli_free(&cli);
}

TEST(VigilCliLibTest, ShortStringFlagSeparate) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run");
    const char *output = NULL;
    vigil_cli_add_string_flag(run, "output", 'o', "Output file", &output);

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"-o", (char *)"bar.txt"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 4, argv, &error), VIGIL_STATUS_OK);
    EXPECT_STREQ(output, "bar.txt");
    vigil_cli_free(&cli);
}

/* ── Positional args ─────────────────────────────────────────────── */

TEST(VigilCliLibTest, PositionalArg) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run");
    const char *file = NULL;
    vigil_cli_add_positional(run, "file", "Script file", &file);

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"main.vigil"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 3, argv, &error), VIGIL_STATUS_OK);
    EXPECT_STREQ(file, "main.vigil");
    vigil_cli_free(&cli);
}

TEST(VigilCliLibTest, PositionalAndFlags) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run");
    const char *file = NULL;
    int verbose = 0;
    vigil_cli_add_positional(run, "file", "Script file", &file);
    vigil_cli_add_bool_flag(run, "verbose", 'v', "Verbose", &verbose);

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"-v", (char *)"main.vigil"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 4, argv, &error), VIGIL_STATUS_OK);
    EXPECT_STREQ(file, "main.vigil");
    EXPECT_EQ(verbose, 1);
    vigil_cli_free(&cli);
}

/* ── Global flags ────────────────────────────────────────────────── */

TEST(VigilCliLibTest, GlobalFlag) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    int verbose = 0;
    vigil_cli_add_global_bool_flag(&cli, "verbose", 'v', "Verbose", &verbose);
    vigil_cli_add_command(&cli, "run", "Run");

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"--verbose"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 3, argv, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(verbose, 1);
    vigil_cli_free(&cli);
}

/* ── Global positionals (no subcommands) ─────────────────────────── */

TEST(VigilCliLibTest, GlobalPositionalNoSubcommands) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    const char *file = NULL;
    vigil_cli_add_global_positional(&cli, "file", "Script file", &file);

    char *argv[] = {(char *)"vigil", (char *)"main.vigil"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 2, argv, &error), VIGIL_STATUS_OK);
    EXPECT_STREQ(file, "main.vigil");
    vigil_cli_free(&cli);
}

/* ── Double dash stops flag parsing ──────────────────────────────── */

TEST(VigilCliLibTest, DoubleDashStopsFlagParsing) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run");
    const char *file = NULL;
    vigil_cli_add_positional(run, "file", "Script file", &file);

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"--", (char *)"--weird-file"};
    vigil_error_t error = {0};
    ASSERT_EQ(vigil_cli_parse(&cli, 4, argv, &error), VIGIL_STATUS_OK);
    EXPECT_STREQ(file, "--weird-file");
    vigil_cli_free(&cli);
}

/* ── Error cases ─────────────────────────────────────────────────── */

TEST(VigilCliLibTest, UnknownFlagFails) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_add_command(&cli, "run", "Run");

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"--bogus"};
    vigil_error_t error = {0};
    EXPECT_EQ(vigil_cli_parse(&cli, 3, argv, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    vigil_cli_free(&cli);
}

TEST(VigilCliLibTest, MissingFlagValueFails) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run");
    const char *output = NULL;
    vigil_cli_add_string_flag(run, "output", 'o', "Output", &output);

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"--output"};
    vigil_error_t error = {0};
    EXPECT_EQ(vigil_cli_parse(&cli, 3, argv, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    vigil_cli_free(&cli);
}

TEST(VigilCliLibTest, ExtraPositionalFails) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_command_t *run = vigil_cli_add_command(&cli, "run", "Run");
    const char *file = NULL;
    vigil_cli_add_positional(run, "file", "Script file", &file);

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"a.vigil", (char *)"b.vigil"};
    vigil_error_t error = {0};
    EXPECT_EQ(vigil_cli_parse(&cli, 4, argv, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    vigil_cli_free(&cli);
}

/* ── Help flag doesn't error ─────────────────────────────────────── */

TEST(VigilCliLibTest, HelpFlagReturnsOk) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_add_command(&cli, "run", "Run");

    char *argv[] = {(char *)"vigil", (char *)"--help"};
    vigil_error_t error = {0};
    EXPECT_EQ(vigil_cli_parse(&cli, 2, argv, &error), VIGIL_STATUS_OK);
    vigil_cli_free(&cli);
}

TEST(VigilCliLibTest, CommandHelpFlagReturnsOk) {
    vigil_cli_t cli;
    vigil_cli_init(&cli, "vigil", "test");
    vigil_cli_add_command(&cli, "run", "Run a script");

    char *argv[] = {(char *)"vigil", (char *)"run", (char *)"-h"};
    vigil_error_t error = {0};
    EXPECT_EQ(vigil_cli_parse(&cli, 3, argv, &error), VIGIL_STATUS_OK);
    vigil_cli_free(&cli);
}

/* ── Custom allocator ────────────────────────────────────────────── */

static size_t g_cli_allocs = 0;
static void *tracking_cli_alloc(void *ud, size_t s) { (void)ud; g_cli_allocs++; return calloc(1, s); }
static void *tracking_cli_realloc(void *ud, void *p, size_t s) { (void)ud; g_cli_allocs++; return realloc(p, s); }
static void tracking_cli_dealloc(void *ud, void *p) { (void)ud; free(p); }

TEST(VigilCliLibTest, CustomAllocator) {
    g_cli_allocs = 0;
    vigil_allocator_t a = {NULL, tracking_cli_alloc, tracking_cli_realloc, tracking_cli_dealloc};
    vigil_cli_t cli;
    vigil_cli_init_with_allocator(&cli, "vigil", "test", &a);
    vigil_cli_add_command(&cli, "run", "Run");
    vigil_cli_add_command(&cli, "check", "Check");

    EXPECT_GT(g_cli_allocs, 0U);
    vigil_cli_free(&cli);
}

void register_cli_lib_tests(void) {
    REGISTER_TEST(VigilCliLibTest, InitAndFree);
    REGISTER_TEST(VigilCliLibTest, MatchesSubcommand);
    REGISTER_TEST(VigilCliLibTest, UnknownCommandFails);
    REGISTER_TEST(VigilCliLibTest, BoolFlagLong);
    REGISTER_TEST(VigilCliLibTest, BoolFlagShort);
    REGISTER_TEST(VigilCliLibTest, StringFlagWithEquals);
    REGISTER_TEST(VigilCliLibTest, StringFlagWithSpace);
    REGISTER_TEST(VigilCliLibTest, ShortStringFlagAttached);
    REGISTER_TEST(VigilCliLibTest, ShortStringFlagSeparate);
    REGISTER_TEST(VigilCliLibTest, PositionalArg);
    REGISTER_TEST(VigilCliLibTest, PositionalAndFlags);
    REGISTER_TEST(VigilCliLibTest, GlobalFlag);
    REGISTER_TEST(VigilCliLibTest, GlobalPositionalNoSubcommands);
    REGISTER_TEST(VigilCliLibTest, DoubleDashStopsFlagParsing);
    REGISTER_TEST(VigilCliLibTest, UnknownFlagFails);
    REGISTER_TEST(VigilCliLibTest, MissingFlagValueFails);
    REGISTER_TEST(VigilCliLibTest, ExtraPositionalFails);
    REGISTER_TEST(VigilCliLibTest, HelpFlagReturnsOk);
    REGISTER_TEST(VigilCliLibTest, CommandHelpFlagReturnsOk);
    REGISTER_TEST(VigilCliLibTest, CustomAllocator);
}
