#include "NavigationFixture.h"

#include <jobs/JobSystem.h>
#include <navigation/NavRoutePlanning.h>

#include <cstring>
#include <vector>

namespace
{
    // Two islands joined only by a jump link, and a hall a second link shortcuts.
    struct Scene
    {
        NavZoneFixture Fixture;
        ZoneNavigation Zone;
        NavLinkId Shortcut{ 0x5157 };

        Scene()
            : Zone(Fixture.Load(Geometry(),
                                { NavZoneFixture::Link(0x1, "navigation.traversal.jump", Vec3d(7.5f, 0.0f, 5.0f),
                                                       Vec3d(12.5f, 0.0f, 5.0f)),
                                  NavZoneFixture::Link(0x5157, "navigation.traversal.teleport",
                                                       Vec3d(1.0f, 0.0f, 25.0f), Vec3d(38.0f, 0.0f, 25.0f), NavLinkDirectionForward, 0.5f) }))
        {
        }

        static NavTestGeometry Geometry()
        {
            NavTestGeometry g = TwoIslands();
            g.AddFloor(0.0f, 20.0f, 40.0f, 30.0f);
            return g;
        }

        NavPlanInput Input(Vec3d from, Vec3d to, std::initializer_list<GameplayTagId> capabilities,
                           std::initializer_list<NavLinkId> avoid = {}) const
        {
            NavPlanInput input;
            input.Zone = &Zone;
            input.Profile = Fixture.Humanoid;
            input.From = from;
            input.To = to;
            input.Extents = Vec3d(1.0f, 2.0f, 1.0f);
            for (const GameplayTagId capability : capabilities)
                input.Capabilities[input.CapabilityCount++] = capability;
            for (const NavLinkId link : avoid)
                input.AvoidLinks[input.AvoidLinkCount++] = link;
            return input;
        }

        std::vector<NavPlanInput> Batch() const
        {
            std::vector<NavPlanInput> batch;
            for (int i = 0; i < 24; ++i)
            {
                const float z = 1.0f + static_cast<float>(i % 8);
                batch.push_back(Input(Vec3d(1.0f, 0.0f, z), Vec3d(18.0f, 0.0f, 9.0f - z), { Fixture.Jump }));
                batch.push_back(Input(Vec3d(2.0f, 0.0f, 22.0f + z * 0.5f), Vec3d(37.0f, 0.0f, 28.0f),
                                      { Fixture.Teleport }, i % 2 == 0 ? std::initializer_list<NavLinkId>{ Shortcut }
                                                                       : std::initializer_list<NavLinkId>{}));
            }
            return batch;
        }
    };

    bool SameRoutes(NavRoutePlanner& a, NavRoutePlanner& b, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (a.Output(i).Status != b.Output(i).Status
                || a.Output(i).Diagnostics.NodesVisited != b.Output(i).Diagnostics.NodesVisited)
                return false;
            const auto ca = a.Route(i).Corners();
            const auto cb = b.Route(i).Corners();
            if (ca.size() != cb.size() || a.Route(i).Steps().size() != b.Route(i).Steps().size())
                return false;
            for (std::size_t k = 0; k < ca.size(); ++k)
                if (ca[k] != cb[k])
                    return false;
        }
        return true;
    }

    bool CrossesLink(const NavRouteBuffer& route, NavLinkId link)
    {
        for (const NavRouteStep& step : route.Steps())
            if (step.Kind == NavRouteStepKind::Traverse && step.Link == link)
                return true;
        return false;
    }
}

TEST(NavRoutePlanning, AParallelBatchMatchesTheSerialReference)
{
    const Scene scene;
    const std::vector<NavPlanInput> batch = scene.Batch();
    JobSystem serialJobs(0);
    JobSystem parallelJobs(4);
    NavRoutePlanner serial(&serialJobs, NavQueryContextConfig{}, NavRouteCapacity{});
    NavRoutePlanner parallel(&parallelJobs, NavQueryContextConfig{}, NavRouteCapacity{});
    serial.Plan(batch, 2);
    parallel.Plan(batch, 2);
    EXPECT_EQ(serial.ContextCount(), 1u);
    EXPECT_EQ(parallel.ContextCount(), 5u);
    ASSERT_TRUE(SameRoutes(serial, parallel, batch.size()));
    for (std::size_t i = 0; i < batch.size(); ++i)
        EXPECT_EQ(serial.Output(i).Status, NavStatus::Success) << "input " << i;
}

TEST(NavRoutePlanning, ASmallBatchStaysOnTheCaller)
{
    const Scene scene;
    const std::vector<NavPlanInput> batch = scene.Batch();
    JobSystem jobs(4);
    NavRoutePlanner planner(&jobs, NavQueryContextConfig{}, NavRouteCapacity{});
    planner.Plan(std::span(batch).first(1), 2);
    EXPECT_EQ(planner.ContextCount(), 1u);
    EXPECT_EQ(planner.Output(0).Status, NavStatus::Success);
}

TEST(NavRoutePlanning, InputsCarryTheirOwnConstraintsWhereverTheyAreCopied)
{
    const Scene scene;
    std::vector<NavPlanInput> gathered{
        scene.Input(Vec3d(1.0f, 0.0f, 5.0f), Vec3d(18.0f, 0.0f, 5.0f), { scene.Fixture.Jump }),
        scene.Input(Vec3d(2.0f, 0.0f, 25.0f), Vec3d(37.0f, 0.0f, 25.0f), { scene.Fixture.Teleport },
                    { scene.Shortcut }),
        scene.Input(Vec3d(2.0f, 0.0f, 25.0f), Vec3d(37.0f, 0.0f, 25.0f), { scene.Fixture.Teleport }),
    };

    // Copy the batch somewhere else, make that storage move by growing it, and
    // wreck the originals. A request spanning into the gathered storage would now
    // read garbage capabilities and avoid-lists.
    std::vector<NavPlanInput> moved = gathered;
    for (int i = 0; i < 64; ++i)
        moved.push_back(moved[static_cast<std::size_t>(i % 3)]);
    moved.resize(3);
    moved.shrink_to_fit();
    for (NavPlanInput& input : gathered)
    {
        std::memset(static_cast<void*>(input.Capabilities.data()), 0xA5, sizeof(input.Capabilities));
        std::memset(static_cast<void*>(input.AvoidLinks.data()), 0xA5, sizeof(input.AvoidLinks));
    }

    JobSystem jobs(2);
    NavRoutePlanner planner(&jobs, NavQueryContextConfig{}, NavRouteCapacity{});
    planner.Plan(moved, 2);
    ASSERT_EQ(planner.Output(0).Status, NavStatus::Success) << "the jump capability went with the copy";
    EXPECT_TRUE(CrossesLink(planner.Route(0), NavLinkId{ 0x1 }));
    ASSERT_EQ(planner.Output(1).Status, NavStatus::Success);
    EXPECT_FALSE(CrossesLink(planner.Route(1), scene.Shortcut)) << "the avoid-list went with the copy";
    ASSERT_EQ(planner.Output(2).Status, NavStatus::Success);
    EXPECT_TRUE(CrossesLink(planner.Route(2), scene.Shortcut));
}

TEST(NavRoutePlanning, EndpointsOffTheMeshAreNamedForTheirEnd)
{
    const Scene scene;
    const std::vector<NavPlanInput> batch{
        scene.Input(Vec3d(100.0f, 0.0f, 100.0f), Vec3d(5.0f, 0.0f, 5.0f), {}),
        scene.Input(Vec3d(5.0f, 0.0f, 5.0f), Vec3d(100.0f, 0.0f, 100.0f), {}),
        scene.Input(Vec3d(1.0f, 0.0f, 5.0f), Vec3d(18.0f, 0.0f, 5.0f), {}),
    };
    NavRoutePlanner planner(nullptr, NavQueryContextConfig{}, NavRouteCapacity{});
    planner.Plan(batch, 2);
    EXPECT_EQ(planner.Output(0).Status, NavStatus::InvalidStart);
    EXPECT_EQ(planner.Output(1).Status, NavStatus::InvalidDestination);
    EXPECT_EQ(planner.Output(2).Status, NavStatus::NoPath) << "no jump capability, no way across";
}
