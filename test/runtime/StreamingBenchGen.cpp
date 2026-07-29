// Evidence generator: wall-clock and counted-work measurements for the unified
// World's streaming and iteration paths. The companion to
// StreamingCostBoundsTests.cpp — bounds that must hold on every machine are
// asserted there; the numbers that depend on this machine and this build are
// recorded here.
//
// Skipped unless SENCHA_STREAMING_BENCH_OUT names the output path (a .json is
// written there and a .csv beside it). Run it through
// scripts/bench_streaming.sh, which builds the profile preset and pins a core;
// numbers taken from the dev preset carry asserts and a debug allocator and are
// roughly an order of magnitude slower, so the emitted `build` field records
// which one produced them and the compare script refuses to mix them.
//
// Measured scenarios and the question each answers:
//   iterate_*    does one partitioned World iterate as fast as N worlds did,
//                and is a dormant partition actually free to skip?
//   import_*     how long does the owner thread stall publishing a zone, and
//                how many row migrations does it pay?
//   detach_*     how long does unload stall, and are chunk slabs returned?
//   propagate_*  sweep cost with every local transform dirty, with none dirty,
//                over a hierarchy, and for the first sweep after a zone attaches
//                (the streaming-hitch number).
//   traversal_*  a focus walking a chain of zones and back through the real
//                streaming path, with frame work every step: the worst owner-thread
//                frame a streaming event costs, which is criterion 5.

#include <gtest/gtest.h>

#include "BenchRecorder.h"
#include "StreamingTraversalFixture.h"

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <render/PointLightComponent.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/transform/PropagationOrderCache.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>
#include <zone/ZoneLoadPackage.h>
#include <zone/ZonePackageImporter.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Bench::Clock;
using Bench::Median;
using Bench::MillisecondsSince;

Bench::Recorder Recorder;

void Record(std::string name, std::string unit, double value)
{
    Recorder.Record(std::move(name), std::move(unit), value);
}

int RepsFromEnvironment(int fallback)
{
    return Bench::RepsFromEnvironment("SENCHA_STREAMING_BENCH_REPS", fallback);
}

WorldComponentSchema RuntimeSchema()
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();
    return schema;
}

void FillEntity(World& world, EntityId entity, int index)
{
    Transform3f transform;
    transform.Position = Vec3d(
        static_cast<float>(index % 64),
        0.0f,
        static_cast<float>(index / 64));
    world.AddComponent(entity, LocalTransform{ transform });
    world.AddComponent(entity, WorldTransform{ transform });
    PointLightComponent light{};
    light.Intensity = static_cast<float>(index % 7) + 1.0f;
    light.Range = 10.0f;
    world.AddComponent(entity, light);
}

ZoneLoadPackage MakeZonePackage(ZoneId zone, int entities)
{
    ZoneLoadPackage package(zone);
    for (int index = 0; index < entities; ++index)
    {
        const ZoneLocalEntityId entity = package.CreateEntity();
        Transform3f transform;
        transform.Position = Vec3d(
            static_cast<float>(index % 64),
            0.0f,
            static_cast<float>(index / 64));
        package.AddComponent(entity, LocalTransform{ transform });
        PointLightComponent light{};
        light.Intensity = 2.0f;
        light.Range = 10.0f;
        package.AddComponent(entity, light);
    }
    return package;
}

// ── Iteration ────────────────────────────────────────────────────────────────

// `interleaved` allocates entities round-robin across zones, which is the
// pessimal chunk ordering a real streaming history can produce; the sequential
// case is the best. Both are recorded so a regression in either is visible.
void MeasureIteration(int zones, int perZone, int activeZones, bool interleaved,
                      int reps, const std::string& label)
{
    const WorldComponentSchema schema = RuntimeSchema();
    RuntimeWorld runtime(schema);
    World& world = runtime.Entities();

    std::vector<StoragePartitionId> partitions;
    for (int zone = 0; zone < zones; ++zone)
    {
        partitions.push_back(
            runtime.AttachZone(ZoneId{ static_cast<uint64_t>(zone + 1) }).Partition);
    }

    int index = 0;
    if (interleaved)
    {
        for (int entity = 0; entity < perZone; ++entity)
            for (int zone = 0; zone < zones; ++zone)
                FillEntity(world, world.CreateEntity(partitions[zone]), index++);
    }
    else
    {
        for (int zone = 0; zone < zones; ++zone)
            for (int entity = 0; entity < perZone; ++entity)
                FillEntity(world, world.CreateEntity(partitions[zone]), index++);
    }

    StoragePartitionSet active;
    for (int zone = 0; zone < activeZones; ++zone)
        active.Add(partitions[zone]);

    Query<Read<WorldTransform>, Read<PointLightComponent>> query(world);
    double checksum = 0.0;
    std::vector<double> samples;
    for (int rep = 0; rep < reps; ++rep)
    {
        const auto start = Clock::now();
        double sum = 0.0;
        query.ForEachChunkIn(active, [&](auto& view)
        {
            const auto transforms = view.template Read<WorldTransform>();
            const auto lights = view.template Read<PointLightComponent>();
            for (uint32_t row = 0; row < view.Count(); ++row)
                sum += transforms[row].Value.Position.X * lights[row].Intensity;
        });
        samples.push_back(MillisecondsSince(start));
        checksum += sum;
    }

    Record("iterate_" + label + "_ms", "ms", Median(samples));
    Record("iterate_" + label + "_chunks", "count",
           static_cast<double>(world.ChunkCount()));
    // Retained so the optimizer cannot elide the sweep; a zero sum would mean
    // the measurement above timed nothing.
    EXPECT_GT(checksum, 0.0);
}

// ── Import and detach ────────────────────────────────────────────────────────

void MeasureImportAndDetach(int entities, int reps)
{
    const WorldComponentSchema schema = RuntimeSchema();
    const std::string label = std::to_string(entities);

    std::vector<double> importSamples;
    std::vector<double> detachSamples;
    uint64_t importMigrations = 0;
    size_t chunksAfterImport = 0;

    for (int rep = 0; rep < reps; ++rep)
    {
        RuntimeWorld runtime(schema);
        const ZoneLoadPackage package = MakeZonePackage(ZoneId{ 7 }, entities);

        const uint64_t migrationsBefore = runtime.Entities().RowMigrationCount();
        const auto importStart = Clock::now();
        ZoneImportError error;
        ASSERT_TRUE(ImportZonePackage(
            runtime,
            schema,
            package,
            ZoneParticipation{ .Visible = true },
            &error))
            << error.Message;
        importSamples.push_back(MillisecondsSince(importStart));
        importMigrations =
            runtime.Entities().RowMigrationCount() - migrationsBefore;
        chunksAfterImport = runtime.Entities().ChunkCount();

        const auto detachStart = Clock::now();
        (void)runtime.RequestDetach(ZoneId{ 7 });
        runtime.FlushLifecycleRequests();
        (void)runtime.BeginResidencyProcessing();
        runtime.FinalizeResidencyProcessing();
        detachSamples.push_back(MillisecondsSince(detachStart));
    }

    Record("import_" + label + "_ms", "ms", Median(importSamples));
    Record("import_" + label + "_row_migrations", "count",
           static_cast<double>(importMigrations));
    Record("import_" + label + "_chunks", "count",
           static_cast<double>(chunksAfterImport));
    Record("detach_" + label + "_ms", "ms", Median(detachSamples));
}

// Repeated load/unload of one zone through a recycled partition slot. The
// chunk census afterwards is the reclamation signal: slabs the unload did not
// return are still resident and are still walked by every query.
void MeasureChurn(int entities, int cycles)
{
    const WorldComponentSchema schema = RuntimeSchema();
    RuntimeWorld runtime(schema);
    const ZoneLoadPackage package = MakeZonePackage(ZoneId{ 1 }, entities);

    size_t chunksAfterFirstCycle = 0;
    for (int cycle = 0; cycle < cycles; ++cycle)
    {
        ZoneImportError error;
        ASSERT_TRUE(ImportZonePackage(
            runtime,
            schema,
            package,
            ZoneParticipation{ .Visible = true },
            &error))
            << error.Message;
        ASSERT_TRUE(runtime.RequestDetach(ZoneId{ 1 }));
        runtime.FlushLifecycleRequests();
        (void)runtime.BeginResidencyProcessing();
        runtime.FinalizeResidencyProcessing();
        if (cycle == 0)
            chunksAfterFirstCycle = runtime.Entities().ChunkCount();
    }

    Record("churn_chunks_after_1_cycle", "count",
           static_cast<double>(chunksAfterFirstCycle));
    Record("churn_chunks_after_" + std::to_string(cycles) + "_cycles", "count",
           static_cast<double>(runtime.Entities().ChunkCount()));
    Record("churn_empty_chunks_retained", "count",
           static_cast<double>(runtime.Entities().EmptyChunkCount()));
}

// ── Propagation ──────────────────────────────────────────────────────────────

// Three numbers on a flat world: a sweep with every local transform dirty, a
// sweep with none dirty, and the first sweep after a small zone attaches. The
// gap between the last two is the streaming hitch, and its growth with world
// size is the property Phase 3 has to remove.
//
// The dirty and clean cases are separated because they exercise different costs
// and a real scene is mostly clean. Frames are never advanced during the dirty
// loop, so every LocalTransform column still reads as written at or after the
// last sweep and the whole world recomputes; the clean loop advances the frame
// and writes nothing, leaving only the cost of deciding to skip.
void MeasurePropagation(int zones, int perZone, int reps)
{
    const WorldComponentSchema schema = RuntimeSchema();
    RuntimeWorld runtime(schema);
    World& world = runtime.Entities();
    world.AddResource<PropagationOrderCache>();

    std::vector<StoragePartitionId> partitions;
    for (int zone = 0; zone < zones; ++zone)
    {
        partitions.push_back(
            runtime.AttachZone(ZoneId{ static_cast<uint64_t>(zone + 1) }).Partition);
    }

    int index = 0;
    for (int zone = 0; zone < zones; ++zone)
        for (int entity = 0; entity < perZone; ++entity)
            FillEntity(world, world.CreateEntity(partitions[zone]), index++);

    StoragePartitionSet active;
    for (const StoragePartitionId partition : partitions)
        active.Add(partition);

    const int worldEntities = zones * perZone;
    const std::string label = std::to_string(worldEntities);

    // The first sweep builds the cache; measure the settled cost after it.
    PropagateTransforms(world, active, TransformPropagationDomain::Simulation);

    std::vector<double> steady;
    for (int rep = 0; rep < reps; ++rep)
    {
        const auto start = Clock::now();
        PropagateTransforms(world, active, TransformPropagationDomain::Simulation);
        steady.push_back(MillisecondsSince(start));
    }
    Record("propagate_steady_" + label + "_ms", "ms", Median(steady));

    // Nothing written and the frame advanced: the sweep should decide to skip
    // and cost nothing proportional to the world.
    std::vector<double> clean;
    world.AdvanceFrame();
    PropagateTransforms(world, active, TransformPropagationDomain::Simulation);
    for (int rep = 0; rep < reps; ++rep)
    {
        world.AdvanceFrame();
        const auto start = Clock::now();
        PropagateTransforms(world, active, TransformPropagationDomain::Simulation);
        clean.push_back(MillisecondsSince(start));
    }
    Record("propagate_clean_" + label + "_ms", "ms", Median(clean));

    // A deliberately tiny zone: any cost above the steady sweep is invalidation
    // blast radius, not the new zone's own work.
    constexpr int kAttachedZoneEntities = 100;
    const uint64_t rebuildsBeforeAttaching =
        world.GetResource<PropagationOrderCache>().RebuildCount();
    const uint64_t resolvesBeforeAttaching =
        world.GetResource<PropagationOrderCache>().AddressResolveCount();
    std::vector<double> afterAttach;
    for (int rep = 0; rep < reps; ++rep)
    {
        const StoragePartitionId partition =
            runtime.AttachZone(ZoneId{ static_cast<uint64_t>(1000 + rep) }).Partition;
        for (int entity = 0; entity < kAttachedZoneEntities; ++entity)
            FillEntity(world, world.CreateEntity(partition), index++);
        active.Add(partition);

        const auto start = Clock::now();
        PropagateTransforms(world, active, TransformPropagationDomain::Simulation);
        afterAttach.push_back(MillisecondsSince(start));
    }
    Record("propagate_after_attach_" + label + "_ms", "ms", Median(afterAttach));
    // Per attach, so the figure does not move with the repetition count. One
    // rebuild per attach is correct — the new zone's entities must enter the
    // global order; more than one would mean an attach invalidates twice.
    const uint64_t attachRebuilds =
        world.GetResource<PropagationOrderCache>().RebuildCount()
        - rebuildsBeforeAttaching;
    Record("propagate_rebuilds_per_attach_" + label, "count",
           static_cast<double>(attachRebuilds) / static_cast<double>(reps));
    // An attach relocates no existing row, but it does move the structural
    // version, so the order's cached addresses are re-resolved. Recorded
    // separately from the rebuild count because the two costs differ by orders
    // of magnitude and only one of them is proportional to the world.
    const uint64_t attachResolves =
        world.GetResource<PropagationOrderCache>().AddressResolveCount()
        - resolvesBeforeAttaching;
    Record("propagate_address_resolves_per_attach_" + label, "count",
           static_cast<double>(attachResolves) / static_cast<double>(reps));
}

// Propagation over parented entities, which is the part of the sweep that
// genuinely needs parent-before-child ordering. Chains of `depth` are built
// level by level, so each level occupies its own chunks: writing only the roots
// leaves every descendant chunk clean while every descendant still has to
// recompute. That is the case a chunk-granular dirty test cannot see on its own,
// and the reason the sweep cannot be a plain filtered query.
void MeasureHierarchyPropagation(int entities, int depth, int reps)
{
    const WorldComponentSchema schema = RuntimeSchema();
    RuntimeWorld runtime(schema);
    World& world = runtime.Entities();
    world.AddResource<PropagationOrderCache>();

    const StoragePartitionId partition =
        runtime.AttachZone(ZoneId{ 1 }).Partition;
    StoragePartitionSet active;
    active.Add(StoragePartitionId::Default());
    active.Add(partition);

    const int chains = entities / depth;
    int index = 0;
    std::vector<EntityId> roots;
    std::vector<EntityId> previousLevel;
    for (int level = 0; level < depth; ++level)
    {
        std::vector<EntityId> current;
        current.reserve(static_cast<size_t>(chains));
        for (int chain = 0; chain < chains; ++chain)
        {
            const EntityId entity = world.CreateEntity(partition);
            FillEntity(world, entity, index++);
            if (level > 0)
            {
                world.AddComponent(
                    entity,
                    Parent{ previousLevel[static_cast<size_t>(chain)] });
            }
            current.push_back(entity);
        }
        if (level == 0)
            roots = current;
        previousLevel = std::move(current);
    }

    const std::string label = std::to_string(entities);

    world.AdvanceFrame();
    PropagateTransforms(world, active, TransformPropagationDomain::Simulation);

    std::vector<double> dirty;
    for (int rep = 0; rep < reps; ++rep)
    {
        world.AdvanceFrame();
        for (const EntityId root : roots)
        {
            world.TryGet<LocalTransform>(root)->Value.Position.Y =
                static_cast<float>(rep);
        }

        const auto start = Clock::now();
        PropagateTransforms(world, active, TransformPropagationDomain::Simulation);
        dirty.push_back(MillisecondsSince(start));
    }
    Record("propagate_hierarchy_dirty_" + label + "_ms", "ms", Median(dirty));

    std::vector<double> clean;
    for (int rep = 0; rep < reps; ++rep)
    {
        world.AdvanceFrame();
        const auto start = Clock::now();
        PropagateTransforms(world, active, TransformPropagationDomain::Simulation);
        clean.push_back(MillisecondsSince(start));
    }
    Record("propagate_hierarchy_clean_" + label + "_ms", "ms", Median(clean));
}

// ── Traversal ────────────────────────────────────────────────────────────────

// The worst owner-thread frame a streaming event costs, measured over a focus
// walking a chain of zones and back through the real streaming path: partition
// demand, async build and commit, residency processing, frame view, propagation.
// Criterion 5.
//
// Frames are separated into those that carried a zone attach or unload and those
// that did not, because they answer different questions: the quiet number is what
// streaming costs when nothing is happening, and the event number is the hitch.
//
// This measures the owner thread only. Nothing in this repo drives multi-zone
// streaming with a renderer attached, so GPU frame cost during a streaming event
// remains unmeasured — see the Phase 7 notes in the hardening plan.
void MeasureTraversal()
{
    using namespace StreamingTraversal;

    Harness traversal;
    const std::string error = traversal.LoadManifest();
    if (!error.empty())
    {
        Record("traversal_manifest_ok", "count", 0.0);
        return;
    }
    Record("traversal_manifest_ok", "count", 1.0);

    // First lap warms the caches and pays the initial attaches.
    traversal.WalkLap();

    std::vector<double> quiet;
    std::vector<double> eventful;
    int attachesBefore = traversal.Attaches();
    std::size_t chunksAfterFirstLap = traversal.World().Entities().ChunkCount();

    constexpr int kLaps = 3;
    for (int lap = 0; lap < kLaps; ++lap)
    {
        traversal.WalkLap([&]
        {
            const std::size_t chunksBefore =
                traversal.World().Entities().ChunkCount();
            const auto start = Clock::now();
            traversal.StepFrame();
            const double elapsed = MillisecondsSince(start);

            // A frame that attached or unloaded a zone shows up either as a new
            // finalize or as a change in the chunk census.
            const bool eventFrame =
                traversal.Attaches() != attachesBefore
                || traversal.World().Entities().ChunkCount() != chunksBefore;
            attachesBefore = traversal.Attaches();

            (eventFrame ? eventful : quiet).push_back(elapsed);
        });
    }

    const auto percentile = [](std::vector<double>& samples, double fraction)
    {
        if (samples.empty())
            return 0.0;
        std::sort(samples.begin(), samples.end());
        const std::size_t index = static_cast<std::size_t>(
            fraction * static_cast<double>(samples.size() - 1));
        return samples[index];
    };

    const double worstEvent =
        eventful.empty() ? 0.0 : *std::max_element(eventful.begin(), eventful.end());
    const double worstQuiet =
        quiet.empty() ? 0.0 : *std::max_element(quiet.begin(), quiet.end());

    Record("traversal_worst_event_frame_ms", "ms", worstEvent);
    Record("traversal_p50_event_frame_ms", "ms", percentile(eventful, 0.5));
    Record("traversal_worst_quiet_frame_ms", "ms", worstQuiet);
    Record("traversal_p50_quiet_frame_ms", "ms", percentile(quiet, 0.5));
    Record("traversal_event_frames", "count",
           static_cast<double>(eventful.size()));
    Record("traversal_quiet_frames", "count", static_cast<double>(quiet.size()));
    Record("traversal_zone_attaches", "count",
           static_cast<double>(traversal.Attaches()));
    // Equal to the census after the first lap, or streaming history is
    // accumulating memory.
    Record("traversal_chunks_after_first_lap", "count",
           static_cast<double>(chunksAfterFirstLap));
    Record("traversal_chunks_after_all_laps", "count",
           static_cast<double>(traversal.World().Entities().ChunkCount()));
    Record("traversal_order_rebuilds", "count",
           static_cast<double>(traversal.Cache().RebuildCount()));
    Record("traversal_address_resolves", "count",
           static_cast<double>(traversal.Cache().AddressResolveCount()));
}

// ── Drift control ────────────────────────────────────────────────────────────

// A fixed memory-streaming loop over a buffer sized like the iteration
// scenario's working set, touching no engine code. Nothing in the engine can
// change what this measures, so the ratio between it and a real metric isolates
// code change from machine state — clock and thermal drift move both together.
//
// It earns its place: two measurements during this harness's own development
// looked like large regressions and were the machine. A run whose control moved
// is a run whose millisecond numbers cannot be compared raw.
void MeasureControl(int reps)
{
    constexpr std::size_t kElements = 20u * 1024u * 1024u / sizeof(std::uint32_t);
    std::vector<std::uint32_t> buffer(kElements);
    for (std::size_t index = 0; index < kElements; ++index)
        buffer[index] = static_cast<std::uint32_t>(index);

    std::uint64_t checksum = 0;
    std::vector<double> samples;
    for (int rep = 0; rep < reps; ++rep)
    {
        const auto start = Clock::now();
        std::uint64_t sum = 0;
        for (std::size_t index = 0; index < kElements; index += 16)
            sum += buffer[index];
        samples.push_back(MillisecondsSince(start));
        checksum += sum;
    }
    Record("control_memory_stream_ms", "ms", Median(samples));
    EXPECT_GT(checksum, 0u);
}

} // namespace

TEST(StreamingBench, Generate)
{
    const char* outEnv = std::getenv("SENCHA_STREAMING_BENCH_OUT");
    if (outEnv == nullptr)
    {
        GTEST_SKIP() << "set SENCHA_STREAMING_BENCH_OUT to record the streaming "
                        "bench (use scripts/bench_streaming.sh)";
    }

    const fs::path jsonPath(outEnv);
    if (jsonPath.has_parent_path() && !jsonPath.parent_path().empty())
        fs::create_directories(jsonPath.parent_path());

    Recorder.Clear();

    const int iterationReps = RepsFromEnvironment(200);
    const int streamingReps = RepsFromEnvironment(30);
    const int propagationReps = std::max(1, RepsFromEnvironment(30) / 2);

    // First, so every later metric can be read against the machine state that
    // produced it.
    MeasureControl(iterationReps);

    // 8 zones x 20k: the iteration shape the review measured, so the recorded
    // baseline is comparable to the evidence doc.
    MeasureIteration(8, 20000, 8, false, iterationReps, "all_active_160k");
    MeasureIteration(8, 20000, 2, false, iterationReps, "two_of_eight_160k");
    MeasureIteration(8, 20000, 8, true, iterationReps, "interleaved_160k");

    for (const int entities : { 1000, 5000, 20000 })
        MeasureImportAndDetach(entities, streamingReps);

    MeasureChurn(1000, 10);

    // Held per-zone entity count, growing world: isolates whether the hitch
    // tracks the streamed zone or everything resident.
    for (const int zones : { 2, 4, 8 })
        MeasurePropagation(zones, 20000, propagationReps);

    // Depth 4: shallow enough to be representative of props and mounts rather
    // than of a skeleton, and deep enough that dirtiness has to travel.
    MeasureHierarchyPropagation(20000, 4, propagationReps);

    MeasureTraversal();

    ASSERT_TRUE(Recorder.WriteJson(jsonPath))
        << "cannot write " << jsonPath.generic_string();
    fs::path csvPath = jsonPath;
    csvPath.replace_extension(".csv");
    ASSERT_TRUE(Recorder.WriteCsv(csvPath))
        << "cannot write " << csvPath.generic_string();

    testing::Test::RecordProperty("metrics",
                                  static_cast<int>(Recorder.Metrics().size()));
}
