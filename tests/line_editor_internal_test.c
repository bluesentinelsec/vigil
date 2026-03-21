#include "vigil_test.h"
#ifdef _WIN32
#include <io.h>
#define close _close
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>

#include "platform/platform.h"

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

int vigil_platform_is_terminal(void)
{
    return g_line_editor_terminal.is_terminal;
}

vigil_status_t vigil_platform_terminal_raw(vigil_terminal_state_t **out_state, vigil_error_t *error)
{
    (void)error;

    if (g_line_editor_terminal.raw_status != VIGIL_STATUS_OK)
    {
        return g_line_editor_terminal.raw_status;
    }

    *out_state = &g_line_editor_terminal.state;
    return VIGIL_STATUS_OK;
}

void vigil_platform_terminal_restore(vigil_terminal_state_t *state)
{
    (void)state;
    g_line_editor_terminal.restore_calls++;
}

int vigil_platform_terminal_read_byte(void)
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
#undef vigil_line_history_init
#undef vigil_line_history_free
#undef vigil_line_history_add
#undef vigil_line_history_get
#undef vigil_line_history_clear
#undef vigil_line_history_load
#undef vigil_line_history_save
#undef vigil_line_editor_readline

static FILE *line_editor_capture_stdout(int *saved_stdout)
{
    FILE *tmp = tmpfile();

    if (tmp == NULL)
    {
        return NULL;
    }

    *saved_stdout = dup(fileno(stdout));
    if (*saved_stdout < 0)
    {
        fclose(tmp);
        return NULL;
    }

    fflush(stdout);
    if (dup2(fileno(tmp), fileno(stdout)) < 0)
    {
        close(*saved_stdout);
        fclose(tmp);
        return NULL;
    }

    return tmp;
}

static void line_editor_restore_stdout(FILE *tmp, int saved_stdout)
{
    fflush(stdout);
    dup2(saved_stdout, fileno(stdout));
    close(saved_stdout);
    fclose(tmp);
}

TEST(LineEditorInternalTest, ReadKeyTranslatesEscapeSequences)
{
    static const int bytes[] = {
        KEY_ESC, '[', 'A',     KEY_ESC, '[',     'B',     KEY_ESC, '[',     'C',     KEY_ESC, '[', 'D',     KEY_ESC,
        '[',     'H', KEY_ESC, '[',     'F',     KEY_ESC, '[',     '1',     '~',     KEY_ESC, '[', '3',     '~',
        KEY_ESC, '[', '4',     '~',     KEY_ESC, '[',     '7',     '~',     KEY_ESC, '[',     '8', '~',     KEY_ESC,
        'O',     'H', KEY_ESC, 'O',     'F',     KEY_ESC, 'b',     KEY_ESC, 'f',     KEY_ESC, 'd', KEY_ESC, 'x',
        KEY_ESC, '[', '9',     '~',     KEY_ESC, '[',     'Z',     KEY_ESC, -1};

    line_editor_mock_reset();
    line_editor_mock_bytes(bytes, sizeof(bytes) / sizeof(bytes[0]));

    EXPECT_EQ(read_key(), KEY_ARROW_UP);
    EXPECT_EQ(read_key(), KEY_ARROW_DOWN);
    EXPECT_EQ(read_key(), KEY_ARROW_RIGHT);
    EXPECT_EQ(read_key(), KEY_ARROW_LEFT);
    EXPECT_EQ(read_key(), KEY_HOME);
    EXPECT_EQ(read_key(), KEY_END);
    EXPECT_EQ(read_key(), KEY_HOME);
    EXPECT_EQ(read_key(), KEY_DELETE);
    EXPECT_EQ(read_key(), KEY_END);
    EXPECT_EQ(read_key(), KEY_HOME);
    EXPECT_EQ(read_key(), KEY_END);
    EXPECT_EQ(read_key(), KEY_HOME);
    EXPECT_EQ(read_key(), KEY_END);
    EXPECT_EQ(read_key(), KEY_ALT_B);
    EXPECT_EQ(read_key(), KEY_ALT_F);
    EXPECT_EQ(read_key(), KEY_ALT_D);
    EXPECT_EQ(read_key(), KEY_NONE);
    EXPECT_EQ(read_key(), KEY_NONE);
    EXPECT_EQ(read_key(), KEY_NONE);
    EXPECT_EQ(read_key(), KEY_ESC);
}

TEST(LineEditorInternalTest, LineBufferHelpersMutateState)
{
    char buf[32];
    char small_buf[8];
    line_buf_t lb;
    line_buf_t small;

    lb_init(&lb, buf, sizeof(buf));
    lb_init(&small, small_buf, sizeof(small_buf));
    EXPECT_EQ(lb.len, 0u);
    EXPECT_EQ(lb.pos, 0u);

    lb_insert(&lb, 'a');
    lb_insert(&lb, 'c');
    lb.pos = 1;
    lb_insert(&lb, 'b');
    EXPECT_STREQ(lb.buf, "abc");

    lb_delete_at(&lb, 1);
    EXPECT_STREQ(lb.buf, "ac");
    lb_delete_at(&lb, lb.len);

    lb_set(&small, "abcdefghi");
    EXPECT_STREQ(small.buf, "abcdefg");

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "abcdefg");
    lb.pos = 4;
    lb_kill_to_end(&lb);
    EXPECT_STREQ(lb.buf, "abcd");

    lb_set(&lb, "abcd");
    lb.pos = 2;
    lb_kill_to_start(&lb);
    EXPECT_STREQ(lb.buf, "cd");

    lb_set(&lb, "one two three");
    lb.pos = lb.len;
    lb_move_word_backward(&lb);
    EXPECT_EQ(lb.pos, 8u);
    lb_move_word_backward(&lb);
    EXPECT_EQ(lb.pos, 4u);
    lb_move_word_forward(&lb);
    EXPECT_EQ(lb.pos, 7u);
    lb_move_word_forward(&lb);
    EXPECT_EQ(lb.pos, 13u);

    lb_set(&lb, "one two three");
    lb.pos = 7;
    lb_kill_word_backward(&lb);
    EXPECT_STREQ(lb.buf, "one  three");

    lb_set(&lb, "one two three");
    lb.pos = 4;
    lb_kill_word_forward(&lb);
    EXPECT_STREQ(lb.buf, "one  three");

    lb_set(&lb, "ab");
    lb.pos = 0;
    lb_transpose_chars(&lb);
    EXPECT_STREQ(lb.buf, "ab");
    lb.pos = 1;
    lb_transpose_chars(&lb);
    EXPECT_STREQ(lb.buf, "ba");
}

TEST(LineEditorInternalTest, HistoryHelpersNavigateAndPreserveSavedLine)
{
    char buf[32];
    line_buf_t lb;
    line_history_nav_t nav;
    vigil_line_history_t history;

    line_editor_test_history_init(&history, 10);
    line_editor_test_history_add(&history, "alpha");
    line_editor_test_history_add(&history, "beta");

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "draft");

    nav.history = &history;
    nav.index = history.count;
    nav.saved_line = NULL;

    EXPECT_TRUE(line_history_can_move_up(&nav));
    EXPECT_FALSE(line_history_can_move_down(&nav));

    line_history_move_up(&lb, &nav);
    EXPECT_STREQ(lb.buf, "beta");
    EXPECT_STREQ(nav.saved_line, "draft");

    line_history_move_up(&lb, &nav);
    EXPECT_STREQ(lb.buf, "alpha");

    line_history_move_down(&lb, &nav);
    EXPECT_STREQ(lb.buf, "beta");

    line_history_move_down(&lb, &nav);
    EXPECT_STREQ(lb.buf, "draft");

    free(nav.saved_line);
    line_editor_test_history_free(&history);
}

TEST(LineEditorInternalTest, EditHandlersCoverTerminalDeleteAndCursorPaths)
{
    char buf[32];
    line_buf_t lb;
    FILE *tmp = NULL;
    int saved_stdout = -1;
    char *saved_line = line_strdup("draft");

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "abc");
    lb.pos = 1;

    tmp = line_editor_capture_stdout(&saved_stdout);
    ASSERT_NE(tmp, NULL);

    EXPECT_EQ(edit_line_handle_terminal_keys(KEY_ENTER, &lb, &saved_line), LINE_EDIT_SUBMIT);
    EXPECT_EQ(saved_line, NULL);

    lb_set(&lb, "abc");
    EXPECT_EQ(edit_line_handle_terminal_keys(KEY_CTRL_C, &lb, &saved_line), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "");

    EXPECT_EQ(edit_line_handle_terminal_keys(KEY_CTRL_D, &lb, &saved_line), LINE_EDIT_EOF);

    lb_set(&lb, "abc");
    lb.pos = 1;
    EXPECT_EQ(edit_line_handle_terminal_keys(KEY_CTRL_D, &lb, &saved_line), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "ac");
    EXPECT_EQ(edit_line_handle_terminal_keys(-1, &lb, &saved_line), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "a");

    line_editor_restore_stdout(tmp, saved_stdout);

    lb_set(&lb, "abcd");
    lb.pos = 4;
    EXPECT_EQ(edit_line_handle_delete_keys(KEY_BACKSPACE, &lb), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "abc");
    EXPECT_EQ(edit_line_handle_delete_keys(KEY_CTRL_H, &lb), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "ab");
    lb.pos = 0;
    EXPECT_EQ(edit_line_handle_delete_keys(KEY_DELETE, &lb), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "b");
    EXPECT_EQ(edit_line_handle_delete_keys(KEY_TAB, &lb), LINE_EDIT_UNHANDLED);

    lb_set(&lb, "abc");
    lb.pos = 1;
    EXPECT_EQ(edit_line_handle_cursor_keys(KEY_ARROW_LEFT, &lb), LINE_EDIT_REFRESH);
    EXPECT_EQ(lb.pos, 0u);
    EXPECT_EQ(edit_line_handle_cursor_keys(KEY_CTRL_B, &lb), LINE_EDIT_REFRESH);
    EXPECT_EQ(lb.pos, 0u);
    EXPECT_EQ(edit_line_handle_cursor_keys(KEY_ARROW_RIGHT, &lb), LINE_EDIT_REFRESH);
    EXPECT_EQ(lb.pos, 1u);
    EXPECT_EQ(edit_line_handle_cursor_keys(KEY_CTRL_F, &lb), LINE_EDIT_REFRESH);
    EXPECT_EQ(lb.pos, 2u);
    EXPECT_EQ(edit_line_handle_cursor_keys(KEY_HOME, &lb), LINE_EDIT_UNHANDLED);

    lb.pos = 1;
    EXPECT_EQ(edit_line_handle_home_end_keys(KEY_HOME, &lb), LINE_EDIT_REFRESH);
    EXPECT_EQ(lb.pos, 0u);
    EXPECT_EQ(edit_line_handle_home_end_keys(KEY_CTRL_E, &lb), LINE_EDIT_REFRESH);
    EXPECT_EQ(lb.pos, lb.len);
    EXPECT_EQ(edit_line_handle_home_end_keys(KEY_ARROW_UP, &lb), LINE_EDIT_UNHANDLED);
}

TEST(LineEditorInternalTest, EditHandlersCoverHistoryKillWordAndLiteralPaths)
{
    char buf[32];
    line_buf_t lb;
    line_history_nav_t nav;
    vigil_line_history_t history;
    FILE *tmp = NULL;
    int saved_stdout = -1;

    line_editor_test_history_init(&history, 10);
    line_editor_test_history_add(&history, "alpha");
    line_editor_test_history_add(&history, "beta");

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "draft");
    nav.history = &history;
    nav.index = history.count;
    nav.saved_line = NULL;

    EXPECT_EQ(edit_line_handle_history_keys(KEY_ARROW_UP, &lb, &nav), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "beta");
    EXPECT_EQ(edit_line_handle_history_keys(KEY_CTRL_P, &lb, &nav), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "alpha");
    EXPECT_EQ(edit_line_handle_history_keys(KEY_ARROW_DOWN, &lb, &nav), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "beta");
    EXPECT_EQ(edit_line_handle_history_keys(KEY_CTRL_N, &lb, &nav), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "draft");
    EXPECT_EQ(edit_line_handle_history_keys(KEY_TAB, &lb, &nav), LINE_EDIT_UNHANDLED);

    lb_set(&lb, "abc def");
    lb.pos = 3;
    EXPECT_EQ(edit_line_handle_kill_keys(KEY_CTRL_K, &lb), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "abc");

    lb_set(&lb, "abc def");
    lb.pos = 4;
    EXPECT_EQ(edit_line_handle_kill_keys(KEY_CTRL_U, &lb), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "def");

    lb_set(&lb, "abc def");
    lb.pos = lb.len;
    EXPECT_EQ(edit_line_handle_kill_keys(KEY_CTRL_W, &lb), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "abc ");

    lb_set(&lb, "ab");
    lb.pos = 1;
    EXPECT_EQ(edit_line_handle_kill_keys(KEY_CTRL_T, &lb), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "ba");

    tmp = line_editor_capture_stdout(&saved_stdout);
    ASSERT_NE(tmp, NULL);
    EXPECT_EQ(edit_line_handle_kill_keys(KEY_CTRL_L, &lb), LINE_EDIT_REFRESH);
    line_editor_restore_stdout(tmp, saved_stdout);
    EXPECT_EQ(edit_line_handle_kill_keys(KEY_TAB, &lb), LINE_EDIT_UNHANDLED);

    lb_set(&lb, "one two three");
    lb.pos = lb.len;
    EXPECT_EQ(edit_line_handle_word_keys(KEY_ALT_B, &lb), LINE_EDIT_REFRESH);
    EXPECT_EQ(lb.pos, 8u);
    EXPECT_EQ(edit_line_handle_word_keys(KEY_ALT_F, &lb), LINE_EDIT_REFRESH);
    EXPECT_EQ(lb.pos, lb.len);
    lb.pos = 4;
    EXPECT_EQ(edit_line_handle_word_keys(KEY_ALT_D, &lb), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "one  three");
    EXPECT_EQ(edit_line_handle_word_keys(KEY_TAB, &lb), LINE_EDIT_UNHANDLED);

    lb_set(&lb, "ab");
    lb.pos = lb.len;
    EXPECT_EQ(edit_line_handle_literal_key('c', &lb), LINE_EDIT_REFRESH);
    EXPECT_STREQ(lb.buf, "abc");
    EXPECT_EQ(edit_line_handle_literal_key(KEY_TAB, &lb), LINE_EDIT_UNHANDLED);
    EXPECT_EQ(edit_line_handle_literal_key(KEY_NONE, &lb), LINE_EDIT_UNHANDLED);
    EXPECT_EQ(edit_line_handle_literal_key(KEY_ESC, &lb), LINE_EDIT_UNHANDLED);

    free(nav.saved_line);
    line_editor_test_history_free(&history);
}

TEST(LineEditorInternalTest, EditLineAndReadlineCoverTerminalPaths)
{
    static const int edit_bytes[] = {'1', '2', KEY_ENTER};
    static const int eof_bytes[] = {KEY_CTRL_D};
    char buf[32];
    vigil_error_t error = {0};
    vigil_line_history_t history;
    FILE *tmp = NULL;
    int saved_stdout = -1;

    line_editor_test_history_init(&history, 10);
    line_editor_mock_reset();
    line_editor_mock_bytes(edit_bytes, sizeof(edit_bytes) / sizeof(edit_bytes[0]));
    tmp = line_editor_capture_stdout(&saved_stdout);
    ASSERT_NE(tmp, NULL);
    EXPECT_EQ(edit_line(">>> ", buf, sizeof(buf), &history), VIGIL_STATUS_OK);
    line_editor_restore_stdout(tmp, saved_stdout);
    EXPECT_STREQ(buf, "12");

    line_editor_mock_reset();
    g_line_editor_terminal.raw_status = VIGIL_STATUS_INVALID_ARGUMENT;
    EXPECT_EQ(line_editor_test_readline(">>> ", buf, sizeof(buf), &history, &error), VIGIL_STATUS_INVALID_ARGUMENT);

    line_editor_mock_reset();
    line_editor_mock_bytes(eof_bytes, sizeof(eof_bytes) / sizeof(eof_bytes[0]));
    tmp = line_editor_capture_stdout(&saved_stdout);
    ASSERT_NE(tmp, NULL);
    EXPECT_EQ(line_editor_test_readline(">>> ", buf, sizeof(buf), &history, &error), VIGIL_STATUS_INTERNAL);
    line_editor_restore_stdout(tmp, saved_stdout);
    EXPECT_STREQ(buf, "");
    EXPECT_EQ(g_line_editor_terminal.restore_calls, 1);

    EXPECT_EQ(line_editor_test_readline(">>> ", NULL, sizeof(buf), &history, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(line_editor_test_readline(">>> ", buf, 0, &history, &error), VIGIL_STATUS_INVALID_ARGUMENT);

    line_editor_test_history_free(&history);
    vigil_error_clear(&error);
}

void register_line_editor_internal_tests(void)
{
    REGISTER_TEST(LineEditorInternalTest, ReadKeyTranslatesEscapeSequences);
    REGISTER_TEST(LineEditorInternalTest, LineBufferHelpersMutateState);
    REGISTER_TEST(LineEditorInternalTest, HistoryHelpersNavigateAndPreserveSavedLine);
    REGISTER_TEST(LineEditorInternalTest, EditHandlersCoverTerminalDeleteAndCursorPaths);
    REGISTER_TEST(LineEditorInternalTest, EditHandlersCoverHistoryKillWordAndLiteralPaths);
    REGISTER_TEST(LineEditorInternalTest, EditLineAndReadlineCoverTerminalPaths);
}
