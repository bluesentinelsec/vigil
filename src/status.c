#include <string.h>

#include "internal/basl_internal.h"

void basl_source_location_clear(basl_source_location_t *location) {
    if (location == NULL) {
        return;
    }

    location->source_id = 0U;
    location->line = 0U;
    location->column = 0U;
}

void basl_error_set_literal(
    basl_error_t *error,
    basl_status_t type,
    const char *value
) {
    if (error == NULL) {
        return;
    }

    error->type = type;
    error->value = value;
    error->length = value == NULL ? 0U : strlen(value);
    basl_source_location_clear(&error->location);
}

void basl_error_clear(basl_error_t *error) {
    if (error == NULL) {
        return;
    }

    error->type = BASL_STATUS_OK;
    error->value = NULL;
    error->length = 0U;
    basl_source_location_clear(&error->location);
}

const char *basl_status_name(basl_status_t status) {
    switch (status) {
        case BASL_STATUS_OK:
            return "ok";
        case BASL_STATUS_INVALID_ARGUMENT:
            return "invalid_argument";
        case BASL_STATUS_OUT_OF_MEMORY:
            return "out_of_memory";
        case BASL_STATUS_INTERNAL:
            return "internal";
        case BASL_STATUS_UNSUPPORTED:
            return "unsupported";
        default:
            return "unknown";
    }
}
