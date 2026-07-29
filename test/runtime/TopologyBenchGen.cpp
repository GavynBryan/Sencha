// Evidence generator: how the partition's manifest-level mechanisms scale with
// world size.
//
// The streaming bench measures one eight-zone chain in depth. This measures the
// other axis: index construction, validation, manifest parsing, hop-rank BFS,
// and demand policy over worlds from eight zones to five hundred and twelve, in
// four topology shapes. Several of these paths were written with a linear scan
// where a lookup would do, and the question a scaling curve answers is not
// whether the scan is quadratic — reading the code answers that — but whether
// the absolute cost is worth changing the code over.
//
// Counted-work bounds that must hold on every machine live in
// ZoneTopologyScalingTests.cpp. Only wall clock is recorded here.
//
// Skipped unless SENCHA_TOPOLOGY_BENCH_OUT names the output path (a .json is
// written there and a .csv beside it). Run it through scripts/bench_topology.sh,
// which builds the profile preset and pins a core. Set
// SENCHA_TOPOLOGY_BENCH_ONLY to a metric-name prefix to restrict the run, which
// is how a perf counter session attributes cleanly to one scenario.

#include <gtest/gtest.h>

#include "BenchRecorder.h"
#include "ZoneTopologyStressFixture.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/WorldPartitionValidation.h>
#include <zone/ZoneDemand.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace ZoneTopologyStress;

namespace
{
namespace fs = std::filesystem;

Bench::Recorder Recorder;
std::string OnlyPrefix;

bool Wanted(const std::string& name)
{
    return OnlyPrefix.empty() || name.compare(0, OnlyPrefix.size(), OnlyPrefix) == 0;
}

void Record(const std::string& name, const std::string& unit, double value)
{
    Recorder.Record(name, unit, value);
}

// Iterations to run inside one timed region. The smallest scenarios here take a
// few hundred nanoseconds, which is close enough to the clock's own granularity
// that a median over single calls varies by a third between runs; batching
// raises each timed region to something the clock can resolve and the recorded
// value stays per-iteration. Batch size falls with zone count so the region
// lands in the same order of magnitude across the sweep.
int BatchFor(int zoneCount)
{
    const int batch = 4096 / (zoneCount > 0 ? zoneCount : 1);
    if (batch < 1)
        return 1;
    return batch > 512 ? 512 : batch;
}

// Median per-iteration milliseconds over `reps` batches. The body returns a
// value so the optimizer cannot discard the work it is being timed for.
template <typename Body>
double MeasureBatched(int reps, int batch, Body&& body)
{
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(reps));
    for (int rep = 0; rep < reps; ++rep)
    {
        const auto start = Bench::Clock::now();
        std::size_t sink = 0;
        for (int iteration = 0; iteration < batch; ++iteration)
            sink += body();
        const double elapsed = Bench::MillisecondsSince(start);
        if (sink == 0xffffffffu)
            std::abort();
        samples.push_back(elapsed / static_cast<double>(batch));
    }
    return Bench::Median(samples);
}

// The control the compare script normalizes milliseconds against. Touches no
// engine code, so a change in it is a change in the machine, not the build.
void MeasureControl()
{
    constexpr int kBytes = 20 * 1024 * 1024;
    std::vector<double> samples;
    for (int rep = 0; rep < 5; ++rep)
    {
        std::vector<unsigned char> buffer(kBytes, 1);
        const auto start = Bench::Clock::now();
        unsigned long long sum = 0;
        for (int index = 0; index < kBytes; index += 64)
            sum += buffer[static_cast<std::size_t>(index)];
        samples.push_back(Bench::MillisecondsSince(start));
        if (sum == 0)
            std::abort();
    }
    Record("control_memory_stream_ms", "ms", Bench::Median(samples));
}

const char* ShapeTag(TopologyShape shape)
{
    switch (shape)
    {
        case TopologyShape::Chain: return "chain";
        case TopologyShape::Grid: return "grid";
        case TopologyShape::Hub: return "hub";
        case TopologyShape::MultiGraph: return "multigraph";
    }
    return "?";
}

std::string MetricName(const char* stage, TopologyShape shape, int zoneCount)
{
    return std::string(stage) + "_" + ShapeTag(shape) + "_z" + std::to_string(zoneCount)
           + "_ms";
}

// Whether a scenario contributes anything under the active filter. Checked
// before its manifest is built, so a restricted run spends no time constructing
// topologies it will not measure and a perf session attributes to one scenario.
bool WantedScenario(const TopologySpec& spec)
{
    if (OnlyPrefix.empty())
        return true;
    for (const char* stage : { "index_build", "validate", "load_manifest",
                               "manifest_write_read", "hop_ranks", "demand",
                               "demand_radius" })
        if (Wanted(MetricName(stage, spec.Shape, spec.ZoneCount)))
            return true;
    return false;
}

// Manifest construction is the bench's own cost, not the engine's; it is done
// once per scenario and excluded from every measurement below.
void MeasureManifestStages(const TopologySpec& spec, int reps)
{
    const WorldPartitionManifest manifest = BuildTopology(spec);
    const int batch = BatchFor(spec.ZoneCount);

    const std::string indexName = MetricName("index_build", spec.Shape, spec.ZoneCount);
    if (Wanted(indexName))
    {
        Record(indexName, "ms", MeasureBatched(reps, batch, [&]
        {
            const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
            return index.ContainsZone(StressZoneAt(0)) ? 1u : 0u;
        }));
    }

    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);

    const std::string validateName = MetricName("validate", spec.Shape, spec.ZoneCount);
    if (Wanted(validateName))
    {
        Record(validateName, "ms", MeasureBatched(reps, batch, [&]
        {
            return ValidateWorldPartitionManifest(manifest, index).size() + 1u;
        }));
    }

    // The runtime's whole accept path: index build plus validation plus the
    // refusal checks, which is what a world load actually pays. The manifest
    // copy is the caller's, not the mechanism's, so it stays outside the timed
    // region.
    const std::string loadName = MetricName("load_manifest", spec.Shape, spec.ZoneCount);
    if (Wanted(loadName))
    {
        Record(loadName, "ms", MeasureBatched(reps, batch, [&]
        {
            WorldPartitionRuntime partition(
                [](const ZoneHeader&) { return ZoneLoadRecipe{}; },
                WorldPartitionStreamingConfig{});
            WorldPartitionManifest copy = manifest;
            std::string error;
            return partition.LoadManifest(std::move(copy), &error) ? 1u : 0u;
        }));
    }

    const std::string codecName =
        MetricName("manifest_write_read", spec.Shape, spec.ZoneCount);
    if (Wanted(codecName))
    {
        Record(codecName, "ms", MeasureBatched(reps, batch, [&]
        {
            const JsonValue written = WriteWorldPartitionManifest(manifest);
            std::string error;
            return ReadWorldPartitionManifest(written, &error).has_value() ? 1u : 0u;
        }));
    }
}

// The per-frame policy path, measured on its own. WorldPartitionRuntime::Update
// runs both of these every frame, plus its own bookkeeping.
void MeasureDemandStages(const TopologySpec& spec, int reps)
{
    const WorldPartitionManifest manifest = BuildTopology(spec);
    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
    const ZoneId focus = StressZoneAt(spec.ZoneCount / 2);
    const Vec3d focusPosition = StressZoneCenter(spec, spec.ZoneCount / 2);

    WorldPartitionStreamingConfig config;
    config.HopCount = 1;
    config.ResidentZoneCap = 8;

    const int batch = BatchFor(spec.ZoneCount);

    const std::string ranksName = MetricName("hop_ranks", spec.Shape, spec.ZoneCount);
    if (Wanted(ranksName))
    {
        Record(ranksName, "ms", MeasureBatched(reps, batch, [&]
        {
            return ComputeZoneHopRanks(manifest, index, focus, config.HopCount).size();
        }));
    }

    const std::string demandName = MetricName("demand", spec.Shape, spec.ZoneCount);
    if (Wanted(demandName))
    {
        Record(demandName, "ms", MeasureBatched(reps, batch, [&]
        {
            return ComputeZoneDemand(manifest, index, focus, {}, config).size();
        }));
    }

    // Radius demand walks every zone's bounds rather than the focus
    // neighbourhood, so it is the one demand shape whose cost is expected to
    // track world size. Recorded separately so the graph-only number stays a
    // clean read.
    WorldPartitionStreamingConfig radiusConfig = config;
    radiusConfig.Radius = spec.ZoneSpan * 1.5;
    const std::string radiusName =
        MetricName("demand_radius", spec.Shape, spec.ZoneCount);
    if (Wanted(radiusName))
    {
        Record(radiusName, "ms", MeasureBatched(reps, batch, [&]
        {
            return ComputeZoneDemand(manifest, index, focus, {}, radiusConfig,
                                     &focusPosition)
                .size();
        }));
    }
}
}  // namespace

TEST(TopologyBench, Generate)
{
    const char* outEnv = std::getenv("SENCHA_TOPOLOGY_BENCH_OUT");
    if (outEnv == nullptr || outEnv[0] == '\0')
    {
        GTEST_SKIP() << "set SENCHA_TOPOLOGY_BENCH_OUT to record the topology "
                        "bench (use scripts/bench_topology.sh)";
    }

    const char* onlyEnv = std::getenv("SENCHA_TOPOLOGY_BENCH_ONLY");
    OnlyPrefix = onlyEnv != nullptr ? onlyEnv : "";

    const fs::path jsonPath(outEnv);
    if (jsonPath.has_parent_path() && !jsonPath.parent_path().empty())
        fs::create_directories(jsonPath.parent_path());

    Recorder.Clear();
    MeasureControl();

    const int manifestReps = Bench::RepsFromEnvironment("SENCHA_TOPOLOGY_BENCH_REPS", 15);
    const int demandReps = Bench::RepsFromEnvironment("SENCHA_TOPOLOGY_BENCH_REPS", 200);

    for (int zoneCount : { 8, 32, 128, 512 })
    {
        for (TopologyShape shape : { TopologyShape::Chain,
                                     TopologyShape::Grid,
                                     TopologyShape::Hub,
                                     TopologyShape::MultiGraph })
        {
            TopologySpec spec{ .Shape = shape, .ZoneCount = zoneCount };
            if (shape == TopologyShape::MultiGraph)
                spec.GraphCount = 4;
            if (!WantedScenario(spec))
                continue;
            MeasureManifestStages(spec, manifestReps);
            MeasureDemandStages(spec, demandReps);
        }
    }

    ASSERT_TRUE(Recorder.WriteJson(jsonPath))
        << "cannot write " << jsonPath.generic_string();
    fs::path csvPath = jsonPath;
    csvPath.replace_extension(".csv");
    ASSERT_TRUE(Recorder.WriteCsv(csvPath))
        << "cannot write " << csvPath.generic_string();

    testing::Test::RecordProperty("metrics", static_cast<int>(Recorder.Metrics().size()));
}
