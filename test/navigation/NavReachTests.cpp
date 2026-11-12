#include "NavAgentHarness.h"

#include <navigation/NavigationQuery.h>

#include <gtest/gtest.h>

#include <array>

TEST(NavReach, AnAgentsRequestNamesItsZoneAndProfile)
{
    NavAgentHarness harness(TwoIslands());
    const EntityId agent = harness.SpawnAgent(Vec3d(4.0f, 0.0f, 5.0f));
    std::array<GameplayTagId, kNavMaxTraversalKinds> capabilities{};
    const std::optional<NavQueryRequest> request =
        NavAgentQueryRequest(harness.Entities(), harness.Runtime, agent, capabilities);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->Zone, harness.Zone);
    EXPECT_EQ(request->Profile, harness.Tag("navigation.profile.humanoid"));

    const EntityId stranger = harness.Entities().CreateEntity(harness.Partition);
    EXPECT_FALSE(NavAgentQueryRequest(harness.Entities(), harness.Runtime, stranger, capabilities).has_value())
        << "no agent, no request";
}

TEST(NavReach, ReachesAcrossOneIslandAndNotToTheOther)
{
    NavZoneFixture f;
    const ZoneNavigation zone = f.Load(TwoIslands());
    NavQueryContext context;
    const NavQueryRequest request = f.Request();
    const Vec3d extents(1.0f, 2.0f, 1.0f);
    const auto reach = [&](Vec3d from, Vec3d to, float tolerance) {
        return NavCheckReach(zone, context, request,
                             NavReachCheck{ .From = from, .FromExtents = extents, .To = to, .ToExtents = extents,
                                            .Tolerance = tolerance });
    };
    const float any = std::numeric_limits<float>::infinity();
    EXPECT_EQ(reach(Vec3d(2, 0, 5), Vec3d(6, 0, 5), any), NavReach::Reachable);
    EXPECT_EQ(reach(Vec3d(2, 0, 5), Vec3d(16, 0, 5), any), NavReach::Unreachable);
    EXPECT_EQ(reach(Vec3d(50, 0, 50), Vec3d(6, 0, 5), any), NavReach::StartOffNavigation);
    EXPECT_EQ(reach(Vec3d(2, 0, 5), Vec3d(6, 1.5f, 5), 0.5f), NavReach::Unreachable)
        << "the end projects too far below the point asked for";
}
