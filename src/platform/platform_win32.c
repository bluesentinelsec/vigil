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

/* ── Environment variables ───────────────────────────────────────── */

BASL_API basl_status_t basl_platform_getenv(
    const char *name, char **out_value, int *out_found, basl_error_t *error
) {
    char buf[32767]; /* max env var size on Windows */
    DWORD len;
    (void)error;
    if (!name || !out_value || !out_found) {
        if (error) { error->type = BASL_STATUS_INVALID_ARGUMENT; error->value = "null argument"; error->length = 13; }
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
        *out_value = NULL;
        *out_found = 0;
    } else {
        *out_value = _strdup(buf);
        *out_found = 1;
    }
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_setenv(
    const char *name, const char *value, basl_error_t *error
) {
    if (!name || !value) {
        if (error) { error->type = BASL_STATUS_INVALID_ARGUMENT; error->value = "null argument"; error->length = 13; }
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (_putenv_s(name, value) != 0) {
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "setenv failed"; error->length = 13; }
        return BASL_STATUS_INTERNAL;
    }
    return BASL_STATUS_OK;
}

/* ── OS information ──────────────────────────────────────────────── */

BASL_API const char *basl_platform_os_name(void) {
    return "windows";
}

BASL_API basl_status_t basl_platform_getcwd(
    char **out_path, basl_error_t *error
) {
    char buf[4096];
    if (!out_path) {
        if (error) { error->type = BASL_STATUS_INVALID_ARGUMENT; error->value = "null argument"; error->length = 13; }
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (!_getcwd(buf, sizeof(buf))) {
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "getcwd failed"; error->length = 13; }
        return BASL_STATUS_INTERNAL;
    }
    *out_path = _strdup(buf);
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_temp_dir(
    char **out_path, basl_error_t *error
) {
    char buf[MAX_PATH + 1];
    DWORD len;
    if (!out_path) {
        if (error) { error->type = BASL_STATUS_INVALID_ARGUMENT; error->value = "null argument"; error->length = 13; }
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    len = GetTempPathA(sizeof(buf), buf);
    if (len == 0) {
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "GetTempPath failed"; error->length = 18; }
        return BASL_STATUS_INTERNAL;
    }
    /* Remove trailing backslash if present. */
    if (len > 0 && (buf[len - 1] == '\\' || buf[len - 1] == '/'))
        buf[len - 1] = '\0';
    *out_path = _strdup(buf);
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_hostname(
    char **out_name, basl_error_t *error
) {
    char buf[256];
    DWORD size = sizeof(buf);
    if (!out_name) {
        if (error) { error->type = BASL_STATUS_INVALID_ARGUMENT; error->value = "null argument"; error->length = 13; }
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (!GetComputerNameA(buf, &size)) {
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "GetComputerName failed"; error->length = 22; }
        return BASL_STATUS_INTERNAL;
    }
    *out_name = _strdup(buf);
    return BASL_STATUS_OK;
}

/* ── Process execution ───────────────────────────────────────────── */

BASL_API basl_status_t basl_platform_exec(
    const char *const *argv,
    char **out_stdout, char **out_stderr, int *out_exit_code,
    basl_error_t *error
) {
    HANDLE stdout_rd = NULL, stdout_wr = NULL;
    HANDLE stderr_rd = NULL, stderr_wr = NULL;
    SECURITY_ATTRIBUTES sa;
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    char cmdline[32768];
    size_t pos = 0;
    int i;
    DWORD exit_code;

    if (!argv || !argv[0] || !out_stdout || !out_stderr || !out_exit_code) {
        if (error) { error->type = BASL_STATUS_INVALID_ARGUMENT; error->value = "null argument"; error->length = 13; }
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    /* Build command line. */
    for (i = 0; argv[i]; i++) {
        if (i > 0 && pos < sizeof(cmdline) - 1) cmdline[pos++] = ' ';
        {
            size_t len = strlen(argv[i]);
            if (pos + len < sizeof(cmdline) - 1) {
                memcpy(cmdline + pos, argv[i], len);
                pos += len;
            }
        }
    }
    cmdline[pos] = '\0';

    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&stdout_rd, &stdout_wr, &sa, 0) ||
        !CreatePipe(&stderr_rd, &stderr_wr, &sa, 0)) {
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "CreatePipe failed"; error->length = 17; }
        return BASL_STATUS_INTERNAL;
    }
    SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_rd, HANDLE_FLAG_INHERIT, 0);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdout_wr;
    si.hStdError = stderr_wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(stdout_rd); CloseHandle(stdout_wr);
        CloseHandle(stderr_rd); CloseHandle(stderr_wr);
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "CreateProcess failed"; error->length = 20; }
        return BASL_STATUS_INTERNAL;
    }
    CloseHandle(stdout_wr);
    CloseHandle(stderr_wr);

    /* Read pipes. */
    {
        char *bufs[2] = {NULL, NULL};
        size_t lens[2] = {0, 0};
        size_t caps[2] = {0, 0};
        HANDLE handles[2] = {stdout_rd, stderr_rd};
        int idx;
        for (idx = 0; idx < 2; idx++) {
            char tmp[4096];
            DWORD n;
            while (ReadFile(handles[idx], tmp, sizeof(tmp), &n, NULL) && n > 0) {
                if (lens[idx] + n >= caps[idx]) {
                    caps[idx] = (lens[idx] + n) * 2 + 1;
                    bufs[idx] = realloc(bufs[idx], caps[idx]);
                }
                memcpy(bufs[idx] + lens[idx], tmp, n);
                lens[idx] += n;
            }
            CloseHandle(handles[idx]);
            if (!bufs[idx]) bufs[idx] = calloc(1, 1);
            bufs[idx][lens[idx]] = '\0';
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exit_code);
        *out_stdout = bufs[0];
        *out_stderr = bufs[1];
        *out_exit_code = (int)exit_code;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return BASL_STATUS_OK;
}

/* ── Dynamic library loading ─────────────────────────────────────── */

BASL_API basl_status_t basl_platform_dlopen(
    const char *path, void **out_handle, basl_error_t *error
) {
    HMODULE h;
    if (!path || !out_handle) {
        if (error) { error->type = BASL_STATUS_INVALID_ARGUMENT; error->value = "null argument"; error->length = 13; }
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    h = LoadLibraryA(path);
    if (!h) {
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "LoadLibrary failed"; error->length = 18; }
        return BASL_STATUS_INTERNAL;
    }
    *out_handle = (void *)h;
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_dlsym(
    void *handle, const char *name, void **out_sym, basl_error_t *error
) {
    FARPROC sym;
    if (!handle || !name || !out_sym) {
        if (error) { error->type = BASL_STATUS_INVALID_ARGUMENT; error->value = "null argument"; error->length = 13; }
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    sym = GetProcAddress((HMODULE)handle, name);
    if (!sym) {
        if (error) { error->type = BASL_STATUS_INTERNAL; error->value = "GetProcAddress failed"; error->length = 21; }
        return BASL_STATUS_INTERNAL;
    }
    *out_sym = (void *)sym;
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_dlclose(
    void *handle, basl_error_t *error
) {
    if (!handle) {
        if (error) { error->type = BASL_STATUS_INVALID_ARGUMENT; error->value = "null handle"; error->length = 11; }
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    FreeLibrary((HMODULE)handle);
    return BASL_STATUS_OK;
}
