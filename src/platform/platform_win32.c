#include "platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>

#include "internal/basl_internal.h"

/* ── Allocator helpers ───────────────────────────────────────────── */

static basl_allocator_t resolve_alloc(const basl_allocator_t *a) {
    if (a != NULL && basl_allocator_is_valid(a)) return *a;
    return basl_default_allocator();
}

/* ── File I/O ────────────────────────────────────────────────────── */

basl_status_t basl_platform_read_file(
    const basl_allocator_t *allocator,
    const char *path,
    char **out_data,
    size_t *out_length,
    basl_error_t *error
) {
    FILE *f = NULL;
    long size;
    char *buf;
    size_t nread;
    basl_allocator_t a;

    if (!path || !out_data || !out_length) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: NULL argument");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    *out_data = NULL;
    *out_length = 0;

    if (fopen_s(&f, path, "rb") != 0 || !f) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "platform: cannot open file");
        return BASL_STATUS_INTERNAL;
    }

    if (fseek(f, 0L, SEEK_END) != 0) { fclose(f); goto io_err; }
    size = ftell(f);
    if (size < 0) { fclose(f); goto io_err; }
    if (fseek(f, 0L, SEEK_SET) != 0) { fclose(f); goto io_err; }

    a = resolve_alloc(allocator);
    buf = (char *)a.allocate(a.user_data, (size_t)size + 1);
    if (!buf) {
        fclose(f);
        basl_error_set_literal(error, BASL_STATUS_OUT_OF_MEMORY,
                               "platform: allocation failed");
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    nread = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[nread] = '\0';
    *out_data = buf;
    *out_length = nread;
    return BASL_STATUS_OK;

io_err:
    basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                           "platform: file read failed");
    return BASL_STATUS_INTERNAL;
}

basl_status_t basl_platform_write_file(
    const char *path,
    const void *data,
    size_t length,
    basl_error_t *error
) {
    FILE *f = NULL;
    if (!path) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: NULL path");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (fopen_s(&f, path, "wb") != 0 || !f) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "platform: cannot create file");
        return BASL_STATUS_INTERNAL;
    }
    if (length > 0 && data) {
        if (fwrite(data, 1, length, f) != length) {
            fclose(f);
            basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                                   "platform: write failed");
            return BASL_STATUS_INTERNAL;
        }
    }
    fclose(f);
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_file_exists(const char *path, int *out_exists) {
    DWORD attr;
    if (!path || !out_exists) return BASL_STATUS_INVALID_ARGUMENT;
    attr = GetFileAttributesA(path);
    *out_exists = (attr != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_is_directory(const char *path, int *out_is_dir) {
    DWORD attr;
    if (!path || !out_is_dir) return BASL_STATUS_INVALID_ARGUMENT;
    attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) { *out_is_dir = 0; return BASL_STATUS_OK; }
    *out_is_dir = (attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_mkdir(const char *path, basl_error_t *error) {
    if (!path) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: NULL path");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (_mkdir(path) != 0 && errno != EEXIST) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "platform: mkdir failed");
        return BASL_STATUS_INTERNAL;
    }
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_mkdir_p(const char *path, basl_error_t *error) {
    char buf[4096];
    size_t len;
    size_t i;

    if (!path) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: NULL path");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    len = strlen(path);
    if (len == 0 || len >= sizeof(buf)) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: path too long");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    memcpy(buf, path, len + 1);

    for (i = 1; i <= len; i++) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0') {
            char saved = buf[i];
            buf[i] = '\0';
            /* Skip drive letter root like "C:" */
            if (i == 2 && buf[1] == ':') { buf[i] = saved; continue; }
            if (_mkdir(buf) != 0 && errno != EEXIST) {
                basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                                       "platform: mkdir_p failed");
                return BASL_STATUS_INTERNAL;
            }
            buf[i] = saved;
        }
    }
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_remove(const char *path, basl_error_t *error) {
    DWORD attr;
    if (!path) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: NULL path");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (_rmdir(path) != 0) {
            basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                                   "platform: rmdir failed");
            return BASL_STATUS_INTERNAL;
        }
        return BASL_STATUS_OK;
    }
    if (remove(path) != 0) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "platform: remove failed");
        return BASL_STATUS_INTERNAL;
    }
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_path_join(
    const char *base, const char *child,
    char *out_buf, size_t buf_size, basl_error_t *error
) {
    size_t blen, clen, total;
    int need_sep;
    if (!base || !child || !out_buf || buf_size == 0) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: NULL argument");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    blen = strlen(base);
    clen = strlen(child);
    need_sep = (blen > 0 && base[blen - 1] != '/' && base[blen - 1] != '\\') ? 1 : 0;
    total = blen + (size_t)need_sep + clen;
    if (total >= buf_size) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: path too long");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    memcpy(out_buf, base, blen);
    if (need_sep) out_buf[blen] = '/';
    memcpy(out_buf + blen + (size_t)need_sep, child, clen);
    out_buf[total] = '\0';
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_readline(
    const char *prompt, char *out_buf, size_t buf_size, basl_error_t *error
) {
    size_t len;
    if (!out_buf || buf_size == 0) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: NULL argument");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    if (!fgets(out_buf, (int)buf_size, stdin)) {
        out_buf[0] = '\0';
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "platform: EOF on stdin");
        return BASL_STATUS_INTERNAL;
    }
    len = strlen(out_buf);
    while (len > 0 && (out_buf[len - 1] == '\n' || out_buf[len - 1] == '\r')) {
        out_buf[--len] = '\0';
    }
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_self_exe(
    char *out_buf, size_t buf_size, basl_error_t *error
) {
    DWORD len;
    if (out_buf == NULL || buf_size == 0) {
        if (error) { error->type = BASL_STATUS_INVALID_ARGUMENT; error->value = "null buffer"; error->length = 11; }
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    len = GetModuleFileNameA(NULL, out_buf, (DWORD)buf_size);
    if (len == 0 || len >= (DWORD)buf_size) {
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "GetModuleFileName failed"; error->length = 24; }
        return BASL_STATUS_INTERNAL;
    }
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_make_executable(
    const char *path, basl_error_t *error
) {
    (void)path; (void)error;
    return BASL_STATUS_OK; /* no-op on Windows */
}

basl_status_t basl_platform_list_dir(
    const char *path,
    basl_platform_dir_callback_t callback,
    void *user_data,
    basl_error_t *error
) {
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "failed to open directory"; error->length = 24; }
        return BASL_STATUS_INTERNAL;
    }
    do {
        if (fd.cFileName[0] == '.' &&
            (fd.cFileName[1] == '\0' ||
             (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0'))) {
            continue;
        }
        int is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        basl_status_t s = callback(fd.cFileName, is_dir, user_data);
        if (s != BASL_STATUS_OK) break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return BASL_STATUS_OK;
}
