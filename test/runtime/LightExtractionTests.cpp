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

    void Extract(std::vector<Registry*>& registries, RenderLightSet& lights)
    {
        LightExtractionSystem extractor;
        extractor.Extract(registries, MakeCamera(), lights);
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

TEST(RenderLightSet, AddDropsBeyondCapAndResetClearsCount)
{
    RenderLightSet lights;
    for (std::uint32_t index = 0; index < kMaxForwardLights + 5u; ++index)
        lights.AddPoint(Vec<3>(0.0f, 0.0f, 0.0f), PointLightComponent{});
    EXPECT_EQ(lights.Count, kMaxForwardLights);

    lights.Reset();
    EXPECT_EQ(lights.Count, 0u);
}
