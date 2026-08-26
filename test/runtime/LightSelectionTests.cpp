// Covers the forward-light policy shared by the runtime and the editor:
// which lights are usable, how a component becomes a candidate, and the
// importance ordering, budget, and shadow-request pairing SelectForwardLights
// applies. All of it is device-free, so these run headless.
//
// This policy had no direct coverage before -- it was reachable only through
// one editor gather test -- while being the single arbiter both hosts depend
// on for identical results.

#include <render/LightSelection.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace
{

RenderEntityKey MakeKey(std::uint32_t index, std::uint64_t scope = 0)
{
    RenderEntityKey key;
    key.Scope = scope;
    key.Entity = EntityId{ index, 1 };
    return key;
}

PointLightComponent MakePoint(float intensity, float range, bool castShadows = false)
{
    PointLightComponent light;
    light.Intensity = intensity;
    light.Range = range;
    light.CastShadows = castShadows;
    return light;
}

SpotLightComponent MakeSpot(float intensity, float range, bool castShadows = false)
{
    SpotLightComponent light;
    light.Intensity = intensity;
    light.Range = range;
    light.CastShadows = castShadows;
    return light;
}

// A candidate placed so its score is driven purely by `intensity`: at equal
// distance and range, LightImportanceScore is monotonic in intensity.
ForwardLightCandidate PointCandidateAt(
    std::uint32_t index, const Vec<3>& position, float intensity, float range = 10.0f)
{
    return MakePointLightCandidate(
        MakeKey(index), position, MakePoint(intensity, range), 1.0f);
}

ForwardLightCandidate BakedPointCandidateAt(
    std::uint32_t index, const Vec<3>& position, float intensity, float range = 10.0f)
{
    PointLightComponent light = MakePoint(intensity, range);
    light.BakeContribution = LightBakeContribution::Direct;
    return MakePointLightCandidate(MakeKey(index), position, light, 1.0f);
}

struct SelectionResult
{
    RenderLightSet Lights;
    std::vector<SpotShadowRequest> SpotRequests;
    std::vector<PointShadowRequest> PointRequests;
    ForwardLightSelectionCounts Counts;
};

SelectionResult Select(std::vector<ForwardLightCandidate> candidates,
                       const Vec<3>& viewOrigin = Vec<3>(0.0f, 0.0f, 0.0f))
{
    SelectionResult result;
    SelectForwardLights(candidates, viewOrigin, result.Lights,
                        result.SpotRequests, result.PointRequests, &result.Counts);
    return result;
}

} // namespace

// -- IsUsableForwardLight ----------------------------------------------------

TEST(IsUsableForwardLight, AcceptsOnlyPositiveFiniteIntensityAndRange)
{
    constexpr float kInf = std::numeric_limits<float>::infinity();
    const float kNan = std::numeric_limits<float>::quiet_NaN();

    struct Case
    {
        float Intensity;
        float Range;
        bool Usable;
        const char* Why;
    };
    const Case cases[] = {
        { 1.0f, 10.0f, true, "ordinary light" },
        { 0.0f, 10.0f, false, "zero intensity contributes nothing" },
        { 1.0f, 0.0f, false, "zero range contributes nothing" },
        { -1.0f, 10.0f, false, "negative intensity" },
        { 1.0f, -10.0f, false, "negative range" },
        { kNan, 10.0f, false, "NaN intensity" },
        { 1.0f, kNan, false, "NaN range" },
        { kInf, 10.0f, false, "infinite intensity" },
        { 1.0f, kInf, false, "infinite range" },
    };

    for (const Case& c : cases)
    {
        EXPECT_EQ(IsUsableForwardLight(c.Intensity, c.Range), c.Usable) << c.Why;
    }
}

// -- Candidate construction --------------------------------------------------

TEST(MakePointLightCandidate, RequestsAShadowOnlyWhenTheLightCastsOne)
{
    const Vec<3> position(1.0f, 2.0f, 3.0f);

    const ForwardLightCandidate off =
        MakePointLightCandidate(MakeKey(1), position, MakePoint(2.0f, 8.0f, false), 1.0f);
    EXPECT_FALSE(off.WantsPointShadow);
    EXPECT_FALSE(off.WantsSpotShadow);
    EXPECT_FLOAT_EQ(off.ShadowBounds.Radius, 0.0f);

    const ForwardLightCandidate on =
        MakePointLightCandidate(MakeKey(1), position, MakePoint(2.0f, 8.0f, true), 1.0f);
    EXPECT_TRUE(on.WantsPointShadow);
    EXPECT_FALSE(on.WantsSpotShadow);
    // The shadow volume is the light's reach, so residency can cull casters.
    EXPECT_FLOAT_EQ(on.ShadowBounds.Radius, 8.0f);
}

TEST(MakePointLightCandidate, CarriesRangeIntensityAndUpdatePolicy)
{
    PointLightComponent light = MakePoint(3.5f, 12.0f, true);
    light.ShadowUpdate = ShadowUpdatePolicy::EveryFrame;

    const ForwardLightCandidate candidate = MakePointLightCandidate(
        MakeKey(7), Vec<3>(0.0f, 0.0f, 0.0f), light, 1.0f);

    EXPECT_FLOAT_EQ(candidate.Intensity, 3.5f);
    EXPECT_FLOAT_EQ(candidate.Range, 12.0f);
    EXPECT_EQ(candidate.ShadowPolicy, ShadowUpdatePolicy::EveryFrame);
    EXPECT_EQ(candidate.Key, MakeKey(7));
}

TEST(MakeSpotLightCandidate, TakesItsAtlasTileSizeFromTheAuthoredResolution)
{
    SpotLightComponent light = MakeSpot(2.0f, 20.0f, true);
    light.ShadowResolution = ShadowResolutionTier::High;

    const ForwardLightCandidate candidate = MakeSpotLightCandidate(
        MakeKey(3), Transform3f{}, light, 1.0f);

    EXPECT_TRUE(candidate.WantsSpotShadow);
    EXPECT_FALSE(candidate.WantsPointShadow);
    EXPECT_EQ(candidate.ShadowTileSize,
              static_cast<std::uint32_t>(ShadowResolutionTier::High));
}

TEST(MakeSpotLightCandidate, LeavesTheTileSizeUnsetWhenItCastsNoShadow)
{
    SpotLightComponent light = MakeSpot(2.0f, 20.0f, false);
    light.ShadowResolution = ShadowResolutionTier::High;

    const ForwardLightCandidate candidate = MakeSpotLightCandidate(
        MakeKey(3), Transform3f{}, light, 1.0f);

    EXPECT_FALSE(candidate.WantsSpotShadow);
    EXPECT_EQ(candidate.ShadowTileSize, 0u);
}

// -- SelectForwardLights: ordering and budget --------------------------------

TEST(SelectForwardLights, PacksBrighterLightsBeforeDimmerOnes)
{
    const Vec<3> at(0.0f, 0.0f, 5.0f);
    std::vector<ForwardLightCandidate> candidates{
        PointCandidateAt(1, at, 1.0f),
        PointCandidateAt(2, at, 9.0f),
        PointCandidateAt(3, at, 4.0f),
    };

    const SelectionResult result = Select(candidates);

    ASSERT_EQ(result.Lights.Count, 3u);
    EXPECT_EQ(result.Counts.Candidates, 3u);
    EXPECT_EQ(result.Counts.Packed, 3u);
    // Same position and range, so intensity alone orders them.
    EXPECT_GT(result.Lights.Lights[0].ColorIntensity.W,
              result.Lights.Lights[1].ColorIntensity.W);
    EXPECT_GT(result.Lights.Lights[1].ColorIntensity.W,
              result.Lights.Lights[2].ColorIntensity.W);
}

TEST(SelectForwardLights, PrefersTheNearerOfTwoIdenticalLights)
{
    std::vector<ForwardLightCandidate> candidates{
        PointCandidateAt(1, Vec<3>(0.0f, 0.0f, 50.0f), 5.0f),
        PointCandidateAt(2, Vec<3>(0.0f, 0.0f, 2.0f), 5.0f),
    };

    const SelectionResult result = Select(candidates);

    ASSERT_EQ(result.Lights.Count, 2u);
    // Reach falls off with distance, so the near light outranks the far one
    // even though both are equally bright.
    EXPECT_FLOAT_EQ(result.Lights.Lights[0].PositionRange.Z, 2.0f);
    EXPECT_FLOAT_EQ(result.Lights.Lights[1].PositionRange.Z, 50.0f);
}

TEST(SelectForwardLights, BreaksScoreTiesOnEntityKeySoTheOrderIsDeterministic)
{
    const Vec<3> at(0.0f, 0.0f, 5.0f);
    // Identical in every scoring input; only the key separates them.
    std::vector<ForwardLightCandidate> ascending{
        PointCandidateAt(1, at, 3.0f),
        PointCandidateAt(2, at, 3.0f),
        PointCandidateAt(3, at, 3.0f),
    };
    std::vector<ForwardLightCandidate> descending{
        PointCandidateAt(3, at, 3.0f),
        PointCandidateAt(2, at, 3.0f),
        PointCandidateAt(1, at, 3.0f),
    };

    SelectionResult a;
    SelectForwardLights(ascending, at, a.Lights, a.SpotRequests, a.PointRequests, nullptr);
    SelectionResult b;
    SelectForwardLights(descending, at, b.Lights, b.SpotRequests, b.PointRequests, nullptr);

    // Submission order must not change the packed order.
    ASSERT_EQ(ascending.size(), descending.size());
    for (std::size_t i = 0; i < ascending.size(); ++i)
        EXPECT_EQ(ascending[i].Key, descending[i].Key) << "at index " << i;
    EXPECT_EQ(ascending.front().Key, MakeKey(1));
}

TEST(SelectForwardLights, StopsAtTheForwardLightCapAndKeepsTheBrightest)
{
    const Vec<3> at(0.0f, 0.0f, 5.0f);
    std::vector<ForwardLightCandidate> candidates;
    const std::uint32_t overflow = kMaxForwardLights + 8;
    candidates.reserve(overflow);
    // Ascending intensity, so the ones that must survive are submitted last.
    for (std::uint32_t i = 0; i < overflow; ++i)
        candidates.push_back(PointCandidateAt(i, at, 1.0f + static_cast<float>(i)));

    const SelectionResult result = Select(candidates);

    EXPECT_EQ(result.Lights.Count, kMaxForwardLights);
    EXPECT_EQ(result.Counts.Candidates, overflow);
    EXPECT_EQ(result.Counts.Packed, kMaxForwardLights);
    // Exactly the top kMaxForwardLights survive, in order: intensities run
    // 1..overflow, so the brightest kept is `overflow` and the dimmest kept is
    // the first one that fits. Naming both pins that the cap drops from the
    // bottom rather than truncating submission order.
    EXPECT_FLOAT_EQ(result.Lights.Lights[0].ColorIntensity.W,
                    static_cast<float>(overflow));
    EXPECT_FLOAT_EQ(result.Lights.Lights[kMaxForwardLights - 1].ColorIntensity.W,
                    static_cast<float>(overflow - kMaxForwardLights + 1));
}

// -- SelectForwardLights: the baked tier -------------------------------------

TEST(MakeLightCandidate, FlagsBakedLightsAndNeverRequestsTheirShadows)
{
    const Vec<3> at(0.0f, 0.0f, 5.0f);

    PointLightComponent point = MakePoint(3.0f, 10.0f, /*castShadows*/ true);
    point.BakeContribution = LightBakeContribution::Direct;
    const ForwardLightCandidate bakedPoint =
        MakePointLightCandidate(MakeKey(1), at, point, 1.0f);
    EXPECT_TRUE(bakedPoint.Baked);
    // CastShadows on a baked light describes the bake; the runtime never
    // spends a slot on it.
    EXPECT_FALSE(bakedPoint.WantsPointShadow);
    EXPECT_NE(bakedPoint.Light.Type & kGpuLightBakedBit, 0u);
    EXPECT_EQ(bakedPoint.Light.Type & kGpuLightTypeMask,
              static_cast<std::uint32_t>(GpuLightType::Point));

    SpotLightComponent spot = MakeSpot(3.0f, 10.0f, /*castShadows*/ true);
    spot.BakeContribution = LightBakeContribution::Direct;
    Transform3f transform;
    transform.Position = at;
    const ForwardLightCandidate bakedSpot =
        MakeSpotLightCandidate(MakeKey(2), transform, spot, 1.0f);
    EXPECT_TRUE(bakedSpot.Baked);
    EXPECT_FALSE(bakedSpot.WantsSpotShadow);
    EXPECT_NE(bakedSpot.Light.Type & kGpuLightBakedBit, 0u);
    EXPECT_EQ(bakedSpot.Light.Type & kGpuLightTypeMask,
              static_cast<std::uint32_t>(GpuLightType::Spot));

    // Indirect stays a live light: realtime direct, baked bounce.
    point.BakeContribution = LightBakeContribution::Indirect;
    const ForwardLightCandidate indirect =
        MakePointLightCandidate(MakeKey(3), at, point, 1.0f);
    EXPECT_FALSE(indirect.Baked);
    EXPECT_TRUE(indirect.WantsPointShadow);
    EXPECT_EQ(indirect.Light.Type & kGpuLightBakedBit, 0u);
}

TEST(SelectForwardLights, PacksEveryLiveLightBeforeAnyBakedOne)
{
    const Vec<3> at(0.0f, 0.0f, 5.0f);
    // The baked light outscores both live ones; it still packs last.
    std::vector<ForwardLightCandidate> candidates{
        BakedPointCandidateAt(1, at, 100.0f),
        PointCandidateAt(2, at, 2.0f),
        PointCandidateAt(3, at, 1.0f),
    };

    const SelectionResult result = Select(candidates);

    ASSERT_EQ(result.Lights.Count, 3u);
    EXPECT_EQ(result.Lights.Lights[0].Type & kGpuLightBakedBit, 0u);
    EXPECT_FLOAT_EQ(result.Lights.Lights[0].ColorIntensity.W, 2.0f);
    EXPECT_EQ(result.Lights.Lights[1].Type & kGpuLightBakedBit, 0u);
    EXPECT_FLOAT_EQ(result.Lights.Lights[1].ColorIntensity.W, 1.0f);
    EXPECT_NE(result.Lights.Lights[2].Type & kGpuLightBakedBit, 0u);
}

TEST(SelectForwardLights, ABakedLightCanNeverEvictALiveOne)
{
    const Vec<3> at(0.0f, 0.0f, 5.0f);
    struct Case
    {
        std::uint32_t Live;
        std::uint32_t Baked;
        std::uint32_t ExpectLivePacked;
        std::uint32_t ExpectBakedPacked;
        const char* Why;
    };
    const Case cases[] = {
        { kMaxForwardLights, 8, kMaxForwardLights, 0,
          "a full live cap leaves no slot for any baked light" },
        { kMaxForwardLights - 2, 8, kMaxForwardLights - 2, 2,
          "baked lights take exactly the slots live lights left empty" },
        { 4, 4, 4, 4, "an uncontended frame packs both tiers whole" },
        { 0, kMaxForwardLights + 4, 0, kMaxForwardLights,
          "an all-baked zone still fills the cap by score" },
    };
    for (const Case& c : cases)
    {
        std::vector<ForwardLightCandidate> candidates;
        // Baked lights are all brighter than every live light, so any
        // eviction bug would surface as a missing live light.
        for (std::uint32_t i = 0; i < c.Live; ++i)
            candidates.push_back(
                PointCandidateAt(i, at, 1.0f + static_cast<float>(i)));
        for (std::uint32_t i = 0; i < c.Baked; ++i)
            candidates.push_back(BakedPointCandidateAt(
                1000 + i, at, 1000.0f + static_cast<float>(i)));

        const SelectionResult result = Select(candidates);

        std::uint32_t livePacked = 0, bakedPacked = 0;
        for (std::uint32_t i = 0; i < result.Lights.Count; ++i)
        {
            if ((result.Lights.Lights[i].Type & kGpuLightBakedBit) != 0u)
                ++bakedPacked;
            else
                ++livePacked;
        }
        EXPECT_EQ(livePacked, c.ExpectLivePacked) << c.Why;
        EXPECT_EQ(bakedPacked, c.ExpectBakedPacked) << c.Why;
    }
}

TEST(SelectForwardLights, ResetsPriorStateSoAFrameNeverInheritsTheLast)
{
    const Vec<3> at(0.0f, 0.0f, 5.0f);
    SelectionResult result;

    std::vector<ForwardLightCandidate> first{
        MakePointLightCandidate(MakeKey(1), at, MakePoint(4.0f, 10.0f, true), 1.0f),
    };
    SelectForwardLights(first, at, result.Lights, result.SpotRequests,
                        result.PointRequests, &result.Counts);
    ASSERT_EQ(result.Lights.Count, 1u);
    ASSERT_EQ(result.PointRequests.size(), 1u);

    std::vector<ForwardLightCandidate> none;
    SelectForwardLights(none, at, result.Lights, result.SpotRequests,
                        result.PointRequests, &result.Counts);

    EXPECT_EQ(result.Lights.Count, 0u);
    EXPECT_TRUE(result.PointRequests.empty());
    EXPECT_TRUE(result.SpotRequests.empty());
    EXPECT_EQ(result.Counts.Candidates, 0u);
    EXPECT_EQ(result.Counts.Packed, 0u);
}

// -- SelectForwardLights: shadow requests ------------------------------------

TEST(SelectForwardLights, ShadowRequestsPointAtThePackedLightIndex)
{
    const Vec<3> at(0.0f, 0.0f, 5.0f);
    // The shadow caster is the dimmest, so it packs last and its request must
    // carry that index rather than its submission position.
    std::vector<ForwardLightCandidate> candidates{
        MakePointLightCandidate(MakeKey(1), at, MakePoint(1.0f, 10.0f, true), 1.0f),
        PointCandidateAt(2, at, 9.0f),
        PointCandidateAt(3, at, 5.0f),
    };

    const SelectionResult result = Select(candidates);

    ASSERT_EQ(result.PointRequests.size(), 1u);
    EXPECT_EQ(result.PointRequests[0].Key, MakeKey(1));
    EXPECT_EQ(result.PointRequests[0].LightIndex, 2u);
    ASSERT_LT(result.PointRequests[0].LightIndex, result.Lights.Count);
}

TEST(SelectForwardLights, EmitsNoRequestForALightThatCastsNoShadow)
{
    const Vec<3> at(0.0f, 0.0f, 5.0f);
    const SelectionResult result = Select({
        PointCandidateAt(1, at, 4.0f),
        PointCandidateAt(2, at, 2.0f),
    });

    EXPECT_EQ(result.Lights.Count, 2u);
    EXPECT_TRUE(result.PointRequests.empty());
    EXPECT_TRUE(result.SpotRequests.empty());
}

TEST(SelectForwardLights, SeparatesSpotAndPointRequestsIntoTheirOwnPools)
{
    SpotLightComponent spot = MakeSpot(6.0f, 20.0f, true);
    std::vector<ForwardLightCandidate> candidates{
        MakeSpotLightCandidate(MakeKey(1), Transform3f{}, spot, 1.0f),
        MakePointLightCandidate(MakeKey(2), Vec<3>(0.0f, 0.0f, 1.0f),
                                MakePoint(4.0f, 10.0f, true), 1.0f),
    };

    const SelectionResult result = Select(candidates);

    ASSERT_EQ(result.SpotRequests.size(), 1u);
    ASSERT_EQ(result.PointRequests.size(), 1u);
    EXPECT_EQ(result.SpotRequests[0].Key, MakeKey(1));
    EXPECT_EQ(result.PointRequests[0].Key, MakeKey(2));
}

TEST(SelectForwardLights, ShadowRequestsFollowTheSamePackingOrderAsTheLights)
{
    const Vec<3> at(0.0f, 0.0f, 5.0f);
    std::vector<ForwardLightCandidate> candidates{
        MakePointLightCandidate(MakeKey(1), at, MakePoint(2.0f, 10.0f, true), 1.0f),
        MakePointLightCandidate(MakeKey(2), at, MakePoint(8.0f, 10.0f, true), 1.0f),
        MakePointLightCandidate(MakeKey(3), at, MakePoint(5.0f, 10.0f, true), 1.0f),
    };

    const SelectionResult result = Select(candidates);

    ASSERT_EQ(result.PointRequests.size(), 3u);
    // Brightest first, and each request's index rises with it, so residency
    // sees requests in the same importance order the shader will.
    EXPECT_EQ(result.PointRequests[0].Key, MakeKey(2));
    EXPECT_EQ(result.PointRequests[1].Key, MakeKey(3));
    EXPECT_EQ(result.PointRequests[2].Key, MakeKey(1));
    EXPECT_LT(result.PointRequests[0].LightIndex, result.PointRequests[1].LightIndex);
    EXPECT_LT(result.PointRequests[1].LightIndex, result.PointRequests[2].LightIndex);
    EXPECT_GT(result.PointRequests[0].Score, result.PointRequests[1].Score);
}

TEST(SelectForwardLights, DropsTheShadowRequestOfALightPushedPastTheCap)
{
    const Vec<3> at(0.0f, 0.0f, 5.0f);
    std::vector<ForwardLightCandidate> candidates;
    candidates.reserve(kMaxForwardLights + 1);
    for (std::uint32_t i = 0; i < kMaxForwardLights; ++i)
        candidates.push_back(PointCandidateAt(i, at, 100.0f + static_cast<float>(i)));
    // Dimmest of the set and a shadow caster: it loses its slot, and its
    // shadow request must go with it rather than referencing an unpacked light.
    candidates.push_back(MakePointLightCandidate(
        MakeKey(9999), at, MakePoint(0.5f, 10.0f, true), 1.0f));

    const SelectionResult result = Select(candidates);

    EXPECT_EQ(result.Lights.Count, kMaxForwardLights);
    EXPECT_TRUE(result.PointRequests.empty());
}
