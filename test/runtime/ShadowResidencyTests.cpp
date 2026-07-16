#include <gtest/gtest.h>

#include <render/ShadowResidency.h>

#include <algorithm>
#include <vector>

namespace
{
    RenderEntityKey MakeKey(std::uint32_t entityIndex)
    {
        return RenderEntityKey{
            .Kind = RegistryKind::Zone,
            .Zone = ZoneId{ 1 },
            .Entity = EntityId{ entityIndex, 1 },
        };
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
    residency.Update(requests, {}, kUnlimited);
    ASSERT_EQ(residency.ScheduledViews().size(), 1u);
    EXPECT_EQ(residency.ScheduledViews()[0].SlotIndex, slot);
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
    EXPECT_EQ(residency.FrameStats().RequestCount, 3u);
    EXPECT_EQ(residency.FrameStats().HeldRequests, 2u);
    EXPECT_EQ(residency.FrameStats().DeniedRequests, 1u);
    EXPECT_EQ(residency.FrameStats().ViewsScheduled, 2u);
    EXPECT_EQ(residency.FrameStats().CachedSlots, 0u);

    // Steady state: both tiles cache, nothing renders.
    residency.Update(requests, {}, twoSlots);
    EXPECT_EQ(residency.FrameStats().ViewsScheduled, 0u);
    EXPECT_EQ(residency.FrameStats().CachedSlots, 2u);
    EXPECT_EQ(residency.FrameStats().DeniedRequests, 1u);
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
