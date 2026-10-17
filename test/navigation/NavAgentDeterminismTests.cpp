// G: what entity-keyed ordering guarantees. The same scenario gives the same
// per-tick record whatever the worker count, run after run, and whatever the
// chunk layout; creation order matters only when the planning queue is full.

#include "NavAgentHarness.h"

#include <controller/LookOrientation.h>
#include <navigation/NavigationGeometry.h>
#include <physics/components/Collider.h>
#include <physics/components/RigidBody.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <map>
#include <vector>

namespace
{
    struct AgentTickRecord
    {
        std::uint32_t X = 0;
        std::uint32_t Y = 0;
        std::uint32_t Z = 0;
        NavAgentPhase Phase = NavAgentPhase::Idle;
        NavAgentFailure Failure = NavAgentFailure::None;
        NavRepathReason LastRepath = NavRepathReason::None;
        std::uint16_t Repaths = 0;

        friend bool operator==(const AgentTickRecord&, const AgentTickRecord&) = default;
    };

    // A floor with pillars, crossed by a wall with three gaps that appears mid-run.
    NavTestGeometry PillarFloor()
    {
        NavTestGeometry g;
        g.AddFloor(0.0f, 0.0f, 48.0f, 48.0f);
        for (int i = 0; i < 8; ++i)
            for (int k = 0; k < 8; ++k)
            {
                const float x = 3.4f + 5.6f * static_cast<float>(i);
                const float z = 3.4f + 5.6f * static_cast<float>(k);
                g.AddBox(Vec3d(x - 0.3f, 0.0f, z - 0.3f), Vec3d(x + 0.3f, 3.0f, z + 0.3f));
            }
        return g;
    }

    void RaiseWall(NavAgentHarness& harness)
    {
        for (const auto& [minZ, maxZ] : { std::pair{ 0.0f, 10.0f }, std::pair{ 13.0f, 30.0f }, std::pair{ 33.0f, 48.0f } })
        {
            World& world = harness.Entities();
            const EntityId wall = world.CreateEntity(harness.Partition);
            Transform3f pose = Transform3f::Identity();
            pose.Position = Vec3d(24.0f, 1.5f, (minZ + maxZ) * 0.5f);
            world.AddComponent<LocalTransform>(wall, LocalTransform{ pose });
            world.AddComponent<WorldTransform>(wall, WorldTransform{ pose });
            Collider collider;
            collider.Shape = CollisionShape::MakeBox(Vec3d(0.4f, 1.5f, (maxZ - minZ) * 0.5f));
            world.AddComponent<Collider>(wall, collider);
            world.AddComponent<RigidBody>(wall, RigidBody{ .Motion = BodyMotion::Static });
            world.AddComponent<NavigationGeometry>(wall, NavigationGeometry{});
        }
    }

    struct Scenario
    {
        std::uint32_t Workers = 0;
        std::size_t Agents = 64;
        // Every third agent also carries an unrelated tag, which puts it in
        // another archetype and so changes chunk iteration.
        bool MixedArchetypes = false;
        // Agents are created in this order of label; empty means label order.
        std::vector<std::size_t> CreationOrder;
        int Ticks = 900;
    };

    // The per-tick record for each agent, by its label.
    std::vector<std::vector<AgentTickRecord>> RecordScenario(const Scenario& scenario)
    {
        NavAgentHarness harness(PillarFloor(), {}, scenario.Workers);
        std::vector<std::size_t> order = scenario.CreationOrder;
        if (order.empty())
            for (std::size_t i = 0; i < scenario.Agents; ++i)
                order.push_back(i);
        std::vector<EntityId> byLabel(scenario.Agents);
        for (const std::size_t label : order)
        {
            const float row = static_cast<float>(label / 8);
            const float column = static_cast<float>(label % 8);
            byLabel[label] = harness.SpawnAgent(Vec3d(2.0f + column * 2.8f, 0.0f, 2.0f + row * 5.6f));
            if (scenario.MixedArchetypes && label % 3 == 0)
                harness.Entities().AddComponent<LocalLookControl>(byLabel[label], LocalLookControl{});
        }
        for (std::size_t label = 0; label < scenario.Agents; ++label)
        {
            const float row = static_cast<float>((label * 5) % 8);
            const float column = static_cast<float>((label * 3) % 8);
            (void)harness.GoTo(byLabel[label], Vec3d(46.0f - column * 2.8f, 0.0f, 46.0f - row * 5.6f));
        }

        std::vector<std::vector<AgentTickRecord>> records(scenario.Agents);
        for (int tick = 0; tick < scenario.Ticks; ++tick)
        {
            if (tick == 90)
                RaiseWall(harness);
            harness.Step();
            for (std::size_t label = 0; label < scenario.Agents; ++label)
            {
                const NavAgentProgress& progress = harness.Progress(byLabel[label]);
                const Vec3d p = harness.Position(byLabel[label]);
                records[label].push_back(AgentTickRecord{ std::bit_cast<std::uint32_t>(p.X),
                                                          std::bit_cast<std::uint32_t>(p.Y),
                                                          std::bit_cast<std::uint32_t>(p.Z), progress.Phase,
                                                          progress.Failure, progress.LastRepath, progress.Repaths });
            }
        }
        return records;
    }

    Scenario ScenarioWithWorkers(std::uint32_t workers)
    {
        Scenario scenario;
        scenario.Workers = workers;
        return scenario;
    }

    std::size_t ArrivalsSeen(const std::vector<std::vector<AgentTickRecord>>& records)
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            records, [](const auto& agent) { return agent.back().Phase == NavAgentPhase::Arrived; }));
    }

    std::size_t RepathsSeen(const std::vector<std::vector<AgentTickRecord>>& records)
    {
        std::size_t repaths = 0;
        for (const std::vector<AgentTickRecord>& agent : records)
            repaths += agent.back().Repaths;
        return repaths;
    }
}

TEST(NavAgentDeterminism, TheWorkerCountChangesNothingAndRunsRepeat)
{
    const auto reference = RecordScenario(ScenarioWithWorkers(0));
    EXPECT_GT(RepathsSeen(reference), 8u) << "the wall must invalidate a good share of the routes";
    EXPECT_GT(ArrivalsSeen(reference), 48u) << "the agents must actually walk the scenario";
    EXPECT_EQ(RecordScenario(ScenarioWithWorkers(0)), reference) << "a repeat of the serial run";
    for (const std::uint32_t workers : { 1u, 4u, JobSystem::DefaultWorkerCount() })
        EXPECT_EQ(RecordScenario(ScenarioWithWorkers(workers)), reference) << workers << " workers";
}

TEST(NavAgentDeterminism, ChunkLayoutChangesNothing)
{
    const auto reference = RecordScenario(ScenarioWithWorkers(4));
    Scenario mixed = ScenarioWithWorkers(4);
    mixed.MixedArchetypes = true;
    EXPECT_EQ(RecordScenario(mixed), reference);
}

// With the queue never full, no agent's admission depends on another's id, so
// shuffling which entity each agent becomes changes nothing about any agent.
// (A full queue plans lower ids first by design, and that is not asserted.)
TEST(NavAgentDeterminism, CreationOrderChangesNothingWhileTheQueueKeepsUp)
{
    Scenario few = ScenarioWithWorkers(4);
    few.Agents = 12;
    few.Ticks = 600;
    Scenario shuffled = few;
    shuffled.CreationOrder = { 7, 2, 11, 0, 9, 4, 1, 10, 5, 3, 8, 6 };
    EXPECT_EQ(RecordScenario(shuffled), RecordScenario(few));
}
