#include "NavigationFixture.h"

#include <jobs/JobSystem.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    std::vector<NavRouteStepKind> Shape(const NavRouteBuffer& route)
    {
        std::vector<NavRouteStepKind> kinds;
        for (const NavRouteStep& step : route.Steps())
            kinds.push_back(step.Kind);
        return kinds;
    }

    const NavRouteStep* FirstTraverse(const NavRouteBuffer& route)
    {
        for (const NavRouteStep& step : route.Steps())
            if (step.Kind == NavRouteStepKind::Traverse)
                return &step;
        return nullptr;
    }
}

TEST(NavigationQuery, ProjectsOntoWalkableSurfaceAndRefusesEmptySpace)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(WallRoom());
    NavQueryContext context;
    const NavQueryRequest request = f.Request();

    const NavProjectResult hit =
        NavProjectPoint(zone, context, request, Vec3d(5.0f, 1.0f, 5.0f), Vec3d(1, 2, 1));
    ASSERT_EQ(hit.Status, NavStatus::Success);
    EXPECT_NEAR(hit.Location.Position.Y, 0.0f, 0.2f);
    EXPECT_EQ(hit.Location.Zone, f.Zone);
    EXPECT_EQ(hit.Location.Generation, zone.Generation());

    EXPECT_EQ(NavProjectPoint(zone, context, request, Vec3d(50.0f, 0.0f, 50.0f), Vec3d(1, 2, 1))
                  .Status,
              NavStatus::InvalidStart);

    NavQueryRequest unknownProfile = request;
    unknownProfile.Profile = f.Jump;
    EXPECT_EQ(NavProjectPoint(zone, context, unknownProfile, Vec3d(5, 0, 5), Vec3d(1, 2, 1))
                  .Status,
              NavStatus::ProfileUnavailable);
}

TEST(NavigationQuery, RoutesAroundAWall)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(WallRoom());
    NavQueryContext context;
    const NavQueryRequest request = f.Request();
    const NavLocation start = f.Project(zone, context, request, Vec3d(5, 0, 2));
    const NavLocation end = f.Project(zone, context, request, Vec3d(15, 0, 2));

    NavRouteBuffer route;
    ASSERT_EQ(NavFindRoute(zone, context, request, start, end, route), NavStatus::Success);
    EXPECT_EQ(Shape(route), std::vector<NavRouteStepKind>{ NavRouteStepKind::Walk });
    // The only way through is the gap north of z = 7.
    const auto corners = route.Corners();
    ASSERT_GE(corners.size(), 2u);
    EXPECT_TRUE(std::any_of(corners.begin(), corners.end(),
                            [](const Vec3d& c) { return c.Z > 7.0f; }));
    EXPECT_NEAR(corners.back().X, 15.0f, 0.05f);
    EXPECT_GT(route.WalkDistance, 12.0f);
    EXPECT_GE(route.Cost, 10.0f);

    const NavCostResult cost = NavTravelCost(zone, context, request, start, end);
    ASSERT_EQ(cost.Status, NavStatus::Success);
    EXPECT_FLOAT_EQ(cost.Cost, route.Cost);
    EXPECT_EQ(NavReachable(zone, context, request, start, end), NavStatus::Success);
}

TEST(NavigationQuery, NavRaycastStopsAtWalkableBoundary)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(WallRoom());
    NavQueryContext context;
    const NavQueryRequest request = f.Request();
    const NavLocation start = f.Project(zone, context, request, Vec3d(5, 0, 2));

    const NavRaycastResult blocked = NavRaycast(zone, context, request, start, Vec3d(15, 0, 2));
    ASSERT_EQ(blocked.Status, NavStatus::Success);
    EXPECT_TRUE(blocked.Hit);
    EXPECT_LT(blocked.Point.X, 9.5f);
    EXPECT_GT(blocked.Point.X, 8.5f);

    const NavRaycastResult clear = NavRaycast(zone, context, request, start, Vec3d(5, 0, 8));
    ASSERT_EQ(clear.Status, NavStatus::Success);
    EXPECT_FALSE(clear.Hit);
    EXPECT_FLOAT_EQ(clear.Fraction, 1.0f);
}

TEST(NavigationQuery, DisconnectedIslandsAreUnreachable)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(TwoIslands());
    NavQueryContext context;
    const NavQueryRequest request = f.Request();
    const NavLocation a = f.Project(zone, context, request, Vec3d(4, 0, 5));
    const NavLocation b = f.Project(zone, context, request, Vec3d(16, 0, 5));
    EXPECT_EQ(NavReachable(zone, context, request, a, b), NavStatus::NoPath);
    EXPECT_EQ(NavTravelCost(zone, context, request, a, b).Status, NavStatus::NoPath);
    NavRouteBuffer route;
    EXPECT_EQ(NavFindRoute(zone, context, request, a, b, route), NavStatus::NoPath);
    EXPECT_TRUE(route.Steps().empty());
}

// Scenario B: area cost data picks the corridor, without rebuilding anything.
TEST(NavigationQuery, AreaCostPolicyChoosesTheCorridor)
{
    NavTestGeometry g;
    g.AddFloor(0.0f, 0.0f, 20.0f, 12.0f);
    g.AddBox(Vec3d(4.0f, 0.0f, 3.0f), Vec3d(16.0f, 3.0f, 9.0f));
    NavAreaVolume water;
    water.Footprint = { Vec3d(4, 0, -1), Vec3d(16, 0, -1), Vec3d(16, 0, 3), Vec3d(4, 0, 3) };
    water.MinY = -1.0f;
    water.MaxY = 1.0f;
    water.Area = 1;

    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(g, {}, { water });
    NavQueryContext context;

    NavQueryPolicy avoidWater;
    avoidWater.AreaCosts.push_back({ f.Water, 5.0f });
    NavQueryPolicy preferWater;
    preferWater.AreaCosts.push_back({ f.Water, 0.5f });

    const auto routeSouth = [&](const NavQueryPolicy& policy)
    {
        const NavQueryRequest request = f.Request({}, &policy);
        const NavLocation start = f.Project(zone, context, request, Vec3d(2, 0, 6));
        const NavLocation end = f.Project(zone, context, request, Vec3d(18, 0, 6));
        NavRouteBuffer route;
        EXPECT_EQ(NavFindRoute(zone, context, request, start, end, route), NavStatus::Success);
        const auto corners = route.Corners();
        return std::any_of(corners.begin(), corners.end(),
                           [](const Vec3d& c) { return c.Z < 3.0f; });
    };
    EXPECT_FALSE(routeSouth(avoidWater));
    EXPECT_TRUE(routeSouth(preferWater));

    // Forbidding the area is also data.
    NavQueryPolicy forbidWater;
    forbidWater.ForbiddenAreas.push_back(f.Water);
    EXPECT_FALSE(routeSouth(forbidWater));
}

// Scenario C: a destination behind a jump link.
TEST(NavigationQuery, JumpLinkRequiresTheCapability)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(
        TwoIslands(), { NavZoneFixture::Link(0x42, "navigation.traversal.jump",
                                             Vec3d(7, 0, 5), Vec3d(13, 0, 5)) });
    NavQueryContext context;

    const NavQueryRequest walker = f.Request();
    const NavLocation a = f.Project(zone, context, walker, Vec3d(2, 0, 5));
    const NavLocation b = f.Project(zone, context, walker, Vec3d(18, 0, 5));
    NavRouteBuffer route;
    EXPECT_EQ(NavFindRoute(zone, context, walker, a, b, route), NavStatus::NoPath);

    const std::array jumper{ f.Jump };
    const NavQueryRequest request = f.Request(jumper);
    ASSERT_EQ(NavFindRoute(zone, context, request, a, b, route), NavStatus::Success);
    EXPECT_EQ(Shape(route), (std::vector<NavRouteStepKind>{
                                NavRouteStepKind::Walk, NavRouteStepKind::Traverse,
                                NavRouteStepKind::Walk }));
    const NavRouteStep* jump = FirstTraverse(route);
    ASSERT_NE(jump, nullptr);
    EXPECT_EQ(jump->Link, NavLinkId{ 0x42 });
    EXPECT_EQ(jump->Traversal, f.Jump);
    EXPECT_NEAR(jump->Entry.X, 7.0f, 0.3f);
    EXPECT_NEAR(jump->Exit.X, 13.0f, 0.3f);
    EXPECT_FALSE(jump->Reversed);
    ASSERT_EQ(route.LinkDependencies().size(), 1u);

    // A capability names a subtree: a parent tag grants every kind below it.
    const std::array anyTraversal{ f.Tags.FindTag("navigation.traversal") };
    const NavQueryRequest broad = f.Request(anyTraversal);
    EXPECT_EQ(NavReachable(zone, context, broad, a, b), NavStatus::Success);

    // One-way: the link does not lead back.
    EXPECT_EQ(NavReachable(zone, context, request, b, a), NavStatus::NoPath);
}

TEST(NavigationQuery, OneWayDropLeadsOnlyDown)
{
    NavTestGeometry g;
    g.AddFloor(0.0f, 0.0f, 8.0f, 10.0f, 3.0f);
    g.AddFloor(9.0f, 0.0f, 20.0f, 10.0f, 0.0f);
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(
        g, { NavZoneFixture::Link(0x7, "navigation.traversal.drop", Vec3d(7, 3, 5),
                                  Vec3d(11, 0, 5)) });
    NavQueryContext context;
    const std::array capabilities{ f.Drop };
    const NavQueryRequest request = f.Request(capabilities);
    const NavLocation top = f.Project(zone, context, request, Vec3d(2, 3, 5));
    const NavLocation bottom = f.Project(zone, context, request, Vec3d(18, 0, 5));
    EXPECT_EQ(NavReachable(zone, context, request, top, bottom), NavStatus::Success);
    EXPECT_EQ(NavReachable(zone, context, request, bottom, top), NavStatus::NoPath);
}

TEST(NavigationQuery, BidirectionalLinkReportsReverseCrossing)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(
        TwoIslands(), { NavZoneFixture::Link(0x42, "navigation.traversal.jump", Vec3d(7, 0, 5),
                                             Vec3d(13, 0, 5), NavLinkDirectionBoth) });
    NavQueryContext context;
    const std::array jumper{ f.Jump };
    const NavQueryRequest request = f.Request(jumper);
    const NavLocation a = f.Project(zone, context, request, Vec3d(2, 0, 5));
    const NavLocation b = f.Project(zone, context, request, Vec3d(18, 0, 5));
    NavRouteBuffer route;
    ASSERT_EQ(NavFindRoute(zone, context, request, b, a, route), NavStatus::Success);
    const NavRouteStep* jump = FirstTraverse(route);
    ASSERT_NE(jump, nullptr);
    EXPECT_TRUE(jump->Reversed);
    EXPECT_NEAR(jump->Entry.X, 13.0f, 0.3f);
    EXPECT_NEAR(jump->Exit.X, 7.0f, 0.3f);
}

// Scenario D and dynamic connectivity: link state changes routing, no recook.
TEST(NavigationQuery, LinkStateAndPersonalAvoidanceSteerRoutes)
{
    NavZoneFixture f;
    ZoneNavigation zone = f.Load(
        TwoIslands(),
        { NavZoneFixture::Link(0xA, "navigation.traversal.jump", Vec3d(7, 0, 2), Vec3d(13, 0, 2)),
          NavZoneFixture::Link(0xB, "navigation.traversal.jump", Vec3d(7, 0, 8),
                               Vec3d(13, 0, 8)) });
    NavQueryContext context;
    const std::array jumper{ f.Jump };
    NavQueryRequest request = f.Request(jumper);
    const NavLocation a = f.Project(zone, context, request, Vec3d(2, 0, 2));
    const NavLocation b = f.Project(zone, context, request, Vec3d(18, 0, 2));

    const auto linkUsed = [&](const NavQueryRequest& r) -> NavLinkId
    {
        NavRouteBuffer route;
        if (NavFindRoute(zone, context, r, a, b, route) != NavStatus::Success)
            return NavLinkId{};
        const NavRouteStep* step = FirstTraverse(route);
        return step != nullptr ? step->Link : NavLinkId{};
    };

    // The near link is the natural choice.
    EXPECT_EQ(linkUsed(request), NavLinkId{ 0xA });

    // Personal avoidance: this query avoids A; another query still uses it.
    const std::array avoid{ NavLinkId{ 0xA } };
    NavQueryRequest avoiding = request;
    avoiding.AvoidLinks = avoid;
    EXPECT_EQ(linkUsed(avoiding), NavLinkId{ 0xB });
    EXPECT_EQ(linkUsed(request), NavLinkId{ 0xA });

    // Per-query cost override biases without forbidding.
    const std::array expensive{ NavLinkCostOverride{ NavLinkId{ 0xA }, 100.0f } };
    NavQueryRequest biased = request;
    biased.LinkCostOverrides = expensive;
    EXPECT_EQ(linkUsed(biased), NavLinkId{ 0xB });

    // Global state: disabling A reroutes everyone; re-enabling restores it.
    const std::uint32_t indexA = *zone.FindLink(NavLinkId{ 0xA });
    const std::uint32_t revision = zone.Link(indexA).Revision;
    zone.SetLinkState(indexA, false, 1.0f);
    EXPECT_NE(zone.Link(indexA).Revision, revision);
    EXPECT_EQ(linkUsed(request), NavLinkId{ 0xB });
    const std::uint32_t indexB = *zone.FindLink(NavLinkId{ 0xB });
    zone.SetLinkState(indexB, false, 1.0f);
    EXPECT_EQ(linkUsed(request), NavLinkId{});
    zone.SetLinkState(indexA, true, 1.0f);
    zone.SetLinkState(indexB, true, 1.0f);
    EXPECT_EQ(linkUsed(request), NavLinkId{ 0xA });

    // A global cost scale is also state, not geometry.
    zone.SetLinkState(indexA, true, 100.0f);
    EXPECT_EQ(linkUsed(request), NavLinkId{ 0xB });
}

TEST(NavigationQuery, DiscontinuousLinkSpansManyTiles)
{
    NavTestGeometry g;
    g.AddFloor(0.0f, 0.0f, 8.0f, 10.0f);
    g.AddFloor(40.0f, 0.0f, 48.0f, 10.0f);
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(
        g, { NavZoneFixture::Link(0x99, "navigation.traversal.teleport", Vec3d(4, 0, 5),
                                  Vec3d(44, 0, 5)) });
    NavQueryContext context;
    const std::array capabilities{ f.Teleport };
    const NavQueryRequest request = f.Request(capabilities);
    const NavLocation a = f.Project(zone, context, request, Vec3d(2, 0, 5));
    const NavLocation b = f.Project(zone, context, request, Vec3d(46, 0, 5));

    const NavCostResult cost = NavTravelCost(zone, context, request, a, b);
    ASSERT_EQ(cost.Status, NavStatus::Success);
    // 2 m walk + link + 2 m walk: far cheaper than the 44 m between them.
    EXPECT_LT(cost.Cost, 6.0f);
    NavRouteBuffer route;
    ASSERT_EQ(NavFindRoute(zone, context, request, a, b, route), NavStatus::Success);
    EXPECT_EQ(Shape(route), (std::vector<NavRouteStepKind>{
                                NavRouteStepKind::Walk, NavRouteStepKind::Traverse,
                                NavRouteStepKind::Walk }));
}

// Scenario E: an environmental query can generate, test, and rank candidates
// with public navigation operations only.
TEST(NavigationQuery, ReachableRegionsSupportCandidateRanking)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(WallRoom());
    NavQueryContext context;
    const NavQueryRequest request = f.Request();
    const NavLocation origin = f.Project(zone, context, request, Vec3d(5, 0, 2));

    NavReachableBuffer regions;
    ASSERT_EQ(NavCollectReachable(zone, context, request, origin, 15.0f, 40.0f, regions),
              NavStatus::Success);
    ASSERT_FALSE(regions.Regions().empty());
    EXPECT_EQ(regions.Regions().front().Region.Ref, origin.Ref);
    EXPECT_FLOAT_EQ(regions.Regions().front().EntryCost, 0.0f);
    for (std::size_t i = 1; i < regions.Regions().size(); ++i)
        EXPECT_LE(regions.Regions()[i - 1].EntryCost, regions.Regions()[i].EntryCost);

    // The caller samples: here, the point of each region nearest a target.
    struct Candidate
    {
        NavLocation Location;
        float Cost;
    };
    std::vector<Candidate> candidates;
    const Vec3d target(15.0f, 0.0f, 5.0f);
    for (const NavReachableRegion& region : regions.Regions())
    {
        const NavProjectResult point = NavClosestPointInRegion(zone, context, region.Region, target);
        ASSERT_EQ(point.Status, NavStatus::Success);
        ASSERT_TRUE(NavRegionBounds(zone, region.Region).has_value());
        const NavCostResult cost = NavTravelCost(zone, context, request, origin, point.Location);
        ASSERT_EQ(cost.Status, NavStatus::Success);
        EXPECT_TRUE(std::isfinite(cost.Cost));
        candidates.push_back({ point.Location, cost.Cost });
    }
    const auto cheapest = std::min_element(candidates.begin(), candidates.end(),
        [](const Candidate& x, const Candidate& y) { return x.Cost < y.Cost; });
    EXPECT_NE(cheapest, candidates.end());

    // A tight cost budget returns fewer regions.
    NavReachableBuffer near;
    ASSERT_EQ(NavCollectReachable(zone, context, request, origin, 15.0f, 1.0f, near),
              NavStatus::Success);
    EXPECT_LT(near.Regions().size(), regions.Regions().size());
}

TEST(NavigationQuery, ExhaustedBudgetsAreExplicit)
{
    NavTestGeometry g;
    g.AddFloor(0.0f, 0.0f, 40.0f, 40.0f);
    for (float x = 3.0f; x < 38.0f; x += 4.0f)
        for (float z = 3.0f; z < 38.0f; z += 4.0f)
            g.AddBox(Vec3d(x, 0.0f, z), Vec3d(x + 1.0f, 3.0f, z + 1.0f));
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(g);
    const NavQueryRequest request = f.Request();

    NavQueryContext roomy;
    const NavLocation a = f.Project(zone, roomy, request, Vec3d(1, 0, 1));
    const NavLocation b = f.Project(zone, roomy, request, Vec3d(39, 0, 39));
    NavRouteBuffer route;
    ASSERT_EQ(NavFindRoute(zone, roomy, request, a, b, route), NavStatus::Success);

    NavQueryContext tiny(NavQueryContextConfig{ .MaxNodes = 16, .MaxCorridor = 16 });
    NavQueryDiagnostics diagnostics;
    EXPECT_EQ(NavReachable(zone, tiny, request, a, b, &diagnostics),
              NavStatus::SearchLimitReached);
    EXPECT_TRUE(diagnostics.SearchExhausted);

    NavRouteBuffer small(1, 1, 64);
    EXPECT_EQ(NavFindRoute(zone, roomy, request, a, b, small),
              NavStatus::OutputCapacityReached);
    EXPECT_TRUE(small.Steps().empty());
}

TEST(NavigationQuery, RouteValidityTracksOnlyItsDependencies)
{
    NavZoneFixture f;
    ZoneNavigation zone = f.Load(
        TwoIslands(),
        { NavZoneFixture::Link(0xA, "navigation.traversal.jump", Vec3d(7, 0, 2), Vec3d(13, 0, 2)),
          NavZoneFixture::Link(0xB, "navigation.traversal.jump", Vec3d(7, 0, 8),
                               Vec3d(13, 0, 8)) });
    NavQueryContext context;
    const std::array jumper{ f.Jump };
    const NavQueryRequest request = f.Request(jumper);
    const NavLocation a = f.Project(zone, context, request, Vec3d(2, 0, 2));
    const NavLocation b = f.Project(zone, context, request, Vec3d(18, 0, 2));
    NavRouteBuffer route;
    ASSERT_EQ(NavFindRoute(zone, context, request, a, b, route), NavStatus::Success);
    EXPECT_EQ(NavValidateRoute(zone, route), NavRouteValidity::Valid);

    // Changing a link the route does not cross leaves it valid.
    zone.SetLinkState(*zone.FindLink(NavLinkId{ 0xB }), false, 1.0f);
    EXPECT_EQ(NavValidateRoute(zone, route), NavRouteValidity::Valid);
    // Changing the one it crosses does not.
    zone.SetLinkState(*zone.FindLink(NavLinkId{ 0xA }), true, 2.0f);
    EXPECT_EQ(NavValidateRoute(zone, route), NavRouteValidity::LinkChanged);
}

TEST(NavigationQuery, ContextsDoNotContaminateEachOther)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(WallRoom());
    const NavQueryRequest request = f.Request();
    NavQueryContext first;
    NavQueryContext second;
    const NavLocation a = f.Project(zone, first, request, Vec3d(5, 0, 2));
    const NavLocation b = f.Project(zone, first, request, Vec3d(15, 0, 2));
    const NavLocation c = f.Project(zone, first, request, Vec3d(2, 0, 8));

    const float ab = NavTravelCost(zone, first, request, a, b).Cost;
    (void)NavTravelCost(zone, second, request, c, a);
    EXPECT_EQ(NavTravelCost(zone, first, request, a, b).Cost, ab);
    EXPECT_EQ(NavTravelCost(zone, second, request, a, b).Cost, ab);
}

TEST(NavigationQuery, ParallelQueriesMatchTheSerialReference)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(WallRoom());
    const NavQueryRequest request = f.Request();

    NavQueryContext setup;
    std::vector<std::pair<NavLocation, NavLocation>> pairs;
    for (int i = 0; i < 64; ++i)
    {
        const float x0 = 1.0f + static_cast<float>((i * 7) % 18);
        const float z0 = 1.0f + static_cast<float>((i * 3) % 8);
        const float x1 = 1.0f + static_cast<float>((i * 11 + 5) % 18);
        const float z1 = 1.0f + static_cast<float>((i * 5 + 2) % 8);
        const NavProjectResult p0 = NavProjectPoint(zone, setup, request, Vec3d(x0, 0, z0),
                                                    Vec3d(1, 2, 1));
        const NavProjectResult p1 = NavProjectPoint(zone, setup, request, Vec3d(x1, 0, z1),
                                                    Vec3d(1, 2, 1));
        if (p0.Status == NavStatus::Success && p1.Status == NavStatus::Success)
            pairs.emplace_back(p0.Location, p1.Location);
    }
    ASSERT_GT(pairs.size(), 32u);

    const auto run = [&](JobSystem& jobs)
    {
        std::vector<NavQueryContext> contexts(jobs.WorkerCount() + 1);
        std::vector<float> costs(pairs.size());
        std::vector<std::size_t> corners(pairs.size());
        std::vector<NavRouteBuffer> routes(jobs.WorkerCount() + 1);
        jobs.ParallelFor(static_cast<std::uint32_t>(pairs.size()), [&](std::uint32_t i)
        {
            const std::uint32_t worker = jobs.CurrentWorkerIndex();
            NavRouteBuffer& route = routes[worker];
            (void)NavFindRoute(zone, contexts[worker], request, pairs[i].first, pairs[i].second,
                               route);
            costs[i] = route.Cost;
            corners[i] = route.Corners().size();
        });
        return std::make_pair(costs, corners);
    };

    JobSystem serial(0);
    JobSystem parallel(4);
    EXPECT_EQ(run(serial), run(parallel));
}
