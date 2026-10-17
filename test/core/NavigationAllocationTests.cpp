// Steady-state navigation queries allocate nothing: a context sizes its scratch
// once, and route and reachable buffers have fixed capacity. Lives in core_tests
// because only this binary replaces operator new to count allocations.

#include "AllocationCounter.h"

#include <app/GameContexts.h>
#include <assets/cook/NavigationCook.h>
#include <core/config/EngineConfig.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/JobSystem.h>
#include <movement/MovementRegistration.h>
#include <movement/components/CharacterMovement.h>
#include <navigation/NavAgentRequests.h>
#include <navigation/NavAgentSystem.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
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
        ASSERT_FALSE(reachable.Regions().empty());
        const NavRegion region = reachable.Regions()[0].Region;
        EXPECT_EQ(NavClosestPointInRegion(zone, context, region, Vec3d(3, 0, 6)).Status,
                  NavStatus::Success);
        EXPECT_TRUE(NavRegionBounds(zone, region).has_value());
        EXPECT_EQ(NavValidateRoute(zone, route), NavRouteValidity::Valid);
    };

    queries(); // warm-up sizes the context
    const std::size_t before = AllocationCount();
    for (int i = 0; i < 10; ++i)
        queries();
    EXPECT_EQ(AllocationCount(), before) << "a warmed navigation query allocated";
}

// The follower in steady state: agents re-ordered every tick are adopted,
// planned in a batch, committed and followed, and nothing allocates once the
// slot pool, queue and planning buffers have grown to the crowd. With workers,
// the one allocation a tick is JobSystem's own per-batch control block.
class NavigationAgentAllocation : public ::testing::TestWithParam<std::uint32_t>
{
};

TEST_P(NavigationAgentAllocation, AWarmedAgentSystemAllocatesOnlyWhatTheJobSystemDoes)
{
    std::vector<Vec3d> positions;
    std::vector<std::uint32_t> indices;
    AddBox(positions, indices, Vec3d(0, -0.5f, 0), Vec3d(24, 0, 24));
    NavigationSettings settings;
    NavBuildProfile profile;
    profile.CellSize = 0.2f;
    profile.TileCells = 40;
    settings.Profiles.push_back({ "navigation.profile.humanoid", profile });
    NavigationCookInput input;
    input.Positions = positions;
    input.Indices = indices;
    input.Settings = &settings;
    NavigationCookResult cooked = CookZoneNavigation(input);
    ASSERT_TRUE(cooked.Navigation.has_value());

    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();
    RuntimeWorld runtime(schema);
    World& world = runtime.Entities();
    RegisterMovement(world);
    (void)world.GetResource<GameplayTagRegistry>().RegisterTag("navigation.profile.humanoid");
    RuntimeZoneRecord& zone = runtime.BeginZoneImport(ZoneId{ 7 });
    ASSERT_NE(AttachZoneNavigation(runtime, zone, std::move(*cooked.Navigation)), nullptr);
    ASSERT_TRUE(runtime.PublishZone(ZoneId{ 7 }, ZoneParticipation{ .Logic = true }));
    (void)runtime.BeginResidencyProcessing();
    runtime.FinalizeResidencyProcessing();

    JobSystem jobs(GetParam());
    NavAgentSystem system(NavAgentSystem::Services{ runtime, &jobs, nullptr, nullptr });
    std::vector<EntityId> agents;
    for (int i = 0; i < 24; ++i)
    {
        const EntityId agent = world.CreateEntity(zone.Partition);
        Transform3f pose = Transform3f::Identity();
        pose.Position = Vec3d(1.0f + static_cast<float>(i % 6) * 3.0f, 0.9f, 1.0f + static_cast<float>(i / 6) * 3.0f);
        world.AddComponent<WorldTransform>(agent, WorldTransform{ pose });
        world.AddComponent<CharacterMovement>(agent, CharacterMovement{});
        NavAgent config;
        config.Profile = "navigation.profile.humanoid";
        world.AddComponent<NavAgent>(agent, config);
        agents.push_back(agent);
    }

    EngineConfig config;
    RuntimeFrameLoop loop;
    std::uint64_t tick = 0;
    const auto step = [&] {
        for (std::size_t i = 0; i < agents.size(); ++i)
        {
            const float far = static_cast<float>((tick + i) % 2) * 10.0f;
            (void)RequestNavigation(world, agents[i], NavDestination::AtPoint(Vec3d(12.0f + far, 0.0f, 22.0f - far)));
        }
        const FrameZoneView& zones = runtime.BuildFrameView();
        FixedLogicContext context{ .Config = config, .Runtime = loop,
                                   .Time = FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = tick++ },
                                   .Entities = world, .Partitions = zones.Logic };
        system.FixedLogic(context);
        runtime.EndFrameView();
    };
    for (int i = 0; i < 8; ++i)
        step();
    const std::size_t before = AllocationCount();
    std::size_t batches = 0;
    for (int i = 0; i < 20; ++i)
    {
        step();
        batches += system.LastStats().PlannedInParallel ? 1 : 0;
    }
    EXPECT_GT(system.LastStats().Planned, 0u);
    EXPECT_EQ(batches > 0, GetParam() > 0);
    EXPECT_EQ(AllocationCount() - before, batches) << "the follower allocated beyond the job system's batches";
}

INSTANTIATE_TEST_SUITE_P(Workers, NavigationAgentAllocation, ::testing::Values(0u, 2u));
