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
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Read an entire file into a newly allocated buffer (null-terminated). */
basl_status_t basl_platform_read_file(
    const basl_allocator_t *allocator,
    const char *path,
    char **out_data,
    size_t *out_length,
    basl_error_t *error
);

/** Write data to a file, creating or truncating it. */
basl_status_t basl_platform_write_file(
    const char *path,
    const void *data,
    size_t length,
    basl_error_t *error
);

/** Check whether a path exists (file or directory). */
basl_status_t basl_platform_file_exists(
    const char *path,
    int *out_exists
);

/** Check whether a path is a directory. */
basl_status_t basl_platform_is_directory(
    const char *path,
    int *out_is_dir
);

/** Create a single directory.  Fails if parent doesn't exist. */
basl_status_t basl_platform_mkdir(
    const char *path,
    basl_error_t *error
);

/** Create a directory and all missing parents. */
basl_status_t basl_platform_mkdir_p(
    const char *path,
    basl_error_t *error
);

/** Remove a file or empty directory. */
basl_status_t basl_platform_remove(
    const char *path,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
