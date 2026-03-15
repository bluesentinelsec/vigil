#ifndef BASL_EMBED_H
#define BASL_EMBED_H

#include <stddef.h>

#include "basl/export.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generate a BASL source module that embeds a single file as base64.
 * Caller must free *out_text.
 */
BASL_API basl_status_t basl_embed_single(
    const char *file_path,
    char **out_text,
    size_t *out_length,
    basl_error_t *error
);

/*
 * Generate a BASL source module that embeds multiple files as base64.
 * Provides get(path), list(), and count.
 * Caller must free *out_text.
 */
BASL_API basl_status_t basl_embed_multi(
    const char **file_paths,
    const char **rel_paths,
    size_t file_count,
    char **out_text,
    size_t *out_length,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
