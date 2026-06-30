#include <gtest/gtest.h>

#include <ecs/World.h>
#include <render/LightExtractionSystem.h>
#include <render/PointLightComponent.h>
#include <render/RenderLight.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>

namespace
{
    EntityId MakeLight(World& world, const Vec<3>& position, const PointLightComponent& light)
    {
        const EntityId entity = world.CreateEntity();
        WorldTransform transform{};
        transform.Value.Position = position;
        world.AddComponent(entity, transform);
        world.AddComponent(entity, light);
        return entity;
    }

    World MakeLightWorld()
    {
        World world;
        world.RegisterComponent<WorldTransform>();
        world.RegisterComponent<PointLightComponent>();
        return world;
    }
}

TEST(LightExtraction, EmitsOnePointLightPerEnabledEntityAtWorldPosition)
{
    World world = MakeLightWorld();

    PointLightComponent a{};
    a.Color = Vec<3>(0.2f, 0.4f, 0.6f);
    a.Intensity = 3.0f;
    a.Range = 12.0f;
    MakeLight(world, Vec<3>(2.0f, 3.0f, 4.0f), a);

    RenderLightSet lights;
    LightExtractionSystem extractor;
    extractor.Extract(world, lights);

    ASSERT_EQ(lights.Count, 1u);
    const GpuLight& g = lights.Lights[0];
    EXPECT_EQ(g.Type, static_cast<std::uint32_t>(GpuLightType::Point));
    EXPECT_EQ(g.ShadowIndex, UINT32_MAX);
    EXPECT_FLOAT_EQ(g.PositionRange.X, 2.0f);
    EXPECT_FLOAT_EQ(g.PositionRange.Y, 3.0f);
    EXPECT_FLOAT_EQ(g.PositionRange.Z, 4.0f);
    EXPECT_FLOAT_EQ(g.PositionRange.W, 12.0f);   // range packs into w
    EXPECT_FLOAT_EQ(g.ColorIntensity.X, 0.2f);
    EXPECT_FLOAT_EQ(g.ColorIntensity.W, 3.0f);   // intensity packs into w
}

TEST(LightExtraction, SkipsDisabledLights)
{
    World world = MakeLightWorld();

    PointLightComponent on{};
    PointLightComponent off{};
    off.Enabled = false;
    MakeLight(world, Vec<3>(0.0f, 0.0f, 0.0f), off);
    MakeLight(world, Vec<3>(1.0f, 0.0f, 0.0f), on);

    RenderLightSet lights;
    LightExtractionSystem extractor;
    extractor.Extract(world, lights);

    EXPECT_EQ(lights.Count, 1u);
    EXPECT_FLOAT_EQ(lights.Lights[0].PositionRange.X, 1.0f);
}

TEST(LightExtraction, ClampsAtTheForwardLightCap)
{
    World world = MakeLightWorld();
    for (std::uint32_t i = 0; i < kMaxForwardLights + 10u; ++i)
        MakeLight(world, Vec<3>(static_cast<float>(i), 0.0f, 0.0f), PointLightComponent{});

    RenderLightSet lights;
    LightExtractionSystem extractor;
    extractor.Extract(world, lights);

    EXPECT_EQ(lights.Count, kMaxForwardLights);
}

TEST(RenderLightSet, AddPointDropsBeyondCapAndResetClearsCount)
{
    RenderLightSet lights;
    for (std::uint32_t i = 0; i < kMaxForwardLights + 5u; ++i)
        lights.AddPoint(Vec<3>(0.0f, 0.0f, 0.0f), PointLightComponent{});
    EXPECT_EQ(lights.Count, kMaxForwardLights);

    lights.Reset();
    EXPECT_EQ(lights.Count, 0u);
}
