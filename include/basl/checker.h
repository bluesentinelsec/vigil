#ifndef BASL_CHECKER_H
#define BASL_CHECKER_H

#include "basl/diagnostic.h"
#include "basl/export.h"
#include "basl/source.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Validates a BASL source file without returning an executable entrypoint.
 * Diagnostics describe syntax and semantic errors discovered during lexing,
 * declaration parsing, and function-body compilation.
 */
BASL_API basl_status_t basl_check_source(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
