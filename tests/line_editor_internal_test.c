#include "vigil_test.h"
#include <string.h>

#ifdef _WIN32
#define VIGIL_EXPORTS
#endif
#include "platform/platform.h"

static void line_editor_test_error_set_literal(vigil_error_t *error, vigil_status_t type, const char *value)
{
    (void)error;
    (void)type;
    (void)value;
}

#define vigil_platform_is_terminal line_editor_test_platform_is_terminal
#define vigil_platform_terminal_raw line_editor_test_platform_terminal_raw
#define vigil_platform_terminal_restore line_editor_test_platform_terminal_restore
#define vigil_platform_terminal_read_byte line_editor_test_platform_terminal_read_byte
#define vigil_error_set_literal line_editor_test_error_set_literal

typedef struct vigil_terminal_state
{
    int dummy;
} vigil_terminal_state_t;

typedef struct
{
    const int *bytes;
    size_t count;
    size_t index;
    int is_terminal;
    vigil_status_t raw_status;
    int restore_calls;
    vigil_terminal_state_t state;
} line_editor_mock_terminal_t;

static line_editor_mock_terminal_t g_line_editor_terminal;

static void line_editor_mock_reset(void)
{
    memset(&g_line_editor_terminal, 0, sizeof(g_line_editor_terminal));
    g_line_editor_terminal.is_terminal = 1;
    g_line_editor_terminal.raw_status = VIGIL_STATUS_OK;
}

static void line_editor_mock_bytes(const int *bytes, size_t count)
{
    g_line_editor_terminal.bytes = bytes;
    g_line_editor_terminal.count = count;
    g_line_editor_terminal.index = 0;
}

VIGIL_API int vigil_platform_is_terminal(void)
{
    return g_line_editor_terminal.is_terminal;
}

VIGIL_API vigil_status_t vigil_platform_terminal_raw(vigil_terminal_state_t **out_state, vigil_error_t *error)
{
    (void)error;

    if (g_line_editor_terminal.raw_status != VIGIL_STATUS_OK)
    {
        return g_line_editor_terminal.raw_status;
    }

    *out_state = &g_line_editor_terminal.state;
    return VIGIL_STATUS_OK;
}

VIGIL_API void vigil_platform_terminal_restore(vigil_terminal_state_t *state)
{
    (void)state;
    g_line_editor_terminal.restore_calls++;
}

VIGIL_API int vigil_platform_terminal_read_byte(void)
{
    if (g_line_editor_terminal.index >= g_line_editor_terminal.count)
    {
        return -1;
    }

    return g_line_editor_terminal.bytes[g_line_editor_terminal.index++];
}

#define vigil_line_history_init line_editor_test_history_init
#define vigil_line_history_free line_editor_test_history_free
#define vigil_line_history_add line_editor_test_history_add
#define vigil_line_history_get line_editor_test_history_get
#define vigil_line_history_clear line_editor_test_history_clear
#define vigil_line_history_load line_editor_test_history_load
#define vigil_line_history_save line_editor_test_history_save
#define vigil_line_editor_readline line_editor_test_readline
#include "../src/platform/line_editor.c"
#undef vigil_error_set_literal
#undef vigil_platform_is_terminal
#undef vigil_platform_terminal_raw
#undef vigil_platform_terminal_restore
#undef vigil_platform_terminal_read_byte
#undef vigil_line_history_init
#undef vigil_line_history_free
#undef vigil_line_history_add
#undef vigil_line_history_get
#undef vigil_line_history_clear
#undef vigil_line_history_load
#undef vigil_line_history_save
#undef vigil_line_editor_readline

static int line_editor_expect_keys(const int *bytes, size_t byte_count, const int *expected, size_t expected_count)
{
    size_t i;

    line_editor_mock_reset();
    line_editor_mock_bytes(bytes, byte_count);
    for (i = 0; i < expected_count; i++)
    {
        if (read_key() != expected[i])
        {
            return 0;
        }
    }

    return 1;
}

static int line_editor_insert_delete_case(void)
{
    char buf[32];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_insert(&lb, 'a');
    lb_insert(&lb, 'c');
    lb.pos = 1;
    lb_insert(&lb, 'b');
    lb_delete_at(&lb, 1);
    lb_delete_at(&lb, lb.len);
    return strcmp(lb.buf, "ac") == 0;
}

static int line_editor_truncate_case(void)
{
    char buf[8];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "abcdefghi");
    return strcmp(lb.buf, "abcdefg") == 0;
}

static int line_editor_kill_ranges_case(void)
{
    char buf[32];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "abcdefg");
    lb.pos = 4;
    lb_kill_to_end(&lb);
    if (strcmp(lb.buf, "abcd") != 0)
    {
        return 0;
    }

    lb_set(&lb, "abcd");
    lb.pos = 2;
    lb_kill_to_start(&lb);
    return strcmp(lb.buf, "cd") == 0;
}

static int line_editor_word_motion_case(void)
{
    char buf[32];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "one two three");
    lb.pos = lb.len;
    lb_move_word_backward(&lb);
    if (lb.pos != 8u)
    {
        return 0;
    }

    lb_move_word_backward(&lb);
    if (lb.pos != 4u)
    {
        return 0;
    }

    lb_move_word_forward(&lb);
    if (lb.pos != 7u)
    {
        return 0;
    }

    lb_move_word_forward(&lb);
    return lb.pos == 13u;
}

static int line_editor_word_delete_case(void)
{
    char buf[32];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "one two three");
    lb.pos = 7;
    lb_kill_word_backward(&lb);
    if (strcmp(lb.buf, "one  three") != 0)
    {
        return 0;
    }

    lb_set(&lb, "one two three");
    lb.pos = 4;
    lb_kill_word_forward(&lb);
    return strcmp(lb.buf, "one  three") == 0;
}

static int line_editor_transpose_case(void)
{
    char buf[32];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "ab");
    lb.pos = 0;
    lb_transpose_chars(&lb);
    if (strcmp(lb.buf, "ab") != 0)
    {
        return 0;
    }

    lb.pos = 1;
    lb_transpose_chars(&lb);
    return strcmp(lb.buf, "ba") == 0;
}

static int line_editor_history_navigation_case(void)
{
    char buf[32];
    line_buf_t lb;
    line_history_nav_t nav;
    vigil_line_history_t history;
    int ok = 1;

    line_editor_test_history_init(&history, 10);
    line_editor_test_history_add(&history, "alpha");
    line_editor_test_history_add(&history, "beta");

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "draft");
    nav.history = &history;
    nav.index = history.count;
    nav.saved_line = NULL;

    if (!line_history_can_move_up(&nav) || line_history_can_move_down(&nav))
    {
        ok = 0;
        goto cleanup;
    }

    line_history_move_up(&lb, &nav);
    if (strcmp(lb.buf, "beta") != 0 || nav.saved_line == NULL || strcmp(nav.saved_line, "draft") != 0)
    {
        ok = 0;
        goto cleanup;
    }

    line_history_move_up(&lb, &nav);
    if (strcmp(lb.buf, "alpha") != 0)
    {
        ok = 0;
        goto cleanup;
    }

    line_history_move_down(&lb, &nav);
    if (strcmp(lb.buf, "beta") != 0)
    {
        ok = 0;
        goto cleanup;
    }

    line_history_move_down(&lb, &nav);
    ok = strcmp(lb.buf, "draft") == 0;

cleanup:
    free(nav.saved_line);
    line_editor_test_history_free(&history);
    return ok;
}

static int line_editor_terminal_delete_and_eof_case(void)
{
    char buf[32];
    line_buf_t lb;
    char *saved_line = NULL;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "abc");
    lb.pos = 1;
    if (edit_line_handle_terminal_keys(KEY_CTRL_D, &lb, &saved_line) != LINE_EDIT_REFRESH || strcmp(lb.buf, "ac") != 0)
    {
        return 0;
    }

    if (edit_line_handle_terminal_keys(-1, &lb, &saved_line) != LINE_EDIT_REFRESH || strcmp(lb.buf, "a") != 0)
    {
        return 0;
    }

    lb_set(&lb, "");
    return edit_line_handle_terminal_keys(KEY_CTRL_D, &lb, &saved_line) == LINE_EDIT_EOF;
}

static int line_editor_delete_keys_case(void)
{
    char buf[32];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "abcd");
    lb.pos = 4;
    if (edit_line_handle_delete_keys(KEY_BACKSPACE, &lb) != LINE_EDIT_REFRESH || strcmp(lb.buf, "abc") != 0)
    {
        return 0;
    }

    if (edit_line_handle_delete_keys(KEY_CTRL_H, &lb) != LINE_EDIT_REFRESH || strcmp(lb.buf, "ab") != 0)
    {
        return 0;
    }

    lb.pos = 0;
    if (edit_line_handle_delete_keys(KEY_DELETE, &lb) != LINE_EDIT_REFRESH || strcmp(lb.buf, "b") != 0)
    {
        return 0;
    }

    return edit_line_handle_delete_keys(KEY_TAB, &lb) == LINE_EDIT_UNHANDLED;
}

static int line_editor_cursor_keys_case(void)
{
    char buf[32];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "abc");
    lb.pos = 1;
    if (edit_line_handle_cursor_keys(KEY_ARROW_LEFT, &lb) != LINE_EDIT_REFRESH || lb.pos != 0u)
    {
        return 0;
    }

    if (edit_line_handle_cursor_keys(KEY_CTRL_B, &lb) != LINE_EDIT_REFRESH || lb.pos != 0u)
    {
        return 0;
    }

    if (edit_line_handle_cursor_keys(KEY_ARROW_RIGHT, &lb) != LINE_EDIT_REFRESH || lb.pos != 1u)
    {
        return 0;
    }

    if (edit_line_handle_cursor_keys(KEY_CTRL_F, &lb) != LINE_EDIT_REFRESH || lb.pos != 2u)
    {
        return 0;
    }

    return edit_line_handle_cursor_keys(KEY_HOME, &lb) == LINE_EDIT_UNHANDLED;
}

static int line_editor_home_end_keys_case(void)
{
    char buf[32];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "abc");
    lb.pos = 1;
    if (edit_line_handle_home_end_keys(KEY_HOME, &lb) != LINE_EDIT_REFRESH || lb.pos != 0u)
    {
        return 0;
    }

    if (edit_line_handle_home_end_keys(KEY_CTRL_E, &lb) != LINE_EDIT_REFRESH || lb.pos != lb.len)
    {
        return 0;
    }

    return edit_line_handle_home_end_keys(KEY_ARROW_UP, &lb) == LINE_EDIT_UNHANDLED;
}

static int line_editor_history_keys_case(void)
{
    char buf[32];
    line_buf_t lb;
    line_history_nav_t nav;
    vigil_line_history_t history;
    int ok = 1;

    line_editor_test_history_init(&history, 10);
    line_editor_test_history_add(&history, "alpha");
    line_editor_test_history_add(&history, "beta");

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "draft");
    nav.history = &history;
    nav.index = history.count;
    nav.saved_line = NULL;

    if (edit_line_handle_history_keys(KEY_ARROW_UP, &lb, &nav) != LINE_EDIT_REFRESH || strcmp(lb.buf, "beta") != 0)
    {
        ok = 0;
        goto cleanup;
    }

    if (edit_line_handle_history_keys(KEY_CTRL_P, &lb, &nav) != LINE_EDIT_REFRESH || strcmp(lb.buf, "alpha") != 0)
    {
        ok = 0;
        goto cleanup;
    }

    if (edit_line_handle_history_keys(KEY_ARROW_DOWN, &lb, &nav) != LINE_EDIT_REFRESH || strcmp(lb.buf, "beta") != 0)
    {
        ok = 0;
        goto cleanup;
    }

    if (edit_line_handle_history_keys(KEY_CTRL_N, &lb, &nav) != LINE_EDIT_REFRESH || strcmp(lb.buf, "draft") != 0)
    {
        ok = 0;
        goto cleanup;
    }

    ok = edit_line_handle_history_keys(KEY_TAB, &lb, &nav) == LINE_EDIT_UNHANDLED;

cleanup:
    free(nav.saved_line);
    line_editor_test_history_free(&history);
    return ok;
}

static int line_editor_kill_keys_case(void)
{
    char buf[32];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "abc def");
    lb.pos = 3;
    if (edit_line_handle_kill_keys(KEY_CTRL_K, &lb) != LINE_EDIT_REFRESH || strcmp(lb.buf, "abc") != 0)
    {
        return 0;
    }

    lb_set(&lb, "abc def");
    lb.pos = 4;
    if (edit_line_handle_kill_keys(KEY_CTRL_U, &lb) != LINE_EDIT_REFRESH || strcmp(lb.buf, "def") != 0)
    {
        return 0;
    }

    lb_set(&lb, "abc def");
    lb.pos = lb.len;
    if (edit_line_handle_kill_keys(KEY_CTRL_W, &lb) != LINE_EDIT_REFRESH || strcmp(lb.buf, "abc ") != 0)
    {
        return 0;
    }

    lb_set(&lb, "ab");
    lb.pos = 1;
    if (edit_line_handle_kill_keys(KEY_CTRL_T, &lb) != LINE_EDIT_REFRESH || strcmp(lb.buf, "ba") != 0)
    {
        return 0;
    }

    return edit_line_handle_kill_keys(KEY_TAB, &lb) == LINE_EDIT_UNHANDLED;
}

static int line_editor_word_keys_case(void)
{
    char buf[32];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "one two three");
    lb.pos = lb.len;
    if (edit_line_handle_word_keys(KEY_ALT_B, &lb) != LINE_EDIT_REFRESH || lb.pos != 8u)
    {
        return 0;
    }

    if (edit_line_handle_word_keys(KEY_ALT_F, &lb) != LINE_EDIT_REFRESH || lb.pos != lb.len)
    {
        return 0;
    }

    lb.pos = 4;
    if (edit_line_handle_word_keys(KEY_ALT_D, &lb) != LINE_EDIT_REFRESH || strcmp(lb.buf, "one  three") != 0)
    {
        return 0;
    }

    return edit_line_handle_word_keys(KEY_TAB, &lb) == LINE_EDIT_UNHANDLED;
}

static int line_editor_literal_keys_case(void)
{
    char buf[32];
    line_buf_t lb;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "ab");
    lb.pos = lb.len;
    if (edit_line_handle_literal_key('c', &lb) != LINE_EDIT_REFRESH || strcmp(lb.buf, "abc") != 0)
    {
        return 0;
    }

    if (edit_line_handle_literal_key(KEY_TAB, &lb) != LINE_EDIT_UNHANDLED)
    {
        return 0;
    }

    if (edit_line_handle_literal_key(KEY_NONE, &lb) != LINE_EDIT_UNHANDLED)
    {
        return 0;
    }

    return edit_line_handle_literal_key(KEY_ESC, &lb) == LINE_EDIT_UNHANDLED;
}

static int line_editor_readline_invalid_args_case(void)
{
    char buf[32];
    vigil_error_t error = {0};
    int ok = 1;

    line_editor_mock_reset();
    if (line_editor_test_readline(">>> ", NULL, sizeof(buf), NULL, &error) != VIGIL_STATUS_INVALID_ARGUMENT)
    {
        ok = 0;
    }

    if (line_editor_test_readline(">>> ", buf, 0, NULL, &error) != VIGIL_STATUS_INVALID_ARGUMENT)
    {
        ok = 0;
    }

    vigil_error_clear(&error);
    return ok;
}

static int line_editor_readline_raw_error_case(void)
{
    char buf[32];
    vigil_error_t error = {0};

    line_editor_mock_reset();
    g_line_editor_terminal.raw_status = VIGIL_STATUS_INVALID_ARGUMENT;
    if (line_editor_test_readline(">>> ", buf, sizeof(buf), NULL, &error) != VIGIL_STATUS_INVALID_ARGUMENT)
    {
        vigil_error_clear(&error);
        return 0;
    }

    vigil_error_clear(&error);
    return 1;
}

TEST(LineEditorInternalTest, ReadKeyBracketSequences)
{
    static const int bytes[] = {KEY_ESC, '[', 'A', KEY_ESC, '[', 'B', KEY_ESC, '[', 'C', KEY_ESC, '[', 'D'};
    static const int expected[] = {KEY_ARROW_UP, KEY_ARROW_DOWN, KEY_ARROW_RIGHT, KEY_ARROW_LEFT};
    EXPECT_TRUE(line_editor_expect_keys(bytes, sizeof(bytes) / sizeof(bytes[0]), expected,
                                        sizeof(expected) / sizeof(expected[0])));
}

TEST(LineEditorInternalTest, ReadKeyNumericSequences)
{
    static const int bytes[] = {KEY_ESC, '[', '1', '~', KEY_ESC, '[', '3', '~', KEY_ESC, '[', '8', '~'};
    static const int expected[] = {KEY_HOME, KEY_DELETE, KEY_END};
    EXPECT_TRUE(line_editor_expect_keys(bytes, sizeof(bytes) / sizeof(bytes[0]), expected,
                                        sizeof(expected) / sizeof(expected[0])));
}

TEST(LineEditorInternalTest, ReadKeyOSequencesAndAlt)
{
    static const int bytes[] = {KEY_ESC, 'O', 'H', KEY_ESC, 'O', 'F', KEY_ESC, 'b', KEY_ESC, 'f', KEY_ESC, 'd'};
    static const int expected[] = {KEY_HOME, KEY_END, KEY_ALT_B, KEY_ALT_F, KEY_ALT_D};
    EXPECT_TRUE(line_editor_expect_keys(bytes, sizeof(bytes) / sizeof(bytes[0]), expected,
                                        sizeof(expected) / sizeof(expected[0])));
}

TEST(LineEditorInternalTest, ReadKeyUnknownAndEscFallback)
{
    static const int bytes[] = {KEY_ESC, 'x', KEY_ESC, '[', 'Z', KEY_ESC};
    static const int expected[] = {KEY_NONE, KEY_NONE, KEY_ESC};
    EXPECT_TRUE(line_editor_expect_keys(bytes, sizeof(bytes) / sizeof(bytes[0]), expected,
                                        sizeof(expected) / sizeof(expected[0])));
}

TEST(LineEditorInternalTest, LineBufferInsertDeleteWorks)
{
    EXPECT_TRUE(line_editor_insert_delete_case());
}

TEST(LineEditorInternalTest, LineBufferSetTruncatesToCapacity)
{
    EXPECT_TRUE(line_editor_truncate_case());
}

TEST(LineEditorInternalTest, LineBufferKillRangesWork)
{
    EXPECT_TRUE(line_editor_kill_ranges_case());
}

TEST(LineEditorInternalTest, LineBufferMovesByWord)
{
    EXPECT_TRUE(line_editor_word_motion_case());
}

TEST(LineEditorInternalTest, LineBufferKillsWords)
{
    EXPECT_TRUE(line_editor_word_delete_case());
}

TEST(LineEditorInternalTest, LineBufferTransposesChars)
{
    EXPECT_TRUE(line_editor_transpose_case());
}

TEST(LineEditorInternalTest, HistoryNavigationPreservesDraft)
{
    EXPECT_TRUE(line_editor_history_navigation_case());
}

TEST(LineEditorInternalTest, TerminalCtrlDDeletesAndEndsOnEmpty)
{
    EXPECT_TRUE(line_editor_terminal_delete_and_eof_case());
}

TEST(LineEditorInternalTest, DeleteKeysMutateBuffer)
{
    EXPECT_TRUE(line_editor_delete_keys_case());
}

TEST(LineEditorInternalTest, CursorKeysMoveWithinBuffer)
{
    EXPECT_TRUE(line_editor_cursor_keys_case());
}

TEST(LineEditorInternalTest, HomeEndKeysJumpToEdges)
{
    EXPECT_TRUE(line_editor_home_end_keys_case());
}

TEST(LineEditorInternalTest, HistoryKeysTraverseEntries)
{
    EXPECT_TRUE(line_editor_history_keys_case());
}

TEST(LineEditorInternalTest, KillKeysMutateBuffer)
{
    EXPECT_TRUE(line_editor_kill_keys_case());
}

TEST(LineEditorInternalTest, WordKeysMutateBuffer)
{
    EXPECT_TRUE(line_editor_word_keys_case());
}

TEST(LineEditorInternalTest, LiteralKeysInsertPrintableChars)
{
    EXPECT_TRUE(line_editor_literal_keys_case());
}

TEST(LineEditorInternalTest, ReadlineRejectsInvalidArgs)
{
    EXPECT_TRUE(line_editor_readline_invalid_args_case());
}

TEST(LineEditorInternalTest, ReadlinePropagatesRawModeError)
{
    EXPECT_TRUE(line_editor_readline_raw_error_case());
}

void register_line_editor_internal_tests(void)
{
    REGISTER_TEST(LineEditorInternalTest, ReadKeyBracketSequences);
    REGISTER_TEST(LineEditorInternalTest, ReadKeyNumericSequences);
    REGISTER_TEST(LineEditorInternalTest, ReadKeyOSequencesAndAlt);
    REGISTER_TEST(LineEditorInternalTest, ReadKeyUnknownAndEscFallback);
    REGISTER_TEST(LineEditorInternalTest, LineBufferInsertDeleteWorks);
    REGISTER_TEST(LineEditorInternalTest, LineBufferSetTruncatesToCapacity);
    REGISTER_TEST(LineEditorInternalTest, LineBufferKillRangesWork);
    REGISTER_TEST(LineEditorInternalTest, LineBufferMovesByWord);
    REGISTER_TEST(LineEditorInternalTest, LineBufferKillsWords);
    REGISTER_TEST(LineEditorInternalTest, LineBufferTransposesChars);
    REGISTER_TEST(LineEditorInternalTest, HistoryNavigationPreservesDraft);
    REGISTER_TEST(LineEditorInternalTest, TerminalCtrlDDeletesAndEndsOnEmpty);
    REGISTER_TEST(LineEditorInternalTest, DeleteKeysMutateBuffer);
    REGISTER_TEST(LineEditorInternalTest, CursorKeysMoveWithinBuffer);
    REGISTER_TEST(LineEditorInternalTest, HomeEndKeysJumpToEdges);
    REGISTER_TEST(LineEditorInternalTest, HistoryKeysTraverseEntries);
    REGISTER_TEST(LineEditorInternalTest, KillKeysMutateBuffer);
    REGISTER_TEST(LineEditorInternalTest, WordKeysMutateBuffer);
    REGISTER_TEST(LineEditorInternalTest, LiteralKeysInsertPrintableChars);
    REGISTER_TEST(LineEditorInternalTest, ReadlineRejectsInvalidArgs);
    REGISTER_TEST(LineEditorInternalTest, ReadlinePropagatesRawModeError);
}
