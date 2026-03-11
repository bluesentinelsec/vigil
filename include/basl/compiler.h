#ifndef BASL_COMPILER_H
#define BASL_COMPILER_H

#include "basl/diagnostic.h"
#include "basl/export.h"
#include "basl/source.h"
#include "basl/status.h"
#include "basl/value.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Current implementation compiles a narrow runnable BASL slice:
 * multiple top-level `fn` declarations with direct same-module calls,
 * `i32`/`bool` parameters and return types, and a required
 * `fn main() -> i32 { ... }` entrypoint. Function bodies support typed local
 * declarations, integer/bool expressions, assignment, `if`, `while`, and
 * explicit `return` statements.
 */
BASL_API basl_status_t basl_compile_source(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_object_t **out_function,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
