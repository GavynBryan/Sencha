// Phase 4 ECS benchmark
//
// Measures: transform propagation throughput vs pre-migration baseline,
// render extraction chunk-query throughput, RenderQueueItem sort time,
// archetype count and memory footprint under representative scenes.
//
// Build (from repo root):
//   g++-14 -std=c++20 -O2 -march=native -DNDEBUG -DSENCHA_ENABLE_VULKAN \
//     -I engine/include \
//     -I build-verify/_deps/vulkanmemoryallocator-src/include \
//     example/EcsBenchmark/EcsBenchmark.cpp \
//     build-verify/engine/libsencha_engine.a \
//     -lSDL3 -lvulkan \
//     -o build-verify/EcsBenchmark

#include <ecs/Ecs.h>
#include <render/RenderQueue.h>
#include <render/StaticMeshComponent.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

double ElapsedUs(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double, std::micro>(end - start).count();
}

struct SampleStats
{
    double MeanUs      = 0.0;
    double MedianUs    = 0.0;
    double P95Us       = 0.0;
    double NsPerEntity = 0.0;
};

SampleStats ComputeStats(std::vector<double>& samples, size_t entityCount)
{
    std::sort(samples.begin(), samples.end());
    SampleStats s;
    s.MeanUs      = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    s.MedianUs    = samples[samples.size() / 2];
    const size_t p95Idx = static_cast<size_t>(samples.size() * 0.95);
    s.P95Us       = samples[std::min(p95Idx, samples.size() - 1)];
    s.NsPerEntity = (s.MeanUs * 1000.0) / static_cast<double>(entityCount);
    return s;
}

Transform3f MakeTransform(size_t i)
{
    const float x = static_cast<float>(i % 17) * 0.1f;
    const float y = static_cast<float>((i * 7) % 19) * 0.1f;
    const float z = static_cast<float>((i * 11) % 23) * 0.1f;
    return Transform3f(
        Vec3d(x, y, z),
        Quatf::FromAxisAngle(Vec3d(0, 1, 0), static_cast<float>(i % 31) * 0.01f),
        Vec3d(1, 1, 1));
}

size_t ParentFor(size_t i, size_t bf) { return (i - 1) / bf; }

// ─── B1: Transform propagation ────────────────────────────────────────────────
//
// 100k entities in a 4-ary tree.
// Steady-state: cached-sweep cost (no hierarchy change each frame).
// Rebuild: force PropagationOrderCache dirty before each measurement.

void BenchmarkTransformPropagation()
{
    constexpr size_t N       = 100'000;
    constexpr size_t BF      = 4;
    constexpr size_t WARMUP  = 10;
    constexpr size_t MEASURE = 50;

    World world;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<Parent>();

    std::vector<EntityId> entities;
    entities.reserve(N);

    {
        EntityId root = world.CreateEntity();
        world.AddComponent<LocalTransform>(root, { MakeTransform(0) });
        world.AddComponent<WorldTransform>(root, {});
        entities.push_back(root);
    }

    for (size_t i = 1; i < N; ++i)
    {
        EntityId e = world.CreateEntity();
        world.AddComponent<LocalTransform>(e, { MakeTransform(i) });
        world.AddComponent<WorldTransform>(e, {});
        world.AddComponent<Parent>(e, { entities[ParentFor(i, BF)] });
        entities.push_back(e);
    }

    world.AdvanceFrame();

    for (size_t i = 0; i < WARMUP; ++i)
    {
        PropagateTransforms(world);
        world.AdvanceFrame();
    }

    // Steady-state: hierarchy is unchanged, cache is hot.
    std::vector<double> steadySamples;
    steadySamples.reserve(MEASURE);

    for (size_t i = 0; i < MEASURE; ++i)
    {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const auto t0 = Clock::now();
        PropagateTransforms(world);
        const auto t1 = Clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        steadySamples.push_back(ElapsedUs(t0, t1));
        world.AdvanceFrame();
    }

    // Rebuild cost: invalidate cache before each call.
    std::vector<double> rebuildSamples;
    rebuildSamples.reserve(MEASURE);

    PropagationOrderCache& cache = world.GetResource<PropagationOrderCache>();

    for (size_t i = 0; i < MEASURE; ++i)
    {
        cache.Invalidate();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const auto t0 = Clock::now();
        PropagateTransforms(world);
        const auto t1 = Clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        rebuildSamples.push_back(ElapsedUs(t0, t1));
        world.AdvanceFrame();
    }

    const auto steady  = ComputeStats(steadySamples, N);
    const auto rebuild = ComputeStats(rebuildSamples, N);

    std::cout << "\n=== B1: Transform Propagation (ECS, 100k entities, 4-ary tree) ===\n";
    std::cout << "Steady-state cached sweep:\n";
    std::cout << "  mean_us:      " << steady.MeanUs   << "\n";
    std::cout << "  median_us:    " << steady.MedianUs << "\n";
    std::cout << "  p95_us:       " << steady.P95Us    << "\n";
    std::cout << "  ns/transform: " << steady.NsPerEntity << "\n";
    std::cout << "Rebuild + sweep (cache dirty each iteration):\n";
    std::cout << "  mean_us:      " << rebuild.MeanUs   << "\n";
    std::cout << "  median_us:    " << rebuild.MedianUs << "\n";
    std::cout << "  p95_us:       " << rebuild.P95Us    << "\n";
    std::cout << "  ns/transform: " << rebuild.NsPerEntity << "\n";
}

// ─── B2: Render extraction chunk-query iteration ──────────────────────────────
//
// Measures the chunk-query pass over Read<WorldTransform> + Read<StaticMeshComponent>.
// This is the hot inner loop of RenderExtractionSystem::Extract (minus the GPU
// resource lookups that require Vulkan). We accumulate a checksum to prevent
// the compiler from eliding the work.
//
// Pre-migration baseline: ForEachComponent<StaticMeshComponent> followed by
// TryGet<WorldTransform> per entity — two passes, per-entity hash map probes.
// New path: single archetype chunk query, both columns contiguous.

void BenchmarkRenderExtractionQuery()
{
    constexpr size_t N       = 10'000;
    constexpr size_t WARMUP  = 5;
    constexpr size_t MEASURE = 50;

    World world;
    world.RegisterComponent<WorldTransform>();
    world.RegisterComponent<StaticMeshComponent>();

    for (size_t i = 0; i < N; ++i)
    {
        EntityId e = world.CreateEntity();
        WorldTransform wt;
        wt.Value = MakeTransform(i);
        world.AddComponent<WorldTransform>(e, wt);
        StaticMeshComponent smc;
        smc.Visible = (i % 10 != 0); // 90% visible
        world.AddComponent<StaticMeshComponent>(e, smc);
    }

    Query<Read<WorldTransform>, Read<StaticMeshComponent>> query(world);

    volatile double checksum = 0.0;

    // Warmup
    for (size_t w = 0; w < WARMUP; ++w)
    {
        double localSum = 0.0;
        query.ForEachChunk([&](auto& view)
        {
            const auto transforms = view.template Read<WorldTransform>();
            const auto renderers  = view.template Read<StaticMeshComponent>();
            for (uint32_t i = 0; i < view.Count(); ++i)
            {
                if (!renderers[i].Visible) continue;
                localSum += static_cast<double>(transforms[i].Value.Position.X);
            }
        });
        checksum = localSum;
    }

    std::vector<double> samples;
    samples.reserve(MEASURE);

    for (size_t m = 0; m < MEASURE; ++m)
    {
        double localSum = 0.0;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const auto t0 = Clock::now();
        query.ForEachChunk([&](auto& view)
        {
            const auto transforms = view.template Read<WorldTransform>();
            const auto renderers  = view.template Read<StaticMeshComponent>();
            for (uint32_t i = 0; i < view.Count(); ++i)
            {
                if (!renderers[i].Visible) continue;
                localSum += static_cast<double>(transforms[i].Value.Position.X);
            }
        });
        const auto t1 = Clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        checksum = localSum;
        samples.push_back(ElapsedUs(t0, t1));
    }

    const auto s = ComputeStats(samples, N);

    std::cout << "\n=== B2: Render Extraction Chunk Query (10k entities) ===\n";
    std::cout << "  mean_us:   " << s.MeanUs      << "\n";
    std::cout << "  median_us: " << s.MedianUs    << "\n";
    std::cout << "  p95_us:    " << s.P95Us       << "\n";
    std::cout << "  ns/entity: " << s.NsPerEntity << "\n";
    std::cout << "  checksum:  " << checksum      << "  (non-zero = work not elided)\n";
}

// ─── B3: RenderQueueItem sort ─────────────────────────────────────────────────
//
// Sort a pre-populated queue of 10k items with varied sort keys.

void BenchmarkRenderQueueSort()
{
    constexpr size_t N       = 10'000;
    constexpr size_t MEASURE = 50;

    // Build reference items with varied sort keys (reverse-depth order).
    std::vector<RenderQueueItem> refItems;
    refItems.reserve(N);
    for (size_t i = 0; i < N; ++i)
    {
        RenderQueueItem item{};
        item.CameraDepth = static_cast<float>(N - i);
        item.Pass = ShaderPassId::ForwardOpaque;
        item.SortKey = BuildOpaqueSortKey(item);
        refItems.push_back(item);
    }

    std::vector<double> samples;
    samples.reserve(MEASURE);

    for (size_t m = 0; m < MEASURE; ++m)
    {
        RenderQueue q;
        for (const auto& item : refItems)
            q.AddOpaque(item);

        std::atomic_signal_fence(std::memory_order_seq_cst);
        const auto t0 = Clock::now();
        q.SortOpaque();
        const auto t1 = Clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        samples.push_back(ElapsedUs(t0, t1));
    }

    const auto s = ComputeStats(samples, N);

    std::cout << "\n=== B3: RenderQueueItem Sort (" << N << " items) ===\n";
    std::cout << "  mean_us:   " << s.MeanUs      << "\n";
    std::cout << "  median_us: " << s.MedianUs    << "\n";
    std::cout << "  p95_us:    " << s.P95Us       << "\n";
    std::cout << "  ns/item:   " << s.NsPerEntity << "\n";
}

// ─── B4: Archetype count and memory footprint ─────────────────────────────────
//
// Representative scenes showing archetype counts and chunk overhead.

void BenchmarkArchetypeFootprint()
{
    auto PrintFootprint = [](const char* label, const World& w, size_t registered) {
        size_t chunkCount = 0;
        for (const auto& arch : w.GetArchetypes())
            chunkCount += arch->Chunks.size();
        const size_t chunkDataBytes = chunkCount * ChunkSizeBytes;

        std::cout << "\n=== " << label << " ===\n";
        std::cout << "  registered_components: " << registered << "\n";
        std::cout << "  archetype_count:       " << w.GetArchetypes().size() << "\n";
        std::cout << "  chunk_count:           " << chunkCount << "\n";
        std::cout << "  chunk_data_bytes:      " << chunkDataBytes << "\n";
        std::cout << "  entities:              " << w.EntityCount() << "\n";

        for (const auto& arch : w.GetArchetypes())
        {
            size_t rows = 0;
            for (const auto& chunk : arch->Chunks) rows += chunk->RowCount;
            if (rows == 0) continue;
            std::cout << "    sig_popcount=" << arch->Signature.count()
                      << "  rows_per_chunk=" << arch->RowsPerChunk
                      << "  chunks=" << arch->Chunks.size()
                      << "  entity_rows=" << rows
                      << "\n";
        }
    };

    // Scene A: 100 flat-transform entities
    {
        World w;
        w.RegisterComponent<LocalTransform>();
        w.RegisterComponent<WorldTransform>();
        for (size_t i = 0; i < 100; ++i)
        {
            EntityId e = w.CreateEntity();
            w.AddComponent<LocalTransform>(e, { MakeTransform(i) });
            w.AddComponent<WorldTransform>(e, {});
        }
        PrintFootprint("B4a: Scene A — 100 flat-transform entities", w, 2);
    }

    // Scene B: 1000 renderable entities (LocalTransform + WorldTransform + StaticMeshComponent)
    {
        World w;
        w.RegisterComponent<LocalTransform>();
        w.RegisterComponent<WorldTransform>();
        w.RegisterComponent<StaticMeshComponent>();
        for (size_t i = 0; i < 1000; ++i)
        {
            EntityId e = w.CreateEntity();
            w.AddComponent<LocalTransform>(e, { MakeTransform(i) });
            w.AddComponent<WorldTransform>(e, {});
            w.AddComponent<StaticMeshComponent>(e, {});
        }
        PrintFootprint("B4b: Scene B — 1000 renderable entities", w, 3);
    }

    // Scene C: 500 root + 500 parented renderables (two archetypes: with/without Parent)
    {
        World w;
        w.RegisterComponent<LocalTransform>();
        w.RegisterComponent<WorldTransform>();
        w.RegisterComponent<Parent>();
        w.RegisterComponent<StaticMeshComponent>();

        std::vector<EntityId> roots;
        for (size_t i = 0; i < 500; ++i)
        {
            EntityId e = w.CreateEntity();
            w.AddComponent<LocalTransform>(e, { MakeTransform(i) });
            w.AddComponent<WorldTransform>(e, {});
            w.AddComponent<StaticMeshComponent>(e, {});
            roots.push_back(e);
        }
        for (size_t i = 0; i < 500; ++i)
        {
            EntityId e = w.CreateEntity();
            w.AddComponent<LocalTransform>(e, { MakeTransform(500 + i) });
            w.AddComponent<WorldTransform>(e, {});
            w.AddComponent<StaticMeshComponent>(e, {});
            w.AddComponent<Parent>(e, { roots[i % 500] });
        }
        PrintFootprint("B4c: Scene C — 500 root + 500 parented renderables", w, 4);
    }

    // Scene D: 10k entities, mixed signatures (renderable roots, parented, light-only)
    {
        World w;
        w.RegisterComponent<LocalTransform>();
        w.RegisterComponent<WorldTransform>();
        w.RegisterComponent<Parent>();
        w.RegisterComponent<StaticMeshComponent>();

        std::vector<EntityId> roots;

        // 5000 renderable roots
        for (size_t i = 0; i < 5000; ++i)
        {
            EntityId e = w.CreateEntity();
            w.AddComponent<LocalTransform>(e, { MakeTransform(i) });
            w.AddComponent<WorldTransform>(e, {});
            w.AddComponent<StaticMeshComponent>(e, {});
            roots.push_back(e);
        }
        // 4000 parented renderables
        for (size_t i = 0; i < 4000; ++i)
        {
            EntityId e = w.CreateEntity();
            w.AddComponent<LocalTransform>(e, { MakeTransform(5000 + i) });
            w.AddComponent<WorldTransform>(e, {});
            w.AddComponent<StaticMeshComponent>(e, {});
            w.AddComponent<Parent>(e, { roots[i % 5000] });
        }
        // 1000 transform-only (no StaticMesh — pivot / dummy entities)
        for (size_t i = 0; i < 1000; ++i)
        {
            EntityId e = w.CreateEntity();
            w.AddComponent<LocalTransform>(e, { MakeTransform(9000 + i) });
            w.AddComponent<WorldTransform>(e, {});
        }

        PrintFootprint("B4d: Scene D — 10k entities, mixed signatures", w, 4);
    }
}

} // namespace

int main()
{
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Sencha ECS Phase 4 Benchmark\n";
    std::cout << "Build: g++-14 -O2 -march=native -DNDEBUG\n";
    std::cout << "Platform: Linux 6.6 WSL2\n";

    BenchmarkTransformPropagation();
    BenchmarkRenderExtractionQuery();
    BenchmarkRenderQueueSort();
    BenchmarkArchetypeFootprint();

    std::cout << "\nDone.\n";
    return 0;
}
