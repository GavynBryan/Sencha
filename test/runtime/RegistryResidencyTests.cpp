// Registry residency transitions: every lifecycle mutation records one
// coalesced change per registry per drain window; DestroyZone defers
// destruction so the Detaching registry stays alive and readable through the
// batch until FinalizeResidencyProcessing; queries and frame views never
// observe a detaching registry.

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
} // namespace

// ─── Recording and coalescing ────────────────────────────────────────────────

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
    EXPECT_FALSE(changes[0].Current.Any()); // dormant attach
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
    runtime.FinalizeResidencyProcessing(); // close the attach window

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
    runtime.FinalizeResidencyProcessing();

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
    runtime.FinalizeResidencyProcessing();

    runtime.SetParticipation(ZoneId{ 1 }, Full());
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly()); // back where it started

    EXPECT_TRUE(runtime.ResidencyChanges().empty());
}

TEST(RegistryResidency, NoOpSetParticipationRecordsNothing)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    runtime.FinalizeResidencyProcessing();

    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());

    EXPECT_TRUE(runtime.ResidencyChanges().empty());
}

// ─── Two-step destruction ────────────────────────────────────────────────────

TEST(RegistryResidency, DestroyZoneMarksDetachingAndDefersDestruction)
{
    ZoneRuntime runtime;
    Registry& zone = runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    const RegistryId id = zone.Id;
    const EntityId entity = zone.Components.CreateEntity();
    runtime.FinalizeResidencyProcessing();

    ASSERT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));

    // Invisible to every query immediately...
    EXPECT_FALSE(runtime.IsZoneLoaded(ZoneId{ 1 }));
    EXPECT_EQ(runtime.FindZone(ZoneId{ 1 }), nullptr);
    EXPECT_EQ(runtime.FindRegistry(id), nullptr);
    EXPECT_EQ(runtime.ZoneCount(), 0u);

    // ...but alive and readable through the change record until finalize.
    const auto changes = runtime.ResidencyChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, RegistryResidencyChangeKind::Detaching);
    EXPECT_EQ(changes[0].Previous, LogicOnly());
    EXPECT_FALSE(changes[0].Current.Any());
    ASSERT_EQ(changes[0].Instance, &zone);
    EXPECT_TRUE(changes[0].Instance->Components.IsAlive(entity));

    runtime.FinalizeResidencyProcessing();
    EXPECT_TRUE(runtime.ResidencyChanges().empty());
}

TEST(RegistryResidency, ParticipationChangeThenDestroyCoalescesToDetaching)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    runtime.FinalizeResidencyProcessing();

    runtime.SetParticipation(ZoneId{ 1 }, Full());
    runtime.DestroyZone(ZoneId{ 1 });

    const auto changes = runtime.ResidencyChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].Kind, RegistryResidencyChangeKind::Detaching);
    EXPECT_EQ(changes[0].Previous, LogicOnly()); // first observed, not Full
}

TEST(RegistryResidency, AttachAndDestroyInOneWindowEmitsNothing)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    runtime.DestroyZone(ZoneId{ 1 });

    EXPECT_TRUE(runtime.ResidencyChanges().empty());
    EXPECT_EQ(runtime.ZoneCount(), 0u);

    runtime.FinalizeResidencyProcessing(); // frees the never-observed zone
    EXPECT_FALSE(runtime.IsZoneLoaded(ZoneId{ 1 }));
}

TEST(RegistryResidency, DoubleDestroyReturnsFalse)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    runtime.FinalizeResidencyProcessing();

    EXPECT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));
    EXPECT_FALSE(runtime.DestroyZone(ZoneId{ 1 }));
}

TEST(RegistryResidency, ZoneIdIsReusableWhilePredecessorDetaches)
{
    ZoneRuntime runtime;
    Registry& first = runtime.CreateZone(ZoneId{ 1 });
    const RegistryId firstId = first.Id;
    runtime.FinalizeResidencyProcessing();

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

// ─── Frame view resolution ───────────────────────────────────────────────────

TEST(RegistryResidency, FrameViewExcludesDetachingAndResolvesParticipating)
{
    ZoneRuntime runtime;
    Registry& active = runtime.CreateZone(ZoneId{ 1 });
    Registry& dormant = runtime.CreateZone(ZoneId{ 2 });
    Registry& detaching = runtime.CreateZone(ZoneId{ 3 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    runtime.SetParticipation(ZoneId{ 3 }, Full());
    runtime.FinalizeResidencyProcessing();
    runtime.DestroyZone(ZoneId{ 3 });

    FrameRegistryView view = runtime.BuildFrameView();

    ASSERT_EQ(view.Participating.size(), 2u); // global + active
    EXPECT_EQ(view.Participating[0], &runtime.Global());
    EXPECT_EQ(view.Participating[1], &active);

    EXPECT_EQ(view.Find(active.Id), &active);
    EXPECT_EQ(view.Find(runtime.Global().Id), &runtime.Global());
    EXPECT_EQ(view.Find(dormant.Id), nullptr);   // attached but dormant
    EXPECT_EQ(view.Find(detaching.Id), nullptr);
    EXPECT_EQ(view.Find(RegistryId::Invalid()), nullptr);

    runtime.EndFrameView();
}

TEST(RegistryResidency, IsAliveResolvesThroughTheView)
{
    ZoneRuntime runtime;
    Registry& active = runtime.CreateZone(ZoneId{ 1 });
    Registry& dormant = runtime.CreateZone(ZoneId{ 2 });
    runtime.SetParticipation(ZoneId{ 1 }, LogicOnly());
    runtime.FinalizeResidencyProcessing();

    const EntityId live = active.Components.CreateEntity();
    const EntityId dead = active.Components.CreateEntity();
    active.Components.DestroyEntity(dead);
    const EntityId inDormant = dormant.Components.CreateEntity();

    FrameRegistryView view = runtime.BuildFrameView();

    EXPECT_TRUE(view.IsAlive(EntityRef{ active.Id, live }));
    EXPECT_FALSE(view.IsAlive(EntityRef{ active.Id, dead }));
    EXPECT_FALSE(view.IsAlive(EntityRef{ dormant.Id, inDormant })); // unresolvable
    EXPECT_FALSE(view.IsAlive(EntityRef{}));

    runtime.EndFrameView();
}
