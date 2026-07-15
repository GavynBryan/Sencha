#include <gtest/gtest.h>

#include <ecs/Ecs.h>
#include <world/registry/Registry.h>
#include <zone/ZoneRuntime.h>

struct RegistryEpochMarker
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(RegistryEpochMarker, "test.registry_epoch_marker");

TEST(RegistryEpoch, RuntimeRegistryStartsAtOne)
{
    Registry registry = MakeZoneRegistry(RegistryId{ 2, 1 }, ZoneId{ 1 });

    EXPECT_EQ(registry.Components.CurrentFrame(), 1u);
}

TEST(RegistryEpoch, InitialComponentWriteIsVisibleToChangedFilter)
{
    Registry registry = MakeZoneRegistry(RegistryId{ 2, 1 }, ZoneId{ 1 });
    registry.Components.RegisterComponent<RegistryEpochMarker>();

    const EntityId entity = registry.Components.CreateEntity();
    registry.Components.AddComponent(entity, RegistryEpochMarker{ 7 });

    Query<Changed<RegistryEpochMarker>> changed(registry.Components);
    int matchingChunks = 0;
    changed.ForEachChunk([&](auto&) { ++matchingChunks; }, 0);

    EXPECT_EQ(matchingChunks, 1);
}

TEST(RegistryEpoch, ZoneRuntimeAdvancesGlobalActiveAndDormantRegistriesOnce)
{
    ZoneRuntime runtime;
    Registry& active = runtime.CreateZone(ZoneId{ 1 });
    Registry& dormant = runtime.CreateZone(ZoneId{ 2 });
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{
        .Visible = true,
        .Physics = true,
        .Logic = true,
        .Audio = true,
    });

    ASSERT_EQ(runtime.Global().Components.CurrentFrame(), 1u);
    ASSERT_EQ(active.Components.CurrentFrame(), 1u);
    ASSERT_EQ(dormant.Components.CurrentFrame(), 1u);

    runtime.AdvanceFrameEpochs();

    EXPECT_EQ(runtime.Global().Components.CurrentFrame(), 2u);
    EXPECT_EQ(active.Components.CurrentFrame(), 2u);
    EXPECT_EQ(dormant.Components.CurrentFrame(), 2u);

    runtime.AdvanceFrameEpochs();

    EXPECT_EQ(runtime.Global().Components.CurrentFrame(), 3u);
    EXPECT_EQ(active.Components.CurrentFrame(), 3u);
    EXPECT_EQ(dormant.Components.CurrentFrame(), 3u);
}
