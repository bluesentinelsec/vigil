#ifndef BASL_COMPILER_H
#define BASL_COMPILER_H

#include "basl/diagnostic.h"
#include "basl/export.h"
#include "basl/native_module.h"
#include "basl/source.h"
#include "basl/status.h"
#include "basl/value.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Current implementation compiles a runnable BASL subset across multiple
 * source files: top-level `import`, `const`, `class`, and `fn` declarations
 * with a required `fn main() -> i32 { ... }` entrypoint. Function bodies
 * support typed local declarations, integer/bool expressions, short-circuit
 * `&&`/`||`, assignment, `if`, `while`, `break`, `continue`, explicit
 * `return` statements, class construction, field reads/writes, and class
 * methods with implicit `self`.
 */
BASL_API basl_status_t basl_compile_source(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_object_t **out_function,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
);

/*
 * Compile in REPL mode.  Top-level statements and expressions are allowed
 * without a fn main() wrapper.  The compiler synthesizes an entrypoint
 * from any non-declaration tokens found at the top level.  If the source
 * contains only declarations and no statements, *out_has_statements is set
 * to 0 (the returned function still validates global initializers).
 */
BASL_API basl_status_t basl_compile_source_repl(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    const basl_native_registry_t *natives,
    basl_object_t **out_function,
    int *out_has_statements,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif
