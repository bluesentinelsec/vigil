#ifndef BASL_COMPILER_INTERNAL_H
#define BASL_COMPILER_INTERNAL_H

#include "basl/compiler.h"

typedef enum basl_compile_mode {
    BASL_COMPILE_MODE_CHECK_ONLY = 0,
    BASL_COMPILE_MODE_BUILD_ENTRYPOINT = 1
} basl_compile_mode_t;

basl_status_t basl_compile_source_internal(
    const basl_source_registry_t *registry,
    basl_source_id_t source_id,
    basl_compile_mode_t mode,
    basl_object_t **out_function,
    basl_diagnostic_list_t *diagnostics,
    basl_error_t *error
);

#endif
