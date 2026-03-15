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

#ifdef __cplusplus
}
#endif

#endif
