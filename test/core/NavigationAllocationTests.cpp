// Steady-state navigation queries allocate nothing: a context sizes its scratch
// once, and route and reachable buffers have fixed capacity. Lives in core_tests
// because only this binary replaces operator new to count allocations.

#include "AllocationCounter.h"

#include <assets/cook/NavigationCook.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <navigation/NavLinkComponent.h>
#include <navigation/NavigationFile.h>
#include <navigation/NavigationQuery.h>
#include <navigation/ZoneNavigation.h>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace
{
    void AddBox(std::vector<Vec3d>& positions, std::vector<std::uint32_t>& indices,
                const Vec3d& min, const Vec3d& max)
    {
        // Top face only, wound counter-clockwise seen from above.
        const std::uint32_t base = static_cast<std::uint32_t>(positions.size());
        positions.insert(positions.end(), { Vec3d(min.X, max.Y, min.Z), Vec3d(min.X, max.Y, max.Z),
                                            Vec3d(max.X, max.Y, max.Z), Vec3d(max.X, max.Y, min.Z) });
        indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
}

TEST(NavigationAllocation, WarmedQueriesAllocateNothing)
{
    std::vector<Vec3d> positions;
    std::vector<std::uint32_t> indices;
    AddBox(positions, indices, Vec3d(0, -0.5f, 0), Vec3d(8, 0, 10));
    AddBox(positions, indices, Vec3d(12, -0.5f, 0), Vec3d(20, 0, 10));

    GameplayTagRegistry tags;
    const GameplayTagId humanoid = *tags.RegisterTag("navigation.profile.humanoid");
    const GameplayTagId jump = *tags.RegisterTag("navigation.traversal.jump");

    NavigationSettings settings;
    NavBuildProfile profile;
    profile.CellSize = 0.2f;
    profile.TileCells = 40;
    settings.Profiles.push_back({ "navigation.profile.humanoid", profile });
    NavLinkRecord link;
    link.Id = NavLinkId{ 1 };
    link.Traversal = "navigation.traversal.jump";
    link.Directions = NavLinkDirectionForward;
    link.Entry = Vec3d(7, 0, 5);
    link.Exit = Vec3d(13, 0, 5);
    NavigationCookInput input;
    input.Positions = positions;
    input.Indices = indices;
    input.Settings = &settings;
    input.Links = std::span<const NavLinkRecord>(&link, 1);
    NavigationCookResult cooked = CookZoneNavigation(input);
    ASSERT_TRUE(cooked.Navigation.has_value());
    ZoneNavigation zone;
    ASSERT_TRUE(zone.Load(ZoneId{ 1 }, std::move(*cooked.Navigation), &tags));

    NavQueryContext context;
    NavRouteBuffer route;
    NavReachableBuffer reachable;
    const std::array capabilities{ jump };
    NavQueryRequest request;
    request.Zone = ZoneId{ 1 };
    request.Profile = humanoid;
    request.Capabilities = capabilities;

    const auto queries = [&]
    {
        const NavLocation a =
            NavProjectPoint(zone, context, request, Vec3d(2, 0, 5), Vec3d(1, 2, 1)).Location;
        const NavLocation b =
            NavProjectPoint(zone, context, request, Vec3d(18, 0, 5), Vec3d(1, 2, 1)).Location;
        EXPECT_EQ(NavFindRoute(zone, context, request, a, b, route), NavStatus::Success);
        EXPECT_EQ(NavTravelCost(zone, context, request, a, b).Status, NavStatus::Success);
        EXPECT_EQ(NavReachable(zone, context, request, a, b), NavStatus::Success);
        (void)NavRaycast(zone, context, request, a, Vec3d(2, 0, 9));
        EXPECT_EQ(NavCollectReachable(zone, context, request, a, 10.0f, 50.0f, reachable),
                  NavStatus::Success);
        EXPECT_EQ(NavValidateRoute(zone, route), NavRouteValidity::Valid);
    };

    queries(); // warm-up sizes the context
    const std::size_t before = AllocationCount();
    for (int i = 0; i < 10; ++i)
        queries();
    EXPECT_EQ(AllocationCount(), before) << "a warmed navigation query allocated";
}
