#ifndef BASL_STATUS_H
#define BASL_STATUS_H

#include <stddef.h>
#include <stdint.h>

#include "basl/export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum basl_status {
    BASL_STATUS_OK = 0,
    BASL_STATUS_INVALID_ARGUMENT = 1,
    BASL_STATUS_OUT_OF_MEMORY = 2,
    BASL_STATUS_INTERNAL = 3,
    BASL_STATUS_UNSUPPORTED = 4,
    BASL_STATUS_SYNTAX_ERROR = 5
} basl_status_t;

typedef struct basl_source_location {
    uint32_t source_id;
    uint32_t line;
    uint32_t column;
} basl_source_location_t;

typedef struct basl_error {
    basl_status_t type;
    const char *value;
    size_t length;
    basl_source_location_t location;
} basl_error_t;

BASL_API void basl_error_clear(basl_error_t *error);
BASL_API void basl_source_location_clear(basl_source_location_t *location);
BASL_API const char *basl_error_message(const basl_error_t *error);
BASL_API const char *basl_status_name(basl_status_t status);

#ifdef __cplusplus
}
#endif

#endif
