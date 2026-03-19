#include <string.h>

#include "internal/vigil_internal.h"

static vigil_allocator_t vigil_resolve_allocator(const vigil_runtime_options_t *options)
{
    vigil_allocator_t allocator;

    allocator = vigil_default_allocator();
    if (options != NULL && options->allocator != NULL)
    {
        allocator = *options->allocator;
    }

    return allocator;
}

static vigil_logger_t vigil_resolve_logger(const vigil_logger_t *logger)
{
    vigil_logger_t resolved_logger;

    vigil_logger_init(&resolved_logger);
    if (logger != NULL)
    {
        resolved_logger = *logger;
        if (resolved_logger.handler == NULL)
        {
            resolved_logger.handler = vigil_stderr_log_handler;
        }
    }

    return resolved_logger;
}

static int vigil_logger_level_is_valid(vigil_log_level_t level)
{
    return level >= VIGIL_LOG_DEBUG && level <= VIGIL_LOG_FATAL;
}

void vigil_runtime_options_init(vigil_runtime_options_t *options)
{
    if (options == NULL)
    {
        return;
    }

    memset(options, 0, sizeof(*options));
}

vigil_status_t vigil_runtime_open(vigil_runtime_t **out_runtime, const vigil_runtime_options_t *options,
                                  vigil_error_t *error)
{
    vigil_allocator_t allocator;
    vigil_runtime_t *runtime;
    vigil_status_t status;

    vigil_error_clear(error);

    if (out_runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    *out_runtime = NULL;

    allocator = vigil_resolve_allocator(options);

    if (!vigil_allocator_is_valid(&allocator))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "allocator must define allocate and deallocate");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    runtime = (vigil_runtime_t *)allocator.allocate(allocator.user_data, sizeof(*runtime));
    if (runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_OUT_OF_MEMORY, "failed to allocate runtime");
        return VIGIL_STATUS_OUT_OF_MEMORY;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->allocator = allocator;
    status = vigil_runtime_set_logger(runtime, options == NULL ? NULL : options->logger, error);
    if (status != VIGIL_STATUS_OK)
    {
        allocator.deallocate(allocator.user_data, runtime);
        return status;
    }
    *out_runtime = runtime;
    return VIGIL_STATUS_OK;
}

void vigil_runtime_close(vigil_runtime_t **runtime)
{
    vigil_allocator_t allocator;
    vigil_runtime_t *resolved_runtime;

    if (runtime == NULL || *runtime == NULL)
    {
        return;
    }

    resolved_runtime = *runtime;
    allocator = resolved_runtime->allocator;
    allocator.deallocate(allocator.user_data, resolved_runtime);
    *runtime = NULL;
}

const vigil_allocator_t *vigil_runtime_allocator(const vigil_runtime_t *runtime)
{
    if (runtime == NULL)
    {
        return NULL;
    }

    return &runtime->allocator;
}

const vigil_logger_t *vigil_runtime_logger(const vigil_runtime_t *runtime)
{
    if (runtime == NULL)
    {
        return NULL;
    }

    return &runtime->logger;
}

vigil_status_t vigil_runtime_set_logger(vigil_runtime_t *runtime, const vigil_logger_t *logger, vigil_error_t *error)
{
    vigil_logger_t resolved_logger;

    vigil_error_clear(error);

    if (runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "runtime must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    resolved_logger = vigil_resolve_logger(logger);
    if (!vigil_logger_level_is_valid(resolved_logger.minimum_level))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "logger minimum_level is invalid");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    runtime->logger = resolved_logger;
    return VIGIL_STATUS_OK;
}
