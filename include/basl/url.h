/* BASL URL parsing library.
 *
 * Implements RFC 3986 URI parsing with percent-encoding/decoding.
 * Pure C11, no external dependencies.
 */
#ifndef BASL_URL_H
#define BASL_URL_H

#include "basl/export.h"
#include "basl/status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parsed URL structure.
 * All strings are owned by the structure and freed by basl_url_free().
 */
typedef struct basl_url {
    char *scheme;       /* e.g. "https" */
    char *username;     /* userinfo username (decoded) */
    char *password;     /* userinfo password (decoded), NULL if not set */
    char *host;         /* hostname or IP (decoded) */
    char *port;         /* port string, NULL if not specified */
    char *path;         /* path (decoded) */
    char *raw_query;    /* query string without '?' (encoded) */
    char *fragment;     /* fragment without '#' (decoded) */
} basl_url_t;

/**
 * Parse a URL string into components.
 * Returns BASL_STATUS_OK on success.
 */
BASL_API basl_status_t basl_url_parse(
    const char *url_string,
    size_t url_length,
    basl_url_t *out_url,
    basl_error_t *error
);

/**
 * Free all memory owned by a parsed URL.
 */
BASL_API void basl_url_free(basl_url_t *url);

/**
 * Reconstruct a URL string from components.
 * Caller must free the returned string.
 */
BASL_API basl_status_t basl_url_string(
    const basl_url_t *url,
    char **out_string,
    size_t *out_length,
    basl_error_t *error
);

/**
 * Percent-encode a string for use in a URL path.
 * Caller must free the returned string.
 */
BASL_API basl_status_t basl_url_path_escape(
    const char *input,
    size_t input_length,
    char **out_escaped,
    size_t *out_length,
    basl_error_t *error
);

/**
 * Percent-encode a string for use in a URL query.
 * Caller must free the returned string.
 */
BASL_API basl_status_t basl_url_query_escape(
    const char *input,
    size_t input_length,
    char **out_escaped,
    size_t *out_length,
    basl_error_t *error
);

/**
 * Decode a percent-encoded string.
 * Caller must free the returned string.
 */
BASL_API basl_status_t basl_url_unescape(
    const char *input,
    size_t input_length,
    char **out_decoded,
    size_t *out_length,
    basl_error_t *error
);

/**
 * Get the hostname from a URL (without port).
 */
BASL_API const char *basl_url_hostname(const basl_url_t *url);

/**
 * Check if URL is absolute (has scheme).
 */
BASL_API int basl_url_is_absolute(const basl_url_t *url);

#ifdef __cplusplus
}
#endif

#endif /* BASL_URL_H */
