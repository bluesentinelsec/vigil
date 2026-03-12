#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/basl_internal.h"
#include "basl/log.h"

static const basl_logger_t BASL_DEFAULT_LOGGER = {
    BASL_LOG_INFO,
    basl_stderr_log_handler,
    NULL
};

static const basl_logger_t *basl_resolve_logger(const basl_logger_t *logger) {
    if (logger == NULL) {
        return &BASL_DEFAULT_LOGGER;
    }

    return logger;
}

static int basl_log_level_is_valid(basl_log_level_t level) {
    return level >= BASL_LOG_DEBUG && level <= BASL_LOG_FATAL;
}

static basl_status_t basl_logger_validate(
    const basl_logger_t *logger,
    basl_error_t *error
) {
    if (!basl_log_level_is_valid(logger->minimum_level)) {
        basl_error_set_literal(
            error,
            BASL_STATUS_INVALID_ARGUMENT,
            "logger minimum_level is invalid"
        );
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    return BASL_STATUS_OK;
}

static basl_log_handler_fn basl_resolve_handler(const basl_logger_t *logger) {
    if (logger->handler != NULL) {
        return logger->handler;
    }

    return basl_stderr_log_handler;
}

static void basl_write_quoted(FILE *stream, const char *value) {
    size_t index;

    fputc('"', stream);
    if (value != NULL) {
        for (index = 0U; value[index] != '\0'; index += 1U) {
            char ch;

            ch = value[index];
            switch (ch) {
                case '\\':
                case '"':
                    fputc('\\', stream);
                    fputc(ch, stream);
                    break;
                case '\n':
                    fputs("\\n", stream);
                    break;
                case '\r':
                    fputs("\\r", stream);
                    break;
                case '\t':
                    fputs("\\t", stream);
                    break;
                default:
                    fputc(ch, stream);
                    break;
            }
        }
    }
    fputc('"', stream);
}

void basl_logger_init(basl_logger_t *logger) {
    if (logger == NULL) {
        return;
    }

    *logger = BASL_DEFAULT_LOGGER;
}

const char *basl_log_level_name(basl_log_level_t level) {
    switch (level) {
        case BASL_LOG_DEBUG:
            return "debug";
        case BASL_LOG_INFO:
            return "info";
        case BASL_LOG_WARNING:
            return "warning";
        case BASL_LOG_ERROR:
            return "error";
        case BASL_LOG_FATAL:
            return "fatal";
        default:
            return "unknown";
    }
}

int basl_logger_should_log(const basl_logger_t *logger, basl_log_level_t level) {
    const basl_logger_t *resolved_logger;

    if (!basl_log_level_is_valid(level)) {
        return 0;
    }

    resolved_logger = basl_resolve_logger(logger);
    if (level == BASL_LOG_FATAL) {
        return 1;
    }

    return level >= resolved_logger->minimum_level;
}

void basl_stderr_log_handler(void *user_data, const basl_log_record_t *record) {
    size_t index;

    (void)user_data;

    if (record == NULL || record->message == NULL) {
        return;
    }

    fprintf(stderr, "level=%s msg=", basl_log_level_name(record->level));
    basl_write_quoted(stderr, record->message);

    for (index = 0U; index < record->field_count; index += 1U) {
        const basl_log_field_t *field;

        field = &record->fields[index];
        if (field->key == NULL || field->value == NULL) {
            continue;
        }

        fprintf(stderr, " %s=", field->key);
        basl_write_quoted(stderr, field->value);
    }

    fputc('\n', stderr);
    fflush(stderr);
}

basl_status_t basl_logger_log(
    const basl_logger_t *logger,
    basl_log_level_t level,
    const char *message,
    const basl_log_field_t *fields,
    size_t field_count,
    basl_error_t *error
) {
    const basl_logger_t *resolved_logger;
    basl_log_handler_fn handler;
    basl_log_record_t record;
    size_t index;
    basl_status_t status;

    basl_error_clear(error);

    if (!basl_log_level_is_valid(level)) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "log level is invalid");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (message == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "log message must not be null");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    if (field_count != 0U && fields == NULL) {
        basl_error_set_literal(error, BASL_STATUS_INVALID_ARGUMENT, "log fields must not be null when field_count is non-zero");
        return BASL_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < field_count; index += 1U) {
        if (fields[index].key == NULL || fields[index].value == NULL) {
            basl_error_set_literal(
                error,
                BASL_STATUS_INVALID_ARGUMENT,
                "log fields must define key and value"
            );
            return BASL_STATUS_INVALID_ARGUMENT;
        }
    }

    resolved_logger = basl_resolve_logger(logger);
    status = basl_logger_validate(resolved_logger, error);
    if (status != BASL_STATUS_OK) {
        return status;
    }

    if (!basl_logger_should_log(resolved_logger, level)) {
        return BASL_STATUS_OK;
    }

    handler = basl_resolve_handler(resolved_logger);
    record.level = level;
    record.message = message;
    record.message_length = strlen(message);
    record.fields = fields;
    record.field_count = field_count;
    handler(resolved_logger->user_data, &record);

    return BASL_STATUS_OK;
}

basl_status_t basl_logger_debug(
    const basl_logger_t *logger,
    const char *message,
    basl_error_t *error
) {
    return basl_logger_log(logger, BASL_LOG_DEBUG, message, NULL, 0U, error);
}

basl_status_t basl_logger_info(
    const basl_logger_t *logger,
    const char *message,
    basl_error_t *error
) {
    return basl_logger_log(logger, BASL_LOG_INFO, message, NULL, 0U, error);
}

basl_status_t basl_logger_warning(
    const basl_logger_t *logger,
    const char *message,
    basl_error_t *error
) {
    return basl_logger_log(logger, BASL_LOG_WARNING, message, NULL, 0U, error);
}

basl_status_t basl_logger_error(
    const basl_logger_t *logger,
    const char *message,
    basl_error_t *error
) {
    return basl_logger_log(logger, BASL_LOG_ERROR, message, NULL, 0U, error);
}

void basl_logger_fatal(
    const basl_logger_t *logger,
    const char *message,
    const basl_log_field_t *fields,
    size_t field_count
) {
    basl_error_t error;
    basl_log_record_t record;

    memset(&error, 0, sizeof(error));
    if (
        basl_logger_log(logger, BASL_LOG_FATAL, message, fields, field_count, &error) !=
        BASL_STATUS_OK
    ) {
        record.level = BASL_LOG_FATAL;
        record.message =
            basl_error_message(&error) != NULL ? basl_error_message(&error) : "fatal log failure";
        record.message_length = strlen(record.message);
        record.fields = NULL;
        record.field_count = 0U;
        basl_stderr_log_handler(NULL, &record);
    }

    abort();
}
