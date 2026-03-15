#include "basl_test.h"

#include <stdio.h>
#include <string.h>


#include "basl/basl.h"

struct CaptureState {
    int call_count;
    basl_log_level_t level;
    char message[128];
    size_t field_count;
    char field_key[64];
    char field_value[128];
};

void CaptureHandler(void *user_data, const basl_log_record_t *record) {
    struct CaptureState *state = (struct CaptureState *)(user_data);

    state->call_count += 1;
    state->level = record->level;
    snprintf(state->message, sizeof(state->message), "%s", record->message);
    state->field_count = record->field_count;
    if (record->field_count != 0U) {
        snprintf(state->field_key, sizeof(state->field_key), "%s", record->fields[0].key);
        snprintf(
            state->field_value,
            sizeof(state->field_value),
            "%s",
            record->fields[0].value
        );
    }
}


TEST(BaslLogTest, LoggerInitSetsDefaultConfiguration) {
    basl_logger_t logger = {0};

    basl_logger_init(&logger);

    EXPECT_EQ(logger.minimum_level, BASL_LOG_INFO);
    EXPECT_NE(logger.handler, NULL);
    EXPECT_EQ(logger.user_data, NULL);
}

TEST(BaslLogTest, LoggerPassesStructuredRecordsToCustomHandler) {
    struct CaptureState state = {0};
    basl_logger_t logger = {0};
    basl_log_field_t field = {"path", "/tmp/example.basl"};
    basl_error_t error = {0};

    basl_logger_init(&logger);
    logger.minimum_level = BASL_LOG_DEBUG;
    logger.handler = CaptureHandler;
    logger.user_data = &state;

    EXPECT_EQ(
        basl_logger_log(&logger, BASL_LOG_INFO, "compiled", &field, 1U, &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(state.call_count, 1);
    EXPECT_EQ(state.level, BASL_LOG_INFO);
    EXPECT_STREQ(state.message, "compiled");
    EXPECT_EQ(state.field_count, (size_t)(1));
    EXPECT_STREQ(state.field_key, "path");
    EXPECT_STREQ(state.field_value, "/tmp/example.basl");
}

TEST(BaslLogTest, LoggerFiltersMessagesBelowMinimumLevel) {
    struct CaptureState state = {0};
    basl_logger_t logger = {0};
    basl_error_t error = {0};

    basl_logger_init(&logger);
    logger.minimum_level = BASL_LOG_WARNING;
    logger.handler = CaptureHandler;
    logger.user_data = &state;

    EXPECT_EQ(basl_logger_info(&logger, "skip me", &error), BASL_STATUS_OK);
    EXPECT_EQ(state.call_count, 0);

    EXPECT_EQ(basl_logger_error(&logger, "keep me", &error), BASL_STATUS_OK);
    EXPECT_EQ(state.call_count, 1);
    EXPECT_EQ(state.level, BASL_LOG_ERROR);
    EXPECT_STREQ(state.message, "keep me");
}

TEST(BaslLogTest, RuntimeUsesConfiguredLogger) {
    struct CaptureState state = {0};
    basl_logger_t logger = {0};
    basl_runtime_options_t options = {0};
    basl_runtime_t *runtime = NULL;
    basl_error_t error = {0};

    basl_logger_init(&logger);
    logger.minimum_level = BASL_LOG_DEBUG;
    logger.handler = CaptureHandler;
    logger.user_data = &state;
    basl_runtime_options_init(&options);
    options.logger = &logger;

    ASSERT_EQ(basl_runtime_open(&runtime, &options, &error), BASL_STATUS_OK);
    ASSERT_NE(runtime, NULL);
    ASSERT_NE(basl_runtime_logger(runtime), NULL);

    EXPECT_EQ(
        basl_logger_warning(basl_runtime_logger(runtime), "runtime logger", &error),
        BASL_STATUS_OK
    );
    EXPECT_EQ(state.call_count, 1);
    EXPECT_EQ(state.level, BASL_LOG_WARNING);
    EXPECT_STREQ(state.message, "runtime logger");

    basl_runtime_close(&runtime);
}

TEST(BaslLogTest, RuntimeSetLoggerRejectsInvalidLevel) {
    basl_runtime_t *runtime = NULL;
    basl_logger_t logger = {0};
    basl_error_t error = {0};

    ASSERT_EQ(basl_runtime_open(&runtime, NULL, &error), BASL_STATUS_OK);
    basl_logger_init(&logger);
    logger.minimum_level = (basl_log_level_t)(999);

    EXPECT_EQ(
        basl_runtime_set_logger(runtime, &logger, &error),
        BASL_STATUS_INVALID_ARGUMENT
    );
    EXPECT_EQ(error.type, BASL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "logger minimum_level is invalid"), 0);

    basl_runtime_close(&runtime);
}

void register_log_tests(void) {
    REGISTER_TEST(BaslLogTest, LoggerInitSetsDefaultConfiguration);
    REGISTER_TEST(BaslLogTest, LoggerPassesStructuredRecordsToCustomHandler);
    REGISTER_TEST(BaslLogTest, LoggerFiltersMessagesBelowMinimumLevel);
    REGISTER_TEST(BaslLogTest, RuntimeUsesConfiguredLogger);
    REGISTER_TEST(BaslLogTest, RuntimeSetLoggerRejectsInvalidLevel);
}
