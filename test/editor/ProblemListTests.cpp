#include "notices/NoticeQueue.h"
#include "problems/ProblemList.h"

#include <gtest/gtest.h>

TEST(ProblemList, ASourceRepublishingReplacesWhatItSaidBefore)
{
    // A fixed problem is gone rather than waiting for someone to dismiss it.
    ProblemList problems;
    int heard = 0;
    auto token = problems.Subscribe([&] { ++heard; });
    problems.Publish("cook", { Problem{ ProblemSeverity::Error, "missing texture" },
                               Problem{ ProblemSeverity::Warning, "unused material" } });
    problems.Publish("ui", { Problem{ ProblemSeverity::Warning, "binding missing" } });
    EXPECT_EQ(problems.All().size(), 3u);
    EXPECT_EQ(problems.Count(ProblemSeverity::Error), 1u);
    EXPECT_EQ(problems.Count(ProblemSeverity::Warning), 3u);

    problems.Publish("cook", { Problem{ ProblemSeverity::Warning, "unused material" } });
    EXPECT_EQ(problems.Count(ProblemSeverity::Error), 0u);
    EXPECT_EQ(heard, 3);

    problems.Publish("cook", { Problem{ ProblemSeverity::Warning, "unused material" } });
    EXPECT_EQ(heard, 3) << "republishing the same set is no change";

    problems.Clear("cook");
    ASSERT_EQ(problems.All().size(), 1u);
    EXPECT_EQ(problems.All().front()->Message, "binding missing");
}

TEST(NoticeQueue, ANoticeShowsForAMomentAndOnlyAFewShowAtOnce)
{
    NoticeQueue notices;
    notices.Post("Saved", NoticeTone::Done, 0.0);
    EXPECT_EQ(notices.Showing(1.0).size(), 1u);
    EXPECT_TRUE(notices.Showing(NoticeQueue::kLifetimeSeconds).empty());

    for (int i = 0; i < 5; ++i)
        notices.Post("Cooked", NoticeTone::Done, 10.0);
    notices.Post("Undo: Move 3 brushes (Level)", NoticeTone::Info, 10.5);
    const auto showing = notices.Showing(11.0);
    ASSERT_EQ(showing.size(), NoticeQueue::kVisible);
    EXPECT_EQ(showing.back().Text, "Undo: Move 3 brushes (Level)") << "the newest is always shown";
}
