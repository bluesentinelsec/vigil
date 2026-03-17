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

/* ── Terminal raw mode ───────────────────────────────────────────── */

struct basl_terminal_state {
    DWORD orig_in_mode;
    DWORD orig_out_mode;
    HANDLE h_in;
    HANDLE h_out;
};

int basl_platform_is_terminal(void) {
    DWORD mode;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}

basl_status_t basl_platform_terminal_raw(
    basl_terminal_state_t **out_state, basl_error_t *error
) {
    basl_terminal_state_t *state = malloc(sizeof(*state));
    if (!state) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "out of memory");
        return BASL_STATUS_INTERNAL;
    }
    state->h_in = GetStdHandle(STD_INPUT_HANDLE);
    state->h_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(state->h_in, &state->orig_in_mode)) {
        free(state);
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "GetConsoleMode failed");
        return BASL_STATUS_INTERNAL;
    }
    GetConsoleMode(state->h_out, &state->orig_out_mode);
    SetConsoleMode(state->h_in,
        ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(state->h_out,
        state->orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    *out_state = state;
    return BASL_STATUS_OK;
}

void basl_platform_terminal_restore(basl_terminal_state_t *state) {
    if (!state) return;
    SetConsoleMode(state->h_in, state->orig_in_mode);
    SetConsoleMode(state->h_out, state->orig_out_mode);
    free(state);
}

int basl_platform_terminal_read_byte(void) {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    char c;
    DWORD n;
    if (!ReadFile(h, &c, 1, &n, NULL) || n == 0) return -1;
    return (unsigned char)c;
}

int basl_platform_terminal_width(void) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
        return info.srWindow.Right - info.srWindow.Left + 1;
    return 80;
}

/* ── Extended filesystem operations ──────────────────────────────── */

BASL_API basl_status_t basl_platform_copy_file(
    const char *src,
    const char *dst,
    basl_error_t *error
) {
    if (!CopyFileA(src, dst, FALSE)) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "CopyFile failed");
        return BASL_STATUS_INTERNAL;
    }
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_rename(
    const char *src,
    const char *dst,
    basl_error_t *error
) {
    if (!MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING)) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "MoveFileEx failed");
        return BASL_STATUS_INTERNAL;
    }
    return BASL_STATUS_OK;
}

BASL_API int64_t basl_platform_file_size(const char *path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return -1;
    LARGE_INTEGER size;
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart;
}

BASL_API int64_t basl_platform_file_mtime(const char *path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return -1;
    /* Convert FILETIME to Unix timestamp */
    ULARGE_INTEGER ull;
    ull.LowPart = data.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return (int64_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

BASL_API basl_status_t basl_platform_is_file(const char *path, int *out_is_file) {
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        *out_is_file = 0;
        return BASL_STATUS_OK;
    }
    *out_is_file = (attr & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 1;
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_symlink(
    const char *target,
    const char *linkpath,
    basl_error_t *error
) {
    DWORD flags = 0;
    DWORD attr = GetFileAttributesA(target);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
    if (!CreateSymbolicLinkA(linkpath, target, flags)) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "CreateSymbolicLink failed");
        return BASL_STATUS_INTERNAL;
    }
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_hardlink(
    const char *target,
    const char *linkpath,
    basl_error_t *error
) {
    if (!CreateHardLinkA(linkpath, target, NULL)) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "CreateHardLink failed");
        return BASL_STATUS_INTERNAL;
    }
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_readlink(
    const char *path,
    char **out_target,
    basl_error_t *error
) {
    /* Windows symlink reading is complex; simplified version */
    (void)path;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED, "readlink not fully supported on Windows");
    *out_target = NULL;
    return BASL_STATUS_UNSUPPORTED;
}

BASL_API basl_status_t basl_platform_home_dir(char **out_path, basl_error_t *error) {
    char *userprofile = getenv("USERPROFILE");
    if (userprofile) {
        *out_path = _strdup(userprofile);
        if (!*out_path) return BASL_STATUS_OUT_OF_MEMORY;
        return BASL_STATUS_OK;
    }
    basl_error_set_literal(error, BASL_STATUS_INTERNAL, "USERPROFILE not set");
    return BASL_STATUS_INTERNAL;
}

BASL_API basl_status_t basl_platform_config_dir(char **out_path, basl_error_t *error) {
    char *appdata = getenv("APPDATA");
    if (appdata) {
        *out_path = _strdup(appdata);
        if (!*out_path) return BASL_STATUS_OUT_OF_MEMORY;
        return BASL_STATUS_OK;
    }
    basl_error_set_literal(error, BASL_STATUS_INTERNAL, "APPDATA not set");
    return BASL_STATUS_INTERNAL;
}

BASL_API basl_status_t basl_platform_cache_dir(char **out_path, basl_error_t *error) {
    char *localappdata = getenv("LOCALAPPDATA");
    if (localappdata) {
        *out_path = _strdup(localappdata);
        if (!*out_path) return BASL_STATUS_OUT_OF_MEMORY;
        return BASL_STATUS_OK;
    }
    basl_error_set_literal(error, BASL_STATUS_INTERNAL, "LOCALAPPDATA not set");
    return BASL_STATUS_INTERNAL;
}

BASL_API basl_status_t basl_platform_data_dir(char **out_path, basl_error_t *error) {
    return basl_platform_config_dir(out_path, error);
}

BASL_API basl_status_t basl_platform_temp_file(
    const char *prefix,
    char **out_path,
    basl_error_t *error
) {
    char tmpdir[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, tmpdir)) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "GetTempPath failed");
        return BASL_STATUS_INTERNAL;
    }
    char path[MAX_PATH];
    if (!GetTempFileNameA(tmpdir, prefix ? prefix : "tmp", 0, path)) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "GetTempFileName failed");
        return BASL_STATUS_INTERNAL;
    }
    *out_path = _strdup(path);
    if (!*out_path) return BASL_STATUS_OUT_OF_MEMORY;
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_append_file(
    const char *path,
    const void *data,
    size_t length,
    basl_error_t *error
) {
    FILE *f = fopen(path, "ab");
    if (!f) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "cannot open file for append");
        return BASL_STATUS_INTERNAL;
    }
    if (fwrite(data, 1, length, f) != length) {
        fclose(f);
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "append write failed");
        return BASL_STATUS_INTERNAL;
    }
    fclose(f);
    return BASL_STATUS_OK;
}

/* ── Threading primitives ────────────────────────────────────────── */

struct basl_platform_thread {
    HANDLE handle;
    basl_thread_func_t func;
    void *arg;
};

static DWORD WINAPI thread_wrapper(LPVOID arg) {
    basl_platform_thread_t *t = (basl_platform_thread_t *)arg;
    t->func(t->arg);
    return 0;
}

BASL_API basl_status_t basl_platform_thread_create(
    basl_platform_thread_t **out_thread,
    basl_thread_func_t func,
    void *arg,
    basl_error_t *error
) {
    basl_platform_thread_t *t = malloc(sizeof(*t));
    if (!t) return BASL_STATUS_OUT_OF_MEMORY;
    t->func = func;
    t->arg = arg;
    t->handle = CreateThread(NULL, 0, thread_wrapper, t, 0, NULL);
    if (!t->handle) {
        free(t);
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "CreateThread failed");
        return BASL_STATUS_INTERNAL;
    }
    *out_thread = t;
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_thread_join(
    basl_platform_thread_t *thread,
    basl_error_t *error
) {
    if (WaitForSingleObject(thread->handle, INFINITE) == WAIT_FAILED) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "WaitForSingleObject failed");
        return BASL_STATUS_INTERNAL;
    }
    CloseHandle(thread->handle);
    free(thread);
    return BASL_STATUS_OK;
}

BASL_API basl_status_t basl_platform_thread_detach(
    basl_platform_thread_t *thread,
    basl_error_t *error
) {
    (void)error;
    CloseHandle(thread->handle);
    free(thread);
    return BASL_STATUS_OK;
}

BASL_API uint64_t basl_platform_thread_current_id(void) {
    return (uint64_t)GetCurrentThreadId();
}

BASL_API void basl_platform_thread_yield(void) {
    SwitchToThread();
}

BASL_API void basl_platform_thread_sleep(uint64_t milliseconds) {
    Sleep((DWORD)milliseconds);
}

/* ── Mutex (using CRITICAL_SECTION for efficiency) ───────────────── */

struct basl_platform_mutex {
    CRITICAL_SECTION cs;
};

BASL_API basl_status_t basl_platform_mutex_create(
    basl_platform_mutex_t **out_mutex,
    basl_error_t *error
) {
    (void)error;
    basl_platform_mutex_t *m = malloc(sizeof(*m));
    if (!m) return BASL_STATUS_OUT_OF_MEMORY;
    InitializeCriticalSection(&m->cs);
    *out_mutex = m;
    return BASL_STATUS_OK;
}

BASL_API void basl_platform_mutex_destroy(basl_platform_mutex_t *mutex) {
    if (mutex) {
        DeleteCriticalSection(&mutex->cs);
        free(mutex);
    }
}

BASL_API void basl_platform_mutex_lock(basl_platform_mutex_t *mutex) {
    EnterCriticalSection(&mutex->cs);
}

BASL_API void basl_platform_mutex_unlock(basl_platform_mutex_t *mutex) {
    LeaveCriticalSection(&mutex->cs);
}

BASL_API int basl_platform_mutex_trylock(basl_platform_mutex_t *mutex) {
    return TryEnterCriticalSection(&mutex->cs) ? 1 : 0;
}

/* ── Condition variable ──────────────────────────────────────────── */

struct basl_platform_cond {
    CONDITION_VARIABLE cv;
};

BASL_API basl_status_t basl_platform_cond_create(
    basl_platform_cond_t **out_cond,
    basl_error_t *error
) {
    (void)error;
    basl_platform_cond_t *c = malloc(sizeof(*c));
    if (!c) return BASL_STATUS_OUT_OF_MEMORY;
    InitializeConditionVariable(&c->cv);
    *out_cond = c;
    return BASL_STATUS_OK;
}

BASL_API void basl_platform_cond_destroy(basl_platform_cond_t *cond) {
    /* Windows condition variables don't need explicit destruction */
    free(cond);
}

BASL_API void basl_platform_cond_wait(
    basl_platform_cond_t *cond,
    basl_platform_mutex_t *mutex
) {
    SleepConditionVariableCS(&cond->cv, &mutex->cs, INFINITE);
}

BASL_API void basl_platform_cond_signal(basl_platform_cond_t *cond) {
    WakeConditionVariable(&cond->cv);
}

BASL_API void basl_platform_cond_broadcast(basl_platform_cond_t *cond) {
    WakeAllConditionVariable(&cond->cv);
}

/* ── Read-write lock (using SRWLOCK) ─────────────────────────────── */

struct basl_platform_rwlock {
    SRWLOCK lock;
};

BASL_API basl_status_t basl_platform_rwlock_create(
    basl_platform_rwlock_t **out_rwlock,
    basl_error_t *error
) {
    (void)error;
    basl_platform_rwlock_t *rw = malloc(sizeof(*rw));
    if (!rw) return BASL_STATUS_OUT_OF_MEMORY;
    InitializeSRWLock(&rw->lock);
    *out_rwlock = rw;
    return BASL_STATUS_OK;
}

BASL_API void basl_platform_rwlock_destroy(basl_platform_rwlock_t *rwlock) {
    /* Windows SRWLOCKs don't need explicit destruction */
    free(rwlock);
}

BASL_API void basl_platform_rwlock_rdlock(basl_platform_rwlock_t *rwlock) {
    AcquireSRWLockShared(&rwlock->lock);
}

BASL_API void basl_platform_rwlock_wrlock(basl_platform_rwlock_t *rwlock) {
    AcquireSRWLockExclusive(&rwlock->lock);
}

BASL_API void basl_platform_rwlock_unlock(basl_platform_rwlock_t *rwlock) {
    /* SRWLOCK requires knowing if it was shared or exclusive.
       We use TryAcquire to detect - if exclusive succeeds, it was shared. */
    if (TryAcquireSRWLockExclusive(&rwlock->lock)) {
        ReleaseSRWLockExclusive(&rwlock->lock);
        ReleaseSRWLockShared(&rwlock->lock);
    } else {
        ReleaseSRWLockExclusive(&rwlock->lock);
    }
}

/* ── Thread-local storage ────────────────────────────────────────── */

struct basl_platform_tls_key {
    DWORD key;
};

BASL_API basl_status_t basl_platform_tls_create(
    basl_platform_tls_key_t **out_key,
    void (*destructor)(void *),
    basl_error_t *error
) {
    (void)destructor; /* Windows TLS doesn't support destructors directly */
    basl_platform_tls_key_t *k = malloc(sizeof(*k));
    if (!k) return BASL_STATUS_OUT_OF_MEMORY;
    k->key = TlsAlloc();
    if (k->key == TLS_OUT_OF_INDEXES) {
        free(k);
        basl_error_set_literal(error, BASL_STATUS_INTERNAL, "TlsAlloc failed");
        return BASL_STATUS_INTERNAL;
    }
    *out_key = k;
    return BASL_STATUS_OK;
}

BASL_API void basl_platform_tls_destroy(basl_platform_tls_key_t *key) {
    if (key) {
        TlsFree(key->key);
        free(key);
    }
}

BASL_API void basl_platform_tls_set(basl_platform_tls_key_t *key, void *value) {
    TlsSetValue(key->key, value);
}

BASL_API void *basl_platform_tls_get(basl_platform_tls_key_t *key) {
    return TlsGetValue(key->key);
}

/* ── Atomic operations ───────────────────────────────────────────── */

BASL_API int64_t basl_atomic_load(const volatile int64_t *ptr) {
    return InterlockedCompareExchange64((volatile LONG64 *)ptr, 0, 0);
}

BASL_API void basl_atomic_store(volatile int64_t *ptr, int64_t value) {
    InterlockedExchange64((volatile LONG64 *)ptr, value);
}

BASL_API int64_t basl_atomic_add(volatile int64_t *ptr, int64_t value) {
    return InterlockedExchangeAdd64((volatile LONG64 *)ptr, value);
}

BASL_API int64_t basl_atomic_sub(volatile int64_t *ptr, int64_t value) {
    return InterlockedExchangeAdd64((volatile LONG64 *)ptr, -value);
}

BASL_API int basl_atomic_cas(volatile int64_t *ptr, int64_t expected, int64_t desired) {
    return InterlockedCompareExchange64((volatile LONG64 *)ptr, desired, expected) == expected ? 1 : 0;
}
