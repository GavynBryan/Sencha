// Registry residency transitions: lifecycle mutations coalesce in a pending
// batch; BeginResidencyProcessing swaps that batch into stable storage so
// handlers can enqueue next-frame changes without invalidating their span.

#include <gtest/gtest.h>

#include <world/registry/EntityRef.h>
#include <world/registry/Registry.h>
#include <zone/RegistryResidency.h>
#include <zone/ZoneId.h>
#include <zone/ZoneRuntime.h>

namespace
{
ZoneParticipation LogicOnly()
{
    ZoneParticipation p;
    p.Logic = true;
    return p;
}

ZoneParticipation Full()
{
    return ZoneParticipation{ true, true, true, true };
}

void ProcessResidency(ZoneRuntime& runtime)
{
    (void)runtime.BeginResidencyProcessing();
    runtime.FinalizeResidencyProcessing();
}
} // namespace

TEST(RegistryResidency, CreateZoneRecordsAttached)
{
    ZoneRuntime runtime;
    Registry& zone = runtime.CreateZone(ZoneId{ 1 });

    const auto changes = runtime.ResidencyChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, RegistryResidencyChangeKind::Attached);
    EXPECT_EQ(changes[0].Id, zone.Id);
    EXPECT_EQ(changes[0].Instance, &zone);
    EXPECT_FALSE(changes[0].Previous.Any());
    EXPECT_FALSE(changes[0].Current.Any());
}

TEST(RegistryResidency, CreateThenSetParticipationCoalescesIntoAttached)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());

    const auto changes = runtime.ResidencyChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, RegistryResidencyChangeKind::Attached);
    EXPECT_EQ(changes[0].Current, LogicOnly());
}

TEST(RegistryResidency, AttachZoneRecordsInitialParticipation)
{
    ZoneRuntime runtime;
    const RegistryId reserved = runtime.ReserveRegistryId();
    auto registry = std::make_unique<Registry>(MakeZoneRegistry(reserved, ZoneId{ 4 }));
    runtime.AttachZone(std::move(registry), Full());

    const auto changes = runtime.ResidencyChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, RegistryResidencyChangeKind::Attached);
    EXPECT_EQ(changes[0].Id, reserved);
    EXPECT_EQ(changes[0].Current, Full());
}

TEST(RegistryResidency, SetParticipationRecordsPreviousAndCurrent)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    ProcessResidency(runtime);

    runtime.SetParticipation(ZoneId{ 1 }, Full());

    const auto changes = runtime.ResidencyChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, RegistryResidencyChangeKind::ParticipationChanged);
    EXPECT_FALSE(changes[0].Previous.Any());
    EXPECT_EQ(changes[0].Current, Full());
}

TEST(RegistryResidency, RepeatedChangesCoalesceToFirstPreviousFinalCurrent)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    ProcessResidency(runtime);

    runtime.SetParticipation(ZoneId{ 1 }, Full());
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{});

    const auto changes = runtime.ResidencyChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Previous, LogicOnly());
    EXPECT_FALSE(changes[0].Current.Any());
}

TEST(RegistryResidency, ParticipationRoundTripDropsTheRecord)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    ProcessResidency(runtime);

    runtime.SetParticipation(ZoneId{ 1 }, Full());
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());

    EXPECT_TRUE(runtime.ResidencyChanges().empty());
}

TEST(RegistryResidency, NoOpSetParticipationRecordsNothing)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    ProcessResidency(runtime);

    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());

    EXPECT_TRUE(runtime.ResidencyChanges().empty());
}

TEST(RegistryResidency, ProcessingBatchIsStableUnderReentrantMutation)
{
    ZoneRuntime runtime;
    Registry& first = runtime.CreateZone(ZoneId{ 1 });
    Registry& second = runtime.CreateZone(ZoneId{ 2 });
    ProcessResidency(runtime);

    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    const auto processing = runtime.BeginResidencyProcessing();
    ASSERT_EQ(processing.size(), 1u);
    EXPECT_EQ(processing[0].Id, first.Id);

    runtime.SetParticipation(ZoneId{ 2 }, Full());

    // The active span is unchanged; the new mutation lives in next frame's
    // pending batch instead of reallocating or being cleared by finalization.
    ASSERT_EQ(processing.size(), 1u);
    EXPECT_EQ(processing[0].Id, first.Id);
    ASSERT_EQ(runtime.ResidencyChanges().size(), 1u);
    EXPECT_EQ(runtime.ResidencyChanges()[0].Id, second.Id);

    runtime.FinalizeResidencyProcessing();
    ASSERT_EQ(runtime.ResidencyChanges().size(), 1u);

    const auto next = runtime.BeginResidencyProcessing();
    ASSERT_EQ(next.size(), 1u);
    EXPECT_EQ(next[0].Id, second.Id);
    runtime.FinalizeResidencyProcessing();
}

TEST(RegistryResidency, DestroyZoneMarksDetachingAndDefersDestruction)
{
    ZoneRuntime runtime;
    Registry& zone = runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    const RegistryId id = zone.Id;
    const EntityId entity = zone.Components.CreateEntity();
    ProcessResidency(runtime);

    ASSERT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));

    EXPECT_FALSE(runtime.IsZoneLoaded(ZoneId{ 1 }));
    EXPECT_EQ(runtime.FindZone(ZoneId{ 1 }), nullptr);
    EXPECT_EQ(runtime.FindRegistry(id), nullptr);
    EXPECT_EQ(runtime.ZoneCount(), 0u);

    const auto pending = runtime.ResidencyChanges();
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].Kind, RegistryResidencyChangeKind::Detaching);
    EXPECT_EQ(pending[0].Previous, LogicOnly());
    EXPECT_FALSE(pending[0].Current.Any());
    ASSERT_EQ(pending[0].Instance, &zone);
    EXPECT_TRUE(pending[0].Instance->Components.IsAlive(entity));

    const auto processing = runtime.BeginResidencyProcessing();
    ASSERT_EQ(processing.size(), 1u);
    EXPECT_TRUE(processing[0].Instance->Components.IsAlive(entity));
    runtime.FinalizeResidencyProcessing();
    EXPECT_TRUE(runtime.ResidencyChanges().empty());
}

TEST(RegistryResidency, ParticipationChangeThenDestroyCoalescesToDetaching)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    ProcessResidency(runtime);

    runtime.SetParticipation(ZoneId{ 1 }, Full());
    runtime.DestroyZone(ZoneId{ 1 });

    const auto changes = runtime.ResidencyChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, RegistryResidencyChangeKind::Detaching);
    EXPECT_EQ(changes[0].Previous, LogicOnly());
}

TEST(RegistryResidency, AttachAndDestroyInOneWindowStillEmitsDetaching)
{
    ZoneRuntime runtime;
    Registry& zone = runtime.CreateZone(ZoneId{ 1 });
    const RegistryId id = zone.Id;
    runtime.DestroyZone(ZoneId{ 1 });

    const auto changes = runtime.ResidencyChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, RegistryResidencyChangeKind::Detaching);
    EXPECT_EQ(changes[0].Id, id);
    EXPECT_EQ(changes[0].Instance, &zone);

    ProcessResidency(runtime);
    EXPECT_FALSE(runtime.IsZoneLoaded(ZoneId{ 1 }));
}

TEST(RegistryResidency, DoubleDestroyReturnsFalse)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    ProcessResidency(runtime);

    EXPECT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));
    EXPECT_FALSE(runtime.DestroyZone(ZoneId{ 1 }));
}

TEST(RegistryResidency, ZoneIdIsReusableWhilePredecessorDetaches)
{
    ZoneRuntime runtime;
    Registry& first = runtime.CreateZone(ZoneId{ 1 });
    const RegistryId firstId = first.Id;
    ProcessResidency(runtime);

    runtime.DestroyZone(ZoneId{ 1 });
    Registry& second = runtime.CreateZone(ZoneId{ 1 });

    EXPECT_NE(second.Id, firstId);
    EXPECT_EQ(runtime.FindZone(ZoneId{ 1 }), &second);

    const auto changes = runtime.ResidencyChanges();
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].Kind, RegistryResidencyChangeKind::Detaching);
    EXPECT_EQ(changes[0].Id, firstId);
    EXPECT_EQ(changes[1].Kind, RegistryResidencyChangeKind::Attached);
    EXPECT_EQ(changes[1].Id, second.Id);
}

TEST(RegistryResidency, FrameViewExcludesDetachingAndResolvesParticipating)
{
    ZoneRuntime runtime;
    Registry& active = runtime.CreateZone(ZoneId{ 1 });
    Registry& dormant = runtime.CreateZone(ZoneId{ 2 });
    Registry& detaching = runtime.CreateZone(ZoneId{ 3 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    runtime.SetParticipation(ZoneId{ 3 }, Full());
    ProcessResidency(runtime);
    runtime.DestroyZone(ZoneId{ 3 });

    FrameRegistryView view = runtime.BuildFrameView();

    ASSERT_EQ(view.Participating.size(), 2u);
    EXPECT_EQ(view.Participating[0], &runtime.Global());
    EXPECT_EQ(view.Participating[1], &active);

    EXPECT_EQ(view.FindParticipating(active.Id), &active);
    EXPECT_EQ(view.FindParticipating(runtime.Global().Id), &runtime.Global());
    EXPECT_EQ(view.FindParticipating(dormant.Id), nullptr);
    EXPECT_EQ(view.FindParticipating(detaching.Id), nullptr);
    EXPECT_EQ(view.FindParticipating(RegistryId::Invalid()), nullptr);

    runtime.EndFrameView();
}

TEST(RegistryResidency, DomainScopedLookupDoesNotResolveOtherDomains)
{
    ZoneRuntime runtime;
    Registry& logicOnly = runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    ProcessResidency(runtime);

    FrameRegistryView view = runtime.BuildFrameView();
    EXPECT_EQ(FindRegistry(view.Logic, logicOnly.Id), &logicOnly);
    EXPECT_EQ(FindRegistry(view.Physics, logicOnly.Id), nullptr);
    runtime.EndFrameView();
}

TEST(RegistryResidency, IsAliveResolvesThroughChosenSpan)
{
    ZoneRuntime runtime;
    Registry& active = runtime.CreateZone(ZoneId{ 1 });
    Registry& dormant = runtime.CreateZone(ZoneId{ 2 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    ProcessResidency(runtime);

    const EntityId live = active.Components.CreateEntity();
    const EntityId dead = active.Components.CreateEntity();
    active.Components.DestroyEntity(dead);
    const EntityId inDormant = dormant.Components.CreateEntity();

    FrameRegistryView view = runtime.BuildFrameView();

    EXPECT_TRUE(IsAliveIn(view.Logic, EntityRef{ active.Id, live }));
    EXPECT_FALSE(IsAliveIn(view.Logic, EntityRef{ active.Id, dead }));
    EXPECT_FALSE(IsAliveIn(view.Logic, EntityRef{ dormant.Id, inDormant }));
    EXPECT_FALSE(IsAliveIn(view.Logic, EntityRef{}));

    runtime.EndFrameView();
}
