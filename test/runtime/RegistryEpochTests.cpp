#include <gtest/gtest.h>

#include <ecs/Ecs.h>
#include <world/registry/Registry.h>
#include <zone/ZoneRuntime.h>

#include <type_traits>

struct RegistryEpochMarker
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(RegistryEpochMarker, "test.registry_epoch_marker");

static_assert(!std::is_copy_constructible_v<Registry>);
static_assert(!std::is_copy_assignable_v<Registry>);
static_assert(!std::is_move_constructible_v<Registry>);
static_assert(!std::is_move_assignable_v<Registry>);

TEST(RegistryIdentity, DefaultRegistryIsTransient)
{
    Registry registry;

    EXPECT_EQ(registry.Kind, RegistryKind::Transient);
    EXPECT_FALSE(registry.Id.IsValid());
    EXPECT_FALSE(registry.Zone.IsValid());
}

TEST(RegistryEpoch, RuntimeRegistryStartsAtOne)
{
    Registry registry(RegistryId{ 2, 1 }, RegistryKind::Zone, ZoneId{ 1 });

    EXPECT_EQ(registry.Entities.CurrentFrame(), 1u);
}

TEST(RegistryEpoch, InitialComponentWriteIsVisibleToChangedFilter)
{
    Registry registry(RegistryId{ 2, 1 }, RegistryKind::Zone, ZoneId{ 1 });
    registry.Entities.RegisterComponent<RegistryEpochMarker>();

    const EntityId entity = registry.Entities.CreateEntity();
    registry.Entities.AddComponent(entity, RegistryEpochMarker{ 7 });

    Query<Changed<RegistryEpochMarker>> changed(registry.Entities);
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

    ASSERT_EQ(runtime.Global().Entities.CurrentFrame(), 1u);
    ASSERT_EQ(active.Entities.CurrentFrame(), 1u);
    ASSERT_EQ(dormant.Entities.CurrentFrame(), 1u);

    runtime.AdvanceFrameEpochs();

    EXPECT_EQ(runtime.Global().Entities.CurrentFrame(), 2u);
    EXPECT_EQ(active.Entities.CurrentFrame(), 2u);
    EXPECT_EQ(dormant.Entities.CurrentFrame(), 2u);

    runtime.AdvanceFrameEpochs();

    EXPECT_EQ(runtime.Global().Entities.CurrentFrame(), 3u);
    EXPECT_EQ(active.Entities.CurrentFrame(), 3u);
    EXPECT_EQ(dormant.Entities.CurrentFrame(), 3u);
}
