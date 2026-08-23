#include <gtest/gtest.h>

#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <render/Camera.h>
#include <render/LightExtractionSystem.h>
#include <render/PointLightComponent.h>
#include <render/RenderLight.h>
#include <render/SpotLightComponent.h>
#include <world/transform/TransformComponents.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    CameraRenderData MakeCamera()
    {
        World world;
        world.RegisterComponent<WorldTransform>();
        world.RegisterComponent<CameraComponent>();

        const EntityId entity = world.CreateEntity();
        WorldTransform transform{};
        world.AddComponent(entity, transform);
        world.AddComponent(entity, CameraComponent{});

        ActiveCameraService active;
        active.SetActive(entity);

        CameraRenderData camera;
        EXPECT_TRUE(CameraRenderDataSystem::Build(
            active, world, RenderExtent{ 1280, 720 }, camera));
        return camera;
    }

    constexpr StoragePartitionId kZoneOnePartition{ 1 };
    constexpr StoragePartitionId kZoneTwoPartition{ 2 };

    World MakeLightWorld()
    {
        World world;
        world.RegisterComponent<WorldTransform>();
        world.RegisterComponent<PointLightComponent>();
        world.RegisterComponent<SpotLightComponent>();
        return world;
    }

    EntityId MakePoint(World& world,
                       const Vec<3>& position,
                       const PointLightComponent& light,
                       StoragePartitionId partition = StoragePartitionId::Default())
    {
        const EntityId entity = world.CreateEntity(partition);
        WorldTransform transform{};
        transform.Value.Position = position;
        world.AddComponent(entity, transform);
        world.AddComponent(entity, light);
        return entity;
    }

    EntityId MakeSpot(World& world,
                      const Vec<3>& position,
                      const SpotLightComponent& light,
                      StoragePartitionId partition = StoragePartitionId::Default())
    {
        const EntityId entity = world.CreateEntity(partition);
        WorldTransform transform{};
        transform.Value.Position = position;
        world.AddComponent(entity, transform);
        world.AddComponent(entity, light);
        return entity;
    }

    StoragePartitionSet AllLivePartitions(const World& world)
    {
        StoragePartitionSet partitions;
        partitions.Add(StoragePartitionId::Default());
        for (EntityId entity : world.GetAliveEntities())
            partitions.Add(world.GetEntityPartition(entity));
        return partitions;
    }

    void Extract(const World& world,
                 const StoragePartitionSet& partitions,
                 RenderLightSet& lights,
                 std::vector<SpotShadowRequest>* requestsOut = nullptr,
                 std::vector<PointShadowRequest>* pointRequestsOut = nullptr)
    {
        LightExtractionSystem extractor;
        std::vector<SpotShadowRequest> requests;
        std::vector<PointShadowRequest> pointRequests;
        extractor.Extract(world, partitions, MakeCamera(), lights,
                          requestsOut != nullptr ? *requestsOut : requests,
                          pointRequestsOut != nullptr ? *pointRequestsOut : pointRequests);
    }

    void Extract(const World& world,
                 RenderLightSet& lights,
                 std::vector<SpotShadowRequest>* requestsOut = nullptr,
                 std::vector<PointShadowRequest>* pointRequestsOut = nullptr)
    {
        Extract(world, AllLivePartitions(world), lights, requestsOut,
                pointRequestsOut);
    }
}

TEST(LightExtraction, EmitsPointAndSpotLights)
{
    World world = MakeLightWorld();

    PointLightComponent point{};
    point.Color = Vec<3>(0.2f, 0.4f, 0.6f);
    point.Intensity = 3.0f;
    point.Range = 12.0f;
    MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), point);

    SpotLightComponent spot{};
    spot.Color = Vec<3>(0.9f, 0.7f, 0.5f);
    spot.Intensity = 2.0f;
    spot.Range = 8.0f;
    spot.InnerAngleDegrees = 20.0f;
    spot.OuterAngleDegrees = 35.0f;
    MakeSpot(world, Vec<3>(0.0f, 0.0f, -3.0f), spot);

    RenderLightSet lights;
    Extract(world, lights);

    ASSERT_EQ(lights.Count, 2u);
    EXPECT_EQ(lights.Lights[0].Type, static_cast<std::uint32_t>(GpuLightType::Point));
    EXPECT_FLOAT_EQ(lights.Lights[0].ColorIntensity.W, 3.0f);

    const GpuLight& packedSpot = lights.Lights[1];
    EXPECT_EQ(packedSpot.Type, static_cast<std::uint32_t>(GpuLightType::Spot));
    EXPECT_FLOAT_EQ(packedSpot.DirectionCone.X, 0.0f);
    EXPECT_FLOAT_EQ(packedSpot.DirectionCone.Y, 0.0f);
    EXPECT_FLOAT_EQ(packedSpot.DirectionCone.Z, -1.0f);
    EXPECT_GT(packedSpot.ConeScale, 0.0f);
    EXPECT_LT(packedSpot.ConeOffset, 0.0f);
}

TEST(LightExtraction, SkipsDisabledAndCulledLights)
{
    World world = MakeLightWorld();

    PointLightComponent disabled{};
    disabled.Enabled = false;
    MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), disabled);

    PointLightComponent outside{};
    outside.Range = 0.5f;
    MakePoint(world, Vec<3>(100.0f, 0.0f, -2.0f), outside);

    PointLightComponent visible{};
    visible.Intensity = 4.0f;
    MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), visible);

    RenderLightSet lights;
    Extract(world, lights);

    ASSERT_EQ(lights.Count, 1u);
    EXPECT_FLOAT_EQ(lights.Lights[0].ColorIntensity.W, 4.0f);
}

TEST(LightExtraction, SkipsLightsOutsideTheActivePartitions)
{
    World world = MakeLightWorld();

    PointLightComponent resident{};
    resident.Intensity = 4.0f;
    MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), resident, kZoneOnePartition);

    PointLightComponent dormant{};
    dormant.Intensity = 9.0f;
    MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), dormant, kZoneTwoPartition);

    StoragePartitionSet partitions;
    partitions.Add(StoragePartitionId::Default());
    partitions.Add(kZoneOnePartition);

    RenderLightSet lights;
    Extract(world, partitions, lights);

    ASSERT_EQ(lights.Count, 1u);
    EXPECT_FLOAT_EQ(lights.Lights[0].ColorIntensity.W, 4.0f);
}

TEST(LightExtraction, PacksBakedDirectLightsFlaggedAfterLiveOnes)
{
    World world = MakeLightWorld();

    // A baked-direct light stays in the forward set so movers receive it
    // live; it carries the baked bit for the shader's charted-receiver skip,
    // packs after every live light regardless of score, and never requests
    // a shadow slot.
    PointLightComponent bakedPoint{};
    bakedPoint.Intensity = 5.0f;
    bakedPoint.CastShadows = true;
    bakedPoint.BakeContribution = LightBakeContribution::Direct;
    MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), bakedPoint);

    SpotLightComponent bakedSpot{};
    bakedSpot.Intensity = 5.0f;
    bakedSpot.CastShadows = true;
    bakedSpot.BakeContribution = LightBakeContribution::Direct;
    MakeSpot(world, Vec<3>(0.0f, 0.0f, -3.0f), bakedSpot);

    PointLightComponent dynamicPoint{};
    dynamicPoint.Intensity = 4.0f;
    MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), dynamicPoint);

    RenderLightSet lights;
    std::vector<SpotShadowRequest> requests;
    std::vector<PointShadowRequest> pointRequests;
    Extract(world, lights, &requests, &pointRequests);

    ASSERT_EQ(lights.Count, 3u);
    // The dimmer live light packs first; the brighter baked pair follows.
    EXPECT_EQ(lights.Lights[0].Type & 0x80000000u, 0u);
    EXPECT_FLOAT_EQ(lights.Lights[0].ColorIntensity.W, 4.0f);
    EXPECT_NE(lights.Lights[1].Type & 0x80000000u, 0u);
    EXPECT_NE(lights.Lights[2].Type & 0x80000000u, 0u);
    // CastShadows on a baked light is a bake fact, not a slot request.
    EXPECT_TRUE(requests.empty());
    EXPECT_TRUE(pointRequests.empty());
}

TEST(LightExtraction, PrioritizesInfluentialLightsBeforeTheCap)
{
    World world = MakeLightWorld();
    constexpr std::uint32_t candidateCount = kMaxForwardLights + 10u;
    for (std::uint32_t index = 0; index < candidateCount; ++index)
    {
        PointLightComponent light{};
        light.Intensity = static_cast<float>(index + 1u);
        MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), light);
    }

    RenderLightSet lights;
    Extract(world, lights);

    ASSERT_EQ(lights.Count, kMaxForwardLights);
    EXPECT_FLOAT_EQ(lights.Lights[0].ColorIntensity.W,
                    static_cast<float>(candidateCount));
    EXPECT_FLOAT_EQ(lights.Lights[kMaxForwardLights - 1u].ColorIntensity.W, 11.0f);
}

TEST(LightExtraction, PartitionSetOrderDoesNotChangeTieBreaks)
{
    World world = MakeLightWorld();

    PointLightComponent red{};
    red.Color = Vec<3>(1.0f, 0.0f, 0.0f);
    MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), red, kZoneOnePartition);

    PointLightComponent blue{};
    blue.Color = Vec<3>(0.0f, 0.0f, 1.0f);
    MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), blue, kZoneTwoPartition);

    StoragePartitionSet forwardOrder;
    forwardOrder.Add(kZoneTwoPartition);
    forwardOrder.Add(kZoneOnePartition);

    StoragePartitionSet reverseOrder;
    reverseOrder.Add(kZoneOnePartition);
    reverseOrder.Add(kZoneTwoPartition);

    RenderLightSet forward;
    Extract(world, forwardOrder, forward);

    RenderLightSet reverse;
    Extract(world, reverseOrder, reverse);

    ASSERT_EQ(forward.Count, 2u);
    ASSERT_EQ(reverse.Count, 2u);
    EXPECT_FLOAT_EQ(forward.Lights[0].ColorIntensity.X, 1.0f);
    EXPECT_FLOAT_EQ(reverse.Lights[0].ColorIntensity.X, 1.0f);
    EXPECT_EQ(forward.Lights[0].ColorIntensity, reverse.Lights[0].ColorIntensity);
    EXPECT_EQ(forward.Lights[1].ColorIntensity, reverse.Lights[1].ColorIntensity);
}

TEST(LightExtraction, PartitionSetOrderDoesNotChangeShadowRequestsOrGrants)
{
    World world = MakeLightWorld();

    SpotLightComponent red{};
    red.Color = Vec<3>(1.0f, 0.0f, 0.0f);
    red.CastShadows = true;
    MakeSpot(world, Vec<3>(0.0f, 0.0f, -2.0f), red, kZoneOnePartition);

    SpotLightComponent blue{};
    blue.Color = Vec<3>(0.0f, 0.0f, 1.0f);
    blue.CastShadows = true;
    MakeSpot(world, Vec<3>(0.0f, 0.0f, -2.0f), blue, kZoneTwoPartition);

    StoragePartitionSet forwardOrder;
    forwardOrder.Add(kZoneTwoPartition);
    forwardOrder.Add(kZoneOnePartition);

    StoragePartitionSet reverseOrder;
    reverseOrder.Add(kZoneOnePartition);
    reverseOrder.Add(kZoneTwoPartition);

    RenderLightSet forward;
    std::vector<SpotShadowRequest> forwardRequests;
    Extract(world, forwardOrder, forward, &forwardRequests);

    RenderLightSet reverse;
    std::vector<SpotShadowRequest> reverseRequests;
    Extract(world, reverseOrder, reverse, &reverseRequests);

    ASSERT_EQ(forwardRequests.size(), 2u);
    ASSERT_EQ(reverseRequests.size(), 2u);
    for (std::uint32_t index = 0; index < 2u; ++index)
    {
        EXPECT_EQ(forwardRequests[index].Key, reverseRequests[index].Key);
        EXPECT_EQ(forwardRequests[index].LightIndex, reverseRequests[index].LightIndex);
        EXPECT_EQ(forwardRequests[index].StateHash, reverseRequests[index].StateHash);
    }

    // Identical request sequences produce identical residency decisions.
    ShadowResidency forwardResidency;
    ShadowResidency reverseResidency;
    const ShadowResidencyBudgets budgets{};
    forwardResidency.Update(forwardRequests, {}, budgets);
    reverseResidency.Update(reverseRequests, {}, budgets);

    ASSERT_EQ(forwardResidency.Grants().size(), 2u);
    ASSERT_EQ(reverseResidency.Grants().size(), 2u);
    for (std::size_t index = 0; index < 2u; ++index)
    {
        EXPECT_EQ(forwardResidency.Grants()[index].LightIndex,
                  reverseResidency.Grants()[index].LightIndex);
        EXPECT_EQ(forwardResidency.Grants()[index].SlotIndex,
                  reverseResidency.Grants()[index].SlotIndex);
        EXPECT_EQ(forwardResidency.SlotRecord(index).AtlasScaleBias,
                  reverseResidency.SlotRecord(index).AtlasScaleBias);
    }
}

TEST(LightExtraction, ResidencyGrantsAtMostTheSlotBudget)
{
    World world = MakeLightWorld();
    constexpr std::uint32_t candidateCount = kMaxSpotShadows + 3u;
    for (std::uint32_t index = 0; index < candidateCount; ++index)
    {
        SpotLightComponent light{};
        light.CastShadows = true;
        MakeSpot(world, Vec<3>(0.0f, 0.0f, -2.0f), light);
    }

    RenderLightSet lights;
    std::vector<SpotShadowRequest> requests;
    Extract(world, lights, &requests);

    // Extraction emits every packed request; the budget belongs to residency.
    ASSERT_EQ(lights.Count, candidateCount);
    ASSERT_EQ(requests.size(), candidateCount);

    ShadowResidency residency;
    residency.Update(requests, {}, ShadowResidencyBudgets{});
    for (const SpotShadowGrant& grant : residency.Grants())
        lights.Lights[grant.LightIndex].ShadowIndex = grant.SlotIndex;

    ASSERT_EQ(residency.Grants().size(), kMaxSpotShadows);
    for (std::uint32_t index = 0; index < kMaxSpotShadows; ++index)
        EXPECT_EQ(lights.Lights[index].ShadowIndex, index);
    for (std::uint32_t index = kMaxSpotShadows; index < candidateCount; ++index)
        EXPECT_EQ(lights.Lights[index].ShadowIndex, UINT32_MAX);
}

TEST(LightExtraction, PacksSpotShadowSamplingScaleAndClampsSoftness)
{
    World world = MakeLightWorld();
    SpotLightComponent spot{};
    spot.Range = 20.0f;
    spot.OuterAngleDegrees = 45.0f;
    spot.CastShadows = true;
    spot.ShadowSoftness = 3.0f;
    spot.ShadowBiasScale = 1.75f;
    MakeSpot(world, Vec<3>(0.0f, 0.0f, -2.0f), spot);

    RenderLightSet lights;
    lights.ShadowSoftness = 2.0f;
    std::vector<SpotShadowRequest> requests;
    Extract(world, lights, &requests);

    ASSERT_EQ(requests.size(), 1u);
    const Vec4& params = requests[0].SamplingParams;
    EXPECT_NEAR(params.X, 40.0f / static_cast<float>(kSpotShadowInnerExtent), 1.0e-6f);
    EXPECT_FLOAT_EQ(params.Y, kSpotShadowSoftnessMaxTexels);
    EXPECT_FLOAT_EQ(params.Z, 1.75f);
    EXPECT_FLOAT_EQ(params.W, 0.0f);
    EXPECT_EQ(requests[0].TileSize,
              static_cast<std::uint32_t>(ShadowResolutionTier::Medium));
    EXPECT_EQ(requests[0].Policy, ShadowUpdatePolicy::OnChange);
}

TEST(LightExtraction, PacksPointShadowRequestAndClampsSamplingState)
{
    World world = MakeLightWorld();
    PointLightComponent point{};
    point.Range = 20.0f;
    point.CastShadows = true;
    point.ShadowUpdate = ShadowUpdatePolicy::EveryFrame;
    point.ShadowSoftness = 3.0f;
    point.ShadowBiasScale = 1.75f;
    MakePoint(world, Vec<3>(1.0f, 2.0f, -3.0f), point);

    RenderLightSet lights;
    lights.ShadowSoftness = 2.0f;
    std::vector<SpotShadowRequest> spotRequests;
    std::vector<PointShadowRequest> pointRequests;
    Extract(world, lights, &spotRequests, &pointRequests);

    EXPECT_TRUE(spotRequests.empty());
    ASSERT_EQ(pointRequests.size(), 1u);
    EXPECT_EQ(pointRequests[0].LightIndex, 0u);
    EXPECT_EQ(pointRequests[0].Policy, ShadowUpdatePolicy::EveryFrame);
    EXPECT_EQ(pointRequests[0].View.PositionFar, Vec4(1.0f, 2.0f, -3.0f, 20.0f));
    EXPECT_FLOAT_EQ(pointRequests[0].View.Params.X, 0.4f);
    EXPECT_FLOAT_EQ(pointRequests[0].View.Params.Y, kSpotShadowSoftnessMaxTexels);
    EXPECT_FLOAT_EQ(pointRequests[0].View.Params.Z, 1.75f);
    EXPECT_TRUE(pointRequests[0].Bounds.Contains(Vec<3>(1.0f, 2.0f, -3.0f)));
}

TEST(LightExtraction, PartitionSetOrderDoesNotChangePointShadowGrants)
{
    World world = MakeLightWorld();

    PointLightComponent red{};
    red.Color = Vec<3>(1.0f, 0.0f, 0.0f);
    red.CastShadows = true;
    MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), red, kZoneOnePartition);

    PointLightComponent blue{};
    blue.Color = Vec<3>(0.0f, 0.0f, 1.0f);
    blue.CastShadows = true;
    MakePoint(world, Vec<3>(0.0f, 0.0f, -2.0f), blue, kZoneTwoPartition);

    StoragePartitionSet forwardOrder;
    forwardOrder.Add(kZoneTwoPartition);
    forwardOrder.Add(kZoneOnePartition);

    StoragePartitionSet reverseOrder;
    reverseOrder.Add(kZoneOnePartition);
    reverseOrder.Add(kZoneTwoPartition);

    RenderLightSet forward;
    std::vector<SpotShadowRequest> forwardSpots;
    std::vector<PointShadowRequest> forwardPoints;
    Extract(world, forwardOrder, forward, &forwardSpots, &forwardPoints);

    RenderLightSet reverse;
    std::vector<SpotShadowRequest> reverseSpots;
    std::vector<PointShadowRequest> reversePoints;
    Extract(world, reverseOrder, reverse, &reverseSpots, &reversePoints);

    ASSERT_EQ(forwardPoints.size(), 2u);
    ASSERT_EQ(reversePoints.size(), 2u);
    for (std::uint32_t index = 0; index < 2u; ++index)
    {
        EXPECT_EQ(forwardPoints[index].Key, reversePoints[index].Key);
        EXPECT_EQ(forwardPoints[index].LightIndex, reversePoints[index].LightIndex);
        EXPECT_EQ(forwardPoints[index].StateHash, reversePoints[index].StateHash);
    }

    ShadowResidency forwardResidency;
    ShadowResidency reverseResidency;
    forwardResidency.Update({}, forwardPoints, {}, ShadowResidencyBudgets{});
    reverseResidency.Update({}, reversePoints, {}, ShadowResidencyBudgets{});
    ASSERT_EQ(forwardResidency.PointGrants().size(), 2u);
    ASSERT_EQ(reverseResidency.PointGrants().size(), 2u);
    for (std::size_t index = 0; index < 2u; ++index)
    {
        EXPECT_EQ(forwardResidency.PointGrants()[index].LightIndex,
                  reverseResidency.PointGrants()[index].LightIndex);
        EXPECT_EQ(forwardResidency.PointGrants()[index].SlotIndex,
                  reverseResidency.PointGrants()[index].SlotIndex);
    }
}

TEST(PointShadowProjection, FaceAxesMapToFaceCenters)
{
    const std::array<Vec<3>, kPointShadowFaceCount> axes = {
        Vec<3>(1.0f, 0.0f, 0.0f),
        Vec<3>(-1.0f, 0.0f, 0.0f),
        Vec<3>(0.0f, 1.0f, 0.0f),
        Vec<3>(0.0f, -1.0f, 0.0f),
        Vec<3>(0.0f, 0.0f, 1.0f),
        Vec<3>(0.0f, 0.0f, -1.0f),
    };
    const Vec<3> origin(3.0f, 4.0f, 5.0f);
    for (std::uint32_t face = 0; face < kPointShadowFaceCount; ++face)
    {
        const Mat4 viewProjection = MakePointShadowFaceViewProjection(
            origin, face, 0.1f, 10.0f);
        const Vec<3> point = origin + axes[face] * 5.0f;
        const Vec4 clip = viewProjection * Vec4(point.X, point.Y, point.Z, 1.0f);
        ASSERT_GT(clip.W, 0.0f);
        EXPECT_NEAR(clip.X / clip.W, 0.0f, 1.0e-5f);
        EXPECT_NEAR(clip.Y / clip.W, 0.0f, 1.0e-5f);
        EXPECT_GT(clip.Z / clip.W, 0.0f);
        EXPECT_LT(clip.Z / clip.W, 1.0f);
    }
}

TEST(SpotShadowAtlas, FilterReachStaysInsideTheGuardBand)
{
    const auto derivedReach = static_cast<std::uint32_t>(
        std::ceil(1.5f * kSpotShadowSoftnessMaxTexels)) + 1u;
    EXPECT_EQ(kSpotShadowFilterReachTexels, derivedReach);
    EXPECT_LT(kSpotShadowFilterReachTexels, kSpotShadowGuardTexels);
}

TEST(RenderLightSet, AddDropsBeyondCapAndResetClearsCount)
{
    RenderLightSet lights;
    for (std::uint32_t index = 0; index < kMaxForwardLights + 5u; ++index)
        lights.AddPoint(Vec<3>(0.0f, 0.0f, 0.0f), PointLightComponent{});
    EXPECT_EQ(lights.Count, kMaxForwardLights);

    lights.Reset();
    EXPECT_EQ(lights.Count, 0u);
}
