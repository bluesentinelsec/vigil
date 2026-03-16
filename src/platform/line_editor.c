/* Portable line editor with history support.
 *
 * Uses platform raw-mode primitives for key reading.
 * Falls back to fgets when stdin is not a terminal.
 */
#include "platform.h"
#include "internal/basl_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Key constants ───────────────────────────────────────────────── */

enum {
    KEY_NONE   = 0,
    KEY_CTRL_A = 1,
    KEY_CTRL_B = 2,
    KEY_CTRL_C = 3,
    KEY_CTRL_D = 4,
    KEY_CTRL_E = 5,
    KEY_CTRL_F = 6,
    KEY_CTRL_H = 8,
    KEY_TAB    = 9,
    KEY_ENTER  = 13,
    KEY_CTRL_K = 11,
    KEY_CTRL_L = 12,
    KEY_CTRL_N = 14,
    KEY_CTRL_P = 16,
    KEY_CTRL_T = 20,
    KEY_CTRL_U = 21,
    KEY_CTRL_W = 23,
    KEY_ESC    = 27,
    KEY_BACKSPACE = 127,

    /* Virtual keys for escape sequences. */
    KEY_ARROW_UP    = 256,
    KEY_ARROW_DOWN  = 257,
    KEY_ARROW_RIGHT = 258,
    KEY_ARROW_LEFT  = 259,
    KEY_HOME        = 260,
    KEY_END         = 261,
    KEY_DELETE       = 262
};

/* Read a keypress, translating escape sequences. */
static int read_key(void) {
    int c = basl_platform_terminal_read_byte();
    if (c != KEY_ESC) return c;

    int c2 = basl_platform_terminal_read_byte();
    if (c2 == -1) return KEY_ESC;
    if (c2 == '[') {
        int c3 = basl_platform_terminal_read_byte();
        if (c3 >= '0' && c3 <= '9') {
            int c4 = basl_platform_terminal_read_byte();
            if (c4 == '~') {
                switch (c3) {
                    case '1': return KEY_HOME;
                    case '3': return KEY_DELETE;
                    case '4': return KEY_END;
                    case '7': return KEY_HOME;
                    case '8': return KEY_END;
                }
            }
            return KEY_NONE;
        }
        switch (c3) {
            case 'A': return KEY_ARROW_UP;
            case 'B': return KEY_ARROW_DOWN;
            case 'C': return KEY_ARROW_RIGHT;
            case 'D': return KEY_ARROW_LEFT;
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
        }
    } else if (c2 == 'O') {
        int c3 = basl_platform_terminal_read_byte();
        switch (c3) {
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
        }
    }
    return KEY_NONE;
}

/* ── Line buffer ─────────────────────────────────────────────────── */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    size_t pos;       /* cursor position */
} line_buf_t;

static void lb_init(line_buf_t *lb, char *buf, size_t cap) {
    lb->buf = buf;
    lb->len = 0;
    lb->cap = cap - 1; /* reserve space for NUL */
    lb->pos = 0;
    lb->buf[0] = '\0';
}

static void lb_insert(line_buf_t *lb, char c) {
    if (lb->len >= lb->cap) return;
    memmove(lb->buf + lb->pos + 1, lb->buf + lb->pos, lb->len - lb->pos);
    lb->buf[lb->pos] = c;
    lb->len++;
    lb->pos++;
    lb->buf[lb->len] = '\0';
}

static void lb_delete_at(line_buf_t *lb, size_t pos) {
    if (pos >= lb->len) return;
    memmove(lb->buf + pos, lb->buf + pos + 1, lb->len - pos - 1);
    lb->len--;
    lb->buf[lb->len] = '\0';
}

static void lb_set(line_buf_t *lb, const char *s) {
    size_t slen = strlen(s);
    if (slen > lb->cap) slen = lb->cap;
    memcpy(lb->buf, s, slen);
    lb->len = slen;
    lb->pos = slen;
    lb->buf[lb->len] = '\0';
}

/* ── Screen refresh ──────────────────────────────────────────────── */

static void refresh_line(const char *prompt, const line_buf_t *lb) {
    size_t plen = prompt ? strlen(prompt) : 0;
    /* \r: go to column 0, print prompt + buffer, \x1b[K: clear to EOL */
    fputs("\r", stdout);
    if (prompt) fputs(prompt, stdout);
    fwrite(lb->buf, 1, lb->len, stdout);
    fputs("\x1b[K", stdout);
    /* Move cursor to correct position. */
    if (plen + lb->pos < plen + lb->len) {
        fprintf(stdout, "\r\x1b[%zuC", plen + lb->pos);
    }
    fflush(stdout);
}

/* ── History ─────────────────────────────────────────────────────── */

void basl_line_history_init(basl_line_history_t *h, size_t max_entries) {
    h->entries = NULL;
    h->count = 0;
    h->capacity = 0;
    h->max_entries = max_entries > 0 ? max_entries : 1000;
}

void basl_line_history_free(basl_line_history_t *h) {
    for (size_t i = 0; i < h->count; i++) free(h->entries[i]);
    free(h->entries);
    h->entries = NULL;
    h->count = 0;
    h->capacity = 0;
}

void basl_line_history_add(basl_line_history_t *h, const char *line) {
    if (!line || !line[0]) return;
    /* Skip duplicates of the most recent entry. */
    if (h->count > 0 && strcmp(h->entries[h->count - 1], line) == 0) return;
    /* Evict oldest if at capacity. */
    if (h->count >= h->max_entries) {
        free(h->entries[0]);
        memmove(h->entries, h->entries + 1, (h->count - 1) * sizeof(char *));
        h->count--;
    }
    if (h->count >= h->capacity) {
        size_t new_cap = h->capacity == 0 ? 64 : h->capacity * 2;
        char **new_entries = realloc(h->entries, new_cap * sizeof(char *));
        if (!new_entries) return;
        h->entries = new_entries;
        h->capacity = new_cap;
    }
    h->entries[h->count] = malloc(strlen(line) + 1);
    if (h->entries[h->count]) {
        strcpy(h->entries[h->count], line);
        h->count++;
    }
}

const char *basl_line_history_get(const basl_line_history_t *h, size_t index) {
    if (index >= h->count) return NULL;
    return h->entries[index];
}

void basl_line_history_clear(basl_line_history_t *h) {
    for (size_t i = 0; i < h->count; i++) free(h->entries[i]);
    h->count = 0;
}

basl_status_t basl_line_history_load(
    basl_line_history_t *h, const char *path, basl_error_t *error
) {
    FILE *f = fopen(path, "r");
    if (!f) {
        /* Missing file is not an error — just no history. */
        (void)error;
        return BASL_STATUS_OK;
    }
    char line[4096];
    while (fgets(line, (int)sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        basl_line_history_add(h, line);
    }
    fclose(f);
    return BASL_STATUS_OK;
}

basl_status_t basl_line_history_save(
    const basl_line_history_t *h, const char *path, basl_error_t *error
) {
    FILE *f = fopen(path, "w");
    if (!f) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "cannot open history file");
        return BASL_STATUS_INTERNAL;
    }
    for (size_t i = 0; i < h->count; i++)
        fprintf(f, "%s\n", h->entries[i]);
    fclose(f);
    return BASL_STATUS_OK;
}

/* ── Main editing loop ───────────────────────────────────────────── */

static basl_status_t edit_line(
    const char *prompt, char *out_buf, size_t buf_size,
    basl_line_history_t *history
) {
    line_buf_t lb;
    size_t hist_index;

    lb_init(&lb, out_buf, buf_size);
    hist_index = history ? history->count : 0;

    refresh_line(prompt, &lb);

    for (;;) {
        int key = read_key();
        if (key == -1 || key == KEY_CTRL_D) {
            if (lb.len == 0) return BASL_STATUS_INTERNAL; /* EOF */
            /* Ctrl-D with content: delete char under cursor. */
            lb_delete_at(&lb, lb.pos);
            refresh_line(prompt, &lb);
            continue;
        }

        switch (key) {
        case KEY_ENTER:
            fputs("\n", stdout);
            fflush(stdout);
            return BASL_STATUS_OK;

        case KEY_CTRL_C:
            lb.len = 0;
            lb.pos = 0;
            lb.buf[0] = '\0';
            fputs("^C\n", stdout);
            refresh_line(prompt, &lb);
            continue;

        case KEY_BACKSPACE:
        case KEY_CTRL_H:
            if (lb.pos > 0) {
                lb.pos--;
                lb_delete_at(&lb, lb.pos);
            }
            break;

        case KEY_DELETE:
            lb_delete_at(&lb, lb.pos);
            break;

        case KEY_ARROW_LEFT:
        case KEY_CTRL_B:
            if (lb.pos > 0) lb.pos--;
            break;

        case KEY_ARROW_RIGHT:
        case KEY_CTRL_F:
            if (lb.pos < lb.len) lb.pos++;
            break;

        case KEY_HOME:
        case KEY_CTRL_A:
            lb.pos = 0;
            break;

        case KEY_END:
        case KEY_CTRL_E:
            lb.pos = lb.len;
            break;

        case KEY_ARROW_UP:
        case KEY_CTRL_P:
            if (history && hist_index > 0) {
                hist_index--;
                lb_set(&lb, history->entries[hist_index]);
            }
            break;

        case KEY_ARROW_DOWN:
        case KEY_CTRL_N:
            if (history) {
                if (hist_index < history->count - 1) {
                    hist_index++;
                    lb_set(&lb, history->entries[hist_index]);
                } else {
                    hist_index = history->count;
                    lb_set(&lb, "");
                }
            }
            break;

        case KEY_CTRL_K:
            /* Kill to end of line. */
            lb.len = lb.pos;
            lb.buf[lb.len] = '\0';
            break;

        case KEY_CTRL_U:
            /* Kill to start of line. */
            memmove(lb.buf, lb.buf + lb.pos, lb.len - lb.pos);
            lb.len -= lb.pos;
            lb.pos = 0;
            lb.buf[lb.len] = '\0';
            break;

        case KEY_CTRL_W: {
            /* Kill word backwards. */
            size_t old = lb.pos;
            while (lb.pos > 0 && lb.buf[lb.pos - 1] == ' ') lb.pos--;
            while (lb.pos > 0 && lb.buf[lb.pos - 1] != ' ') lb.pos--;
            memmove(lb.buf + lb.pos, lb.buf + old, lb.len - old);
            lb.len -= (old - lb.pos);
            lb.buf[lb.len] = '\0';
            break;
        }

        case KEY_CTRL_L:
            /* Clear screen. */
            fputs("\x1b[H\x1b[2J", stdout);
            break;

        case KEY_CTRL_T:
            /* Transpose chars. */
            if (lb.pos > 0 && lb.pos < lb.len) {
                char tmp = lb.buf[lb.pos - 1];
                lb.buf[lb.pos - 1] = lb.buf[lb.pos];
                lb.buf[lb.pos] = tmp;
                lb.pos++;
            }
            break;

        case KEY_TAB:
        case KEY_NONE:
            continue;

        default:
            if (key >= 32 && key < 127) {
                lb_insert(&lb, (char)key);
            }
            break;
        }

        refresh_line(prompt, &lb);
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

basl_status_t basl_line_editor_readline(
    const char *prompt, char *out_buf, size_t buf_size,
    basl_line_history_t *history, basl_error_t *error
) {
    basl_terminal_state_t *term_state = NULL;
    basl_status_t status;

    if (!out_buf || buf_size == 0) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: NULL argument");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    /* Non-terminal: fall back to fgets. */
    if (!basl_platform_is_terminal()) {
        size_t len;
        if (prompt) { fputs(prompt, stdout); fflush(stdout); }
        if (!fgets(out_buf, (int)buf_size, stdin)) {
            out_buf[0] = '\0';
            basl_error_set_literal(error, BASL_STATUS_INTERNAL, "platform: EOF on stdin");
            return BASL_STATUS_INTERNAL;
        }
        len = strlen(out_buf);
        while (len > 0 && (out_buf[len - 1] == '\n' || out_buf[len - 1] == '\r'))
            out_buf[--len] = '\0';
        return BASL_STATUS_OK;
    }

    /* Terminal: enter raw mode, edit, restore. */
    status = basl_platform_terminal_raw(&term_state, error);
    if (status != BASL_STATUS_OK) return status;

    status = edit_line(prompt, out_buf, buf_size, history);

    basl_platform_terminal_restore(term_state);

    if (status != BASL_STATUS_OK) {
        out_buf[0] = '\0';
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "platform: EOF on stdin");
    }
    return status;
}
