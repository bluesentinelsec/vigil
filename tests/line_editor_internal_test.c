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

static int line_editor_run_with_stdout(vigil_status_t (*fn)(void *ctx), void *ctx)
{
    FILE *tmp = NULL;
    int saved_stdout = -1;
    vigil_status_t status;

    tmp = line_editor_capture_stdout(&saved_stdout);
    if (tmp == NULL)
    {
        return -1;
    }

    status = fn(ctx);
    line_editor_restore_stdout(tmp, saved_stdout);
    return (int)status;
}

typedef struct
{
    int key;
    line_buf_t *lb;
    char **saved_line;
} line_editor_terminal_ctx_t;

static vigil_status_t line_editor_run_terminal_key(void *ctx)
{
    line_editor_terminal_ctx_t *terminal = ctx;
    return (vigil_status_t)edit_line_handle_terminal_keys(terminal->key, terminal->lb, terminal->saved_line);
}

typedef struct
{
    int key;
    line_buf_t *lb;
} line_editor_line_buf_ctx_t;

static vigil_status_t line_editor_run_kill_key(void *ctx)
{
    line_editor_line_buf_ctx_t *line = ctx;
    return (vigil_status_t)edit_line_handle_kill_keys(line->key, line->lb);
}

static int line_editor_terminal_submit_case(void)
{
    char buf[32];
    line_buf_t lb;
    char *saved_line = line_strdup("draft");
    line_editor_terminal_ctx_t ctx;
    int ok;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "abc");
    ctx.key = KEY_ENTER;
    ctx.lb = &lb;
    ctx.saved_line = &saved_line;
    ok = line_editor_run_with_stdout(line_editor_run_terminal_key, &ctx) == LINE_EDIT_SUBMIT && saved_line == NULL;
    free(saved_line);
    return ok;
}

static int line_editor_terminal_interrupt_case(void)
{
    char buf[32];
    line_buf_t lb;
    char *saved_line = NULL;
    line_editor_terminal_ctx_t ctx;
    int status;

    lb_init(&lb, buf, sizeof(buf));
    lb_set(&lb, "abc");
    ctx.key = KEY_CTRL_C;
    ctx.lb = &lb;
    ctx.saved_line = &saved_line;
    status = line_editor_run_with_stdout(line_editor_run_terminal_key, &ctx);
    return status == LINE_EDIT_REFRESH && strcmp(lb.buf, "") == 0;
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
    line_editor_line_buf_ctx_t clear_ctx;

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

    clear_ctx.key = KEY_CTRL_L;
    clear_ctx.lb = &lb;
    if (line_editor_run_with_stdout(line_editor_run_kill_key, &clear_ctx) != LINE_EDIT_REFRESH)
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

typedef struct
{
    const int *bytes;
    size_t count;
    char *buf;
    size_t buf_size;
    vigil_line_history_t *history;
} line_editor_edit_ctx_t;

static vigil_status_t line_editor_run_edit_line(void *ctx)
{
    line_editor_edit_ctx_t *edit = ctx;

    line_editor_mock_reset();
    line_editor_mock_bytes(edit->bytes, edit->count);
    return edit_line(">>> ", edit->buf, edit->buf_size, edit->history);
}

static int line_editor_edit_line_case(void)
{
    static const int bytes[] = {'1', '2', KEY_ENTER};
    char buf[32];
    vigil_line_history_t history;
    line_editor_edit_ctx_t ctx;

    line_editor_test_history_init(&history, 10);
    ctx.bytes = bytes;
    ctx.count = sizeof(bytes) / sizeof(bytes[0]);
    ctx.buf = buf;
    ctx.buf_size = sizeof(buf);
    ctx.history = &history;

    if (line_editor_run_with_stdout(line_editor_run_edit_line, &ctx) != VIGIL_STATUS_OK)
    {
        line_editor_test_history_free(&history);
        return 0;
    }

    line_editor_test_history_free(&history);
    return strcmp(buf, "12") == 0;
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

static vigil_status_t line_editor_run_terminal_readline(void *ctx)
{
    line_editor_edit_ctx_t *edit = ctx;
    vigil_error_t error = {0};
    vigil_status_t status;

    line_editor_mock_reset();
    line_editor_mock_bytes(edit->bytes, edit->count);
    status = line_editor_test_readline(">>> ", edit->buf, edit->buf_size, edit->history, &error);
    vigil_error_clear(&error);
    return status;
}

static int line_editor_readline_eof_case(void)
{
    static const int bytes[] = {KEY_CTRL_D};
    char buf[32] = "sentinel";
    line_editor_edit_ctx_t ctx;

    ctx.bytes = bytes;
    ctx.count = sizeof(bytes) / sizeof(bytes[0]);
    ctx.buf = buf;
    ctx.buf_size = sizeof(buf);
    ctx.history = NULL;

    if (line_editor_run_with_stdout(line_editor_run_terminal_readline, &ctx) != VIGIL_STATUS_INTERNAL)
    {
        return 0;
    }

    return strcmp(buf, "") == 0 && g_line_editor_terminal.restore_calls == 1;
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

TEST(LineEditorInternalTest, TerminalEnterSubmitsLine)
{
    EXPECT_TRUE(line_editor_terminal_submit_case());
}

TEST(LineEditorInternalTest, TerminalCtrlCClearsLine)
{
    EXPECT_TRUE(line_editor_terminal_interrupt_case());
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

TEST(LineEditorInternalTest, EditLineReadsTerminalInput)
{
    EXPECT_TRUE(line_editor_edit_line_case());
}

TEST(LineEditorInternalTest, ReadlineRejectsInvalidArgs)
{
    EXPECT_TRUE(line_editor_readline_invalid_args_case());
}

TEST(LineEditorInternalTest, ReadlinePropagatesRawModeError)
{
    EXPECT_TRUE(line_editor_readline_raw_error_case());
}

TEST(LineEditorInternalTest, ReadlineReturnsEofAndRestoresTerminal)
{
    EXPECT_TRUE(line_editor_readline_eof_case());
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
    REGISTER_TEST(LineEditorInternalTest, TerminalEnterSubmitsLine);
    REGISTER_TEST(LineEditorInternalTest, TerminalCtrlCClearsLine);
    REGISTER_TEST(LineEditorInternalTest, TerminalCtrlDDeletesAndEndsOnEmpty);
    REGISTER_TEST(LineEditorInternalTest, DeleteKeysMutateBuffer);
    REGISTER_TEST(LineEditorInternalTest, CursorKeysMoveWithinBuffer);
    REGISTER_TEST(LineEditorInternalTest, HomeEndKeysJumpToEdges);
    REGISTER_TEST(LineEditorInternalTest, HistoryKeysTraverseEntries);
    REGISTER_TEST(LineEditorInternalTest, KillKeysMutateBuffer);
    REGISTER_TEST(LineEditorInternalTest, WordKeysMutateBuffer);
    REGISTER_TEST(LineEditorInternalTest, LiteralKeysInsertPrintableChars);
    REGISTER_TEST(LineEditorInternalTest, EditLineReadsTerminalInput);
    REGISTER_TEST(LineEditorInternalTest, ReadlineRejectsInvalidArgs);
    REGISTER_TEST(LineEditorInternalTest, ReadlinePropagatesRawModeError);
    REGISTER_TEST(LineEditorInternalTest, ReadlineReturnsEofAndRestoresTerminal);
}
