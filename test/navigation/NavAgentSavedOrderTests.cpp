#include "NavAgentHarness.h"

#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <world/identity/PendingReferences.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializer.h>

#include <gtest/gtest.h>

namespace
{
    constexpr PersistentEntityId kLeader{ 0x00000000000000c3ull };

    JsonValue SaveOrder(NavAgentHarness& harness, EntityId agent)
    {
        LoggingProvider logging;
        SceneSerializationContext context(logging);
        context.Purpose = SceneRecordPurpose::Save;
        JsonWriteArchive archive;
        EXPECT_TRUE(ComponentSerializer<NavAgentRequest>{}.Save(archive, agent, harness.Entities(), context));
        return archive.TakeValue();
    }

    void LoadOrder(NavAgentHarness& harness, EntityId agent, const JsonValue& saved)
    {
        LoggingProvider logging;
        SceneSerializationContext context(logging);
        JsonReadArchive archive(saved);
        ComponentSerializer<NavAgentRequest> orders;
        EXPECT_TRUE(orders.LoadIntoWorld(archive, agent, harness.Entities(), context));
    }

    EntityId Leader(NavAgentHarness& harness, Vec3d feet)
    {
        const EntityId leader = harness.SpawnMarker(feet);
        harness.Entities().AddComponent<PersistentIdComponent>(leader, PersistentIdComponent{ kLeader });
        return leader;
    }
}

TEST(NavAgentSavedOrders, AWalkOrderLoadedIntoAnotherRunIsTakenUpAndFinishes)
{
    NavAgentHarness harness(WallRoom());
    const EntityId walker = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 3.0f));
    (void)harness.GoTo(walker, Vec3d(17.0f, 0.0f, 3.0f));
    harness.Step();
    const JsonValue saved = SaveOrder(harness, walker);
    harness.Entities().DestroyEntity(walker);
    for (int tick = 0; tick < 7; ++tick)
        harness.Step();

    const EntityId loaded = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 3.0f));
    LoadOrder(harness, loaded, saved);
    ASSERT_TRUE(harness.StepUntil([&] { return harness.HasEnded(loaded); }, 60 * 30));
    EXPECT_EQ(harness.Progress(loaded).Phase, NavAgentPhase::Arrived) << NavStatusName(harness.Progress(loaded).LastQuery);
    EXPECT_LE(NavPlanarDistance(harness.Position(loaded), Vec3d(17.0f, 0.0f, 3.0f)), 0.75f);
}

TEST(NavAgentSavedOrders, AFollowOrderWaitsForItsTargetToArriveAndThenFollows)
{
    NavAgentHarness harness(WallRoom());
    const EntityId leader = Leader(harness, Vec3d(5.0f, 0.0f, 3.0f));
    const EntityId follower = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 3.0f));
    (void)RequestNavigation(harness.Entities(), follower, NavDestination::AtEntity(leader, true));
    harness.Step();
    const JsonValue saved = SaveOrder(harness, follower);
    harness.Entities().DestroyEntity(follower);
    harness.Entities().DestroyEntity(leader);

    const EntityId loaded = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 3.0f));
    LoadOrder(harness, loaded, saved);
    for (int tick = 0; tick < 60; ++tick)
        harness.Step();
    EXPECT_NE(harness.Progress(loaded).Phase, NavAgentPhase::Failed)
        << NavEnumName(harness.Progress(loaded).Failure) << ": the order ended before its target arrived";

    const EntityId arrived = Leader(harness, Vec3d(17.0f, 0.0f, 3.0f));
    EXPECT_EQ(std::as_const(harness.Entities()).TryGet<NavAgentRequest>(loaded)->Target, arrived);
    ASSERT_TRUE(harness.StepUntil([&] { return harness.Progress(loaded).Phase == NavAgentPhase::Holding; }, 60 * 30))
        << NavEnumName(harness.Progress(loaded).Phase) << " " << NavEnumName(harness.Progress(loaded).Failure);
    EXPECT_LE(NavPlanarDistance(harness.Position(loaded), Vec3d(17.0f, 0.0f, 3.0f)), 1.5f);

    harness.MoveMarker(arrived, Vec3d(3.0f, 0.0f, 3.0f));
    EXPECT_TRUE(harness.StepUntil(
        [&] { return NavPlanarDistance(harness.Position(loaded), Vec3d(3.0f, 0.0f, 3.0f)) < 1.5f; }, 60 * 30))
        << "the loaded order does not keep following";
}

TEST(NavAgentSavedOrders, AnOrderGivenWhileALoadedOneWaitsReplacesIt)
{
    NavAgentHarness harness(WallRoom());
    const EntityId leader = Leader(harness, Vec3d(5.0f, 0.0f, 3.0f));
    const EntityId follower = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 3.0f));
    (void)RequestNavigation(harness.Entities(), follower, NavDestination::AtEntity(leader, true));
    const JsonValue saved = SaveOrder(harness, follower);
    harness.Entities().DestroyEntity(follower);
    harness.Entities().DestroyEntity(leader);

    const EntityId loaded = harness.SpawnAgent(Vec3d(3.0f, 0.0f, 3.0f));
    LoadOrder(harness, loaded, saved);
    (void)harness.GoTo(loaded, Vec3d(17.0f, 0.0f, 3.0f));
    EXPECT_FALSE(harness.Entities().GetResource<PendingReferences>().Awaits(loaded,
                                                                             ResolveComponentTypeId<NavAgentRequest>()));
    (void)Leader(harness, Vec3d(5.0f, 0.0f, 3.0f));
    EXPECT_FALSE(std::as_const(harness.Entities()).TryGet<NavAgentRequest>(loaded)->Target.IsValid());
}
