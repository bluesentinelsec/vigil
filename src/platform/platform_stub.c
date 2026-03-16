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
