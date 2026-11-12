// A handle means something only in the World that issued it, and for as long as
// that World holds the activation.

#include <abilities/AbilityActivations.h>
#include <abilities/AbilityCueLog.h>
#include <abilities/AbilityPresentationState.h>
#include <ecs/World.h>

#include <gtest/gtest.h>

#include <utility>

namespace
{
void Install(World& world)
{
    world.RegisterComponent<AbilityCueLog>();
    world.RegisterComponent<AbilityPresentationState>();
    world.RegisterComponent<ActiveAbilities>();
    world.AddResource<AbilityActivations>();
}
}

TEST(AbilityActivations, AVacantSlotReadsAsFree)
{
    const AbilityActivations pool;
    const ActiveAbilities active{};
    EXPECT_EQ(pool.Record(active, 0).State, AbilityRecordState::Free);
}

TEST(AbilityActivations, AFreedHandleNamesNothingEvenAfterItsSlotIsReused)
{
    AbilityActivations pool;
    ActiveAbilities active{};
    pool.Claim(active, 0, EntityId{}).Record.State = AbilityRecordState::Live;
    const ActiveAbilities remembered = active;
    pool.Free(active, 0);
    pool.Claim(active, 0, EntityId{}).Record.State = AbilityRecordState::Live;
    EXPECT_EQ(std::as_const(pool).Record(remembered, 0).State, AbilityRecordState::Free);
    EXPECT_EQ(std::as_const(pool).Record(active, 0).State, AbilityRecordState::Live);
}

TEST(AbilityActivations, ClaimingAnOccupiedSlotWipesIt)
{
    AbilityActivations pool;
    ActiveAbilities active{};
    pool.Claim(active, 0, EntityId{}).Record.Pulse = 9;
    EXPECT_EQ(pool.Claim(active, 0, EntityId{}).Record.Pulse, 0u);
    EXPECT_EQ(pool.Stats().Live, 1u);
}

TEST(AbilityActivations, EnsureKeepsWhatTheSlotHolds)
{
    AbilityActivations pool;
    ActiveAbilities active{};
    pool.Claim(active, 0, EntityId{}).Locals.EntityCount = 3;
    EXPECT_EQ(pool.Ensure(active, 0, EntityId{}).Locals.EntityCount, 3u);
}

TEST(AbilityActivations, AMovedWorldKeepsResolvingItsHandles)
{
    World first;
    Install(first);
    const EntityId actor = first.CreateEntity();
    first.AddComponent<ActiveAbilities>(actor);
    first.GetResource<AbilityActivations>()
        .Claim(*first.TryGet<ActiveAbilities>(actor), 0, actor)
        .Record.State = AbilityRecordState::Live;

    World moved(std::move(first));
    moved.GetResource<AbilityActivations>().CollectOrphans(moved);
    EXPECT_EQ(moved.GetResource<AbilityActivations>().Stats().Live, 1u);
    EXPECT_EQ(AbilityRecordsOf(moved, actor)[0].State, AbilityRecordState::Live);
}

TEST(AbilityActivations, AnActivationOutlivingItsWorldIsNotPossible)
{
    World world;
    Install(world);
    const EntityId actor = world.CreateEntity();
    world.AddComponent<ActiveAbilities>(actor);
    world.GetResource<AbilityActivations>().Claim(*world.TryGet<ActiveAbilities>(actor), 0, actor);
    EXPECT_EQ(world.GetResource<AbilityActivations>().Stats().Live, 1u);
}
