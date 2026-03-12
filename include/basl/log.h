#ifndef BASL_LOG_H
#define BASL_LOG_H

#include <stddef.h>

#include "basl/export.h"
#include "basl/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum basl_log_level {
    BASL_LOG_DEBUG = 0,
    BASL_LOG_INFO = 1,
    BASL_LOG_WARNING = 2,
    BASL_LOG_ERROR = 3,
    BASL_LOG_FATAL = 4
} basl_log_level_t;

typedef struct basl_log_field {
    const char *key;
    const char *value;
} basl_log_field_t;

typedef struct basl_log_record {
    basl_log_level_t level;
    const char *message;
    size_t message_length;
    const basl_log_field_t *fields;
    size_t field_count;
} basl_log_record_t;

typedef void (*basl_log_handler_fn)(void *user_data, const basl_log_record_t *record);

typedef struct basl_logger {
    basl_log_level_t minimum_level;
    basl_log_handler_fn handler;
    void *user_data;
} basl_logger_t;

BASL_API void basl_logger_init(basl_logger_t *logger);
BASL_API const char *basl_log_level_name(basl_log_level_t level);
BASL_API int basl_logger_should_log(
    const basl_logger_t *logger,
    basl_log_level_t level
);
BASL_API void basl_stderr_log_handler(
    void *user_data,
    const basl_log_record_t *record
);
BASL_API basl_status_t basl_logger_log(
    const basl_logger_t *logger,
    basl_log_level_t level,
    const char *message,
    const basl_log_field_t *fields,
    size_t field_count,
    basl_error_t *error
);
BASL_API basl_status_t basl_logger_debug(
    const basl_logger_t *logger,
    const char *message,
    basl_error_t *error
);
BASL_API basl_status_t basl_logger_info(
    const basl_logger_t *logger,
    const char *message,
    basl_error_t *error
);
BASL_API basl_status_t basl_logger_warning(
    const basl_logger_t *logger,
    const char *message,
    basl_error_t *error
);
BASL_API basl_status_t basl_logger_error(
    const basl_logger_t *logger,
    const char *message,
    basl_error_t *error
);
BASL_API void basl_logger_fatal(
    const basl_logger_t *logger,
    const char *message,
    const basl_log_field_t *fields,
    size_t field_count
);

#ifdef __cplusplus
}
#endif

#endif
