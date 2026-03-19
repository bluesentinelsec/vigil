#ifndef VIGIL_PACKAGE_H
#define VIGIL_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#include "vigil/export.h"
#include "vigil/status.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define VIGIL_PACKAGE_MAGIC "VIGLPKG1"
#define VIGIL_PACKAGE_MAGIC_LEN 8
#define VIGIL_PACKAGE_KEY_LEN 32
/* Trailer: [8 bytes payload len][32 bytes key][8 bytes magic] = 48 */
#define VIGIL_PACKAGE_TRAILER_LEN (8 + VIGIL_PACKAGE_KEY_LEN + VIGIL_PACKAGE_MAGIC_LEN)

    /* ── Bundle file entry ───────────────────────────────────────────── */

    typedef struct vigil_package_file
    {
        const char *path; /* bundle-internal path (e.g. "entry.vigil") */
        size_t path_length;
        const char *data;
        size_t data_length;
    } vigil_package_file_t;

    /* ── Build a packaged binary ─────────────────────────────────────── */

    /*
     * Build a standalone binary by copying the current executable and
     * appending a zip bundle of the given files.
     * If key is non-NULL and key_length > 0, the zip payload is XOR-encrypted.
     */
    VIGIL_API vigil_status_t vigil_package_build(const char *output_path, const vigil_package_file_t *files,
                                                 size_t file_count, const char *key, size_t key_length,
                                                 vigil_error_t *error);

    /* ── Read bundle from a binary ───────────────────────────────────── */

    typedef struct vigil_package_bundle
    {
        char **paths;
        char **contents;
        size_t *content_lengths;
        size_t file_count;
    } vigil_package_bundle_t;

    /*
     * Read the appended bundle from a binary file.
     * Returns VIGIL_STATUS_INTERNAL if no bundle is present.
     */
    VIGIL_API vigil_status_t vigil_package_read(const char *binary_path, vigil_package_bundle_t *out_bundle,
                                                vigil_error_t *error);

    VIGIL_API void vigil_package_bundle_free(vigil_package_bundle_t *bundle);

    /*
     * Check if the current executable has an appended bundle.
     */
    VIGIL_API vigil_status_t vigil_package_read_self(vigil_package_bundle_t *out_bundle, vigil_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
