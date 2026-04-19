#include <gtest/gtest.h>
#include <app/Engine.h>
#include <app/TransformPropagationPass.h>
#include <runtime/RenderPacket.h>
#include <zone/DefaultZoneBuilder.h>
#include <zone/ZoneRuntime.h>

namespace
{
    struct PropagationHarness
    {
        EngineConfig Config;
        Engine EngineInstance{ Config };
        RuntimeFrameLoop Runtime;
        InputFrame Input;
        RenderPacketDoubleBuffer Packets;
        ZoneRuntime Zones;
    };
}

TEST(TransformPropagationPass, PostFixedPropagatesLogicRegistryTransforms)
{
    PropagationHarness harness;
    Registry& registry = CreateDefault3DZone(
        harness.Zones, ZoneId{ 1 }, ZoneParticipation{ .Logic = true });
    EntityId entity = CreateDefaultEntity(registry);
    auto& transforms = registry.Components.Get<TransformStore<Transform3f>>();
    transforms.SetLocal(entity, Transform3f(Vec3d(3.0f, 4.0f, 5.0f), Quatf::Identity(), Vec3d::One()));

    FrameRegistryView view = harness.Zones.BuildFrameView();
    PostFixedContext ctx{
        .EngineInstance = harness.EngineInstance,
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .Time = {},
        .Registries = view,
        .ActiveRegistries = view.Logic,
    };

    TransformPropagationPass pass;
    pass.PostFixed(ctx);

    const Transform3f* world = transforms.TryGetWorld(entity);
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(world->Position, Vec3d(3.0f, 4.0f, 5.0f));
}

TEST(TransformPropagationPass, ExtractRenderPropagatesVisibleRegistryTransforms)
{
    PropagationHarness harness;
    Registry& registry = CreateDefault3DZone(
        harness.Zones, ZoneId{ 1 }, ZoneParticipation{ .Visible = true });
    EntityId entity = CreateDefaultEntity(registry);
    auto& transforms = registry.Components.Get<TransformStore<Transform3f>>();
    transforms.SetLocal(entity, Transform3f(Vec3d(7.0f, 8.0f, 9.0f), Quatf::Identity(), Vec3d::One()));

    FrameRegistryView view = harness.Zones.BuildFrameView();
    RenderExtractContext ctx{
        .EngineInstance = harness.EngineInstance,
        .Config = harness.Config,
        .Runtime = harness.Runtime,
        .Input = harness.Input,
        .PacketWrite = harness.Packets.WriteSlot(),
        .PacketRead = harness.Packets.ReadSlot(),
        .Presentation = {},
        .Registries = view,
        .ActiveRegistries = view.Visible,
    };

    TransformPropagationPass pass;
    pass.ExtractRender(ctx);

    const Transform3f* world = transforms.TryGetWorld(entity);
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(world->Position, Vec3d(7.0f, 8.0f, 9.0f));
}
