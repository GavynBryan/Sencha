// bench_iteration.cpp
//
// Phase 0 benchmark 1: Iteration throughput
//
// Archetype ECS: 100k entities with {Position, Velocity}.
// One system iterates the join (Read<Position>, Write<Velocity>) and
// accumulates a checksum to prevent dead-code elimination.
//
// Sparse-set baseline: same component data in two parallel std::vector<T>
// accessed via a per-entity indirection through a sparse array (matching
// SparseSet<T> semantics from the existing engine).
//
// Timing: wall-clock via std::chrono, repeated N_ITER times, median reported.

#include <ecs/Ecs.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>

// ─── Component types ─────────────────────────────────────────────────────────

struct Position { float X, Y, Z; };
struct Velocity { float X, Y, Z; };

// ─── Timing helpers ───────────────────────────────────────────────────────────

using Clock = std::chrono::high_resolution_clock;

double NowMs()
{
    return std::chrono::duration<double, std::milli>(
               Clock::now().time_since_epoch()).count();
}

double MedianMs(std::vector<double>& samples)
{
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// ─── Archetype ECS benchmark ─────────────────────────────────────────────────

struct ArchetypeSetup
{
    World world;
    Query<Read<Position>, Write<Velocity>> q;

    explicit ArchetypeSetup(int N) : q(world)
    {
        // Hack: can't default-construct Query without world — use placement trick.
        // Simpler: build world first, then construct query.
    }
};

// Build world separately, then benchmark only iteration.
static double BenchArchetypeIteration(int N, int iters)
{
    World world;
    const ComponentId posId = world.RegisterComponent<Position>();
    const ComponentId velId = world.RegisterComponent<Velocity>();

    ArchetypeSignature sig;
    sig.set(posId);
    sig.set(velId);

    for (int i = 0; i < N; ++i)
    {
        EntityId e = world.CreateEntity(sig);
        *world.TryGet<Position>(e) = { float(i), float(i), float(i) };
        *world.TryGet<Velocity>(e) = { 1.0f, 0.5f, 0.25f };
    }

    Query<Read<Position>, Write<Velocity>> q(world);

    float checksum = 0.0f;
    const double t0 = NowMs();
    for (int it = 0; it < iters; ++it)
    {
        world.AdvanceFrame();
        q.ForEachChunk([&checksum](auto& view) {
            auto pos = view.template Read<Position>();
            auto vel = view.template Write<Velocity>();
            const uint32_t count = view.Count();
            for (uint32_t i = 0; i < count; ++i)
            {
                vel[i].X += pos[i].X * 0.016f;
                vel[i].Y += pos[i].Y * 0.016f;
                vel[i].Z += pos[i].Z * 0.016f;
                checksum += vel[i].X;
            }
        });
    }
    const double elapsed = NowMs() - t0;
    // Prevent DCE.
    if (checksum == 0.0f) std::printf("(checksum zero)\n");
    return elapsed;
}

// ─── Sparse-set baseline ──────────────────────────────────────────────────────
//
// Mirrors the SparseSet<T> layout: dense arrays + sparse indirection array.
// The join is: for each entity in the velocity set, look up its position
// via a sparse hop — matching what RenderExtractionSystem does today.

struct SparseSetBaseline
{
    std::vector<Position>  PosDense;
    std::vector<uint32_t>  PosSparse; // entity_index → dense index (UINT32_MAX = absent)
    std::vector<Velocity>  VelDense;
    std::vector<uint32_t>  VelOwners; // dense index → entity index

    void Add(uint32_t entity, Position p, Velocity v)
    {
        if (entity >= PosSparse.size())
            PosSparse.resize(entity + 1, UINT32_MAX);
        PosSparse[entity] = static_cast<uint32_t>(PosDense.size());
        PosDense.push_back(p);
        VelDense.push_back(v);
        VelOwners.push_back(entity);
    }
};

static double BenchSparseSetIteration(int N, int iters)
{
    SparseSetBaseline ss;
    for (int i = 0; i < N; ++i)
        ss.Add(uint32_t(i),
               Position{ float(i), float(i), float(i) },
               Velocity{ 1.0f, 0.5f, 0.25f });

    float checksum = 0.0f;
    const double t0 = NowMs();
    for (int it = 0; it < iters; ++it)
    {
        for (size_t di = 0; di < ss.VelDense.size(); ++di)
        {
            const uint32_t entity = ss.VelOwners[di];
            const uint32_t posIdx = ss.PosSparse[entity]; // sparse hop
            if (posIdx == UINT32_MAX) continue;

            const Position& pos = ss.PosDense[posIdx];
            Velocity& vel       = ss.VelDense[di];
            vel.X += pos.X * 0.016f;
            vel.Y += pos.Y * 0.016f;
            vel.Z += pos.Z * 0.016f;
            checksum += vel.X;
        }
    }
    const double elapsed = NowMs() - t0;
    if (checksum == 0.0f) std::printf("(checksum zero)\n");
    return elapsed;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    constexpr int N        = 100'000;
    constexpr int ITERS    = 100; // iterations per timed run
    constexpr int REPEATS  = 11;  // repeated timed runs for median

    std::printf("=== Phase 0: Iteration throughput benchmark ===\n");
    std::printf("Entities: %d  Iterations per run: %d  Runs: %d\n", N, ITERS, REPEATS);
    std::printf("(World setup is excluded from timing; only iteration loop is timed.)\n\n");

    // ── Archetype ECS ────────────────────────────────────────────────────────
    {
        std::vector<double> times;
        for (int r = 0; r < REPEATS; ++r)
            times.push_back(BenchArchetypeIteration(N, ITERS));
        const double med = MedianMs(times);
        std::printf("Archetype ECS  median total: %7.2f ms  per-iter: %.3f ms\n",
                    med, med / ITERS);
    }

    // ── Sparse-set baseline ──────────────────────────────────────────────────
    {
        std::vector<double> times;
        for (int r = 0; r < REPEATS; ++r)
            times.push_back(BenchSparseSetIteration(N, ITERS));
        const double med = MedianMs(times);
        std::printf("Sparse-set     median total: %7.2f ms  per-iter: %.3f ms\n",
                    med, med / ITERS);
    }

    return 0;
}
