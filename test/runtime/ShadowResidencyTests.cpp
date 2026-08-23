#include <gtest/gtest.h>

#include <core/hash/Fnv1a.h>
#include <render/ShadowResidency.h>

#include <algorithm>
#include <vector>

namespace
{
    RenderEntityKey MakeKey(std::uint32_t entityIndex)
    {
        return RenderEntityKey{ .Entity = EntityId{ entityIndex, 1 } };
    }

    SpotShadowRequest MakeRequest(std::uint32_t entityIndex,
                                  std::uint32_t lightIndex,
                                  float score,
                                  ShadowUpdatePolicy policy = ShadowUpdatePolicy::EveryFrame,
                                  std::uint32_t tileSize = 512,
                                  std::uint64_t stateHash = 1)
    {
        return SpotShadowRequest{
            .Key = MakeKey(entityIndex),
            .LightIndex = lightIndex,
            .Score = score,
            .TileSize = tileSize,
            .Policy = policy,
            .StateHash = stateHash,
            .SamplingParams = Vec4(0.01f, 1.5f, 1.0f, 0.0f),
            .Bounds = Sphere(Vec3d(10.0f * static_cast<float>(entityIndex), 0.0f, 0.0f), 4.0f),
        };
    }

    PointShadowRequest MakePointRequest(
        std::uint32_t entityIndex,
        std::uint32_t lightIndex,
        float score,
        ShadowUpdatePolicy policy = ShadowUpdatePolicy::OnChange,
        std::uint64_t stateHash = 1)
    {
        const float x = 10.0f * static_cast<float>(entityIndex);
        return PointShadowRequest{
            .Key = MakeKey(entityIndex),
            .LightIndex = lightIndex,
            .Score = score,
            .Policy = policy,
            .StateHash = stateHash,
            .View = PointShadowView{
                .PositionFar = Vec4(x, 0.0f, 0.0f, 20.0f),
                .Params = Vec4(0.4f, 1.5f, 1.0f, 0.0f),
            },
            .Bounds = Sphere(Vec3d(x, 0.0f, 0.0f), 20.0f),
        };
    }

    const ShadowResidencyBudgets kUnlimited{
        .MaxSlots = kMaxSpotShadows,
        .MaxViewsPerFrame = 0,
        .MinInvalidatedViewsPerFrame = 1,
    };

    [[nodiscard]] bool HasGrantForLight(const ShadowResidency& residency,
                                        std::uint32_t lightIndex,
                                        std::uint32_t* slotOut = nullptr)
    {
        for (const SpotShadowGrant& grant : residency.Grants())
        {
            if (grant.LightIndex == lightIndex)
            {
                if (slotOut != nullptr)
                    *slotOut = grant.SlotIndex;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool HasPointGrantForLight(
        const ShadowResidency& residency,
        std::uint32_t lightIndex,
        std::uint32_t* slotOut = nullptr)
    {
        for (const PointShadowGrant& grant : residency.PointGrants())
        {
            if (grant.LightIndex == lightIndex)
            {
                if (slotOut != nullptr)
                    *slotOut = grant.SlotIndex;
                return true;
            }
        }
        return false;
    }
}

TEST(ShadowResidency, NewPointCubeBurstsSixFacesAndThenGrants)
{
    ShadowResidency residency;
    std::vector<PointShadowRequest> points{
        MakePointRequest(1, 0, 3.0f),
    };

    residency.Update({}, points, {}, kUnlimited);

    ASSERT_EQ(residency.ScheduledPointFaces().size(), kPointShadowFaceCount);
    for (std::uint32_t face = 0; face < kPointShadowFaceCount; ++face)
    {
        EXPECT_EQ(residency.ScheduledPointFaces()[face].SlotIndex, 0u);
        EXPECT_EQ(residency.ScheduledPointFaces()[face].Face, face);
    }
    ASSERT_EQ(residency.PointGrants().size(), 1u);
    EXPECT_EQ(residency.PointGrants()[0].LightIndex, 0u);
    EXPECT_EQ(residency.PointGrants()[0].SlotIndex, 0u);
}

TEST(ShadowResidency, PointCubeStaysUngrantedUntilAllSplitFacesRender)
{
    ShadowResidencyBudgets split = kUnlimited;
    split.MaxViewsPerFrame = 2;

    ShadowResidency residency;
    std::vector<PointShadowRequest> points{
        MakePointRequest(1, 0, 3.0f),
    };

    for (std::uint32_t frame = 0; frame < 3; ++frame)
    {
        residency.Update({}, points, {}, split);
        ASSERT_EQ(residency.ScheduledPointFaces().size(), 2u);
        EXPECT_EQ(residency.ScheduledPointFaces()[0].Face, frame * 2u);
        EXPECT_EQ(residency.ScheduledPointFaces()[1].Face, frame * 2u + 1u);
        EXPECT_EQ(HasPointGrantForLight(residency, 0), frame == 2u);
    }
}

TEST(ShadowResidency, PointSlotBudgetDeniesLowerPriorityRequests)
{
    ShadowResidencyBudgets twoPoints = kUnlimited;
    twoPoints.MaxPointSlots = 2;

    ShadowResidency residency;
    std::vector<PointShadowRequest> points{
        MakePointRequest(1, 0, 3.0f),
        MakePointRequest(2, 1, 2.0f),
        MakePointRequest(3, 2, 1.0f),
    };
    residency.Update({}, points, {}, twoPoints);

    EXPECT_EQ(residency.PointGrants().size(), 2u);
    EXPECT_TRUE(HasPointGrantForLight(residency, 0));
    EXPECT_TRUE(HasPointGrantForLight(residency, 1));
    EXPECT_FALSE(HasPointGrantForLight(residency, 2));
    EXPECT_EQ(residency.FrameStats().Point.DeniedRequests, 1u);
}

TEST(ShadowResidency, InvalidatedPointCubeDrainsUnderSaturatedSpotWork)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> spots;
    for (std::uint32_t index = 0; index < 5; ++index)
        spots.push_back(MakeRequest(index + 1, index, 10.0f));
    std::vector<PointShadowRequest> points{
        MakePointRequest(20, 5, 5.0f, ShadowUpdatePolicy::OnChange),
    };
    residency.Update(spots, points, {}, kUnlimited);
    ASSERT_TRUE(HasPointGrantForLight(residency, 5));

    ShadowResidencyBudgets clamped = kUnlimited;
    clamped.MaxViewsPerFrame = 6;
    const ShadowCasterEvent overlap{
        .Change = ShadowCasterEvent::Kind::Changed,
        .Bounds = Aabb3d(
            Vec3d(199.0f, -1.0f, -1.0f),
            Vec3d(201.0f, 1.0f, 1.0f)),
    };

    for (std::uint32_t frame = 0; frame < kPointShadowFaceCount; ++frame)
    {
        const std::span<const ShadowCasterEvent> events = frame == 0
            ? std::span<const ShadowCasterEvent>(&overlap, 1)
            : std::span<const ShadowCasterEvent>();
        residency.Update(spots, points, events, clamped);
        EXPECT_EQ(residency.ScheduledViews().size(), 5u);
        ASSERT_EQ(residency.ScheduledPointFaces().size(), 1u);
        EXPECT_EQ(residency.ScheduledPointFaces()[0].Face, frame);
        EXPECT_EQ(HasPointGrantForLight(residency, 5),
                  frame + 1u == kPointShadowFaceCount);
    }
}

TEST(ShadowResidency, PointHashCoversRenderedStateButNotPackedLightIndex)
{
    PointShadowView view;
    view.PositionFar = Vec4(1.0f, 2.0f, 3.0f, 20.0f);
    view.Params = Vec4(0.4f, 1.5f, 1.0f, 0.0f);
    view.LightIndex = 3;
    const std::uint64_t original = HashPointShadowState(view);

    view.LightIndex = 9;
    EXPECT_EQ(HashPointShadowState(view), original);
    view.PositionFar.X += 1.0f;
    EXPECT_NE(HashPointShadowState(view), original);
    view.PositionFar.X -= 1.0f;
    view.Params.Z += 0.5f;
    EXPECT_NE(HashPointShadowState(view), original);
}

TEST(ShadowResidency, GrantsSlotsAndSchedulesNewViewsDeterministically)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{
        MakeRequest(1, 0, 3.0f),
        MakeRequest(2, 1, 2.0f),
        MakeRequest(3, 2, 1.0f),
    };
    residency.Update(requests, {}, kUnlimited);

    ASSERT_EQ(residency.ScheduledViews().size(), 3u);
    ASSERT_EQ(residency.Grants().size(), 3u);
    EXPECT_EQ(residency.SlotHighWater(), 3u);

    // Highest score first through the deterministic slot walk; tiles pack
    // the first quadrant in Morton order.
    EXPECT_EQ(residency.ScheduledViews()[0].SlotIndex, 0u);
    EXPECT_EQ(residency.ScheduledViews()[0].Allocation,
              (ShadowAtlasAllocation{ 0, 0, 512 }));
    EXPECT_EQ(residency.ScheduledViews()[1].Allocation,
              (ShadowAtlasAllocation{ 512, 0, 512 }));
    EXPECT_EQ(residency.ScheduledViews()[2].Allocation,
              (ShadowAtlasAllocation{ 0, 512, 512 }));

    EXPECT_EQ(residency.SlotRecord(0).AtlasScaleBias,
              ShadowAtlasAllocator::InsetScaleBias(ShadowAtlasAllocation{ 0, 0, 512 }));
}

TEST(ShadowResidency, CachedOnChangeSlotsStopRenderingAtSteadyState)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{
        MakeRequest(1, 0, 3.0f, ShadowUpdatePolicy::OnChange),
        MakeRequest(2, 1, 2.0f, ShadowUpdatePolicy::OnChange),
    };
    residency.Update(requests, {}, kUnlimited);
    EXPECT_EQ(residency.ScheduledViews().size(), 2u);

    for (int frame = 0; frame < 5; ++frame)
        residency.Update(requests, {}, kUnlimited);
    EXPECT_TRUE(residency.ScheduledViews().empty());
    EXPECT_EQ(residency.Grants().size(), 2u);
}

TEST(ShadowResidency, SlotBudgetCapsGrants)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests;
    for (std::uint32_t index = 0; index < 10; ++index)
        requests.push_back(MakeRequest(index + 1, index, 10.0f - static_cast<float>(index)));

    residency.Update(requests, {}, kUnlimited);
    EXPECT_EQ(residency.Grants().size(), kMaxSpotShadows);
    EXPECT_FALSE(HasGrantForLight(residency, 8));
    EXPECT_FALSE(HasGrantForLight(residency, 9));
}

TEST(ShadowResidency, TierDowngradeServesBeforeDenial)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{
        MakeRequest(1, 0, 9.0f, ShadowUpdatePolicy::EveryFrame, 1024),
        MakeRequest(2, 1, 8.0f, ShadowUpdatePolicy::EveryFrame, 1024),
        MakeRequest(3, 2, 7.0f, ShadowUpdatePolicy::EveryFrame, 1024),
        MakeRequest(4, 3, 6.0f, ShadowUpdatePolicy::EveryFrame, 512),
        MakeRequest(5, 4, 5.0f, ShadowUpdatePolicy::EveryFrame, 1024),
    };
    residency.Update(requests, {}, kUnlimited);

    ASSERT_EQ(residency.Grants().size(), 5u);
    // Three full quadrants plus a 512 leave no 1024 node, so the last light
    // renders at 512 instead of being denied.
    const std::uint32_t lastSlot = residency.Grants()[4].SlotIndex;
    EXPECT_EQ(residency.ScheduledViews()[4].SlotIndex, lastSlot);
    EXPECT_EQ(residency.ScheduledViews()[4].Allocation.Size, 512u);
}

TEST(ShadowResidency, EqualScoresNeverFlickerOwnership)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> holders;
    for (std::uint32_t index = 0; index < kMaxSpotShadows; ++index)
        holders.push_back(MakeRequest(index + 1, index, 1.0f));
    residency.Update(holders, {}, kUnlimited);

    // A ninth light at the same score never outscores a held slot (holders
    // carry the hysteresis multiplier and ties are not steals).
    std::vector<SpotShadowRequest> contested = holders;
    contested.push_back(MakeRequest(100, 8, 1.0f));
    for (int frame = 0; frame < 100; ++frame)
        residency.Update(contested, {}, kUnlimited);

    EXPECT_FALSE(HasGrantForLight(residency, 8));
    EXPECT_EQ(residency.Grants().size(), kMaxSpotShadows);
}

TEST(ShadowResidency, PersistentlyOutscoredHolderIsStolenAfterTheRun)
{
    ShadowResidencyBudgets oneSlot = kUnlimited;
    oneSlot.MaxSlots = 1;

    ShadowResidency residency;
    std::vector<SpotShadowRequest> first{ MakeRequest(1, 0, 1.0f) };
    residency.Update(first, {}, oneSlot);
    EXPECT_TRUE(HasGrantForLight(residency, 0));

    // Contender at double the score: the holder survives the full hysteresis
    // run and loses the slot only when the run completes.
    std::vector<SpotShadowRequest> contested{
        MakeRequest(2, 1, 2.0f),
        MakeRequest(1, 0, 1.0f),
    };
    for (std::uint32_t frame = 0; frame < ShadowResidency::kStealOutscoredFrames - 1; ++frame)
    {
        residency.Update(contested, {}, oneSlot);
        EXPECT_TRUE(HasGrantForLight(residency, 0));
        EXPECT_FALSE(HasGrantForLight(residency, 1));
    }

    residency.Update(contested, {}, oneSlot);
    EXPECT_TRUE(HasGrantForLight(residency, 1));
    EXPECT_FALSE(HasGrantForLight(residency, 0));
}

TEST(ShadowResidency, SaturatedEveryFrameLoadCannotStarveInvalidatedSlots)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests;
    for (std::uint32_t index = 0; index < 5; ++index)
        requests.push_back(MakeRequest(index + 1, index, 10.0f));
    for (std::uint32_t index = 5; index < 8; ++index)
    {
        requests.push_back(
            MakeRequest(index + 1, index, 5.0f, ShadowUpdatePolicy::OnChange));
    }
    residency.Update(requests, {}, kUnlimited);
    EXPECT_EQ(residency.ScheduledViews().size(), 8u);

    // Invalidate the three cached lights through caster events at their
    // volumes, then clamp the frame budget to EveryFrame demand plus the
    // single reserved view.
    std::vector<ShadowCasterEvent> events;
    for (std::uint32_t index = 5; index < 8; ++index)
    {
        const float x = 10.0f * static_cast<float>(index + 1);
        events.push_back(ShadowCasterEvent{
            .Change = ShadowCasterEvent::Kind::Changed,
            .Bounds = Aabb3d(Vec3d(x - 1.0f, -1.0f, -1.0f), Vec3d(x + 1.0f, 1.0f, 1.0f)),
        });
    }

    ShadowResidencyBudgets clamped = kUnlimited;
    clamped.MaxViewsPerFrame = 6;

    std::vector<std::uint32_t> invalidatedServedPerFrame;
    for (int frame = 0; frame < 3; ++frame)
    {
        residency.Update(requests, frame == 0 ? std::span<const ShadowCasterEvent>(events)
                                              : std::span<const ShadowCasterEvent>(),
                         clamped);
        std::uint32_t cachedServed = 0;
        for (const SpotShadowViewJob& view : residency.ScheduledViews())
            cachedServed += view.SlotIndex >= 5 ? 1u : 0u;
        invalidatedServedPerFrame.push_back(cachedServed);
        EXPECT_LE(residency.ScheduledViews().size(), 6u);
    }

    // One reserved view per frame drains the backlog within its bound of
    // ceil(3 / 1) frames, EveryFrame saturation notwithstanding.
    EXPECT_EQ(invalidatedServedPerFrame[0], 1u);
    EXPECT_EQ(invalidatedServedPerFrame[1], 1u);
    EXPECT_EQ(invalidatedServedPerFrame[2], 1u);

    residency.Update(requests, {}, clamped);
    for (const SpotShadowViewJob& view : residency.ScheduledViews())
        EXPECT_LT(view.SlotIndex, 5u);
}

TEST(ShadowResidency, NeverRenderedSlotsScheduleBeforeEverything)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> everyFrame{
        MakeRequest(1, 0, 5.0f),
        MakeRequest(2, 1, 4.0f),
    };
    residency.Update(everyFrame, {}, kUnlimited);

    ShadowResidencyBudgets clamped = kUnlimited;
    clamped.MaxViewsPerFrame = 1;
    std::vector<SpotShadowRequest> withNew = everyFrame;
    withNew.push_back(MakeRequest(3, 2, 0.5f));
    residency.Update(withNew, {}, clamped);

    ASSERT_EQ(residency.ScheduledViews().size(), 1u);
    std::uint32_t newSlot = UINT32_MAX;
    ASSERT_TRUE(HasGrantForLight(residency, 2, &newSlot));
    EXPECT_EQ(residency.ScheduledViews()[0].SlotIndex, newSlot);
}

TEST(ShadowResidency, LightsStayUngrantedUntilTheirSlotFirstRenders)
{
    ShadowResidencyBudgets clamped = kUnlimited;
    clamped.MaxViewsPerFrame = 1;

    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{
        MakeRequest(1, 0, 2.0f),
        MakeRequest(2, 1, 1.0f),
    };
    residency.Update(requests, {}, clamped);

    // Only the rendered slot may be sampled; the still-unrendered slot's
    // light stays shadowless instead of sampling undefined tile contents.
    EXPECT_EQ(residency.ScheduledViews().size(), 1u);
    EXPECT_TRUE(HasGrantForLight(residency, 0));
    EXPECT_FALSE(HasGrantForLight(residency, 1));

    residency.Update(requests, {}, clamped);
    EXPECT_TRUE(HasGrantForLight(residency, 0));
    EXPECT_TRUE(HasGrantForLight(residency, 1));
}

TEST(ShadowResidency, CasterEventsInvalidateOnlyOverlappingOnChangeSlots)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{
        MakeRequest(1, 0, 4.0f, ShadowUpdatePolicy::OnChange),
        MakeRequest(2, 1, 3.0f, ShadowUpdatePolicy::OnChange),
        MakeRequest(3, 2, 2.0f, ShadowUpdatePolicy::Static),
    };
    residency.Update(requests, {}, kUnlimited);
    residency.Update(requests, {}, kUnlimited);
    EXPECT_TRUE(residency.ScheduledViews().empty());

    // The box overlaps light 1's volume (x = 20 +- 4) and the Static light's
    // volume (x = 30 +- 4) but stays clear of light 0's (x = 10 +- 4): only
    // the overlapped OnChange slot may re-render.
    std::vector<ShadowCasterEvent> events{
        ShadowCasterEvent{
            .Change = ShadowCasterEvent::Kind::Changed,
            .Bounds = Aabb3d(Vec3d(14.5f, -1.0f, -1.0f), Vec3d(32.0f, 1.0f, 1.0f)),
        },
    };
    residency.Update(requests, events, kUnlimited);

    std::uint32_t slotForLight0 = UINT32_MAX;
    std::uint32_t slotForLight1 = UINT32_MAX;
    ASSERT_TRUE(HasGrantForLight(residency, 0, &slotForLight0));
    ASSERT_TRUE(HasGrantForLight(residency, 1, &slotForLight1));

    ASSERT_EQ(residency.ScheduledViews().size(), 1u);
    EXPECT_EQ(residency.ScheduledViews()[0].SlotIndex, slotForLight1);
}

TEST(ShadowResidency, StateHashChangeRerendersOnChangeInPlace)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{
        MakeRequest(1, 0, 4.0f, ShadowUpdatePolicy::OnChange, 512, 1),
    };
    residency.Update(requests, {}, kUnlimited);
    std::uint32_t originalSlot = UINT32_MAX;
    ASSERT_TRUE(HasGrantForLight(residency, 0, &originalSlot));

    requests[0].StateHash = 2;
    residency.Update(requests, {}, kUnlimited);

    ASSERT_EQ(residency.ScheduledViews().size(), 1u);
    EXPECT_EQ(residency.ScheduledViews()[0].SlotIndex, originalSlot);

    residency.Update(requests, {}, kUnlimited);
    EXPECT_TRUE(residency.ScheduledViews().empty());
}

TEST(ShadowResidency, StaticSlotsIgnoreDriftUntilExplicitInvalidation)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{
        MakeRequest(1, 0, 4.0f, ShadowUpdatePolicy::Static, 512, 1),
    };
    residency.Update(requests, {}, kUnlimited);
    EXPECT_EQ(residency.ScheduledViews().size(), 1u);

    requests[0].StateHash = 2;
    residency.Update(requests, {}, kUnlimited);
    EXPECT_TRUE(residency.ScheduledViews().empty());

    residency.InvalidateAll();
    residency.Update(requests, {}, kUnlimited);
    EXPECT_EQ(residency.ScheduledViews().size(), 1u);
}

TEST(ShadowResidency, FailedViewRecordingRequeues)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{ MakeRequest(1, 0, 4.0f) };
    residency.Update(requests, {}, kUnlimited);
    ASSERT_EQ(residency.ScheduledViews().size(), 1u);
    const std::uint32_t slot = residency.ScheduledViews()[0].SlotIndex;

    residency.MarkViewFailed(slot);
    EXPECT_TRUE(residency.Grants().empty());
    residency.Update(requests, {}, kUnlimited);
    ASSERT_EQ(residency.ScheduledViews().size(), 1u);
    EXPECT_EQ(residency.ScheduledViews()[0].SlotIndex, slot);
}

TEST(ShadowResidency, FailedPointFaceRecordingRevokesGrantAndRequeuesFace)
{
    ShadowResidency residency;
    std::vector<PointShadowRequest> points{ MakePointRequest(1, 0, 4.0f) };
    residency.Update({}, points, {}, kUnlimited);
    ASSERT_EQ(residency.PointGrants().size(), 1u);
    const std::uint32_t slot = residency.PointGrants()[0].SlotIndex;

    residency.MarkPointFaceFailed(slot, 4);
    EXPECT_TRUE(residency.PointGrants().empty());

    ShadowResidencyBudgets oneView = kUnlimited;
    oneView.MaxViewsPerFrame = 1;
    residency.Update({}, points, {}, oneView);
    ASSERT_EQ(residency.ScheduledPointFaces().size(), 1u);
    EXPECT_EQ(residency.ScheduledPointFaces()[0].SlotIndex, slot);
    EXPECT_EQ(residency.ScheduledPointFaces()[0].Face, 4u);
    EXPECT_TRUE(HasPointGrantForLight(residency, 0));
}

TEST(ShadowResidency, TierChangeReacquiresTheSlotInPlace)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{
        MakeRequest(1, 0, 4.0f, ShadowUpdatePolicy::OnChange, 512),
    };
    residency.Update(requests, {}, kUnlimited);
    std::uint32_t slot = UINT32_MAX;
    ASSERT_TRUE(HasGrantForLight(residency, 0, &slot));

    requests[0].TileSize = 1024;
    residency.Update(requests, {}, kUnlimited);

    ASSERT_EQ(residency.ScheduledViews().size(), 1u);
    EXPECT_EQ(residency.ScheduledViews()[0].SlotIndex, slot);
    EXPECT_EQ(residency.ScheduledViews()[0].Allocation.Size, 1024u);
    EXPECT_EQ(residency.SlotRecord(slot).AtlasScaleBias,
              ShadowAtlasAllocator::InsetScaleBias(residency.ScheduledViews()[0].Allocation));
}

TEST(ShadowResidency, FrameStatsCountDemandViewsAndCacheHits)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{
        MakeRequest(1, 0, 3.0f, ShadowUpdatePolicy::OnChange),
        MakeRequest(2, 1, 2.0f, ShadowUpdatePolicy::OnChange),
        MakeRequest(3, 2, 1.0f, ShadowUpdatePolicy::OnChange),
    };
    const ShadowResidencyBudgets twoSlots{
        .MaxSlots = 2,
        .MaxViewsPerFrame = 0,
        .MinInvalidatedViewsPerFrame = 1,
    };

    residency.Update(requests, {}, twoSlots);
    EXPECT_EQ(residency.FrameStats().Spot.RequestCount, 3u);
    EXPECT_EQ(residency.FrameStats().Spot.HeldRequests, 2u);
    EXPECT_EQ(residency.FrameStats().Spot.DeniedRequests, 1u);
    EXPECT_EQ(residency.FrameStats().ViewsScheduled, 2u);
    EXPECT_EQ(residency.FrameStats().Spot.CachedSlots, 0u);

    // Steady state: both tiles cache, nothing renders.
    residency.Update(requests, {}, twoSlots);
    EXPECT_EQ(residency.FrameStats().ViewsScheduled, 0u);
    EXPECT_EQ(residency.FrameStats().Spot.CachedSlots, 2u);
    EXPECT_EQ(residency.FrameStats().Spot.DeniedRequests, 1u);
}

TEST(ShadowResidency, SlotInfoTracksAllocationAndAges)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{
        MakeRequest(1, 0, 4.0f, ShadowUpdatePolicy::OnChange, 1024),
    };
    residency.Update(requests, {}, kUnlimited);
    std::uint32_t slot = UINT32_MAX;
    ASSERT_TRUE(HasGrantForLight(residency, 0, &slot));

    SpotShadowSlotInfo info = residency.SlotInfo(slot);
    EXPECT_TRUE(info.Live);
    EXPECT_TRUE(info.EverRendered);
    EXPECT_FALSE(info.Invalid);
    EXPECT_EQ(info.Owner, MakeKey(1));
    EXPECT_EQ(info.Allocation.Size, 1024u);
    EXPECT_EQ(info.Policy, ShadowUpdatePolicy::OnChange);
    EXPECT_EQ(info.FramesSinceAcquired, 0u);
    EXPECT_EQ(info.FramesSinceRendered, 0u);

    residency.Update(requests, {}, kUnlimited);
    residency.Update(requests, {}, kUnlimited);
    info = residency.SlotInfo(slot);
    EXPECT_EQ(info.FramesSinceAcquired, 2u);
    EXPECT_EQ(info.FramesSinceRendered, 2u);

    // A re-render resets the rendered age but not the acquisition age.
    residency.InvalidateAll();
    residency.Update(requests, {}, kUnlimited);
    info = residency.SlotInfo(slot);
    EXPECT_EQ(info.FramesSinceAcquired, 3u);
    EXPECT_EQ(info.FramesSinceRendered, 0u);

    EXPECT_FALSE(residency.SlotInfo(slot + 1).Live);
    EXPECT_FALSE(residency.SlotInfo(kMaxSpotShadows).Live);
}

TEST(ShadowResidency, ApplyGrantsWiresLightsToRenderedSlotRecords)
{
    ShadowResidency residency;
    std::vector<SpotShadowRequest> requests{
        MakeRequest(1, 0, 3.0f),
        MakeRequest(2, 2, 1.0f),
    };
    residency.Update(requests, {}, kUnlimited);
    ASSERT_EQ(residency.Grants().size(), 2u);

    RenderLightSet lights;
    (void)lights.Add(GpuLight{});
    (void)lights.Add(GpuLight{});
    (void)lights.Add(GpuLight{});
    residency.ApplyGrants(lights);

    std::uint32_t slotForLight0 = UINT32_MAX;
    std::uint32_t slotForLight2 = UINT32_MAX;
    ASSERT_TRUE(HasGrantForLight(residency, 0, &slotForLight0));
    ASSERT_TRUE(HasGrantForLight(residency, 2, &slotForLight2));

    EXPECT_EQ(lights.Lights[0].ShadowIndex, slotForLight0);
    EXPECT_EQ(lights.Lights[1].ShadowIndex, UINT32_MAX);
    EXPECT_EQ(lights.Lights[2].ShadowIndex, slotForLight2);
    EXPECT_EQ(lights.SpotShadowCount, residency.SlotHighWater());
    EXPECT_EQ(lights.SpotShadows[slotForLight0].AtlasScaleBias,
              residency.SlotRecord(slotForLight0).AtlasScaleBias);
    EXPECT_EQ(lights.SpotShadows[slotForLight0].LightIndex, 0u);
}

TEST(ShadowResidency, ApplyPointGrantsWiresLightsToRenderedCubeRecords)
{
    ShadowResidency residency;
    std::vector<PointShadowRequest> points{
        MakePointRequest(1, 1, 3.0f),
    };
    residency.Update({}, points, {}, kUnlimited);
    ASSERT_EQ(residency.PointGrants().size(), 1u);

    RenderLightSet lights;
    (void)lights.Add(GpuLight{});
    (void)lights.Add(GpuLight{});
    residency.ApplyGrants(lights);

    std::uint32_t slot = UINT32_MAX;
    ASSERT_TRUE(HasPointGrantForLight(residency, 1, &slot));
    EXPECT_EQ(lights.Lights[0].ShadowIndex, UINT32_MAX);
    EXPECT_EQ(lights.Lights[1].ShadowIndex, slot);
    EXPECT_EQ(lights.PointShadowCount, residency.PointSlotHighWater());
    EXPECT_EQ(lights.PointShadows[slot].PositionFar,
              residency.PointSlotRecord(slot).PositionFar);
    EXPECT_EQ(lights.PointShadows[slot].Params,
              residency.PointSlotRecord(slot).Params);
    EXPECT_EQ(lights.PointShadows[slot].LightIndex, 1u);
}

// A zone detach destroys its lights, and the renderer sees that only as their
// requests ceasing. The slots they held must not make newly streamed lights wait
// out the full hysteresis run: with four point slots, one lit room fills the pool,
// so every threshold crossing would cost half a second of unshadowed lights.
TEST(ShadowResidency, SlotsHeldByAbsentOwnersDoNotDelayNewGrants)
{
    ShadowResidencyBudgets twoSlots = kUnlimited;
    twoSlots.MaxSlots = 2;

    ShadowResidency residency;
    std::vector<SpotShadowRequest> departing{
        MakeRequest(1, 0, 1.0f),
        MakeRequest(2, 1, 1.0f),
    };
    residency.Update(departing, {}, twoSlots);
    ASSERT_TRUE(HasGrantForLight(residency, 0));
    ASSERT_TRUE(HasGrantForLight(residency, 1));

    // The zone unloads: entities 1 and 2 are gone, entities 3 and 4 stream in.
    std::vector<SpotShadowRequest> arriving{
        MakeRequest(3, 2, 1.0f),
        MakeRequest(4, 3, 1.0f),
    };
    residency.Update(arriving, {}, twoSlots);

    EXPECT_TRUE(HasGrantForLight(residency, 2))
        << "a newly streamed light waited on a slot whose owner no longer exists";
    EXPECT_TRUE(HasGrantForLight(residency, 3));
    EXPECT_EQ(residency.LiveSlotCount(), 2u);
}

// The same property for the point pool, which is the one that actually bites at
// four slots.
TEST(ShadowResidency, PointSlotsHeldByAbsentOwnersDoNotDelayNewGrants)
{
    ShadowResidencyBudgets budgets = kUnlimited;
    budgets.MaxPointSlots = 1;

    ShadowResidency residency;
    std::vector<PointShadowRequest> departing{ MakePointRequest(1, 0, 1.0f) };
    // A point light stays ungranted until all six faces have rendered, so let it
    // settle before taking the slot away.
    for (int frame = 0; frame < 8; ++frame)
        residency.Update({}, departing, {}, budgets);
    ASSERT_TRUE(HasPointGrantForLight(residency, 0));

    std::vector<PointShadowRequest> arriving{ MakePointRequest(2, 1, 1.0f) };
    for (int frame = 0; frame < 8; ++frame)
        residency.Update({}, arriving, {}, budgets);

    EXPECT_TRUE(HasPointGrantForLight(residency, 1))
        << "a streamed point light waited on a slot held by a destroyed owner";
    EXPECT_FALSE(HasPointGrantForLight(residency, 0));
}

// The relaxation must be narrow: a holder that is still asking keeps its full
// hysteresis protection, or equal-importance lights flicker again.
TEST(ShadowResidency, ARequestingHolderKeepsItsHysteresisProtection)
{
    ShadowResidencyBudgets oneSlot = kUnlimited;
    oneSlot.MaxSlots = 1;

    ShadowResidency residency;
    std::vector<SpotShadowRequest> first{ MakeRequest(1, 0, 1.0f) };
    residency.Update(first, {}, oneSlot);
    ASSERT_TRUE(HasGrantForLight(residency, 0));

    std::vector<SpotShadowRequest> contested{
        MakeRequest(2, 1, 5.0f),
        MakeRequest(1, 0, 1.0f),
    };
    for (std::uint32_t frame = 0; frame < ShadowResidency::kStealOutscoredFrames - 1;
         ++frame)
    {
        residency.Update(contested, {}, oneSlot);
        EXPECT_TRUE(HasGrantForLight(residency, 0))
            << "frame " << frame << ": a live holder lost its slot early";
        EXPECT_FALSE(HasGrantForLight(residency, 1));
    }
}

// Retention is the reason absent slots were held in the first place. An absent
// owner with nothing contending must keep its allocation, so a light that is
// briefly culled does not pay a re-render when it returns.
TEST(ShadowResidency, AnUncontestedAbsentSlotRetainsItsCachedContent)
{
    ShadowResidencyBudgets budgets = kUnlimited;
    budgets.MaxSlots = 4;

    ShadowResidency residency;
    std::vector<SpotShadowRequest> present{
        MakeRequest(1, 0, 1.0f, ShadowUpdatePolicy::OnChange),
    };
    residency.Update(present, {}, budgets);
    ASSERT_TRUE(HasGrantForLight(residency, 0));
    const std::uint32_t liveWhilePresent = residency.LiveSlotCount();

    // Culled for a while, with no other light asking for a slot.
    for (int frame = 0; frame < 10; ++frame)
        residency.Update({}, {}, budgets);
    EXPECT_EQ(residency.LiveSlotCount(), liveWhilePresent)
        << "an uncontested slot was dropped, costing a re-render on return";

    // Back in view: still cached, so no view is scheduled for it.
    residency.Update(present, {}, budgets);
    EXPECT_TRUE(HasGrantForLight(residency, 0));
    EXPECT_TRUE(residency.ScheduledViews().empty())
        << "the returning light re-rendered despite its slot being retained";
}

// ─── Characterization: the recorded pool asymmetries ─────────────────────────
//
// The four tests below document CURRENT behavior ahead of the pool
// unification, so the refactor cannot change any of it silently. They do not
// endorse it: the EveryFrame point behavior in particular is the recorded
// defect-1 candidate, and the decision about it lands separately with its
// own failing-first test.

// A moving EveryFrame point light (state hash changing every frame) has its
// face rotation reset to all six every frame by MarkPointInvalid, so under a
// budget clamp the early faces re-render forever, the later faces starve,
// and the grant -- which needs every face rendered against one state -- is
// withheld indefinitely. The spot twin below shows the other pool treats the
// same situation as a no-op.
TEST(ShadowResidency, EveryFramePointHashChurnResetsRotationAndWithholdsTheGrant)
{
    ShadowResidency residency;
    ShadowResidencyBudgets budgets;
    budgets.MinInvalidatedViewsPerFrame = 0;

    // Establish the cube fully rendered and granted with an open budget.
    budgets.MaxViewsPerFrame = 0;
    std::vector<PointShadowRequest> points{
        MakePointRequest(1, 0, 3.0f, ShadowUpdatePolicy::EveryFrame, 1),
    };
    residency.Update({}, points, {}, budgets);
    ASSERT_EQ(residency.PointGrants().size(), 1u);

    // Now clamp to two views and move the light every frame.
    budgets.MaxViewsPerFrame = 2;
    for (std::uint32_t frame = 0; frame < 12; ++frame)
    {
        points[0] = MakePointRequest(1, 0, 3.0f,
                                     ShadowUpdatePolicy::EveryFrame,
                                     100 + frame);
        residency.Update({}, points, {}, budgets);

        ASSERT_EQ(residency.ScheduledPointFaces().size(), 2u);
        // The reset pre-empts the rotation: always faces 0 and 1.
        EXPECT_EQ(residency.ScheduledPointFaces()[0].Face, 0u);
        EXPECT_EQ(residency.ScheduledPointFaces()[1].Face, 1u);
        // Four faces stay stale against the rendered record, so the grant
        // never returns while the light keeps moving.
        EXPECT_TRUE(residency.PointGrants().empty());
    }
}

// The spot pool's predicate skips EveryFrame entirely: the same per-frame
// hash churn neither invalidates nor destabilizes anything, because an
// EveryFrame tile re-renders whole whenever it is scheduled.
TEST(ShadowResidency, EveryFrameSpotHashChurnKeepsItsGrant)
{
    ShadowResidency residency;
    ShadowResidencyBudgets budgets;
    budgets.MaxViewsPerFrame = 1;
    budgets.MinInvalidatedViewsPerFrame = 0;

    std::vector<SpotShadowRequest> spots{
        MakeRequest(1, 0, 3.0f, ShadowUpdatePolicy::EveryFrame, 512, 1),
    };
    residency.Update(spots, {}, {}, budgets);
    ASSERT_TRUE(HasGrantForLight(residency, 0));

    for (std::uint32_t frame = 0; frame < 12; ++frame)
    {
        spots[0] = MakeRequest(1, 0, 3.0f, ShadowUpdatePolicy::EveryFrame,
                               512, 100 + frame);
        residency.Update(spots, {}, {}, budgets);

        EXPECT_EQ(residency.ScheduledViews().size(), 1u);
        EXPECT_TRUE(HasGrantForLight(residency, 0));
        EXPECT_FALSE(residency.SlotInfo(0).Invalid);
    }
}

// An OnChange point light's hash change dirties every face and withholds the
// grant until the whole cube re-renders against the new state -- the point
// predicate's common case, previously untested.
TEST(ShadowResidency, OnChangePointHashChangeRerendersAllSixFaces)
{
    ShadowResidency residency;
    std::vector<PointShadowRequest> points{
        MakePointRequest(1, 0, 3.0f, ShadowUpdatePolicy::OnChange, 1),
    };
    residency.Update({}, points, {}, kUnlimited);
    ASSERT_EQ(residency.PointGrants().size(), 1u);

    // Unchanged state: fully cached, nothing scheduled.
    residency.Update({}, points, {}, kUnlimited);
    EXPECT_TRUE(residency.ScheduledPointFaces().empty());
    EXPECT_EQ(residency.FrameStats().Point.CachedSlots, 1u);

    // Changed state: all six faces re-render; with an open budget the grant
    // returns within the same frame.
    points[0] = MakePointRequest(1, 0, 3.0f, ShadowUpdatePolicy::OnChange, 2);
    residency.Update({}, points, {}, kUnlimited);
    EXPECT_EQ(residency.ScheduledPointFaces().size(), kPointShadowFaceCount);
    EXPECT_EQ(residency.PointGrants().size(), 1u);
}

// Stealing a spot slot whose victim sat invalid behaves exactly like
// stealing a valid one: the new owner re-queues from scratch through the
// never-rendered path and the grant transfers identically. Pinned because
// the acquire paths differ internally (AcquirePointSlot resets the Invalid
// flag, AcquireSlot does not), and the unification must be able to erase
// that difference without moving behavior -- this test is the proof that
// the stale state was unobservable.
TEST(ShadowResidency, StealingAnInvalidHolderMatchesStealingAValidOne)
{
    const auto runScenario = [](bool invalidateVictim)
    {
        ShadowResidency residency;
        ShadowResidencyBudgets budgets;
        budgets.MaxSlots = 2;
        budgets.MaxViewsPerFrame = 0;
        budgets.MinInvalidatedViewsPerFrame = 0;

        // An EveryFrame anchor that soaks the one-view budget later, and the
        // OnChange victim.
        std::vector<SpotShadowRequest> spots{
            MakeRequest(1, 0, 100.0f, ShadowUpdatePolicy::EveryFrame, 512, 1),
            MakeRequest(2, 1, 2.0f, ShadowUpdatePolicy::OnChange, 512, 1),
        };
        residency.Update(spots, {}, {}, budgets);

        // Starve the victim: one view per frame, taken by the anchor. An
        // invalidated victim stays invalid for the whole outscore run.
        budgets.MaxViewsPerFrame = 1;
        if (invalidateVictim)
            spots[1] = MakeRequest(2, 1, 2.0f, ShadowUpdatePolicy::OnChange,
                                   512, 2);

        // The contender outscores the victim until the steal threshold.
        spots.insert(spots.begin() + 1,
                     MakeRequest(3, 2, 50.0f, ShadowUpdatePolicy::OnChange,
                                 512, 7));

        std::vector<std::uint32_t> grantedLights;
        std::vector<std::size_t> viewCounts;
        for (std::uint32_t frame = 0;
             frame < ShadowResidency::kStealOutscoredFrames + 4; ++frame)
        {
            residency.Update(spots, {}, {}, budgets);
            viewCounts.push_back(residency.ScheduledViews().size());
            grantedLights.clear();
            for (const SpotShadowGrant& grant : residency.Grants())
                grantedLights.push_back(grant.LightIndex);
        }
        return std::pair(grantedLights, viewCounts);
    };

    const auto [grantsInvalid, viewsInvalid] = runScenario(true);
    const auto [grantsValid, viewsValid] = runScenario(false);

    // Post-steal, the contender's light holds the grant in both runs.
    EXPECT_EQ(grantsInvalid, (std::vector<std::uint32_t>{ 0u, 2u }));
    EXPECT_EQ(grantsInvalid, grantsValid);
    EXPECT_EQ(viewsInvalid, viewsValid);
}

// ─── The trajectory digest ───────────────────────────────────────────────────
//
// One scripted scenario across both pools -- all three policies, hash drift,
// tier changes, caster events, slot-budget eviction, steals, an absent
// owner, view-budget clamps, InvalidateAll, and recording failures -- with
// every frame's observable output (scheduled views and faces, grants, stats,
// per-slot info) folded into one FNV digest. The pinned constant is the
// refactor gate: each stage of the pool unification must reproduce it
// exactly, and only a deliberate behavior change (with its own failing-first
// test) may re-record it.
//
// The digest folds explicit fields in fixed order, never raw struct bytes
// (padding), and lives only in this test on this toolchain -- the Fnv1a
// header's rule that such digests are never serialized identities holds.

namespace
{
    void FoldFrame(std::uint64_t& digest, const ShadowResidency& residency)
    {
        for (const SpotShadowViewJob& job : residency.ScheduledViews())
        {
            HashFnv1aValue(digest, job.SlotIndex);
            HashFnv1aValue(digest, job.Allocation.X);
            HashFnv1aValue(digest, job.Allocation.Y);
            HashFnv1aValue(digest, job.Allocation.Size);
            HashFnv1aValue(digest, job.ViewProjection);
        }
        for (const PointShadowFaceJob& job : residency.ScheduledPointFaces())
        {
            HashFnv1aValue(digest, job.SlotIndex);
            HashFnv1aValue(digest, job.Face);
            HashFnv1aValue(digest, job.ViewProjection);
        }
        for (const SpotShadowGrant& grant : residency.Grants())
        {
            HashFnv1aValue(digest, grant.LightIndex);
            HashFnv1aValue(digest, grant.SlotIndex);
        }
        for (const PointShadowGrant& grant : residency.PointGrants())
        {
            HashFnv1aValue(digest, grant.LightIndex);
            HashFnv1aValue(digest, grant.SlotIndex);
        }

        const ShadowFrameStats& stats = residency.FrameStats();
        for (const ShadowPoolFrameStats& pool : { stats.Spot, stats.Point })
        {
            HashFnv1aValue(digest, pool.RequestCount);
            HashFnv1aValue(digest, pool.HeldRequests);
            HashFnv1aValue(digest, pool.DeniedRequests);
            HashFnv1aValue(digest, pool.CachedSlots);
        }
        HashFnv1aValue(digest, stats.ViewsScheduled);

        for (std::uint32_t index = 0; index < kMaxSpotShadows; ++index)
        {
            const SpotShadowSlotInfo info = residency.SlotInfo(index);
            HashFnv1aValue(digest, info.Live);
            HashFnv1aValue(digest, info.Owner.Scope);
            HashFnv1aValue(digest, info.Owner.Entity.Index);
            HashFnv1aValue(digest, info.Owner.Entity.Generation);
            HashFnv1aValue(digest, info.Allocation.X);
            HashFnv1aValue(digest, info.Allocation.Y);
            HashFnv1aValue(digest, info.Allocation.Size);
            HashFnv1aValue(digest, info.Policy);
            HashFnv1aValue(digest, info.EverRendered);
            HashFnv1aValue(digest, info.Invalid);
            HashFnv1aValue(digest, info.FramesSinceAcquired);
            HashFnv1aValue(digest, info.FramesSinceRendered);
        }
        for (std::uint32_t index = 0; index < kMaxPointShadows; ++index)
        {
            const PointShadowSlotInfo info = residency.PointSlotInfo(index);
            HashFnv1aValue(digest, info.Live);
            HashFnv1aValue(digest, info.Owner.Scope);
            HashFnv1aValue(digest, info.Owner.Entity.Index);
            HashFnv1aValue(digest, info.Owner.Entity.Generation);
            HashFnv1aValue(digest, info.Policy);
            HashFnv1aValue(digest, info.EverRendered);
            HashFnv1aValue(digest, info.Invalid);
            HashFnv1aValue(digest, info.PendingFaces);
            HashFnv1aValue(digest, info.FramesSinceAcquired);
            HashFnv1aValue(digest, info.FramesSinceRendered);
        }

        // The retained GPU records ApplyGrants copies onto the light set --
        // what cached content is actually sampled with. Without these, a
        // refactor could corrupt a slot's rendered state unseen.
        for (std::uint32_t index = 0; index < kMaxSpotShadows; ++index)
        {
            const SpotShadowView& record = residency.SlotRecord(index);
            HashFnv1aValue(digest, record.ViewProjection);
            HashFnv1aValue(digest, record.AtlasScaleBias);
            HashFnv1aValue(digest, record.SamplingParams);
            HashFnv1aValue(digest, record.LightIndex);
        }
        for (std::uint32_t index = 0; index < kMaxPointShadows; ++index)
        {
            const PointShadowView& record = residency.PointSlotRecord(index);
            HashFnv1aValue(digest, record.PositionFar);
            HashFnv1aValue(digest, record.Params);
            HashFnv1aValue(digest, record.LightIndex);
        }
    }
}

TEST(ShadowResidency, ATrajectoryDigestPinsTheArbiterEndToEnd)
{
    ShadowResidency residency;
    std::uint64_t digest = kFnv1aOffsetBasis;

    for (std::uint32_t frame = 1; frame <= 500; ++frame)
    {
        ShadowResidencyBudgets budgets;
        // Spot slots clamp to 3 so the strong contender must steal, then to
        // 2 so budget enforcement evicts a live holder.
        budgets.MaxSlots = frame >= 300 && frame < 340 ? 2u
                         : frame >= 80 ? 3u
                                       : kMaxSpotShadows;
        budgets.MaxPointSlots = frame >= 180 ? 3u : kMaxPointShadows;
        // A view-budget clamp era, an open era, and a second clamp.
        budgets.MaxViewsPerFrame = frame >= 60 && frame < 200 ? 3u
                                 : frame >= 380 ? 5u
                                                : 0u;
        budgets.MinInvalidatedViewsPerFrame = 1;

        // Requests arrive score-descending, as the real callers pack them.
        std::vector<SpotShadowRequest> spots;
        if (frame >= 100)
            spots.push_back(MakeRequest(4, 3, 50.0f,
                                        ShadowUpdatePolicy::OnChange, 512, 7));
        // The EveryFrame anchor disappears for a stretch (absent owner).
        if (frame < 220 || frame >= 260)
            spots.push_back(MakeRequest(1, 0, 10.0f,
                                        ShadowUpdatePolicy::EveryFrame, 512, 1));
        // OnChange with stepped hash drift and two tier changes.
        spots.push_back(MakeRequest(
            2, 1, 8.0f, ShadowUpdatePolicy::OnChange,
            frame >= 400 ? 256u : frame >= 150 ? 1024u : 512u,
            1 + frame / 30));
        // Static with per-frame hash drift the policy must ignore.
        spots.push_back(MakeRequest(3, 2, 6.0f,
                                    ShadowUpdatePolicy::Static, 512, frame));

        std::vector<PointShadowRequest> points;
        if (frame >= 200)
            points.push_back(MakePointRequest(
                14, 7, 20.0f, ShadowUpdatePolicy::OnChange, 3));
        // EveryFrame point with slow hash drift (the recorded rotation-reset
        // regime), OnChange with stepped drift, Static with per-frame drift.
        points.push_back(MakePointRequest(
            11, 4, 9.0f, ShadowUpdatePolicy::EveryFrame, 1 + frame / 45));
        points.push_back(MakePointRequest(
            12, 5, 7.0f, ShadowUpdatePolicy::OnChange, 1 + frame / 60));
        points.push_back(MakePointRequest(
            13, 6, 5.0f, ShadowUpdatePolicy::Static, frame));

        // Caster churn near the OnChange spot (entity 2, x = 20) and the
        // OnChange point (entity 12, x = 120).
        std::vector<ShadowCasterEvent> events;
        if (frame % 90 == 0)
        {
            events.push_back(ShadowCasterEvent{
                .Change = ShadowCasterEvent::Kind::Changed,
                .Bounds = Aabb3d(Vec3d(19.0f, -1.0f, -1.0f),
                                 Vec3d(21.0f, 1.0f, 1.0f)),
            });
            events.push_back(ShadowCasterEvent{
                .Change = ShadowCasterEvent::Kind::Changed,
                .Bounds = Aabb3d(Vec3d(119.0f, -1.0f, -1.0f),
                                 Vec3d(121.0f, 1.0f, 1.0f)),
            });
        }

        residency.Update(spots, points, events, budgets);

        // Recording failures on scheduled work, and the console invalidate.
        if (frame == 130 && !residency.ScheduledViews().empty())
            residency.MarkViewFailed(residency.ScheduledViews()[0].SlotIndex);
        if (frame == 131 && !residency.ScheduledPointFaces().empty())
            residency.MarkPointFaceFailed(
                residency.ScheduledPointFaces()[0].SlotIndex,
                residency.ScheduledPointFaces()[0].Face);
        if (frame == 250)
            residency.InvalidateAll();

        FoldFrame(digest, residency);
    }

    EXPECT_EQ(digest, 0x3575c087ae5ecb6dULL)
        << "trajectory digest moved: 0x" << std::hex << digest
        << ". A refactor stage must reproduce it exactly; only a deliberate "
           "behavior change re-records it, in the same commit, with the "
           "delta named.";
}
