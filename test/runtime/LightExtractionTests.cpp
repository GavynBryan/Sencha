#include <gtest/gtest.h>

#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <render/Camera.h>
#include <render/LightExtractionSystem.h>
#include <render/PointLightComponent.h>
#include <render/RenderLight.h>
#include <render/SpotLightComponent.h>
#include <world/registry/Registry.h>
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
            active, world, VkExtent2D{ 1280, 720 }, camera));
        return camera;
    }

    Registry MakeLightRegistry(RegistryId id, ZoneId zone = {})
    {
        Registry registry = zone.IsValid()
            ? MakeZoneRegistry(id, zone)
            : MakeGlobalRegistry(id);
        registry.Components.RegisterComponent<WorldTransform>();
        registry.Components.RegisterComponent<PointLightComponent>();
        registry.Components.RegisterComponent<SpotLightComponent>();
        return registry;
    }

    EntityId MakePoint(Registry& registry,
                       const Vec<3>& position,
                       const PointLightComponent& light)
    {
        const EntityId entity = registry.Components.CreateEntity();
        WorldTransform transform{};
        transform.Value.Position = position;
        registry.Components.AddComponent(entity, transform);
        registry.Components.AddComponent(entity, light);
        return entity;
    }

    EntityId MakeSpot(Registry& registry,
                      const Vec<3>& position,
                      const SpotLightComponent& light)
    {
        const EntityId entity = registry.Components.CreateEntity();
        WorldTransform transform{};
        transform.Value.Position = position;
        registry.Components.AddComponent(entity, transform);
        registry.Components.AddComponent(entity, light);
        return entity;
    }

    void Extract(std::vector<Registry*>& registries,
                 RenderLightSet& lights,
                 std::vector<SpotShadowRequest>* requestsOut = nullptr,
                 std::vector<PointShadowRequest>* pointRequestsOut = nullptr)
    {
        LightExtractionSystem extractor;
        std::vector<SpotShadowRequest> requests;
        std::vector<PointShadowRequest> pointRequests;
        extractor.Extract(registries, MakeCamera(), lights,
                          requestsOut != nullptr ? *requestsOut : requests,
                          pointRequestsOut != nullptr ? *pointRequestsOut : pointRequests);
    }
}

TEST(LightExtraction, EmitsPointAndSpotLights)
{
    Registry registry = MakeLightRegistry(RegistryId::Global());

    PointLightComponent point{};
    point.Color = Vec<3>(0.2f, 0.4f, 0.6f);
    point.Intensity = 3.0f;
    point.Range = 12.0f;
    MakePoint(registry, Vec<3>(0.0f, 0.0f, -2.0f), point);

    SpotLightComponent spot{};
    spot.Color = Vec<3>(0.9f, 0.7f, 0.5f);
    spot.Intensity = 2.0f;
    spot.Range = 8.0f;
    spot.InnerAngleDegrees = 20.0f;
    spot.OuterAngleDegrees = 35.0f;
    MakeSpot(registry, Vec<3>(0.0f, 0.0f, -3.0f), spot);

    RenderLightSet lights;
    std::vector<Registry*> registries{ &registry };
    Extract(registries, lights);

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
    Registry registry = MakeLightRegistry(RegistryId::Global());

    PointLightComponent disabled{};
    disabled.Enabled = false;
    MakePoint(registry, Vec<3>(0.0f, 0.0f, -2.0f), disabled);

    PointLightComponent outside{};
    outside.Range = 0.5f;
    MakePoint(registry, Vec<3>(100.0f, 0.0f, -2.0f), outside);

    PointLightComponent visible{};
    visible.Intensity = 4.0f;
    MakePoint(registry, Vec<3>(0.0f, 0.0f, -2.0f), visible);

    RenderLightSet lights;
    std::vector<Registry*> registries{ &registry };
    Extract(registries, lights);

    ASSERT_EQ(lights.Count, 1u);
    EXPECT_FLOAT_EQ(lights.Lights[0].ColorIntensity.W, 4.0f);
}

TEST(LightExtraction, PrioritizesInfluentialLightsBeforeTheCap)
{
    Registry registry = MakeLightRegistry(RegistryId::Global());
    constexpr std::uint32_t candidateCount = kMaxForwardLights + 10u;
    for (std::uint32_t index = 0; index < candidateCount; ++index)
    {
        PointLightComponent light{};
        light.Intensity = static_cast<float>(index + 1u);
        MakePoint(registry, Vec<3>(0.0f, 0.0f, -2.0f), light);
    }

    RenderLightSet lights;
    std::vector<Registry*> registries{ &registry };
    Extract(registries, lights);

    ASSERT_EQ(lights.Count, kMaxForwardLights);
    EXPECT_FLOAT_EQ(lights.Lights[0].ColorIntensity.W,
                    static_cast<float>(candidateCount));
    EXPECT_FLOAT_EQ(lights.Lights[kMaxForwardLights - 1u].ColorIntensity.W, 11.0f);
}

TEST(LightExtraction, ZoneAttachmentOrderDoesNotChangeTieBreaks)
{
    Registry zoneTwo = MakeLightRegistry(RegistryId{ 2, 1 }, ZoneId{ 2 });
    Registry zoneOne = MakeLightRegistry(RegistryId{ 3, 1 }, ZoneId{ 1 });

    PointLightComponent blue{};
    blue.Color = Vec<3>(0.0f, 0.0f, 1.0f);
    MakePoint(zoneTwo, Vec<3>(0.0f, 0.0f, -2.0f), blue);

    PointLightComponent red{};
    red.Color = Vec<3>(1.0f, 0.0f, 0.0f);
    MakePoint(zoneOne, Vec<3>(0.0f, 0.0f, -2.0f), red);

    RenderLightSet forward;
    std::vector<Registry*> forwardOrder{ &zoneTwo, &zoneOne };
    Extract(forwardOrder, forward);

    RenderLightSet reverse;
    std::vector<Registry*> reverseOrder{ &zoneOne, &zoneTwo };
    Extract(reverseOrder, reverse);

    ASSERT_EQ(forward.Count, 2u);
    ASSERT_EQ(reverse.Count, 2u);
    EXPECT_FLOAT_EQ(forward.Lights[0].ColorIntensity.X, 1.0f);
    EXPECT_FLOAT_EQ(reverse.Lights[0].ColorIntensity.X, 1.0f);
    EXPECT_EQ(forward.Lights[0].ColorIntensity, reverse.Lights[0].ColorIntensity);
    EXPECT_EQ(forward.Lights[1].ColorIntensity, reverse.Lights[1].ColorIntensity);
}

TEST(LightExtraction, ZoneAttachmentOrderDoesNotChangeShadowRequestsOrGrants)
{
    Registry zoneTwo = MakeLightRegistry(RegistryId{ 2, 1 }, ZoneId{ 2 });
    Registry zoneOne = MakeLightRegistry(RegistryId{ 3, 1 }, ZoneId{ 1 });

    SpotLightComponent blue{};
    blue.Color = Vec<3>(0.0f, 0.0f, 1.0f);
    blue.CastShadows = true;
    MakeSpot(zoneTwo, Vec<3>(0.0f, 0.0f, -2.0f), blue);

    SpotLightComponent red{};
    red.Color = Vec<3>(1.0f, 0.0f, 0.0f);
    red.CastShadows = true;
    MakeSpot(zoneOne, Vec<3>(0.0f, 0.0f, -2.0f), red);

    RenderLightSet forward;
    std::vector<SpotShadowRequest> forwardRequests;
    std::vector<Registry*> forwardOrder{ &zoneTwo, &zoneOne };
    Extract(forwardOrder, forward, &forwardRequests);

    RenderLightSet reverse;
    std::vector<SpotShadowRequest> reverseRequests;
    std::vector<Registry*> reverseOrder{ &zoneOne, &zoneTwo };
    Extract(reverseOrder, reverse, &reverseRequests);

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
    Registry registry = MakeLightRegistry(RegistryId::Global());
    constexpr std::uint32_t candidateCount = kMaxSpotShadows + 3u;
    for (std::uint32_t index = 0; index < candidateCount; ++index)
    {
        SpotLightComponent light{};
        light.CastShadows = true;
        MakeSpot(registry, Vec<3>(0.0f, 0.0f, -2.0f), light);
    }

    RenderLightSet lights;
    std::vector<SpotShadowRequest> requests;
    std::vector<Registry*> registries{ &registry };
    Extract(registries, lights, &requests);

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
    Registry registry = MakeLightRegistry(RegistryId::Global());
    SpotLightComponent spot{};
    spot.Range = 20.0f;
    spot.OuterAngleDegrees = 45.0f;
    spot.CastShadows = true;
    spot.ShadowSoftness = 3.0f;
    spot.ShadowBiasScale = 1.75f;
    MakeSpot(registry, Vec<3>(0.0f, 0.0f, -2.0f), spot);

    RenderLightSet lights;
    lights.ShadowSoftness = 2.0f;
    std::vector<SpotShadowRequest> requests;
    std::vector<Registry*> registries{ &registry };
    Extract(registries, lights, &requests);

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
    Registry registry = MakeLightRegistry(RegistryId::Global());
    PointLightComponent point{};
    point.Range = 20.0f;
    point.CastShadows = true;
    point.ShadowUpdate = ShadowUpdatePolicy::EveryFrame;
    point.ShadowSoftness = 3.0f;
    point.ShadowBiasScale = 1.75f;
    MakePoint(registry, Vec<3>(1.0f, 2.0f, -3.0f), point);

    RenderLightSet lights;
    lights.ShadowSoftness = 2.0f;
    std::vector<SpotShadowRequest> spotRequests;
    std::vector<PointShadowRequest> pointRequests;
    std::vector<Registry*> registries{ &registry };
    Extract(registries, lights, &spotRequests, &pointRequests);

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

TEST(LightExtraction, ZoneAttachmentOrderDoesNotChangePointShadowGrants)
{
    Registry zoneTwo = MakeLightRegistry(RegistryId{ 2, 1 }, ZoneId{ 2 });
    Registry zoneOne = MakeLightRegistry(RegistryId{ 3, 1 }, ZoneId{ 1 });

    PointLightComponent blue{};
    blue.Color = Vec<3>(0.0f, 0.0f, 1.0f);
    blue.CastShadows = true;
    MakePoint(zoneTwo, Vec<3>(0.0f, 0.0f, -2.0f), blue);

    PointLightComponent red{};
    red.Color = Vec<3>(1.0f, 0.0f, 0.0f);
    red.CastShadows = true;
    MakePoint(zoneOne, Vec<3>(0.0f, 0.0f, -2.0f), red);

    RenderLightSet forward;
    std::vector<SpotShadowRequest> forwardSpots;
    std::vector<PointShadowRequest> forwardPoints;
    std::vector<Registry*> forwardOrder{ &zoneTwo, &zoneOne };
    Extract(forwardOrder, forward, &forwardSpots, &forwardPoints);

    RenderLightSet reverse;
    std::vector<SpotShadowRequest> reverseSpots;
    std::vector<PointShadowRequest> reversePoints;
    std::vector<Registry*> reverseOrder{ &zoneOne, &zoneTwo };
    Extract(reverseOrder, reverse, &reverseSpots, &reversePoints);

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
