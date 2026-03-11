#ifndef BASL_LEXER_H
#define BASL_LEXER_H

#include "basl/diagnostic.h"
#include "basl/export.h"
#include "basl/source.h"
#include "basl/status.h"
#include "basl/token.h"

#ifdef __cplusplus
extern "C" {
#endif

BASL_API basl_status_t basl_lex_source(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_token_list_t *tokens,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
