#ifndef BASL_FMT_H
#define BASL_FMT_H

#include <stddef.h>

#include "basl/export.h"
#include "basl/status.h"
#include "basl/token.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Format BASL source code.
 *
 * Takes the original source text and its token list (produced by
 * basl_lex_source).  Returns a newly allocated formatted string in
 * *out_text with length *out_length.  Caller must free *out_text.
 *
 * Comments are preserved by scanning the source text between token spans.
 */
BASL_API basl_status_t basl_fmt(
    const char *source_text,
    size_t source_length,
    const basl_token_list_t *tokens,
    char **out_text,
    size_t *out_length,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
