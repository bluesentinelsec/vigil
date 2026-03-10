#include <stddef.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/diagnostic.h"

static basl_status_t basl_diagnostic_list_validate_mutable(
    const basl_diagnostic_list_t *list,
    basl_error_t *error
) {
    basl_error_clear(error);

    if (list == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "diagnostic list must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (list->runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "diagnostic list runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (list->items == NULL && (list->count != 0U || list->capacity != 0U)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "diagnostic list state is inconsistent"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (list->count > list->capacity) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "diagnostic list count exceeds capacity"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return BASL_STATUS_OK;
}

static basl_status_t basl_diagnostic_list_grow(
    basl_diagnostic_list_t *list,
    size_t minimum_capacity,
    basl_error_t *error
) {
    size_t old_capacity;
    size_t capacity;
    size_t next_capacity;
    void *memory;
    basl_status_t status;

    if (minimum_capacity <= list->capacity) {
        basl_error_clear(error);
        return BASL_STATUS_OK;
    }

    old_capacity = list->capacity;
    capacity = old_capacity == 0U ? 4U : old_capacity;
    next_capacity = capacity;
    while (next_capacity < minimum_capacity) {
        if (next_capacity > (SIZE_MAX / 2U)) {
            next_capacity = minimum_capacity;
            break;
        }

        next_capacity *= 2U;
    }

    if (next_capacity > (SIZE_MAX / sizeof(*list->items))) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "diagnostic list allocation overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    if (list->items == NULL) {
        memory = NULL;
        status = basl_runtime_alloc(
            list->runtime,
            next_capacity * sizeof(*list->items),
            &memory,
            error
        );
        if (status != BASL_STATUS_OK) {
            return status;
        }

        list->items = (basl_diagnostic_t *)memory;
        list->capacity = next_capacity;
        return BASL_STATUS_OK;
    }

    memory = list->items;
    status = basl_runtime_realloc(
        list->runtime,
        &memory,
        next_capacity * sizeof(*list->items),
        error
    );
    if (status != BASL_STATUS_OK) {
        return status;
    }

    list->items = (basl_diagnostic_t *)memory;
    memset(
        list->items + old_capacity,
        0,
        (next_capacity - old_capacity) * sizeof(*list->items)
    );
    list->capacity = next_capacity;
    return BASL_STATUS_OK;
}

void basl_diagnostic_list_init(
    basl_diagnostic_list_t *list,
    basl_runtime_t *runtime
) {
    if (list == NULL) {
        return;
    }

    memset(list, 0, sizeof(*list));
    list->runtime = runtime;
}

void basl_diagnostic_list_clear(basl_diagnostic_list_t *list) {
    size_t i;

    if (list == NULL) {
        return;
    }

    for (i = 0U; i < list->count; ++i) {
        basl_string_free(&list->items[i].message);
        memset(&list->items[i], 0, sizeof(list->items[i]));
    }

    list->count = 0U;
}

void basl_diagnostic_list_free(basl_diagnostic_list_t *list) {
    void *memory;

    if (list == NULL) {
        return;
    }

    basl_diagnostic_list_clear(list);

    memory = list->items;
    if (list->runtime != NULL) {
        basl_runtime_free(list->runtime, &memory);
    }

    memset(list, 0, sizeof(*list));
}

size_t basl_diagnostic_list_count(const basl_diagnostic_list_t *list) {
    if (list == NULL) {
        return 0U;
    }

    return list->count;
}

const basl_diagnostic_t *basl_diagnostic_list_get(
    const basl_diagnostic_list_t *list,
    size_t index
) {
    if (list == NULL || index >= list->count) {
        return NULL;
    }

    return &list->items[index];
}

basl_status_t basl_diagnostic_list_append(
    basl_diagnostic_list_t *list,
    basl_diagnostic_severity_t severity,
    basl_source_span_t span,
    const char *message,
    size_t message_length,
    basl_error_t *error
) {
    basl_status_t status;
    basl_diagnostic_t *diagnostic;

    status = basl_diagnostic_list_validate_mutable(list, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (message == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "diagnostic message must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (list->count == SIZE_MAX) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "diagnostic list capacity overflow"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    status = basl_diagnostic_list_grow(list, list->count + 1U, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    diagnostic = &list->items[list->count];
    basl_string_init(&diagnostic->message, list->runtime);
    status = basl_string_assign(&diagnostic->message, message, message_length, error);
    if (status != BASL_STATUS_OK) {
        basl_string_free(&diagnostic->message);
        return status;
    }

    diagnostic->severity = severity;
    diagnostic->span = span;
    list->count += 1U;
    return BASL_STATUS_OK;
}

basl_status_t basl_diagnostic_list_append_cstr(
    basl_diagnostic_list_t *list,
    basl_diagnostic_severity_t severity,
    basl_source_span_t span,
    const char *message,
    basl_error_t *error
) {
    if (message == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "diagnostic message must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return basl_diagnostic_list_append(
        list,
        severity,
        span,
        message,
        strlen(message),
        error
    );
}
