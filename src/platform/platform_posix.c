#include "platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

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
    FILE *f;
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

    f = fopen(path, "rb");
    if (!f) {
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
    FILE *f;
    if (!path) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: NULL path");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    f = fopen(path, "wb");
    if (!f) {
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
    struct stat st;
    if (!path || !out_exists) return BASL_STATUS_INVALID_ARGUMENT;
    *out_exists = (stat(path, &st) == 0) ? 1 : 0;
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_is_directory(const char *path, int *out_is_dir) {
    struct stat st;
    if (!path || !out_is_dir) return BASL_STATUS_INVALID_ARGUMENT;
    if (stat(path, &st) != 0) { *out_is_dir = 0; return BASL_STATUS_OK; }
    *out_is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
    return BASL_STATUS_OK;
}

basl_status_t basl_platform_mkdir(const char *path, basl_error_t *error) {
    if (!path) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: NULL path");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
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
        if (buf[i] == '/' || buf[i] == '\0') {
            char saved = buf[i];
            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
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
    if (!path) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT,
                               "platform: NULL path");
        return BASL_STATUS_INVALID_ARGUMENT;
    }
    if (remove(path) != 0) {
        basl_error_set_literal(error, BASL_STATUS_INTERNAL,
                               "platform: remove failed");
        return BASL_STATUS_INTERNAL;
    }
    return BASL_STATUS_OK;
}
