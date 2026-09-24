// Navigation as the runtime holds it: a zone resource that lives and dies with
// its zone, found by ZoneId, driven by gameplay link state once per tick.

#include "NavigationRuntimeHarness.h"

#include <gtest/gtest.h>

#include <array>

namespace
{
    NavTestGeometry Room()
    {
        NavTestGeometry g;
        g.AddFloor(0.0f, 0.0f, 20.0f, 10.0f);
        return g;
    }
}

// Scenario F: unloading the zone that holds a route makes the next request fail
// cleanly; nothing stale survives into a reload or into a reused partition.
TEST(NavigationRuntime, ZoneLifetimeInvalidatesEverythingMadeAgainstIt)
{
    NavRuntimeHarness h;
    const ZoneId zoneA{ 0xA };
    const ZoneId zoneB{ 0xB };
    const StoragePartitionId partition = h.Attach(zoneA, Room()).Partition;

    const NavigationQuery query = h.System.Queries();
    const NavQueryRequest request = h.Request(zoneA);
    const NavLocation a = h.Project(zoneA, Vec3d(2, 0, 5));
    const NavLocation b = h.Project(zoneA, Vec3d(18, 0, 5));
    NavRouteBuffer route;
    ASSERT_EQ(query.FindRoute(h.Context, request, a, b, route), NavStatus::Success);
    EXPECT_EQ(query.ValidateRoute(route), NavRouteValidity::Valid);

    // Detach: the zone resource dies with the zone.
    h.Detach(zoneA);
    EXPECT_EQ(query.FindZone(zoneA), nullptr);
    EXPECT_EQ(query.Reachable(h.Context, request, a, b), NavStatus::ZoneUnavailable);
    EXPECT_EQ(query.ValidateRoute(route), NavRouteValidity::ZoneUnavailable);

    // Another zone takes the freed partition slot. The old location names zone
    // A, so it cannot address zone B's navigation.
    ASSERT_EQ(h.Attach(zoneB, Room()).Partition, partition);
    const NavLocation bStart = h.Project(zoneB, Vec3d(2, 0, 5));
    EXPECT_EQ(query.Reachable(h.Context, h.Request(zoneB), a, bStart),
              NavStatus::CrossZoneUnsupported);

    // Zone A loads again: same zone, new generation, so old references are stale.
    h.Attach(zoneA, Room());
    EXPECT_EQ(query.Reachable(h.Context, request, a, b), NavStatus::StaleLocation);
    EXPECT_EQ(query.ValidateRoute(route), NavRouteValidity::ZoneReloaded);
}

TEST(NavigationRuntime, CrossZoneRequestsFailExplicitly)
{
    NavRuntimeHarness h;
    h.Attach(ZoneId{ 1 }, Room());
    h.Attach(ZoneId{ 2 }, Room());

    const NavigationQuery query = h.System.Queries();
    const NavLocation one = h.Project(ZoneId{ 1 }, Vec3d(2, 0, 5));
    const NavLocation two = h.Project(ZoneId{ 2 }, Vec3d(18, 0, 5));
    NavRouteBuffer route;
    EXPECT_EQ(query.FindRoute(h.Context, h.Request(ZoneId{ 1 }), one, two, route),
              NavStatus::CrossZoneUnsupported);
    EXPECT_EQ(query.ProjectPoint(h.Context, h.Request(ZoneId{ 99 }), Vec3d(2, 0, 5),
                                 Vec3d(1, 2, 1)).Status,
              NavStatus::ZoneUnavailable);
}

// Dynamic connectivity: a gameplay entity's link state reaches routing at the
// system's tick point, with no recook and no callback into gameplay.
TEST(NavigationRuntime, LinkStateComponentDrivesRouting)
{
    NavRuntimeHarness h;
    const ZoneId zone{ 7 };
    h.Attach(zone, TwoIslands(), { NavZoneFixture::Link(0x42, "navigation.traversal.jump",
                                                        Vec3d(7, 0, 5), Vec3d(13, 0, 5)) });
    ASSERT_EQ(h.System.FindLinkZone(NavLinkId{ 0x42 }), zone);

    const NavigationQuery query = h.System.Queries();
    const std::array jump{ h.Tag("navigation.traversal.jump") };
    const NavQueryRequest request = h.Request(zone, jump);
    const NavLocation a = h.Project(zone, Vec3d(2, 0, 5));
    const NavLocation b = h.Project(zone, Vec3d(18, 0, 5));
    NavQueryContext& context = h.Context;
    NavigationSystem& system = h.System;
    EXPECT_EQ(query.Reachable(context, request, a, b), NavStatus::Success);

    // A door in the persistent partition closes the gap.
    World& world = h.Runtime.Entities();
    const EntityId door = world.CreateEntity();
    world.AddComponent(door, NavLinkState{ NavLinkId{ 0x42 }, false, 1.0f });
    // Nothing changes until the tick point applies it.
    EXPECT_EQ(query.Reachable(context, request, a, b), NavStatus::Success);
    system.ApplyLinkState();
    EXPECT_EQ(system.LastLinkStateStats().LinksChanged, 1u);
    EXPECT_EQ(query.Reachable(context, request, a, b), NavStatus::NoPath);

    // Open again.
    world.TryGet<NavLinkState>(door)->Enabled = true;
    system.ApplyLinkState();
    EXPECT_EQ(query.Reachable(context, request, a, b), NavStatus::Success);

    // Removing the component restores the authored state; an unchanged tick
    // bumps nothing.
    world.TryGet<NavLinkState>(door)->Enabled = false;
    system.ApplyLinkState();
    world.DestroyEntity(door);
    system.ApplyLinkState();
    EXPECT_EQ(query.Reachable(context, request, a, b), NavStatus::Success);
    system.ApplyLinkState();
    EXPECT_EQ(system.LastLinkStateStats().LinksChanged, 0u);

    // A state naming a link no resident zone has is counted, not guessed at.
    const EntityId stray = world.CreateEntity();
    world.AddComponent(stray, NavLinkState{ NavLinkId{ 0x999 }, false, 1.0f });
    system.ApplyLinkState();
    EXPECT_EQ(system.LastLinkStateStats().UnknownLinks, 1u);
}
