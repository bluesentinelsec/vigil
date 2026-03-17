#include "platform.h"

#include "internal/basl_internal.h"

/* Stub implementation — returns BASL_STATUS_UNSUPPORTED for all operations. */

basl_status_t basl_platform_read_file(
    const basl_allocator_t *allocator,
    const char *path,
    char **out_data,
    size_t *out_length,
    basl_error_t *error
) {
    (void)allocator; (void)path; (void)out_data; (void)out_length;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED,
                           "file I/O is not supported on this platform");
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_write_file(
    const char *path,
    const void *data,
    size_t length,
    basl_error_t *error
) {
    (void)path; (void)data; (void)length;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED,
                           "file I/O is not supported on this platform");
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_file_exists(const char *path, int *out_exists) {
    (void)path; (void)out_exists;
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_is_directory(const char *path, int *out_is_dir) {
    (void)path; (void)out_is_dir;
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_mkdir(const char *path, basl_error_t *error) {
    (void)path;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED,
                           "file I/O is not supported on this platform");
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_mkdir_p(const char *path, basl_error_t *error) {
    (void)path;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED,
                           "file I/O is not supported on this platform");
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_remove(const char *path, basl_error_t *error) {
    (void)path;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED,
                           "file I/O is not supported on this platform");
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_path_join(
    const char *base, const char *child,
    char *out_buf, size_t buf_size, basl_error_t *error
) {
    (void)base; (void)child; (void)out_buf; (void)buf_size;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED,
                           "file I/O is not supported on this platform");
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_readline(
    const char *prompt, char *out_buf, size_t buf_size, basl_error_t *error
) {
    (void)prompt; (void)out_buf; (void)buf_size;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED,
                           "stdin is not supported on this platform");
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_self_exe(
    char *out_buf, size_t buf_size, basl_error_t *error
) {
    (void)out_buf; (void)buf_size;
    if (error) { error->type = BASL_STATUS_UNSUPPORTED; error->value = "not supported"; error->length = 13; }
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_make_executable(
    const char *path, basl_error_t *error
) {
    (void)path; (void)error;
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_list_dir(
    const char *path,
    basl_platform_dir_callback_t callback,
    void *user_data,
    basl_error_t *error
) {
    (void)path; (void)callback; (void)user_data;
    if (error) { error->type = BASL_STATUS_UNSUPPORTED; error->value = "not supported"; error->length = 13; }
    return BASL_STATUS_UNSUPPORTED;
}

/* ── Environment variables ───────────────────────────────────────── */

basl_status_t basl_platform_getenv(
    const char *name, char **out_value, int *out_found, basl_error_t *error
) {
    (void)name; (void)out_value; (void)out_found;
    if (error) { error->type = BASL_STATUS_UNSUPPORTED; error->value = "not supported"; error->length = 13; }
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_setenv(
    const char *name, const char *value, basl_error_t *error
) {
    (void)name; (void)value;
    if (error) { error->type = BASL_STATUS_UNSUPPORTED; error->value = "not supported"; error->length = 13; }
    return BASL_STATUS_UNSUPPORTED;
}

/* ── OS information ──────────────────────────────────────────────── */

const char *basl_platform_os_name(void) {
    return "unknown";
}

basl_status_t basl_platform_getcwd(char **out_path, basl_error_t *error) {
    (void)out_path;
    if (error) { error->type = BASL_STATUS_UNSUPPORTED; error->value = "not supported"; error->length = 13; }
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_temp_dir(char **out_path, basl_error_t *error) {
    (void)out_path;
    if (error) { error->type = BASL_STATUS_UNSUPPORTED; error->value = "not supported"; error->length = 13; }
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_hostname(char **out_name, basl_error_t *error) {
    (void)out_name;
    if (error) { error->type = BASL_STATUS_UNSUPPORTED; error->value = "not supported"; error->length = 13; }
    return BASL_STATUS_UNSUPPORTED;
}

/* ── Process execution ───────────────────────────────────────────── */

basl_status_t basl_platform_exec(
    const char *const *argv,
    char **out_stdout, char **out_stderr, int *out_exit_code,
    basl_error_t *error
) {
    (void)argv; (void)out_stdout; (void)out_stderr; (void)out_exit_code;
    if (error) { error->type = BASL_STATUS_UNSUPPORTED; error->value = "not supported"; error->length = 13; }
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_dlopen(
    const char *path, void **out_handle, basl_error_t *error
) {
    (void)path; (void)out_handle;
    if (error) { error->type = BASL_STATUS_UNSUPPORTED; error->value = "not supported"; error->length = 13; }
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_dlsym(
    void *handle, const char *name, void **out_sym, basl_error_t *error
) {
    (void)handle; (void)name; (void)out_sym;
    if (error) { error->type = BASL_STATUS_UNSUPPORTED; error->value = "not supported"; error->length = 13; }
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_dlclose(
    void *handle, basl_error_t *error
) {
    (void)handle;
    if (error) { error->type = BASL_STATUS_UNSUPPORTED; error->value = "not supported"; error->length = 13; }
    return BASL_STATUS_UNSUPPORTED;
}

/* ── Terminal raw mode (stub — no terminal support) ──────────────── */

int basl_platform_is_terminal(void) { return 0; }

basl_status_t basl_platform_terminal_raw(
    basl_terminal_state_t **out_state, basl_error_t *error
) {
    (void)out_state;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED, "no terminal support");
    return BASL_STATUS_UNSUPPORTED;
}

void basl_platform_terminal_restore(basl_terminal_state_t *state) { (void)state; }
int basl_platform_terminal_read_byte(void) { return -1; }
int basl_platform_terminal_width(void) { return 80; }

/* ── Threading primitives (stub — single-threaded only) ──────────── */

basl_status_t basl_platform_thread_create(
    basl_platform_thread_t **out_thread,
    basl_thread_func_t func,
    void *arg,
    basl_error_t *error
) {
    (void)out_thread; (void)func; (void)arg;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED, "threading not supported");
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_thread_join(basl_platform_thread_t *thread, basl_error_t *error) {
    (void)thread;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED, "threading not supported");
    return BASL_STATUS_UNSUPPORTED;
}

basl_status_t basl_platform_thread_detach(basl_platform_thread_t *thread, basl_error_t *error) {
    (void)thread;
    basl_error_set_literal(error, BASL_STATUS_UNSUPPORTED, "threading not supported");
    return BASL_STATUS_UNSUPPORTED;
}

uint64_t basl_platform_thread_current_id(void) { return 1; }
void basl_platform_thread_yield(void) { }
void basl_platform_thread_sleep(uint64_t milliseconds) { (void)milliseconds; }

basl_status_t basl_platform_mutex_create(basl_platform_mutex_t **out_mutex, basl_error_t *error) {
    (void)error;
    *out_mutex = (basl_platform_mutex_t *)1; /* dummy non-NULL */
    return BASL_STATUS_OK;
}

void basl_platform_mutex_destroy(basl_platform_mutex_t *mutex) { (void)mutex; }
void basl_platform_mutex_lock(basl_platform_mutex_t *mutex) { (void)mutex; }
void basl_platform_mutex_unlock(basl_platform_mutex_t *mutex) { (void)mutex; }
int basl_platform_mutex_trylock(basl_platform_mutex_t *mutex) { (void)mutex; return 1; }

basl_status_t basl_platform_cond_create(basl_platform_cond_t **out_cond, basl_error_t *error) {
    (void)error;
    *out_cond = (basl_platform_cond_t *)1;
    return BASL_STATUS_OK;
}

void basl_platform_cond_destroy(basl_platform_cond_t *cond) { (void)cond; }
void basl_platform_cond_wait(basl_platform_cond_t *cond, basl_platform_mutex_t *mutex) { (void)cond; (void)mutex; }
void basl_platform_cond_signal(basl_platform_cond_t *cond) { (void)cond; }
void basl_platform_cond_broadcast(basl_platform_cond_t *cond) { (void)cond; }

basl_status_t basl_platform_rwlock_create(basl_platform_rwlock_t **out_rwlock, basl_error_t *error) {
    (void)error;
    *out_rwlock = (basl_platform_rwlock_t *)1;
    return BASL_STATUS_OK;
}

void basl_platform_rwlock_destroy(basl_platform_rwlock_t *rwlock) { (void)rwlock; }
void basl_platform_rwlock_rdlock(basl_platform_rwlock_t *rwlock) { (void)rwlock; }
void basl_platform_rwlock_wrlock(basl_platform_rwlock_t *rwlock) { (void)rwlock; }
void basl_platform_rwlock_unlock(basl_platform_rwlock_t *rwlock) { (void)rwlock; }

basl_status_t basl_platform_tls_create(basl_platform_tls_key_t **out_key, void (*destructor)(void *), basl_error_t *error) {
    (void)destructor; (void)error;
    *out_key = (basl_platform_tls_key_t *)1;
    return BASL_STATUS_OK;
}

void basl_platform_tls_destroy(basl_platform_tls_key_t *key) { (void)key; }
void basl_platform_tls_set(basl_platform_tls_key_t *key, void *value) { (void)key; (void)value; }
void *basl_platform_tls_get(basl_platform_tls_key_t *key) { (void)key; return NULL; }

/* ── Atomic operations (stub — non-atomic fallback) ──────────────── */

int64_t basl_atomic_load(const volatile int64_t *ptr) { return *ptr; }
void basl_atomic_store(volatile int64_t *ptr, int64_t value) { *ptr = value; }
int64_t basl_atomic_add(volatile int64_t *ptr, int64_t value) { int64_t old = *ptr; *ptr += value; return old; }
int64_t basl_atomic_sub(volatile int64_t *ptr, int64_t value) { int64_t old = *ptr; *ptr -= value; return old; }
int basl_atomic_cas(volatile int64_t *ptr, int64_t expected, int64_t desired) {
    if (*ptr == expected) { *ptr = desired; return 1; }
    return 0;
}
