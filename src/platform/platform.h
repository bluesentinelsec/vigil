#ifndef BASL_PLATFORM_H
#define BASL_PLATFORM_H

/*
 * Platform abstraction layer for file I/O.
 *
 * This is an internal header — not part of the public API.
 * Stdlib modules and the CLI call these functions.
 * Each platform provides its own implementation:
 *   platform_posix.c  — Linux, macOS, BSD, Android, iOS
 *   platform_win32.c  — Windows
 *   platform_stub.c   — Emscripten, unknown
 *
 * All allocating functions use the provided allocator.  Pass NULL
 * to use malloc/free.
 */

#include <stddef.h>

#include "basl/allocator.h"
#include "basl/export.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Read an entire file into a newly allocated buffer (null-terminated). */
BASL_API basl_status_t basl_platform_read_file(
    const basl_allocator_t *allocator,
    const char *path,
    char **out_data,
    size_t *out_length,
    basl_error_t *error
);

/** Write data to a file, creating or truncating it. */
BASL_API basl_status_t basl_platform_write_file(
    const char *path,
    const void *data,
    size_t length,
    basl_error_t *error
);

/** Check whether a path exists (file or directory). */
BASL_API basl_status_t basl_platform_file_exists(
    const char *path,
    int *out_exists
);

/** Check whether a path is a directory. */
BASL_API basl_status_t basl_platform_is_directory(
    const char *path,
    int *out_is_dir
);

/** Create a single directory.  Fails if parent doesn't exist. */
BASL_API basl_status_t basl_platform_mkdir(
    const char *path,
    basl_error_t *error
);

/** Create a directory and all missing parents. */
BASL_API basl_status_t basl_platform_mkdir_p(
    const char *path,
    basl_error_t *error
);

/** Remove a file or empty directory. */
BASL_API basl_status_t basl_platform_remove(
    const char *path,
    basl_error_t *error
);

/** Join two path segments with the platform separator.  Always uses '/'. */
BASL_API basl_status_t basl_platform_path_join(
    const char *base,
    const char *child,
    char *out_buf,
    size_t buf_size,
    basl_error_t *error
);

/**
 * Read a line from stdin.  Strips trailing newline.
 * If prompt is non-NULL, prints it to stdout first.
 * Returns BASL_STATUS_INTERNAL on EOF.
 */
BASL_API basl_status_t basl_platform_readline(
    const char *prompt,
    char *out_buf,
    size_t buf_size,
    basl_error_t *error
);

/*
 * Get the path to the currently running executable.
 */
BASL_API basl_status_t basl_platform_self_exe(
    char *out_buf,
    size_t buf_size,
    basl_error_t *error
);

/*
 * Make a file executable (no-op on Windows).
 */
BASL_API basl_status_t basl_platform_make_executable(
    const char *path,
    basl_error_t *error
);

/*
 * List files in a directory (non-recursive).
 * Callback is called for each entry with its name and whether it's a directory.
 * Return BASL_STATUS_OK from callback to continue, anything else to stop.
 */
typedef basl_status_t (*basl_platform_dir_callback_t)(
    const char *name,
    int is_dir,
    void *user_data
);

BASL_API basl_status_t basl_platform_list_dir(
    const char *path,
    basl_platform_dir_callback_t callback,
    void *user_data,
    basl_error_t *error
);

/* ── Environment variables ───────────────────────────────────────── */

/**
 * Get an environment variable.  Returns BASL_STATUS_OK and sets
 * *out_value to the value (caller must free with free()) and
 * *out_found to 1 if found, or *out_found to 0 if not found.
 */
BASL_API basl_status_t basl_platform_getenv(
    const char *name,
    char **out_value,
    int *out_found,
    basl_error_t *error
);

/**
 * Set an environment variable.  Uses setenv on POSIX, _putenv_s on
 * Windows.  Returns BASL_STATUS_UNSUPPORTED on stub.
 */
BASL_API basl_status_t basl_platform_setenv(
    const char *name,
    const char *value,
    basl_error_t *error
);

/* ── OS information ──────────────────────────────────────────────── */

/** Return a static string identifying the OS: "linux", "darwin", "windows", etc. */
BASL_API const char *basl_platform_os_name(void);

/** Get the current working directory.  Caller must free *out_path. */
BASL_API basl_status_t basl_platform_getcwd(
    char **out_path,
    basl_error_t *error
);

/** Get the system temporary directory path.  Caller must free *out_path. */
BASL_API basl_status_t basl_platform_temp_dir(
    char **out_path,
    basl_error_t *error
);

/** Get the machine hostname.  Caller must free *out_name. */
BASL_API basl_status_t basl_platform_hostname(
    char **out_name,
    basl_error_t *error
);

/* ── Process execution ───────────────────────────────────────────── */

/**
 * Execute a child process and capture its output.
 *
 * argv is a NULL-terminated array of strings (argv[0] = program).
 * On success, *out_stdout and *out_stderr are allocated strings the
 * caller must free, and *out_exit_code is the child's exit status.
 * Returns BASL_STATUS_UNSUPPORTED on stub/embedded platforms.
 */
BASL_API basl_status_t basl_platform_exec(
    const char *const *argv,
    char **out_stdout,
    char **out_stderr,
    int *out_exit_code,
    basl_error_t *error
);

/* ── Dynamic library loading ─────────────────────────────────────── */

/**
 * Load a shared library.  Returns an opaque handle.
 * Returns BASL_STATUS_UNSUPPORTED on stub/sandboxed platforms.
 */
BASL_API basl_status_t basl_platform_dlopen(
    const char *path,
    void **out_handle,
    basl_error_t *error
);

/**
 * Look up a symbol in a loaded library.  Returns a function pointer.
 */
BASL_API basl_status_t basl_platform_dlsym(
    void *handle,
    const char *name,
    void **out_sym,
    basl_error_t *error
);

/**
 * Close a loaded library.
 */
BASL_API basl_status_t basl_platform_dlclose(
    void *handle,
    basl_error_t *error
);

/* ── Terminal raw mode and key reading (for line editor) ──────────── */

/** Opaque terminal state saved before entering raw mode. */
typedef struct basl_terminal_state basl_terminal_state_t;

/** Return 1 if file descriptor 0 (stdin) is a terminal. */
BASL_API int basl_platform_is_terminal(void);

/** Enter raw mode.  Caller must call basl_platform_terminal_restore. */
BASL_API basl_status_t basl_platform_terminal_raw(
    basl_terminal_state_t **out_state,
    basl_error_t *error
);

/** Restore terminal to the state saved by basl_platform_terminal_raw. */
BASL_API void basl_platform_terminal_restore(basl_terminal_state_t *state);

/** Read a single byte from stdin (blocking). Returns -1 on EOF. */
BASL_API int basl_platform_terminal_read_byte(void);

/** Get terminal width in columns.  Returns 80 on failure. */
BASL_API int basl_platform_terminal_width(void);

/* ── Portable line editor (implemented in line_editor.c) ─────────── */

/** History buffer. */
typedef struct basl_line_history {
    char **entries;
    size_t count;
    size_t capacity;
    size_t max_entries;
} basl_line_history_t;

BASL_API void basl_line_history_init(basl_line_history_t *h, size_t max_entries);
BASL_API void basl_line_history_free(basl_line_history_t *h);
BASL_API void basl_line_history_add(basl_line_history_t *h, const char *line);
BASL_API const char *basl_line_history_get(const basl_line_history_t *h, size_t index);
BASL_API void basl_line_history_clear(basl_line_history_t *h);
BASL_API basl_status_t basl_line_history_load(basl_line_history_t *h, const char *path, basl_error_t *error);
BASL_API basl_status_t basl_line_history_save(const basl_line_history_t *h, const char *path, basl_error_t *error);

/**
 * Read a line with editing and history support.
 * Uses raw mode when stdin is a terminal, falls back to fgets otherwise.
 * Returns BASL_STATUS_INTERNAL on EOF.
 */
BASL_API basl_status_t basl_line_editor_readline(
    const char *prompt,
    char *out_buf,
    size_t buf_size,
    basl_line_history_t *history,
    basl_error_t *error
);

/* ── Extended filesystem operations ──────────────────────────────── */

/** Copy a file from src to dst. */
BASL_API basl_status_t basl_platform_copy_file(
    const char *src,
    const char *dst,
    basl_error_t *error
);

/** Move/rename a file or directory. */
BASL_API basl_status_t basl_platform_rename(
    const char *src,
    const char *dst,
    basl_error_t *error
);

/** Get file size in bytes. Returns -1 on error. */
BASL_API int64_t basl_platform_file_size(const char *path);

/** Get file modification time as Unix timestamp. Returns -1 on error. */
BASL_API int64_t basl_platform_file_mtime(const char *path);

/** Check if path is a regular file (not directory/symlink). */
BASL_API basl_status_t basl_platform_is_file(
    const char *path,
    int *out_is_file
);

/** Create a symbolic link. */
BASL_API basl_status_t basl_platform_symlink(
    const char *target,
    const char *linkpath,
    basl_error_t *error
);

/** Create a hard link. */
BASL_API basl_status_t basl_platform_hardlink(
    const char *target,
    const char *linkpath,
    basl_error_t *error
);

/** Read the target of a symbolic link. Caller must free *out_target. */
BASL_API basl_status_t basl_platform_readlink(
    const char *path,
    char **out_target,
    basl_error_t *error
);

/** Get user home directory. Caller must free *out_path. */
BASL_API basl_status_t basl_platform_home_dir(
    char **out_path,
    basl_error_t *error
);

/** Get user config directory (XDG_CONFIG_HOME / ~/Library/Application Support / APPDATA). */
BASL_API basl_status_t basl_platform_config_dir(
    char **out_path,
    basl_error_t *error
);

/** Get user cache directory (XDG_CACHE_HOME / ~/Library/Caches / LOCALAPPDATA). */
BASL_API basl_status_t basl_platform_cache_dir(
    char **out_path,
    basl_error_t *error
);

/** Get user data directory (XDG_DATA_HOME / ~/Library/Application Support / APPDATA). */
BASL_API basl_status_t basl_platform_data_dir(
    char **out_path,
    basl_error_t *error
);

/** Create a unique temporary file. Returns path in out_path (caller must free). */
BASL_API basl_status_t basl_platform_temp_file(
    const char *prefix,
    char **out_path,
    basl_error_t *error
);

/** Append data to a file. */
BASL_API basl_status_t basl_platform_append_file(
    const char *path,
    const void *data,
    size_t length,
    basl_error_t *error
);

/* ── Threading primitives ────────────────────────────────────────── */

/** Opaque thread handle. */
typedef struct basl_platform_thread basl_platform_thread_t;

/** Opaque mutex handle. */
typedef struct basl_platform_mutex basl_platform_mutex_t;

/** Opaque condition variable handle. */
typedef struct basl_platform_cond basl_platform_cond_t;

/** Opaque read-write lock handle. */
typedef struct basl_platform_rwlock basl_platform_rwlock_t;

/** Thread entry function type. */
typedef void (*basl_thread_func_t)(void *arg);

/** Create and start a new thread. Caller must free with thread_join or thread_detach. */
BASL_API basl_status_t basl_platform_thread_create(
    basl_platform_thread_t **out_thread,
    basl_thread_func_t func,
    void *arg,
    basl_error_t *error
);

/** Wait for thread to complete and free resources. */
BASL_API basl_status_t basl_platform_thread_join(
    basl_platform_thread_t *thread,
    basl_error_t *error
);

/** Detach thread (will clean up automatically when done). */
BASL_API basl_status_t basl_platform_thread_detach(
    basl_platform_thread_t *thread,
    basl_error_t *error
);

/** Get current thread ID. */
BASL_API uint64_t basl_platform_thread_current_id(void);

/** Yield execution to other threads. */
BASL_API void basl_platform_thread_yield(void);

/** Sleep for specified milliseconds. */
BASL_API void basl_platform_thread_sleep(uint64_t milliseconds);

/** Create a mutex. Caller must free with mutex_destroy. */
BASL_API basl_status_t basl_platform_mutex_create(
    basl_platform_mutex_t **out_mutex,
    basl_error_t *error
);

/** Destroy a mutex. */
BASL_API void basl_platform_mutex_destroy(basl_platform_mutex_t *mutex);

/** Lock a mutex (blocking). */
BASL_API void basl_platform_mutex_lock(basl_platform_mutex_t *mutex);

/** Unlock a mutex. */
BASL_API void basl_platform_mutex_unlock(basl_platform_mutex_t *mutex);

/** Try to lock a mutex without blocking. Returns 1 if acquired, 0 if not. */
BASL_API int basl_platform_mutex_trylock(basl_platform_mutex_t *mutex);

/** Create a condition variable. Caller must free with cond_destroy. */
BASL_API basl_status_t basl_platform_cond_create(
    basl_platform_cond_t **out_cond,
    basl_error_t *error
);

/** Destroy a condition variable. */
BASL_API void basl_platform_cond_destroy(basl_platform_cond_t *cond);

/** Wait on condition variable. Mutex must be held; released while waiting. */
BASL_API void basl_platform_cond_wait(
    basl_platform_cond_t *cond,
    basl_platform_mutex_t *mutex
);

/** Wait on condition variable with timeout.  Returns 1 if signalled, 0 on timeout. */
BASL_API int basl_platform_cond_timedwait(
    basl_platform_cond_t *cond,
    basl_platform_mutex_t *mutex,
    uint64_t timeout_ms
);

/** Signal one waiting thread. */
BASL_API void basl_platform_cond_signal(basl_platform_cond_t *cond);

/** Signal all waiting threads. */
BASL_API void basl_platform_cond_broadcast(basl_platform_cond_t *cond);

/** Create a read-write lock. Caller must free with rwlock_destroy. */
BASL_API basl_status_t basl_platform_rwlock_create(
    basl_platform_rwlock_t **out_rwlock,
    basl_error_t *error
);

/** Destroy a read-write lock. */
BASL_API void basl_platform_rwlock_destroy(basl_platform_rwlock_t *rwlock);

/** Acquire read lock (shared, multiple readers allowed). */
BASL_API void basl_platform_rwlock_rdlock(basl_platform_rwlock_t *rwlock);

/** Acquire write lock (exclusive). */
BASL_API void basl_platform_rwlock_wrlock(basl_platform_rwlock_t *rwlock);

/** Release read or write lock. */
BASL_API void basl_platform_rwlock_unlock(basl_platform_rwlock_t *rwlock);

/* ── Thread-local storage ────────────────────────────────────────── */

/** Opaque TLS key. */
typedef struct basl_platform_tls_key basl_platform_tls_key_t;

/** Create a TLS key. Optional destructor called when thread exits. */
BASL_API basl_status_t basl_platform_tls_create(
    basl_platform_tls_key_t **out_key,
    void (*destructor)(void *),
    basl_error_t *error
);

/** Destroy a TLS key. */
BASL_API void basl_platform_tls_destroy(basl_platform_tls_key_t *key);

/** Set TLS value for current thread. */
BASL_API void basl_platform_tls_set(basl_platform_tls_key_t *key, void *value);

/** Get TLS value for current thread. */
BASL_API void *basl_platform_tls_get(basl_platform_tls_key_t *key);

/* ── TCP sockets ─────────────────────────────────────────────────── */

/** Opaque socket handle (int64_t, -1 = invalid). */
typedef int64_t basl_socket_t;
#define BASL_INVALID_SOCKET ((basl_socket_t)-1)

/** Initialise platform networking (e.g. WSAStartup). Safe to call repeatedly. */
BASL_API basl_status_t basl_platform_net_init(basl_error_t *error);

/** Create a TCP listener bound to host:port. Returns socket handle. */
BASL_API basl_status_t basl_platform_tcp_listen(
    const char *host, int port, basl_socket_t *out_sock, basl_error_t *error);

/** Accept a connection on a listening socket. Blocks. */
BASL_API basl_status_t basl_platform_tcp_accept(
    basl_socket_t listener, basl_socket_t *out_client, basl_error_t *error);

/** Connect to host:port. Returns socket handle. */
BASL_API basl_status_t basl_platform_tcp_connect(
    const char *host, int port, basl_socket_t *out_sock, basl_error_t *error);

/** Send data on a connected socket. Returns bytes sent. */
BASL_API basl_status_t basl_platform_tcp_send(
    basl_socket_t sock, const void *data, size_t len,
    size_t *out_sent, basl_error_t *error);

/** Receive data from a connected socket. Returns bytes received (0 = EOF). */
BASL_API basl_status_t basl_platform_tcp_recv(
    basl_socket_t sock, void *buf, size_t cap,
    size_t *out_received, basl_error_t *error);

/** Close a socket (listener or connection). */
BASL_API basl_status_t basl_platform_tcp_close(
    basl_socket_t sock, basl_error_t *error);

/** Set read/write timeout on a TCP socket (milliseconds, 0 = no timeout). */
BASL_API basl_status_t basl_platform_tcp_set_timeout(
    basl_socket_t sock, int timeout_ms, basl_error_t *error);

/* ── HTTP client (native library, runtime-loaded) ────────────────── */

/** Result of an HTTP request. Caller must free body and headers with free(). */
typedef struct {
    int status_code;
    char *headers;
    size_t headers_len;
    char *body;
    size_t body_len;
} basl_http_response_t;

/**
 * Perform an HTTP/S request using the best available native library
 * (libcurl on POSIX, WinHTTP on Windows), loaded at runtime.
 *
 * Returns BASL_STATUS_UNSUPPORTED if no native HTTP library is available.
 * The caller should fall back to a plain-socket HTTP/1.1 path (no TLS).
 */
BASL_API basl_status_t basl_platform_http_request(
    const char *method,
    const char *url,
    const char *headers,     /* CRLF-separated, may be NULL */
    const char *body,        /* may be NULL */
    size_t body_len,
    basl_http_response_t *out_response,
    basl_error_t *error);

/* ── Atomic operations ───────────────────────────────────────────── */

/** Atomically load a 64-bit value. */
BASL_API int64_t basl_atomic_load(const volatile int64_t *ptr);

/** Atomically store a 64-bit value. */
BASL_API void basl_atomic_store(volatile int64_t *ptr, int64_t value);

/** Atomically add to a 64-bit value, return previous value. */
BASL_API int64_t basl_atomic_add(volatile int64_t *ptr, int64_t value);

/** Atomically subtract from a 64-bit value, return previous value. */
BASL_API int64_t basl_atomic_sub(volatile int64_t *ptr, int64_t value);

/** Atomically compare and swap. Returns 1 if swapped, 0 if not. */
BASL_API int basl_atomic_cas(volatile int64_t *ptr, int64_t expected, int64_t desired);

#ifdef __cplusplus
}
#endif

#endif
