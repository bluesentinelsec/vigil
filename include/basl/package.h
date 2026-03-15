#ifndef BASL_PACKAGE_H
#define BASL_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#include "basl/export.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BASL_PACKAGE_MAGIC "BASLPKG1"
#define BASL_PACKAGE_MAGIC_LEN 8
#define BASL_PACKAGE_KEY_LEN 32
/* Trailer: [8 bytes payload len][32 bytes key][8 bytes magic] = 48 */
#define BASL_PACKAGE_TRAILER_LEN (8 + BASL_PACKAGE_KEY_LEN + BASL_PACKAGE_MAGIC_LEN)

/* ── Bundle file entry ───────────────────────────────────────────── */

typedef struct basl_package_file {
    const char *path;       /* bundle-internal path (e.g. "entry.basl") */
    size_t path_length;
    const char *data;
    size_t data_length;
} basl_package_file_t;

/* ── Build a packaged binary ─────────────────────────────────────── */

/*
 * Build a standalone binary by copying the current executable and
 * appending a zip bundle of the given files.
 * If key is non-NULL and key_length > 0, the zip payload is XOR-encrypted.
 */
BASL_API basl_status_t basl_package_build(
    const char *output_path,
    const basl_package_file_t *files,
    size_t file_count,
    const char *key,
    size_t key_length,
    basl_error_t *error
);

/* ── Read bundle from a binary ───────────────────────────────────── */

typedef struct basl_package_bundle {
    char **paths;
    char **contents;
    size_t *content_lengths;
    size_t file_count;
} basl_package_bundle_t;

/*
 * Read the appended bundle from a binary file.
 * Returns BASL_STATUS_INTERNAL if no bundle is present.
 */
BASL_API basl_status_t basl_package_read(
    const char *binary_path,
    basl_package_bundle_t *out_bundle,
    basl_error_t *error
);

BASL_API void basl_package_bundle_free(basl_package_bundle_t *bundle);

/*
 * Check if the current executable has an appended bundle.
 */
BASL_API basl_status_t basl_package_read_self(
    basl_package_bundle_t *out_bundle,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
