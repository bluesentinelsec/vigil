#include <string.h>

#include "internal/basl_internal.h"

static basl_allocator_t basl_resolve_allocator(const basl_runtime_options_t *options) {
    basl_allocator_t allocator;

    allocator = basl_default_allocator();
    if (options != NULL && options->allocator != NULL) {
        allocator = *options->allocator;
    }

    return allocator;
}

static basl_logger_t basl_resolve_logger(const basl_logger_t *logger) {
    basl_logger_t resolved_logger;

    basl_logger_init(&resolved_logger);
    if (logger != NULL) {
        resolved_logger = *logger;
        if (resolved_logger.handler == NULL) {
            resolved_logger.handler = basl_stderr_log_handler;
        }
    }

    return resolved_logger;
}

static int basl_logger_level_is_valid(basl_log_level_t level) {
    return level >= BASL_LOG_DEBUG && level <= BASL_LOG_FATAL;
}

void basl_runtime_options_init(basl_runtime_options_t *options) {
    if (options == NULL) {
        return;
    }

    memset(options, 0, sizeof(*options));
}

basl_status_t basl_runtime_open(
    basl_runtime_t **out_runtime,
    const basl_runtime_options_t *options,
    basl_error_t *error
) {
    basl_allocator_t allocator;
    basl_runtime_t *runtime;
    basl_status_t status;

    basl_error_clear(error);

    if (out_runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "out_runtime must not be null"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    *out_runtime = NULL;

    allocator = basl_resolve_allocator(options);

    if (!basl_allocator_is_valid(&allocator)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "allocator must define allocate and deallocate"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    runtime = (basl_runtime_t *)allocator.allocate(allocator.user_data, sizeof(*runtime));
    if (runtime == NULL) {
        basl_error_set_literal(
            error,
            BASL_STATUS_OUT_OF_MEMORY,
            "failed to allocate runtime"
        );
        return BASL_STATUS_OUT_OF_MEMORY;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->allocator = allocator;
    status = basl_runtime_set_logger(
        runtime,
        options == NULL ? NULL : options->logger,
        error
    );
    if (status != BASL_STATUS_OK) {
        allocator.deallocate(allocator.user_data, runtime);
        return status;
    }
    *out_runtime = runtime;
    return BASL_STATUS_OK;
}

void basl_runtime_close(basl_runtime_t **runtime) {
    basl_allocator_t allocator;
    basl_runtime_t *resolved_runtime;

    if (runtime == NULL || *runtime == NULL) {
        return;
    }

    resolved_runtime = *runtime;
    allocator = resolved_runtime->allocator;
    allocator.deallocate(allocator.user_data, resolved_runtime);
    *runtime = NULL;
}

const basl_allocator_t *basl_runtime_allocator(const basl_runtime_t *runtime) {
    if (runtime == NULL) {
        return NULL;
    }

    return &runtime->allocator;
}

const basl_logger_t *basl_runtime_logger(const basl_runtime_t *runtime) {
    if (runtime == NULL) {
        return NULL;
    }

    return &runtime->logger;
}

basl_status_t basl_runtime_set_logger(
    basl_runtime_t *runtime,
    const basl_logger_t *logger,
    basl_error_t *error
) {
    basl_logger_t resolved_logger;

    basl_error_clear(error);

    if (runtime == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "runtime must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    resolved_logger = basl_resolve_logger(logger);
    if (!basl_logger_level_is_valid(resolved_logger.minimum_level)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "logger minimum_level is invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    runtime->logger = resolved_logger;
    return BASL_STATUS_OK;
}
