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
