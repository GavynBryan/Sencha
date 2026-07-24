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
//   propagate_*  steady-state sweep cost, and the cost of the first sweep after
//                a zone attaches (the streaming-hitch number).

#include <gtest/gtest.h>

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
using Clock = std::chrono::steady_clock;

// One recorded measurement. Unit is carried so the compare script can apply a
// tolerance appropriate to the quantity: milliseconds drift between runs, counts
// must match exactly.
struct Metric
{
    std::string Name;
    std::string Unit;
    double Value = 0.0;
};

std::vector<Metric> Metrics;

void Record(std::string name, std::string unit, double value)
{
    Metrics.push_back(Metric{ std::move(name), std::move(unit), value });
}

double MillisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double Median(std::vector<double>& samples)
{
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

int RepsFromEnvironment(int fallback)
{
    const char* raw = std::getenv("SENCHA_STREAMING_BENCH_REPS");
    if (raw == nullptr || raw[0] == '\0')
        return fallback;
    const int parsed = std::atoi(raw);
    return parsed > 0 ? parsed : fallback;
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

// Two numbers: the steady-state sweep, and the first sweep after a small zone
// attaches. The gap between them is the streaming hitch, and its growth with
// world size is the property Phase 3 has to remove.
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

    // A deliberately tiny zone: any cost above the steady sweep is invalidation
    // blast radius, not the new zone's own work.
    constexpr int kAttachedZoneEntities = 100;
    const uint64_t rebuildsBeforeAttaching =
        world.GetResource<PropagationOrderCache>().RebuildCount();
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

// ── Output ───────────────────────────────────────────────────────────────────

const char* BuildConfiguration()
{
#ifdef NDEBUG
    return "release-codegen";
#else
    return "debug-asserts";
#endif
}

void WriteJson(const fs::path& path)
{
    std::ofstream out(path, std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << "cannot write " << path.generic_string();
    out << "{\n";
    out << "  \"schema\": 1,\n";
    out << "  \"build\": \"" << BuildConfiguration() << "\",\n";
    out << "  \"metrics\": [\n";
    for (size_t index = 0; index < Metrics.size(); ++index)
    {
        const Metric& metric = Metrics[index];
        out << "    { \"name\": \"" << metric.Name << "\", \"unit\": \""
            << metric.Unit << "\", \"value\": " << metric.Value << " }"
            << (index + 1 < Metrics.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

void WriteCsv(const fs::path& path)
{
    std::ofstream out(path, std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << "cannot write " << path.generic_string();
    out << "name,unit,value\n";
    for (const Metric& metric : Metrics)
        out << metric.Name << ',' << metric.Unit << ',' << metric.Value << '\n';
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

    Metrics.clear();

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

    WriteJson(jsonPath);
    fs::path csvPath = jsonPath;
    csvPath.replace_extension(".csv");
    WriteCsv(csvPath);

    testing::Test::RecordProperty("metrics", static_cast<int>(Metrics.size()));
}
