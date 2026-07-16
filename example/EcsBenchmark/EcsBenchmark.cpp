// ECS benchmark for transform propagation, render extraction, queue sorting,
// and representative archetype footprints.

#include <core/ResourceStore.h>
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

    struct SampleStats
    {
        double MeanUs = 0.0;
        double MedianUs = 0.0;
        double P95Us = 0.0;
        double NsPerEntity = 0.0;
    };

    struct BenchmarkWorld
    {
        ResourceStore Resources;
        EntityStore Entities{ Resources };
    };

    double ElapsedUs(Clock::time_point start, Clock::time_point end)
    {
        return std::chrono::duration<double, std::micro>(end - start).count();
    }

    SampleStats ComputeStats(std::vector<double>& samples, size_t entityCount)
    {
        std::sort(samples.begin(), samples.end());
        SampleStats stats;
        stats.MeanUs =
            std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
        stats.MedianUs = samples[samples.size() / 2];
        const size_t p95Index = static_cast<size_t>(samples.size() * 0.95);
        stats.P95Us = samples[std::min(p95Index, samples.size() - 1)];
        stats.NsPerEntity =
            (stats.MeanUs * 1000.0) / static_cast<double>(entityCount);
        return stats;
    }

    Transform3f MakeTransform(size_t index)
    {
        const float x = static_cast<float>(index % 17) * 0.1f;
        const float y = static_cast<float>((index * 7) % 19) * 0.1f;
        const float z = static_cast<float>((index * 11) % 23) * 0.1f;
        return Transform3f(
            Vec3d(x, y, z),
            Quatf::FromAxisAngle(
                Vec3d(0, 1, 0),
                static_cast<float>(index % 31) * 0.01f),
            Vec3d(1, 1, 1));
    }

    size_t ParentFor(size_t index, size_t branchingFactor)
    {
        return (index - 1) / branchingFactor;
    }

    void PrintStats(std::string_view label, const SampleStats& stats)
    {
        std::cout << label << "\n";
        std::cout << "  mean_us:   " << stats.MeanUs << "\n";
        std::cout << "  median_us: " << stats.MedianUs << "\n";
        std::cout << "  p95_us:    " << stats.P95Us << "\n";
        std::cout << "  ns/item:   " << stats.NsPerEntity << "\n";
    }

    void BenchmarkTransformPropagation()
    {
        constexpr size_t EntityCount = 100'000;
        constexpr size_t BranchingFactor = 4;
        constexpr size_t WarmupIterations = 10;
        constexpr size_t MeasureIterations = 50;

        BenchmarkWorld storage;
        EntityStore& world = storage.Entities;
        world.RegisterComponent<LocalTransform>();
        world.RegisterComponent<WorldTransform>();
        world.RegisterComponent<Parent>();

        std::vector<EntityId> entities;
        entities.reserve(EntityCount);

        EntityId root = world.CreateEntity();
        world.AddComponent<LocalTransform>(root, { MakeTransform(0) });
        world.AddComponent<WorldTransform>(root, {});
        entities.push_back(root);

        for (size_t index = 1; index < EntityCount; ++index)
        {
            EntityId entity = world.CreateEntity();
            world.AddComponent<LocalTransform>(entity, { MakeTransform(index) });
            world.AddComponent<WorldTransform>(entity, {});
            world.AddComponent<Parent>(
                entity,
                { entities[ParentFor(index, BranchingFactor)] });
            entities.push_back(entity);
        }

        PropagationOrderCache cache;
        world.AdvanceFrame();
        for (size_t iteration = 0; iteration < WarmupIterations; ++iteration)
        {
            PropagateTransforms(world, cache);
            world.AdvanceFrame();
        }

        std::vector<double> steadySamples;
        steadySamples.reserve(MeasureIterations);
        for (size_t iteration = 0; iteration < MeasureIterations; ++iteration)
        {
            std::atomic_signal_fence(std::memory_order_seq_cst);
            const auto start = Clock::now();
            PropagateTransforms(world, cache);
            const auto end = Clock::now();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            steadySamples.push_back(ElapsedUs(start, end));
            world.AdvanceFrame();
        }

        std::vector<double> rebuildSamples;
        rebuildSamples.reserve(MeasureIterations);
        for (size_t iteration = 0; iteration < MeasureIterations; ++iteration)
        {
            cache.Invalidate();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            const auto start = Clock::now();
            PropagateTransforms(world, cache);
            const auto end = Clock::now();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            rebuildSamples.push_back(ElapsedUs(start, end));
            world.AdvanceFrame();
        }

        std::cout << "\n=== Transform Propagation: 100k entities ===\n";
        PrintStats(
            "Steady-state cached sweep:",
            ComputeStats(steadySamples, EntityCount));
        PrintStats(
            "Rebuild and sweep:",
            ComputeStats(rebuildSamples, EntityCount));
    }

    void BenchmarkRenderExtractionQuery()
    {
        constexpr size_t EntityCount = 10'000;
        constexpr size_t WarmupIterations = 5;
        constexpr size_t MeasureIterations = 50;

        BenchmarkWorld storage;
        EntityStore& world = storage.Entities;
        world.RegisterComponent<WorldTransform>();
        world.RegisterComponent<StaticMeshComponent>();

        for (size_t index = 0; index < EntityCount; ++index)
        {
            EntityId entity = world.CreateEntity();
            WorldTransform transform;
            transform.Value = MakeTransform(index);
            world.AddComponent<WorldTransform>(entity, transform);

            StaticMeshComponent mesh;
            mesh.Visible = index % 10 != 0;
            world.AddComponent<StaticMeshComponent>(entity, mesh);
        }

        Query<Read<WorldTransform>, Read<StaticMeshComponent>> query(world);
        volatile double checksum = 0.0;

        for (size_t iteration = 0; iteration < WarmupIterations; ++iteration)
        {
            double localSum = 0.0;
            query.ForEachChunk([&](auto& view)
            {
                const auto transforms = view.template Read<WorldTransform>();
                const auto meshes = view.template Read<StaticMeshComponent>();
                for (uint32_t row = 0; row < view.Count(); ++row)
                {
                    if (meshes[row].Visible)
                        localSum += transforms[row].Value.Position.X;
                }
            });
            checksum = localSum;
        }

        std::vector<double> samples;
        samples.reserve(MeasureIterations);
        for (size_t iteration = 0; iteration < MeasureIterations; ++iteration)
        {
            double localSum = 0.0;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            const auto start = Clock::now();
            query.ForEachChunk([&](auto& view)
            {
                const auto transforms = view.template Read<WorldTransform>();
                const auto meshes = view.template Read<StaticMeshComponent>();
                for (uint32_t row = 0; row < view.Count(); ++row)
                {
                    if (meshes[row].Visible)
                        localSum += transforms[row].Value.Position.X;
                }
            });
            const auto end = Clock::now();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            checksum = localSum;
            samples.push_back(ElapsedUs(start, end));
        }

        std::cout << "\n=== Render Extraction Query: 10k entities ===\n";
        PrintStats("Chunk query:", ComputeStats(samples, EntityCount));
        std::cout << "  checksum: " << checksum << "\n";
    }

    void BenchmarkRenderQueueSort()
    {
        constexpr size_t ItemCount = 10'000;
        constexpr size_t MeasureIterations = 50;

        std::vector<RenderQueueItem> reference;
        reference.reserve(ItemCount);
        for (size_t index = 0; index < ItemCount; ++index)
        {
            RenderQueueItem item{};
            item.CameraDepth = static_cast<float>(ItemCount - index);
            item.Pass = ShaderPassId::ForwardOpaque;
            item.SortKey = BuildOpaqueSortKey(item);
            reference.push_back(item);
        }

        std::vector<double> samples;
        samples.reserve(MeasureIterations);
        for (size_t iteration = 0; iteration < MeasureIterations; ++iteration)
        {
            RenderQueue queue;
            for (const RenderQueueItem& item : reference)
                queue.AddOpaque(item);

            std::atomic_signal_fence(std::memory_order_seq_cst);
            const auto start = Clock::now();
            queue.SortOpaque();
            const auto end = Clock::now();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            samples.push_back(ElapsedUs(start, end));
        }

        std::cout << "\n=== Render Queue Sort: 10k items ===\n";
        PrintStats("Opaque sort:", ComputeStats(samples, ItemCount));
    }

    void PrintFootprint(
        std::string_view label,
        const EntityStore& world,
        size_t registeredComponents)
    {
        size_t chunkCount = 0;
        for (const auto& archetype : world.GetArchetypes())
            chunkCount += archetype->Chunks.size();

        std::cout << "\n=== " << label << " ===\n";
        std::cout << "  registered_components: " << registeredComponents << "\n";
        std::cout << "  archetype_count: " << world.GetArchetypes().size() << "\n";
        std::cout << "  chunk_count: " << chunkCount << "\n";
        std::cout << "  chunk_data_bytes: " << chunkCount * ChunkSizeBytes << "\n";
        std::cout << "  entities: " << world.EntityCount() << "\n";

        for (const auto& archetype : world.GetArchetypes())
        {
            size_t rows = 0;
            for (const auto& chunk : archetype->Chunks)
                rows += chunk->RowCount;
            if (rows == 0)
                continue;

            std::cout << "    sig_popcount=" << archetype->Signature.count()
                      << " rows_per_chunk=" << archetype->RowsPerChunk
                      << " chunks=" << archetype->Chunks.size()
                      << " entity_rows=" << rows << "\n";
        }
    }

    void RegisterRenderableComponents(EntityStore& world, bool withParent)
    {
        world.RegisterComponent<LocalTransform>();
        world.RegisterComponent<WorldTransform>();
        if (withParent)
            world.RegisterComponent<Parent>();
        world.RegisterComponent<StaticMeshComponent>();
    }

    void AddRenderable(EntityStore& world, size_t index, EntityId parent = {})
    {
        EntityId entity = world.CreateEntity();
        world.AddComponent<LocalTransform>(entity, { MakeTransform(index) });
        world.AddComponent<WorldTransform>(entity, {});
        world.AddComponent<StaticMeshComponent>(entity, {});
        if (parent.IsValid())
            world.AddComponent<Parent>(entity, { parent });
    }

    void BenchmarkArchetypeFootprint()
    {
        {
            BenchmarkWorld storage;
            EntityStore& world = storage.Entities;
            world.RegisterComponent<LocalTransform>();
            world.RegisterComponent<WorldTransform>();
            for (size_t index = 0; index < 100; ++index)
            {
                EntityId entity = world.CreateEntity();
                world.AddComponent<LocalTransform>(entity, { MakeTransform(index) });
                world.AddComponent<WorldTransform>(entity, {});
            }
            PrintFootprint("100 flat-transform entities", world, 2);
        }

        {
            BenchmarkWorld storage;
            EntityStore& world = storage.Entities;
            RegisterRenderableComponents(world, false);
            for (size_t index = 0; index < 1000; ++index)
                AddRenderable(world, index);
            PrintFootprint("1000 renderable entities", world, 3);
        }

        {
            BenchmarkWorld storage;
            EntityStore& world = storage.Entities;
            RegisterRenderableComponents(world, true);

            std::vector<EntityId> roots;
            roots.reserve(500);
            for (size_t index = 0; index < 500; ++index)
            {
                EntityId entity = world.CreateEntity();
                world.AddComponent<LocalTransform>(entity, { MakeTransform(index) });
                world.AddComponent<WorldTransform>(entity, {});
                world.AddComponent<StaticMeshComponent>(entity, {});
                roots.push_back(entity);
            }
            for (size_t index = 0; index < 500; ++index)
                AddRenderable(world, 500 + index, roots[index]);

            PrintFootprint("500 root and 500 parented renderables", world, 4);
        }

        {
            BenchmarkWorld storage;
            EntityStore& world = storage.Entities;
            RegisterRenderableComponents(world, true);

            std::vector<EntityId> roots;
            roots.reserve(5000);
            for (size_t index = 0; index < 5000; ++index)
            {
                EntityId entity = world.CreateEntity();
                world.AddComponent<LocalTransform>(entity, { MakeTransform(index) });
                world.AddComponent<WorldTransform>(entity, {});
                world.AddComponent<StaticMeshComponent>(entity, {});
                roots.push_back(entity);
            }
            for (size_t index = 0; index < 4000; ++index)
                AddRenderable(world, 5000 + index, roots[index % roots.size()]);
            for (size_t index = 0; index < 1000; ++index)
            {
                EntityId entity = world.CreateEntity();
                world.AddComponent<LocalTransform>(
                    entity,
                    { MakeTransform(9000 + index) });
                world.AddComponent<WorldTransform>(entity, {});
            }

            PrintFootprint("10k entities with mixed signatures", world, 4);
        }
    }
}

int main()
{
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Sencha ECS benchmark\n";

    BenchmarkTransformPropagation();
    BenchmarkRenderExtractionQuery();
    BenchmarkRenderQueueSort();
    BenchmarkArchetypeFootprint();

    std::cout << "\nDone.\n";
    return 0;
}
