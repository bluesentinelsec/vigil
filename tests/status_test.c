#include "vigil_test.h"

#include <string.h>

#include "vigil/vigil.h"

static void ExpectClearedLocation(int *vigil_test_failed_, const vigil_source_location_t *location)
{
    EXPECT_EQ(location->source_id, 0U);
    EXPECT_EQ(location->offset, 0U);
    EXPECT_EQ(location->line, 0U);
    EXPECT_EQ(location->column, 0U);
}

TEST(VigilStatusTest, StatusNamesAreStable)
{
    EXPECT_STREQ(vigil_status_name(VIGIL_STATUS_OK), "ok");
    EXPECT_STREQ(vigil_status_name(VIGIL_STATUS_INVALID_ARGUMENT), "invalid_argument");
    EXPECT_STREQ(vigil_status_name(VIGIL_STATUS_OUT_OF_MEMORY), "out_of_memory");
    EXPECT_STREQ(vigil_status_name(VIGIL_STATUS_INTERNAL), "internal");
    EXPECT_STREQ(vigil_status_name(VIGIL_STATUS_UNSUPPORTED), "unsupported");
    EXPECT_STREQ(vigil_status_name(VIGIL_STATUS_SYNTAX_ERROR), "syntax_error");
}

TEST(VigilStatusTest, ErrorClearResetsSourceLocation)
{
    vigil_error_t error = {0};

    error.type = VIGIL_STATUS_INTERNAL;
    error.value = "bad";
    error.length = 3U;
    error.location.source_id = 7U;
    error.location.offset = 17U;
    error.location.line = 11U;
    error.location.column = 13U;

    vigil_error_clear(&error);

    EXPECT_EQ(error.type, VIGIL_STATUS_OK);
    EXPECT_EQ(error.value, NULL);
    EXPECT_EQ(error.length, 0U);
    ExpectClearedLocation(vigil_test_failed_, &error.location);
}

TEST(VigilStatusTest, SourceLocationClearResetsFields)
{
    vigil_source_location_t location = {0};

    location.source_id = 3U;
    location.offset = 21U;
    location.line = 5U;
    location.column = 8U;

    vigil_source_location_clear(&location);

    ExpectClearedLocation(vigil_test_failed_, &location);
}

TEST(VigilStatusTest, ErrorLengthMatchesMessage)
{
    vigil_error_t error = {0};

    error.type = VIGIL_STATUS_INVALID_ARGUMENT;
    error.value = "example";
    error.length = strlen(error.value);

    EXPECT_EQ(error.length, 7U);
}

TEST(VigilStatusTest, ErrorMessageFallsBackToKnownString)
{
    vigil_error_t error = {0};

    EXPECT_STREQ(vigil_error_message(NULL), "unknown error");
    EXPECT_STREQ(vigil_error_message(&error), "unknown error");

    error.value = "specific";
    EXPECT_STREQ(vigil_error_message(&error), "specific");
}

void register_status_tests(void)
{
    REGISTER_TEST(VigilStatusTest, StatusNamesAreStable);
    REGISTER_TEST(VigilStatusTest, ErrorClearResetsSourceLocation);
    REGISTER_TEST(VigilStatusTest, SourceLocationClearResetsFields);
    REGISTER_TEST(VigilStatusTest, ErrorLengthMatchesMessage);
    REGISTER_TEST(VigilStatusTest, ErrorMessageFallsBackToKnownString);
}
