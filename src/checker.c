#include "basl/checker.h"

#include "internal/basl_compiler_internal.h"

basl_status_t basl_check_source(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
) {
    return basl_compile_source_internal(
        registry,
        source_id,
        BASL_COMPILE_MODE_CHECK_ONLY,
        NULL,
        diagnostics,
        error
    );
}
