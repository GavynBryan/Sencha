#include <gtest/gtest.h>

#include <world/registry/Registry.h>
#include <zone/ZoneId.h>
#include <zone/ZoneRuntime.h>

TEST(ZoneId, DefaultIsInvalid)
{
    ZoneId id;

    EXPECT_FALSE(id.IsValid());
    EXPECT_EQ(id, ZoneId{});
}

TEST(ZoneId, Equality)
{
    EXPECT_EQ(ZoneId{ 7 }, ZoneId{ 7 });
    EXPECT_NE(ZoneId{ 7 }, ZoneId{ 8 });
}

TEST(ZoneRuntime, CreatesGlobalOnConstruction)
{
    ZoneRuntime runtime;

    EXPECT_EQ(runtime.Global().Kind, RegistryKind::Global);
}

TEST(ZoneRuntime, GlobalHasGlobalRegistryId)
{
    ZoneRuntime runtime;

    EXPECT_EQ(runtime.Global().Id, RegistryId::Global());
}

TEST(ZoneRuntime, GlobalHasInvalidZoneId)
{
    ZoneRuntime runtime;

    EXPECT_FALSE(runtime.Global().Zone.IsValid());
}

TEST(ZoneRuntime, CreateZoneReturnsStableReference)
{
    ZoneRuntime runtime;

    Registry& chapel = runtime.CreateZone(ZoneId{ 1 });
    Registry* chapelPtr = &chapel;

    runtime.CreateZone(ZoneId{ 2 });
    runtime.CreateZone(ZoneId{ 3 });

    EXPECT_EQ(runtime.FindZone(ZoneId{ 1 }), chapelPtr);
}

#ifndef NDEBUG
TEST(ZoneRuntime, CreateZoneDuplicateZoneIdAsserts)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });

    EXPECT_DEATH(runtime.CreateZone(ZoneId{ 1 }), "duplicate zone id");
}

TEST(ZoneRuntime, CreateZoneInvalidZoneIdAsserts)
{
    ZoneRuntime runtime;

    EXPECT_DEATH(runtime.CreateZone(ZoneId{}), "zone id must be valid");
}
#endif

TEST(ZoneRuntime, CreateZoneRegistryHasValidZoneId)
{
    ZoneRuntime runtime;

    Registry& registry = runtime.CreateZone(ZoneId{ 42 });

    EXPECT_EQ(registry.Zone, ZoneId{ 42 });
    EXPECT_TRUE(registry.Zone.IsValid());
}

TEST(ZoneRuntime, CreateZoneRegistryHasKindZone)
{
    ZoneRuntime runtime;

    Registry& registry = runtime.CreateZone(ZoneId{ 42 });

    EXPECT_EQ(registry.Kind, RegistryKind::Zone);
}

TEST(ZoneRuntime, DestroyZoneRemovesFromLookup)
{
    ZoneRuntime runtime;

    runtime.CreateZone(ZoneId{ 1 });
    EXPECT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));

    EXPECT_FALSE(runtime.IsZoneLoaded(ZoneId{ 1 }));
    EXPECT_EQ(runtime.FindZone(ZoneId{ 1 }), nullptr);
}

TEST(ZoneRuntime, DestroyZoneMissingReturnsFalse)
{
    ZoneRuntime runtime;

    EXPECT_FALSE(runtime.DestroyZone(ZoneId{ 1 }));
}

TEST(ZoneRuntime, DestroyZoneOtherZonesUnaffected)
{
    ZoneRuntime runtime;

    Registry& bellTower = runtime.CreateZone(ZoneId{ 2 });
    runtime.CreateZone(ZoneId{ 1 });

    EXPECT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));

    EXPECT_TRUE(runtime.IsZoneLoaded(ZoneId{ 2 }));
    EXPECT_EQ(runtime.FindZone(ZoneId{ 2 }), &bellTower);
}

TEST(ZoneRuntime, DestroyZoneExcludesDestroyedZoneFromFrameView)
{
    ZoneRuntime runtime;

    runtime.CreateZone(ZoneId{ 1 });
    Registry& bellTower = runtime.CreateZone(ZoneId{ 2 });
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Visible = true });
    runtime.SetParticipation(ZoneId{ 2 }, ZoneParticipation{ .Visible = true });

    EXPECT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));
    FrameRegistryView view = runtime.BuildFrameView();

    ASSERT_EQ(view.Visible.size(), 2u);   // the global registry plus bellTower
    EXPECT_EQ(view.Visible[1], &bellTower);
}

TEST(ZoneRuntime, DestroyZoneVisiblyInvalidatesPreviouslyBuiltFrameView)
{
    ZoneRuntime runtime;

    runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Visible = true });
    FrameRegistryView view = runtime.BuildFrameView();

    ASSERT_EQ(view.Visible.size(), 2u);
    ASSERT_NE(view.Visible[1], nullptr);

    EXPECT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));

    EXPECT_EQ(view.Visible[1], nullptr);
}

TEST(ZoneRuntime, CreateZoneVisiblyInvalidatesPreviouslyBuiltFrameView)
{
    ZoneRuntime runtime;

    runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Visible = true });
    FrameRegistryView view = runtime.BuildFrameView();

    ASSERT_EQ(view.Visible.size(), 2u);
    ASSERT_NE(view.Visible[1], nullptr);

    runtime.CreateZone(ZoneId{ 2 });

    EXPECT_EQ(view.Visible[1], nullptr);
}

TEST(ZoneRuntime, FindZoneMissingReturnsNull)
{
    ZoneRuntime runtime;

    EXPECT_EQ(runtime.FindZone(ZoneId{ 99 }), nullptr);
}

TEST(ZoneRuntime, FindRegistryByRegistryIdWorks)
{
    ZoneRuntime runtime;

    Registry& chapel = runtime.CreateZone(ZoneId{ 1 });

    EXPECT_EQ(runtime.FindRegistry(RegistryId::Global()), &runtime.Global());
    EXPECT_EQ(runtime.FindRegistry(chapel.Id), &chapel);
}

TEST(ZoneRuntime, FindRegistryInvalidReturnsNull)
{
    ZoneRuntime runtime;

    EXPECT_EQ(runtime.FindRegistry(RegistryId::Invalid()), nullptr);
}

TEST(ZoneRuntime, SetParticipationDefaultIsAllFalse)
{
    ZoneRuntime runtime;

    runtime.CreateZone(ZoneId{ 1 });
    ZoneParticipation participation = runtime.GetParticipation(ZoneId{ 1 });

    EXPECT_FALSE(participation.Visible);
    EXPECT_FALSE(participation.Physics);
    EXPECT_FALSE(participation.Logic);
    EXPECT_FALSE(participation.Audio);
}

#ifndef NDEBUG
TEST(ZoneRuntime, GetParticipationMissingZoneAsserts)
{
    ZoneRuntime runtime;

    EXPECT_DEATH(runtime.GetParticipation(ZoneId{ 1 }), "zone must be loaded");
}
#endif

TEST(ZoneRuntime, SetParticipationUpdatesLoadedZone)
{
    ZoneRuntime runtime;

    runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{
        .Visible = true,
        .Physics = true,
        .Logic = false,
        .Audio = true
    });

    ZoneParticipation participation = runtime.GetParticipation(ZoneId{ 1 });

    EXPECT_TRUE(participation.Visible);
    EXPECT_TRUE(participation.Physics);
    EXPECT_FALSE(participation.Logic);
    EXPECT_TRUE(participation.Audio);
}

TEST(ZoneRuntime, BuildFrameViewEmptyRuntimeHasOnlyGlobal)
{
    ZoneRuntime runtime;

    FrameRegistryView view = runtime.BuildFrameView();

    EXPECT_EQ(view.Global, &runtime.Global());
    ASSERT_EQ(view.Visible.size(), 1u);
    ASSERT_EQ(view.Physics.size(), 1u);
    ASSERT_EQ(view.Logic.size(), 1u);
    ASSERT_EQ(view.Audio.size(), 1u);
    EXPECT_EQ(view.Visible[0], &runtime.Global());
    EXPECT_EQ(view.Physics[0], &runtime.Global());
    EXPECT_EQ(view.Logic[0], &runtime.Global());
    EXPECT_EQ(view.Audio[0], &runtime.Global());
}

TEST(ZoneRuntime, BuildFrameViewIncludesZonesByVisibleFlag)
{
    ZoneRuntime runtime;

    Registry& chapel = runtime.CreateZone(ZoneId{ 1 });
    runtime.CreateZone(ZoneId{ 2 });
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Visible = true });

    FrameRegistryView view = runtime.BuildFrameView();

    ASSERT_EQ(view.Visible.size(), 2u);   // the global registry rides every span
    EXPECT_EQ(view.Visible[1], &chapel);
}

TEST(ZoneRuntime, BuildFrameViewIncludesZonesByPhysicsFlag)
{
    ZoneRuntime runtime;

    Registry& chapel = runtime.CreateZone(ZoneId{ 1 });
    runtime.CreateZone(ZoneId{ 2 });
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Physics = true });

    FrameRegistryView view = runtime.BuildFrameView();

    ASSERT_EQ(view.Physics.size(), 2u);   // the global registry rides every span
    EXPECT_EQ(view.Physics[1], &chapel);
}

TEST(ZoneRuntime, BuildFrameViewIncludesZonesByLogicFlag)
{
    ZoneRuntime runtime;

    Registry& chapel = runtime.CreateZone(ZoneId{ 1 });
    runtime.CreateZone(ZoneId{ 2 });
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Logic = true });

    FrameRegistryView view = runtime.BuildFrameView();

    ASSERT_EQ(view.Logic.size(), 2u);   // the global registry rides every span
    EXPECT_EQ(view.Logic[1], &chapel);
}

TEST(ZoneRuntime, BuildFrameViewIncludesZonesByAudioFlag)
{
    ZoneRuntime runtime;

    Registry& chapel = runtime.CreateZone(ZoneId{ 1 });
    runtime.CreateZone(ZoneId{ 2 });
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Audio = true });

    FrameRegistryView view = runtime.BuildFrameView();

    ASSERT_EQ(view.Audio.size(), 2u);   // the global registry rides every span
    EXPECT_EQ(view.Audio[1], &chapel);
}

// The global registry hosts world-lifetime gameplay state (the partitioned
// world's pawn and camera) and is never dormant: it participates in every
// span, ordered first.
TEST(ZoneRuntime, BuildFrameViewGlobalParticipatesInAllSpans)
{
    ZoneRuntime runtime;
    runtime.CreateZone(ZoneId{ 1 });
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Visible = true });

    FrameRegistryView view = runtime.BuildFrameView();

    EXPECT_EQ(view.Global, &runtime.Global());
    ASSERT_EQ(view.Visible.size(), 2u);
    EXPECT_EQ(view.Visible[0], &runtime.Global());
    ASSERT_EQ(view.Physics.size(), 1u);
    EXPECT_EQ(view.Physics[0], &runtime.Global());
}

TEST(Registry, GlobalKindRequiresInvalidZoneId)
{
    Registry registry = MakeGlobalRegistry(RegistryId::Global());

    EXPECT_EQ(registry.Kind, RegistryKind::Global);
    EXPECT_FALSE(registry.Zone.IsValid());
}

TEST(Registry, ZoneKindRequiresValidZoneId)
{
    Registry registry = MakeZoneRegistry(RegistryId{ 2, 1 }, ZoneId{ 1 });

    EXPECT_EQ(registry.Kind, RegistryKind::Zone);
    EXPECT_TRUE(registry.Zone.IsValid());
}

#ifndef NDEBUG
TEST(Registry, MakeGlobalRegistryNonGlobalIdAsserts)
{
    EXPECT_DEATH(MakeGlobalRegistry(RegistryId{ 2, 1 }), "RegistryId::Global");
}

TEST(Registry, MakeZoneRegistryInvalidZoneIdAsserts)
{
    EXPECT_DEATH(MakeZoneRegistry(RegistryId{ 2, 1 }, ZoneId{}), "valid ZoneId");
}
#endif

// The streaming flicker regression: linger expiry destroys zones in the game
// update, after the frame view was built and before render extraction reads
// it. Destroying one zone must null ONLY its own entries, or every unload
// renders one empty frame.
TEST(ZoneRuntime, DestroyZoneLeavesUnrelatedFrameViewEntriesIntact)
{
    ZoneRuntime runtime;
    Registry& visible = runtime.CreateZone(ZoneId{ 1 });
    runtime.CreateZone(ZoneId{ 2 });   // dormant: in no span
    runtime.SetParticipation(ZoneId{ 1 }, ZoneParticipation{ .Visible = true });

    FrameRegistryView view = runtime.BuildFrameView();
    ASSERT_EQ(view.Visible.size(), 2u);
    ASSERT_EQ(view.Visible[1], &visible);

    // Destroying the dormant zone mid-frame: the visible zone keeps drawing.
    EXPECT_TRUE(runtime.DestroyZone(ZoneId{ 2 }));
    EXPECT_EQ(view.Visible[0], &runtime.Global());
    EXPECT_EQ(view.Visible[1], &visible);

    // Destroying the visible zone nulls exactly its entry.
    EXPECT_TRUE(runtime.DestroyZone(ZoneId{ 1 }));
    EXPECT_EQ(view.Visible[0], &runtime.Global());
    EXPECT_EQ(view.Visible[1], nullptr);
}
