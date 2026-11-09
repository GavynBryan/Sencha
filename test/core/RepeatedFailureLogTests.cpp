#include <gtest/gtest.h>

#include <core/logging/RepeatedFailureLog.h>

TEST(RepeatedFailureLog, OnlyTheFirstFailureOfAnEpisodeAsksToBeLogged)
{
    RepeatedFailureLog log;

    EXPECT_TRUE(log.Record());
    EXPECT_FALSE(log.Record());
    EXPECT_FALSE(log.EndPeriod().has_value());
    EXPECT_FALSE(log.Record());
}

TEST(RepeatedFailureLog, AnEpisodeEndsAfterACleanPeriodAndReportsItsSize)
{
    RepeatedFailureLog log;

    (void)log.Record();
    (void)log.Record();
    EXPECT_FALSE(log.EndPeriod().has_value());
    (void)log.Record();
    EXPECT_FALSE(log.EndPeriod().has_value());

    const auto ended = log.EndPeriod();
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->Failures, 3u);
    EXPECT_EQ(ended->Periods, 2u);
}

TEST(RepeatedFailureLog, AFailureAfterAnEpisodeEndedOpensANewOne)
{
    RepeatedFailureLog log;

    (void)log.Record();
    (void)log.EndPeriod();
    (void)log.EndPeriod();

    EXPECT_TRUE(log.Record());
    (void)log.EndPeriod();
    const auto ended = log.EndPeriod();
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(ended->Failures, 1u);
    EXPECT_EQ(ended->Periods, 1u);
}

TEST(RepeatedFailureLog, PeriodsWithoutFailuresReportNothing)
{
    RepeatedFailureLog log;

    EXPECT_FALSE(log.EndPeriod().has_value());
    EXPECT_FALSE(log.EndPeriod().has_value());
}
