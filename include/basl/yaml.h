/* BASL YAML parsing library.
 *
 * Parses a subset of YAML 1.2 into basl_json_value_t structures.
 * Supports: scalars, block mappings, block sequences, comments,
 * quoted strings, and block scalars (| and >).
 * Pure C11, no external dependencies.
 */
#ifndef BASL_YAML_H
#define BASL_YAML_H

#include "basl/allocator.h"
#include "basl/export.h"
#include "basl/json.h"
#include "basl/status.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse a YAML string into a JSON value tree.
 * Returns BASL_STATUS_OK on success.
 * The returned value must be freed with basl_json_free().
 */
BASL_API basl_status_t basl_yaml_parse(
    const char *yaml,
    size_t length,
    const basl_allocator_t *allocator,
    basl_json_value_t **out,
    basl_error_t *error
);

#ifdef __cplusplus
}
#endif

#endif /* BASL_YAML_H */
