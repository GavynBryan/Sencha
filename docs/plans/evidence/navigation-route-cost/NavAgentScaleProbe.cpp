// Measurement probe for docs/plans/navigation-agent.md (Stage 6). Copy into
// test/navigation/ to run; not built from the evidence folder.
//
// Drives NavAgentSystem alone over a cooked zone, moving each agent along its
// own wish at walking speed instead of through physics, so the times are the
// follower's: planning ticks and follow-only ticks are reported apart.

#include "NavigationFixture.h"

#include <app/GameContexts.h>
#include <core/config/EngineConfig.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/JobSystem.h>
#include <movement/MovementIntent.h>
#include <movement/MovementRegistration.h>
#include <movement/components/CharacterMovement.h>
#include <navigation/NavAgentRequests.h>
#include <navigation/NavAgentSystem.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace
{
    using ProbeClock = std::chrono::steady_clock;

    double MedianOf(std::vector<double> samples)
    {
        if (samples.empty())
            return 0.0;
        std::sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];
    }

    double MaximumOf(const std::vector<double>& samples)
    {
        return samples.empty() ? 0.0 : *std::max_element(samples.begin(), samples.end());
    }

    NavBuildProfile TemplateBuildProfile()
    {
        NavBuildProfile profile;
        profile.Radius = 0.3f;
        profile.Height = 1.8f;
        profile.MaxSlopeDegrees = 50.0f;
        profile.MaxClimb = 0.35f;
        profile.CellSize = 0.15f;
        profile.CellHeight = 0.1f;
        profile.TileCells = 32;
        return profile;
    }

    // The 96 m maze of the route-cost probe.
    NavTestGeometry MazeGeometry()
    {
        NavTestGeometry g;
        const float side = 96.0f;
        g.AddFloor(0.0f, 0.0f, side, side);
        for (float x = 4.0f; x < side - 1.0f; x += 4.0f)
            for (float z = 4.0f; z < side - 1.0f; z += 4.0f)
                g.AddBox(Vec3d(x - 0.5f, 0.0f, z - 0.5f), Vec3d(x + 0.5f, 3.0f, z + 0.5f));
        int row = 0;
        for (float z = 12.0f; z < side - 6.0f; z += 12.0f, ++row)
        {
            const float gap = (row % 2 == 0) ? side - 6.0f : 2.0f;
            if (gap > 4.0f)
                g.AddBox(Vec3d(0.0f, 0.0f, z - 0.25f), Vec3d(gap, 3.0f, z + 0.25f));
            g.AddBox(Vec3d(gap + 4.0f, 0.0f, z - 0.25f), Vec3d(side, 3.0f, z + 0.25f));
        }
        return g;
    }

    struct ScaleResult
    {
        double PlanningTickMedianUs = 0.0;
        double PlanningTickMaxUs = 0.0;
        double FollowTickMedianUs = 0.0;
        double FollowTickMaxUs = 0.0;
        int TicksToPlanEveryone = 0;
    };

    ScaleResult MeasureCrowd(std::size_t agentCount, std::uint32_t workers)
    {
        NavigationSettings settings;
        settings.Profiles.push_back({ "navigation.profile.humanoid", TemplateBuildProfile() });
        const NavTestGeometry geometry = MazeGeometry();
        NavigationCookInput input;
        input.Positions = geometry.Positions;
        input.Indices = geometry.Indices;
        input.Settings = &settings;
        NavigationCookResult cooked = CookZoneNavigation(input);

        WorldComponentSchema schema;
        RegisterEngineRuntimeComponents(schema);
        schema.Seal();
        RuntimeWorld runtime(schema);
        World& world = runtime.Entities();
        RegisterMovement(world);
        (void)world.GetResource<GameplayTagRegistry>().RegisterTag("navigation.profile.humanoid");
        RuntimeZoneRecord& zone = runtime.BeginZoneImport(ZoneId{ 7 });
        (void)AttachZoneNavigation(runtime, zone, std::move(*cooked.Navigation));
        (void)runtime.PublishZone(ZoneId{ 7 }, ZoneParticipation{ .Logic = true });
        (void)runtime.BeginResidencyProcessing();
        runtime.FinalizeResidencyProcessing();

        JobSystem jobs(workers);
        NavAgentSystem system(NavAgentSystem::Services{ runtime, &jobs, nullptr, nullptr });
        std::vector<EntityId> agents;
        std::uint64_t seed = 0x9e3779b97f4a7c15ull;
        const auto random = [&seed] {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            return 2.0f + static_cast<float>((seed >> 33) & 0xffff) / 65535.0f * 92.0f;
        };
        for (std::size_t i = 0; i < agentCount; ++i)
        {
            const EntityId agent = world.CreateEntity(zone.Partition);
            Transform3f pose = Transform3f::Identity();
            pose.Position = Vec3d(random(), 0.9f, random());
            world.AddComponent<WorldTransform>(agent, WorldTransform{ pose });
            world.AddComponent<CharacterMovement>(agent, CharacterMovement{});
            NavAgent config;
            config.Profile = "navigation.profile.humanoid";
            world.AddComponent<NavAgent>(agent, config);
            agents.push_back(agent);
        }
        const auto orderEveryone = [&] {
            for (const EntityId agent : agents)
                (void)RequestNavigation(world, agent, NavDestination::AtPoint(Vec3d(random(), 0.0f, random())));
        };

        EngineConfig config;
        RuntimeFrameLoop loop;
        std::uint64_t tick = 0;
        std::vector<double> planning;
        std::vector<double> following;
        ScaleResult result;
        const auto step = [&] {
            const FrameZoneView& zones = runtime.BuildFrameView();
            FixedLogicContext context{ .Config = config, .Runtime = loop,
                                       .Time = FixedSimTime{ .DeltaSeconds = 1.0 / 60.0, .TickIndex = tick++ },
                                       .Entities = world, .Partitions = zones.Logic };
            const auto begin = ProbeClock::now();
            system.FixedLogic(context);
            const double us = std::chrono::duration<double, std::micro>(ProbeClock::now() - begin).count();
            runtime.EndFrameView();
            (system.LastStats().Planned > 0 ? planning : following).push_back(us);
            for (const EntityId agent : agents)
            {
                const MovementIntent* intent = std::as_const(world).TryGet<MovementIntent>(agent);
                world.TryGet<WorldTransform>(agent)->Value.Position += intent->WishDir * (6.0f / 60.0f);
            }
        };

        // Everyone ordered at once, then a second storm of fresh orders mid-walk.
        for (int storm = 0; storm < 2; ++storm)
        {
            orderEveryone();
            int ticks = 0;
            do
            {
                step();
                ++ticks;
            } while (system.Queue().Size() > 0);
            result.TicksToPlanEveryone = std::max(result.TicksToPlanEveryone, ticks);
            for (int i = 0; i < 120; ++i)
                step();
        }
        result.PlanningTickMedianUs = MedianOf(planning);
        result.PlanningTickMaxUs = MaximumOf(planning);
        result.FollowTickMedianUs = MedianOf(following);
        result.FollowTickMaxUs = MaximumOf(following);
        return result;
    }
}

TEST(NavAgentScaleProbe, Measure)
{
    for (const std::uint32_t workers : { 0u, JobSystem::DefaultWorkerCount() })
        for (const std::size_t agents : { 1u, 32u, 128u, 256u })
        {
            const ScaleResult r = MeasureCrowd(agents, workers);
            std::printf("[scale] agents %3zu workers %2u: planning tick median %7.0f us max %7.0f us | follow tick "
                        "median %6.1f us max %6.1f us | ticks to plan a storm %d\n",
                        agents, workers, r.PlanningTickMedianUs, r.PlanningTickMaxUs, r.FollowTickMedianUs,
                        r.FollowTickMaxUs, r.TicksToPlanEveryone);
        }
}
