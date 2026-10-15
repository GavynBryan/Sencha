// The frame's ordering and coverage contract, independent of any sense.

#include <perception/PerceptionFrame.h>

#include <gtest/gtest.h>

namespace
{
    const GameplayTagId kSight{ 1 };
    const GameplayTagId kHearing{ 2 };
    const EntityId kLow{ 3, 1 };
    const EntityId kHigh{ 7, 1 };
    const EntityId kSubject{ 9, 1 };
}

TEST(PerceptionFrame, ScansAreOrderedByObserverThenSenseWhateverOrderPassesRan)
{
    PerceptionFrame frame;
    frame.Begin(4);
    frame.BeginScan(kHigh, kSight);
    frame.EndScan(ScanCoverage::Complete);
    frame.BeginScan(kLow, kHearing);
    frame.Add(kSubject, Vec3d(1, 2, 3), 0.5f);
    frame.EndScan(ScanCoverage::Complete);
    frame.BeginScan(kLow, kSight);
    frame.EndScan(ScanCoverage::Partial);
    frame.Publish();

    ASSERT_EQ(frame.Scans().size(), 3u);
    EXPECT_EQ(frame.Scans()[0].Observer, kLow);
    EXPECT_EQ(frame.Scans()[0].Sense, kSight);
    EXPECT_EQ(frame.Scans()[1].Sense, kHearing);
    EXPECT_EQ(frame.Scans()[2].Observer, kHigh);

    EXPECT_EQ(frame.Find(kLow, kSight, kSubject), nullptr);
    const Observation* heard = frame.Find(kLow, kHearing, kSubject);
    ASSERT_NE(heard, nullptr);
    EXPECT_EQ(heard->Sense, kHearing);
    EXPECT_EQ(heard->Tick, 4u);
    EXPECT_FLOAT_EQ(heard->Confidence, 0.5f);
}

TEST(PerceptionFrame, CoverageIsCompleteOnlyWhenEveryScanFinished)
{
    PerceptionFrame frame;
    frame.Begin(1);
    frame.BeginScan(kLow, kSight);
    frame.EndScan(ScanCoverage::Partial);
    frame.BeginScan(kLow, kHearing);
    frame.EndScan(ScanCoverage::Complete);
    frame.BeginScan(kHigh, kSight);
    frame.EndScan(ScanCoverage::Complete);
    frame.Publish();

    EXPECT_EQ(frame.CoverageOf(kLow), ScanCoverage::Partial);
    EXPECT_EQ(frame.CoverageOf(kLow, kHearing), ScanCoverage::Complete);
    EXPECT_EQ(frame.CoverageOf(kHigh), ScanCoverage::Complete);
    EXPECT_EQ(frame.CoverageOf(kSubject), ScanCoverage::NotEvaluated);
}

TEST(PerceptionFrame, BeginForgetsTheLastTick)
{
    PerceptionFrame frame;
    frame.Begin(1);
    frame.BeginScan(kLow, kSight);
    frame.Add(kSubject, Vec3d::Zero(), 1.0f);
    frame.EndScan(ScanCoverage::Complete);
    frame.Publish();

    frame.Begin(2);
    frame.Publish();
    EXPECT_TRUE(frame.Scans().empty());
    EXPECT_EQ(frame.Find(kLow, kSight, kSubject), nullptr);
}

TEST(PerceptionFrame, ConfidenceIsClampedToTheUnitInterval)
{
    PerceptionFrame frame;
    frame.Begin(1);
    frame.BeginScan(kLow, kSight);
    frame.Add(kSubject, Vec3d::Zero(), 3.0f);
    frame.EndScan(ScanCoverage::Complete);
    frame.Publish();
    EXPECT_FLOAT_EQ(frame.Find(kLow, kSight, kSubject)->Confidence, 1.0f);
}
