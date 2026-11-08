#include <gtest/gtest.h>

#include <render/SkinnedPoseSelection.h>

#include <algorithm>
#include <vector>

namespace
{
SkinnedPoseCandidate Candidate(std::uint32_t id, float depth, std::uint32_t waited = 0, bool neverPosed = false)
{
    SkinnedPoseCandidate candidate;
    candidate.Key.Entity = EntityId{ id, 1 };
    candidate.CameraDepth = depth;
    candidate.JointCount = 62;
    candidate.FramesWaited = waited;
    candidate.NeverPosed = neverPosed;
    return candidate;
}

constexpr SkinnedPoseBudget kRoomy{ 1000, 1ull << 30 };
}

TEST(SkinnedPoseSelection, UnderBudgetEveryCandidateIsPosed)
{
    const std::vector<SkinnedPoseCandidate> candidates{ Candidate(1, 3.f), Candidate(2, 1.f), Candidate(3, 2.f) };
    std::vector<std::uint32_t> chosen;

    SelectSkinnedPoses(candidates, kRoomy, chosen);

    EXPECT_EQ(chosen.size(), 3u);
}

TEST(SkinnedPoseSelection, OverBudgetTheNearestWin)
{
    std::vector<SkinnedPoseCandidate> candidates;
    for (std::uint32_t i = 0; i < 16; ++i)
        candidates.push_back(Candidate(i + 1, static_cast<float>(16 - i)));
    std::vector<std::uint32_t> chosen;

    SelectSkinnedPoses(candidates, SkinnedPoseBudget{ 8, 1ull << 30 }, chosen);

    ASSERT_EQ(chosen.size(), 8u);
    for (const std::uint32_t index : chosen)
        EXPECT_LE(candidates[index].CameraDepth, 8.f);
}

TEST(SkinnedPoseSelection, NeverPosedInstancesComeBeforeRefreshes)
{
    std::vector<SkinnedPoseCandidate> candidates{ Candidate(1, 1.f), Candidate(2, 50.f, 0, true), Candidate(3, 60.f, 0, true) };
    std::vector<std::uint32_t> chosen;

    SelectSkinnedPoses(candidates, SkinnedPoseBudget{ 2, 1ull << 30 }, chosen);

    ASSERT_EQ(chosen.size(), 2u);
    EXPECT_TRUE(candidates[chosen[0]].NeverPosed);
    EXPECT_TRUE(candidates[chosen[1]].NeverPosed);
}

TEST(SkinnedPoseSelection, ASpawnBurstClearsInBurstOverBudgetFrames)
{
    std::vector<SkinnedPoseCandidate> candidates;
    for (std::uint32_t i = 0; i < 256; ++i)
        candidates.push_back(Candidate(i + 1, static_cast<float>(i), 0, true));
    const SkinnedPoseBudget budget{ 128, 1ull << 30 };

    int frames = 0;
    std::uint32_t remaining = 256;
    std::vector<std::uint32_t> chosen;
    while (remaining > 0)
    {
        SelectSkinnedPoses(candidates, budget, chosen);
        for (const std::uint32_t index : chosen)
        {
            if (candidates[index].NeverPosed)
            {
                candidates[index].NeverPosed = false;
                --remaining;
            }
        }
        ++frames;
        ASSERT_LE(frames, 2);
    }
    EXPECT_EQ(frames, 2);
}

TEST(SkinnedPoseSelection, TheLongestWaitingGetAShareSoNothingFreezesForever)
{
    std::vector<SkinnedPoseCandidate> candidates;
    for (std::uint32_t i = 0; i < 16; ++i)
        candidates.push_back(Candidate(i + 1, static_cast<float>(i)));
    candidates.push_back(Candidate(100, 900.f, 500));
    std::vector<std::uint32_t> chosen;

    SelectSkinnedPoses(candidates, SkinnedPoseBudget{ 8, 1ull << 30 }, chosen);

    EXPECT_NE(std::find(chosen.begin(), chosen.end(), 16u), chosen.end());
}

TEST(SkinnedPoseSelection, PaletteBytesLimitTheChoiceToo)
{
    std::vector<SkinnedPoseCandidate> candidates{ Candidate(1, 1.f), Candidate(2, 2.f), Candidate(3, 3.f) };
    std::vector<std::uint32_t> chosen;

    SelectSkinnedPoses(candidates, SkinnedPoseBudget{ 100, SkinnedPalettePackedBytes(62) * 2 }, chosen);

    EXPECT_EQ(chosen.size(), 2u);
}

TEST(SkinnedPoseSelection, TiesBreakOnEntityOrder)
{
    std::vector<SkinnedPoseCandidate> candidates{ Candidate(9, 1.f), Candidate(2, 1.f), Candidate(5, 1.f) };
    std::vector<std::uint32_t> chosen;

    SelectSkinnedPoses(candidates, SkinnedPoseBudget{ 1, 1ull << 30 }, chosen);

    ASSERT_EQ(chosen.size(), 1u);
    EXPECT_EQ(candidates[chosen[0]].Key.Entity.Index, 2u);
}

TEST(SkinnedPalettePackedBytes, MatchesAppendInstancePacking)
{
    EXPECT_EQ(SkinnedPalettePackedBytes(62), 4096u);
    EXPECT_EQ(SkinnedPalettePackedBytes(64), 4096u);
    EXPECT_EQ(SkinnedPalettePackedBytes(65), 4352u);
}
