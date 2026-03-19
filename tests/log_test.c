#include "vigil_test.h"

#include <stdio.h>
#include <string.h>

#include "vigil/vigil.h"

struct CaptureState
{
    int call_count;
    vigil_log_level_t level;
    char message[128];
    size_t field_count;
    char field_key[64];
    char field_value[128];
};

void CaptureHandler(void *user_data, const vigil_log_record_t *record)
{
    struct CaptureState *state = (struct CaptureState *)(user_data);

    state->call_count += 1;
    state->level = record->level;
    snprintf(state->message, sizeof(state->message), "%s", record->message);
    state->field_count = record->field_count;
    if (record->field_count != 0U)
    {
        snprintf(state->field_key, sizeof(state->field_key), "%s", record->fields[0].key);
        snprintf(state->field_value, sizeof(state->field_value), "%s", record->fields[0].value);
    }
}

TEST(VigilLogTest, LoggerInitSetsDefaultConfiguration)
{
    vigil_logger_t logger = {0};

    vigil_logger_init(&logger);

    EXPECT_EQ(logger.minimum_level, VIGIL_LOG_INFO);
    EXPECT_NE(logger.handler, NULL);
    EXPECT_EQ(logger.user_data, NULL);
}

TEST(VigilLogTest, LoggerPassesStructuredRecordsToCustomHandler)
{
    struct CaptureState state = {0};
    vigil_logger_t logger = {0};
    vigil_log_field_t field = {"path", "/tmp/example.vigil"};
    vigil_error_t error = {0};

    vigil_logger_init(&logger);
    logger.minimum_level = VIGIL_LOG_DEBUG;
    logger.handler = CaptureHandler;
    logger.user_data = &state;

    EXPECT_EQ(vigil_logger_log(&logger, VIGIL_LOG_INFO, "compiled", &field, 1U, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(state.call_count, 1);
    EXPECT_EQ(state.level, VIGIL_LOG_INFO);
    EXPECT_STREQ(state.message, "compiled");
    EXPECT_EQ(state.field_count, (size_t)(1));
    EXPECT_STREQ(state.field_key, "path");
    EXPECT_STREQ(state.field_value, "/tmp/example.vigil");
}

TEST(VigilLogTest, LoggerFiltersMessagesBelowMinimumLevel)
{
    struct CaptureState state = {0};
    vigil_logger_t logger = {0};
    vigil_error_t error = {0};

    vigil_logger_init(&logger);
    logger.minimum_level = VIGIL_LOG_WARNING;
    logger.handler = CaptureHandler;
    logger.user_data = &state;

    EXPECT_EQ(vigil_logger_info(&logger, "skip me", &error), VIGIL_STATUS_OK);
    EXPECT_EQ(state.call_count, 0);

    EXPECT_EQ(vigil_logger_error(&logger, "keep me", &error), VIGIL_STATUS_OK);
    EXPECT_EQ(state.call_count, 1);
    EXPECT_EQ(state.level, VIGIL_LOG_ERROR);
    EXPECT_STREQ(state.message, "keep me");
}

TEST(VigilLogTest, RuntimeUsesConfiguredLogger)
{
    struct CaptureState state = {0};
    vigil_logger_t logger = {0};
    vigil_runtime_options_t options = {0};
    vigil_runtime_t *runtime = NULL;
    vigil_error_t error = {0};

    vigil_logger_init(&logger);
    logger.minimum_level = VIGIL_LOG_DEBUG;
    logger.handler = CaptureHandler;
    logger.user_data = &state;
    vigil_runtime_options_init(&options);
    options.logger = &logger;

    ASSERT_EQ(vigil_runtime_open(&runtime, &options, &error), VIGIL_STATUS_OK);
    ASSERT_NE(runtime, NULL);
    ASSERT_NE(vigil_runtime_logger(runtime), NULL);

    EXPECT_EQ(vigil_logger_warning(vigil_runtime_logger(runtime), "runtime logger", &error), VIGIL_STATUS_OK);
    EXPECT_EQ(state.call_count, 1);
    EXPECT_EQ(state.level, VIGIL_LOG_WARNING);
    EXPECT_STREQ(state.message, "runtime logger");

    vigil_runtime_close(&runtime);
}

TEST(VigilLogTest, RuntimeSetLoggerRejectsInvalidLevel)
{
    vigil_runtime_t *runtime = NULL;
    vigil_logger_t logger = {0};
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    vigil_logger_init(&logger);
    logger.minimum_level = (vigil_log_level_t)(999);

    EXPECT_EQ(vigil_runtime_set_logger(runtime, &logger, &error), VIGIL_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(error.type, VIGIL_STATUS_INVALID_ARGUMENT);
    ASSERT_NE(error.value, NULL);
    EXPECT_EQ(strcmp(error.value, "logger minimum_level is invalid"), 0);

    vigil_runtime_close(&runtime);
}

void register_log_tests(void)
{
    REGISTER_TEST(VigilLogTest, LoggerInitSetsDefaultConfiguration);
    REGISTER_TEST(VigilLogTest, LoggerPassesStructuredRecordsToCustomHandler);
    REGISTER_TEST(VigilLogTest, LoggerFiltersMessagesBelowMinimumLevel);
    REGISTER_TEST(VigilLogTest, RuntimeUsesConfiguredLogger);
    REGISTER_TEST(VigilLogTest, RuntimeSetLoggerRejectsInvalidLevel);
}
