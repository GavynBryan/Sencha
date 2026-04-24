// bench_churn.cpp
//
// Phase 0 benchmark 2: Structural churn
//
// Setup: 100k entities with components {A, B}.
// Churn:
//   1. Add component C to 10k of those entities (via CommandBuffer).
//   2. Remove component C from those same 10k entities (via CommandBuffer).
//   3. Flush both batches.
//
// Two flush strategies are compared:
//   - Naive: flush Add commands one at a time (simulates no batching).
//   - Batched: group by source archetype (CommandBuffer::Flush default).
//
// Exit criterion: flush time comfortably under 2 ms in an optimized build.

#include <ecs/Ecs.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>

struct CompA { float V; };
struct CompB { float V; };
struct CompC { float V; };

using Clock = std::chrono::high_resolution_clock;

double NowMs()
{
    return std::chrono::duration<double, std::milli>(
               Clock::now().time_since_epoch()).count();
}

// Build a fresh world with N entities {A,B} and return the entity list.
static std::pair<World, std::vector<EntityId>> BuildWorld(int N)
{
    World world;
    world.RegisterComponent<CompA>();
    world.RegisterComponent<CompB>();
    world.RegisterComponent<CompC>();

    const ComponentId aId = world.GetComponentId<CompA>();
    const ComponentId bId = world.GetComponentId<CompB>();

    ArchetypeSignature sig;
    sig.set(aId);
    sig.set(bId);

    std::vector<EntityId> entities;
    entities.reserve(N);
    for (int i = 0; i < N; ++i)
    {
        EntityId e = world.CreateEntity(sig);
        *world.TryGet<CompA>(e) = { float(i) };
        *world.TryGet<CompB>(e) = { float(i) * 2 };
        entities.push_back(e);
    }
    return { std::move(world), std::move(entities) };
}

int main()
{
    constexpr int N_TOTAL  = 100'000;
    constexpr int N_CHURN  = 10'000;
    constexpr int REPEATS  = 7;

    std::printf("=== Phase 0: Structural churn benchmark ===\n");
    std::printf("Total entities: %d  Churned: %d  Runs: %d\n\n",
                N_TOTAL, N_CHURN, REPEATS);

    std::vector<double> addTimes, removeTimes, totalTimes;

    for (int r = 0; r < REPEATS; ++r)
    {
        auto [world, entities] = BuildWorld(N_TOTAL);

        CommandBuffer cmds(world);

        // ── Queue Add C for first N_CHURN entities ────────────────────────
        for (int i = 0; i < N_CHURN; ++i)
            cmds.AddComponent<CompC>(entities[i], CompC{ float(i) });

        const double t0 = NowMs();
        cmds.Flush(); // flush Add commands
        const double addTime = NowMs() - t0;

        // ── Queue Remove C ────────────────────────────────────────────────
        for (int i = 0; i < N_CHURN; ++i)
            cmds.RemoveComponent<CompC>(entities[i]);

        const double t1 = NowMs();
        cmds.Flush(); // flush Remove commands
        const double removeTime = NowMs() - t1;

        addTimes.push_back(addTime);
        removeTimes.push_back(removeTime);
        totalTimes.push_back(addTime + removeTime);
    }

    auto median = [](std::vector<double>& v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };

    std::printf("Add C flush    median: %.3f ms\n", median(addTimes));
    std::printf("Remove C flush median: %.3f ms\n", median(removeTimes));
    std::printf("Total churn    median: %.3f ms\n", median(totalTimes));
    std::printf("\nTarget: total < 2.0 ms in optimized build.\n");

    return 0;
}
