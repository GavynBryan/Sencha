#include <gtest/gtest.h>

#include <anim/AnimPlayback.h>

#include <vector>

namespace
{
    constexpr double kTick = 0.1;

    AnimPlayback Clip(AnimTick start, float duration, bool cyclic = false, float offset = 0.0f, float rate = 1.0f)
    {
        return AnimPlayback{ .StartTick = start, .OffsetSeconds = offset, .Rate = rate, .DurationSeconds = duration,
                             .Cyclic = cyclic };
    }

    struct Crossed
    {
        std::size_t Mark;
        bool Skipped;
        friend bool operator==(const Crossed&, const Crossed&) = default;
    };

    // The marks at `times` (seconds) one pass crosses, in order.
    std::vector<Crossed> Cross(const AnimPlayback& playback, AnimTick through, AnimPlaybackEntry entry, AnimTick now,
                               const std::vector<double>& times)
    {
        std::vector<Crossed> out;
        const AnimCrossing crossing = AnimCrossPlayback(playback, through, entry, now, kTick);
        for (std::uint8_t s = 0; s < crossing.Count; ++s)
            ForEachAnimMark(playback, crossing.Stretches[s], times.size(), [&](std::size_t i) { return times[i]; },
                            [&](std::size_t i) { out.push_back({ i, crossing.Stretches[s].Skipped }); });
        return out;
    }
}

TEST(AnimCrossing, TimeAtATickPlacesByKind)
{
    const AnimPlayback once = Clip(10, 0.25f);
    EXPECT_DOUBLE_EQ(AnimPlaybackSeconds(once, 5, kTick), 0.0);
    EXPECT_NEAR(AnimPlaybackSeconds(once, 12, kTick), 0.2, 1e-9);
    EXPECT_DOUBLE_EQ(AnimPlaybackSeconds(once, 20, kTick), 0.25);
    EXPECT_FALSE(AnimPlaybackEnded(once, 12, kTick));
    EXPECT_TRUE(AnimPlaybackEnded(once, 13, kTick));

    const AnimPlayback loop = Clip(10, 0.25f, true);
    EXPECT_NEAR(AnimPlaybackSeconds(loop, 13, kTick), 0.05, 1e-9);
    EXPECT_FALSE(AnimPlaybackEnded(loop, 100, kTick));

    const AnimPlayback backward = Clip(0, 1.0f, false, 1.0f, -1.0f);
    EXPECT_NEAR(AnimPlaybackSeconds(backward, 3, kTick), 0.7, 1e-9);
    EXPECT_TRUE(AnimPlaybackEnded(backward, 10, kTick));
}

TEST(AnimCrossing, APassPlaysTheTickBeforeIt)
{
    const AnimPlayback clip = Clip(0, 1.0f);
    // Marks at 0.15 and 0.2: the pass at tick 2 plays (0.1, 0.2].
    EXPECT_EQ(Cross(clip, 1, AnimPlaybackEntry::Start, 2, { 0.15, 0.2, 0.25 }),
              (std::vector<Crossed>{ { 0, false }, { 1, false } }));
}

TEST(AnimCrossing, ARepeatedOrEarlierTickCrossesNothing)
{
    const AnimPlayback clip = Clip(0, 1.0f, true);
    EXPECT_TRUE(Cross(clip, 5, AnimPlaybackEntry::Start, 5, { 0.0, 0.45, 0.5 }).empty());
    EXPECT_TRUE(Cross(clip, 5, AnimPlaybackEntry::Start, 4, { 0.0, 0.45, 0.5 }).empty());
}

TEST(AnimCrossing, AGapIsSkippedAndOnlyItsLastTickPlays)
{
    const AnimPlayback clip = Clip(0, 1.0f);
    EXPECT_EQ(Cross(clip, 2, AnimPlaybackEntry::Start, 6, { 0.25, 0.55 }),
              (std::vector<Crossed>{ { 0, true }, { 1, false } }));
}

TEST(AnimCrossing, EnteringAtTheStartCrossesTheFirstMark)
{
    EXPECT_EQ(Cross(Clip(4, 1.0f), kAnimNoTick, AnimPlaybackEntry::Start, 4, { 0.0, 0.05 }),
              (std::vector<Crossed>{ { 0, false } }));
    // Issued after this tick's pass, seen by the next: still its own start.
    EXPECT_EQ(Cross(Clip(4, 1.0f), kAnimNoTick, AnimPlaybackEntry::Start, 5, { 0.0, 0.05 }),
              (std::vector<Crossed>{ { 0, false }, { 1, false } }));
}

TEST(AnimCrossing, EnteringLongAfterTheStartSkipsWhatWentBy)
{
    // A late joiner, a reconstruction or a correction: the instance began at 2.
    EXPECT_EQ(Cross(Clip(2, 1.0f), kAnimNoTick, AnimPlaybackEntry::Start, 7, { 0.0, 0.3, 0.45 }),
              (std::vector<Crossed>{ { 0, true }, { 1, true }, { 2, false } }));
}

TEST(AnimCrossing, ACarriedInstanceOwnsTheTickLeadingToIt)
{
    // Carried into at 0.5: the tick's stretch is (0.4, 0.5], and nothing at 0.4 itself.
    EXPECT_EQ(Cross(Clip(9, 1.0f, true, 0.5f), kAnimNoTick, AnimPlaybackEntry::Carried, 9, { 0.4, 0.45, 0.5 }),
              (std::vector<Crossed>{ { 1, false }, { 2, false } }));
}

TEST(AnimCrossing, ABackwardClipCrossesItsMarksDescending)
{
    const AnimPlayback clip = Clip(0, 1.0f, false, 1.0f, -1.0f);
    // From 0.9 down to 0.8: the destination is reached, the origin was left.
    EXPECT_EQ(Cross(clip, 1, AnimPlaybackEntry::Start, 2, { 0.75, 0.8, 0.85, 0.9 }),
              (std::vector<Crossed>{ { 2, false }, { 1, false } }));
}

TEST(AnimCrossing, ASkippedCycleRecordsEachMarkOnce)
{
    // Twenty loops went by in a gap; the skip names each mark once, and the tick
    // before the pass, (4.1, 4.2], holds none.
    const AnimPlayback clip = Clip(0, 0.2f, true);
    EXPECT_EQ(Cross(clip, 1, AnimPlaybackEntry::Start, 42, { 0.05 }), (std::vector<Crossed>{ { 0, true } }));
    EXPECT_EQ(Cross(clip, 1, AnimPlaybackEntry::Start, 43, { 0.05 }),
              (std::vector<Crossed>{ { 0, true }, { 0, false } }));
}

TEST(AnimCrossing, AClipShorterThanATickLoopsWithinItsCap)
{
    const AnimPlayback clip = Clip(0, 0.01f, true);
    std::size_t crossed = 0;
    const AnimCrossing crossing = AnimCrossPlayback(clip, 0, AnimPlaybackEntry::Start, 1, kTick);
    ASSERT_EQ(crossing.Count, 1u);
    ForEachAnimMark(clip, crossing.Stretches[0], 1, [](std::size_t) { return 0.005; },
                    [&](std::size_t) { ++crossed; });
    EXPECT_EQ(crossed, 10u);
}
