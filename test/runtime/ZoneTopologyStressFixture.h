#pragma once

// Parameterized world-partition topologies for scaling and stress work.
//
// The traversal fixture describes one hand-written scenario: eight zones in a
// line, docked end to end. That is the right shape for bounding what a streaming
// event costs a frame, and the wrong shape for asking how the partition behaves
// as a world grows, branches, or splits across graphs. This builds a manifest
// from a spec instead, so the same code paths can be driven at eight zones and
// at five hundred.
//
// Construction is closed-form: zone placement, edge pairing, and every id come
// from index arithmetic, so a spec names exactly one manifest with no seed to
// record and no ordering to stabilize afterwards.
//
// Zone content is optional. Demand policy, index construction, and validation
// are pure functions of the manifest, so the scaling work that measures them
// asks for no entities at all and pays nothing to build them.

#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <math/geometry/3d/Aabb3d.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/transform/TransformComponents.h>
#include <zone/AsyncZoneLoader.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/ZoneLoadPackage.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace ZoneTopologyStress
{
// Shapes differ in the question they ask of the partition. Chain is the
// degenerate branching case and the one existing coverage already assumes. Grid
// raises edge count per zone to four, which is what makes a hop-count sweep
// widen quadratically instead of linearly. Hub is the high-degree case, and is
// built from links because a single zone cannot share a geometric boundary with
// hundreds of neighbours. MultiGraph exercises the cross-graph policy path,
// where a neighbour resolves its streaming shape from a different graph record.
enum class TopologyShape
{
    Chain,
    Grid,
    Hub,
    MultiGraph,
};

struct TopologySpec
{
    TopologyShape Shape = TopologyShape::Chain;
    int ZoneCount = 8;
    int GraphCount = 1;      // MultiGraph only; other shapes use one graph
    bool Docks = true;       // geometric crossing planes between neighbours
    bool Links = false;      // non-spatial graph edges between neighbours
    int EntitiesPerZone = 0; // 0 builds a manifest with no content at all
    double ZoneSpan = 20.0;
    double ZoneHeight = 4.0;
};

// Id spaces are disjoint per kind so a failure message names the kind by its
// magnitude alone.
inline constexpr std::uint64_t kZoneIdBase = 0x1000;
inline constexpr std::uint64_t kGraphIdBase = 0x2000;
inline constexpr std::uint64_t kDockIdBase = 0x3000;
inline constexpr std::uint64_t kLinkIdBase = 0x4000;

[[nodiscard]] inline ZoneId StressZoneAt(int index)
{
    return ZoneId{ kZoneIdBase + static_cast<std::uint64_t>(index) };
}

[[nodiscard]] inline GraphId StressGraphAt(int index)
{
    return GraphId{ kGraphIdBase + static_cast<std::uint64_t>(index) };
}

// Square-ish, so a grid of N zones is about sqrt(N) on a side.
[[nodiscard]] inline int GridSide(int zoneCount)
{
    int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(zoneCount))));
    return side < 1 ? 1 : side;
}

// Which graph owns a zone. Only MultiGraph splits them; the split is contiguous
// so each graph's zones stay adjacent and the cross-graph edges are few.
[[nodiscard]] inline int GraphIndexOfZone(const TopologySpec& spec, int zoneIndex)
{
    if (spec.Shape != TopologyShape::MultiGraph || spec.GraphCount <= 1)
        return 0;
    const int perGraph = (spec.ZoneCount + spec.GraphCount - 1) / spec.GraphCount;
    const int graph = zoneIndex / (perGraph < 1 ? 1 : perGraph);
    return graph >= spec.GraphCount ? spec.GraphCount - 1 : graph;
}

// Reciprocal endpoints in the cook's mirror convention: side B negates the
// normal and the right vector and keeps up, which is what dock validation
// independently requires of a well-formed pair.
inline void AddStressDockPair(WorldPartitionManifest& manifest,
                              std::uint64_t id,
                              ZoneId zoneA,
                              ZoneId zoneB,
                              Vec3d origin,
                              Vec3d normal,
                              Vec3d right,
                              Vec2d halfExtents)
{
    ZoneHeader* a = nullptr;
    ZoneHeader* b = nullptr;
    for (ZoneHeader& zone : manifest.Zones)
    {
        if (zone.Id == zoneA)
            a = &zone;
        if (zone.Id == zoneB)
            b = &zone;
    }
    if (a == nullptr || b == nullptr)
        return;

    const Vec3d up = Vec3d{ 0, 1, 0 };
    DockEndpoint endpoint{
        .Id = DockId{ id },
        .OwnerZone = zoneA,
        .OtherZone = zoneB,
        .Side = DockSide::A,
        .Origin = origin,
        .Normal = normal,
        .Right = right,
        .Up = up,
        .HalfExtents = halfExtents,
        .Directions = 3,
    };
    a->Docks.push_back(endpoint);

    endpoint.OwnerZone = zoneB;
    endpoint.OtherZone = zoneA;
    endpoint.Side = DockSide::B;
    endpoint.Normal = -normal;
    endpoint.Right = -right;
    b->Docks.push_back(endpoint);
}

inline void AddStressLinkPair(WorldPartitionManifest& manifest,
                              std::uint64_t id,
                              ZoneId zoneA,
                              ZoneId zoneB)
{
    ZoneHeader* a = nullptr;
    ZoneHeader* b = nullptr;
    for (ZoneHeader& zone : manifest.Zones)
    {
        if (zone.Id == zoneA)
            a = &zone;
        if (zone.Id == zoneB)
            b = &zone;
    }
    if (a == nullptr || b == nullptr)
        return;

    LinkEndpoint endpoint{
        .Id = LinkId{ id },
        .OwnerZone = zoneA,
        .OtherZone = zoneB,
        .Side = DockSide::A,
        .Kind = 0,
        .Directions = 3,
    };
    a->Links.push_back(endpoint);

    endpoint.OwnerZone = zoneB;
    endpoint.OtherZone = zoneA;
    endpoint.Side = DockSide::B;
    b->Links.push_back(endpoint);
}

// Zone bounds. Chain and Hub lay zones out along X; Grid tiles XZ; MultiGraph
// lays each graph out as its own row so graph bounds never interleave. Zones
// never overlap in any shape, which keeps containment unambiguous and lets a
// focus position name exactly one zone.
[[nodiscard]] inline Aabb3d StressZoneBounds(const TopologySpec& spec, int index)
{
    const double span = spec.ZoneSpan;
    const double half = span * 0.5;

    double minX = 0.0;
    double minZ = -half;
    switch (spec.Shape)
    {
        case TopologyShape::Chain:
        case TopologyShape::Hub:
            minX = static_cast<double>(index) * span;
            break;
        case TopologyShape::Grid:
        {
            const int side = GridSide(spec.ZoneCount);
            minX = static_cast<double>(index % side) * span;
            minZ = static_cast<double>(index / side) * span;
            break;
        }
        case TopologyShape::MultiGraph:
        {
            const int graph = GraphIndexOfZone(spec, index);
            const int perGraph = (spec.ZoneCount + spec.GraphCount - 1) / spec.GraphCount;
            const int withinGraph = index - graph * (perGraph < 1 ? 1 : perGraph);
            minX = static_cast<double>(withinGraph) * span;
            minZ = static_cast<double>(graph) * span;
            break;
        }
    }

    return Aabb3d{ Vec3d{ minX, 0.0, minZ },
                   Vec3d{ minX + span, spec.ZoneHeight, minZ + span } };
}

// A point comfortably inside a zone: its bounds centre.
[[nodiscard]] inline Vec3d StressZoneCenter(const TopologySpec& spec, int index)
{
    const Aabb3d bounds = StressZoneBounds(spec, index);
    return Vec3d{ (bounds.Min.X + bounds.Max.X) * 0.5f,
                  (bounds.Min.Y + bounds.Max.Y) * 0.5f,
                  (bounds.Min.Z + bounds.Max.Z) * 0.5f };
}

[[nodiscard]] inline WorldPartitionManifest BuildTopology(const TopologySpec& spec)
{
    WorldPartitionManifest manifest;
    manifest.Name = "ZoneTopologyStress";

    const int graphCount = spec.Shape == TopologyShape::MultiGraph
                               ? (spec.GraphCount < 1 ? 1 : spec.GraphCount)
                               : 1;
    for (int graph = 0; graph < graphCount; ++graph)
    {
        manifest.Graphs.push_back(GraphRecord{
            .Id = StressGraphAt(graph),
            .Name = "Graph" + std::to_string(graph),
            .Streaming = {},
        });
    }

    manifest.Zones.reserve(static_cast<std::size_t>(spec.ZoneCount));
    for (int index = 0; index < spec.ZoneCount; ++index)
    {
        const std::string suffix = std::to_string(index);
        manifest.Zones.push_back(ZoneHeader{
            .Id = StressZoneAt(index),
            .Name = "Zone" + suffix,
            .Graph = StressGraphAt(GraphIndexOfZone(spec, index)),
            .SceneRef = "levels/stress" + suffix + ".level.json",
            .Bounds = StressZoneBounds(spec, index),
            .BoundsOverridden = true,
            .CookedSceneRef = "levels/stress" + suffix + ".cooked.json",
            .CookedCollisionRef = "levels/stress" + suffix + ".collision.json",
            // Distinct per zone so failed-load suppression keys them apart.
            .CookedContentHash = 0xd000 + static_cast<std::uint64_t>(index),
        });
    }
    if (spec.ZoneCount > 0)
        manifest.StartZone = StressZoneAt(0);

    // Edges. Each shape names its neighbour pairs; docks and links are then
    // emitted over the same pairs so a spec can ask for either or both and get
    // the same adjacency either way.
    std::vector<std::pair<int, int>> pairs;
    switch (spec.Shape)
    {
        case TopologyShape::Chain:
            for (int index = 0; index + 1 < spec.ZoneCount; ++index)
                pairs.emplace_back(index, index + 1);
            break;
        case TopologyShape::Grid:
        {
            const int side = GridSide(spec.ZoneCount);
            for (int index = 0; index < spec.ZoneCount; ++index)
            {
                const int column = index % side;
                const int row = index / side;
                const int east = index + 1;
                const int south = index + side;
                if (column + 1 < side && east < spec.ZoneCount)
                    pairs.emplace_back(index, east);
                if (south < spec.ZoneCount)
                    pairs.emplace_back(index, south);
                (void)row;
            }
            break;
        }
        case TopologyShape::Hub:
            for (int index = 1; index < spec.ZoneCount; ++index)
                pairs.emplace_back(0, index);
            break;
        case TopologyShape::MultiGraph:
            for (int index = 0; index + 1 < spec.ZoneCount; ++index)
                pairs.emplace_back(index, index + 1);
            break;
    }

    std::uint64_t dockId = kDockIdBase;
    std::uint64_t linkId = kLinkIdBase;
    for (std::size_t edge = 0; edge < pairs.size(); ++edge)
    {
        const int a = pairs[edge].first;
        const int b = pairs[edge].second;

        if (spec.Docks)
        {
            // The plane sits on the boundary the two zones share, oriented along
            // the axis that separates them. A hub's spokes share no boundary
            // with the hub, so a dock there would be geometrically meaningless;
            // the shape asks for links instead.
            const Aabb3d boundsA = StressZoneBounds(spec, a);
            const Aabb3d boundsB = StressZoneBounds(spec, b);
            const bool separatedAlongX = boundsB.Min.X >= boundsA.Max.X - 1.0e-3f;
            const Vec3d normal = separatedAlongX ? Vec3d{ 1, 0, 0 } : Vec3d{ 0, 0, 1 };
            const Vec3d right = separatedAlongX ? Vec3d{ 0, 0, 1 } : Vec3d{ -1, 0, 0 };
            const Vec3d origin =
                separatedAlongX
                    ? Vec3d{ boundsA.Max.X,
                             (boundsA.Min.Y + boundsA.Max.Y) * 0.5f,
                             (boundsA.Min.Z + boundsA.Max.Z) * 0.5f }
                    : Vec3d{ (boundsA.Min.X + boundsA.Max.X) * 0.5f,
                             (boundsA.Min.Y + boundsA.Max.Y) * 0.5f,
                             boundsA.Max.Z };
            const Vec2d halfExtents{ static_cast<float>(spec.ZoneSpan * 0.5),
                                     static_cast<float>(spec.ZoneHeight * 0.5) };
            if (spec.Shape != TopologyShape::Hub)
                AddStressDockPair(manifest, dockId++, StressZoneAt(a), StressZoneAt(b),
                                  origin, normal, right, halfExtents);
        }
        if (spec.Links || spec.Shape == TopologyShape::Hub)
            AddStressLinkPair(manifest, linkId++, StressZoneAt(a), StressZoneAt(b));
    }

    return manifest;
}

// A running partition over a generated topology, stepped in the order the
// engine's frame actually uses.
//
// The existing traversal harness updates the partition first and then settles
// the request it just made, so a load both starts and finishes inside one
// frame. The shipping frame cannot do that: FramePhase::DrainAsyncTasks and
// FramePhase::ZoneResidency run at 3 and 4, and the partition update that asks
// for zones runs at 7. Every request therefore lands no earlier than the next
// frame. Anything asserting how long a zone takes to arrive has to be stepped
// this way or it is describing a frame shape the game does not have.
class TopologyHarness
{
public:
    TopologyHarness(TopologySpec spec, WorldPartitionStreamingConfig config,
                    unsigned taskThreads = 0)
        : Spec_(spec)
        , TaskThreads_(taskThreads)
        , Tasks_(taskThreads)
        , Schema_(MakeSchema())
        , World_(Schema_)
        , SceneContext_(Logging_)
        , Loader_(Tasks_, World_, Schema_, Serializers_, SceneContext_, FrameLoop_)
        , Partition_(MakeRecipe(), config)
    {
    }

    // Empty on success, otherwise the reason.
    std::string LoadManifest()
    {
        std::string error;
        if (!Partition_.LoadManifest(BuildTopology(Spec_), &error))
            return "manifest load failed: " + error;
        return {};
    }

    // One frame in engine phase order: drain and flush (phase 3), residency
    // (phase 4), then the partition update that decides what to stream (phase
    // 7). A zone asked for in this frame can arrive no earlier than the next.
    void StepFrame(double dt = 1.0 / 60.0)
    {
        DrainOnce();
        World_.FlushLifecycleRequests();

        (void)World_.BeginResidencyProcessing();
        World_.FinalizeResidencyProcessing();

        Partition_.Update(dt, Loader_, World_);
        World_.FlushLifecycleRequests();
    }

    void SetFocusToZone(int index)
    {
        Partition_.SetFocus(StressZoneCenter(Spec_, index));
    }

    void RelocateToZone(int index)
    {
        Partition_.RelocateFocus(StressZoneCenter(Spec_, index));
    }

    // Steps until nothing is in flight, so an observation reflects policy
    // rather than how many workers happened to run.
    void SettleLoads(int maxFrames = 512)
    {
        for (int frame = 0; frame < maxFrames && AnyLoading(); ++frame)
            StepFrame();
    }

    [[nodiscard]] bool AnyLoading() const
    {
        for (int index = 0; index < Spec_.ZoneCount; ++index)
            if (Loader_.IsLoading(StressZoneAt(index)))
                return true;
        return false;
    }

    [[nodiscard]] int ResidentCount() const
    {
        int count = 0;
        for (int index = 0; index < Spec_.ZoneCount; ++index)
            if (World_.IsZoneResident(StressZoneAt(index)))
                ++count;
        return count;
    }

    RuntimeWorld& World() { return World_; }
    WorldPartitionRuntime& Partition() { return Partition_; }
    AsyncZoneLoader& Loader() { return Loader_; }

    // Zones finalized and destroyed so far. A scenario that streams nothing
    // satisfies a churn bound trivially, so callers state the churn they got.
    [[nodiscard]] int Attaches() const { return Attaches_; }
    [[nodiscard]] int Detaches() const { return Detaches_; }

    // The order zone builds were issued in. Two runs that made the same
    // decisions produce the same sequence, which is a stronger statement than
    // two runs that ended in the same place.
    [[nodiscard]] const std::vector<ZoneId>& BuildSequence() const
    {
        return BuildSequence_;
    }

private:
    static WorldComponentSchema MakeSchema()
    {
        WorldComponentSchema schema;
        RegisterEngineRuntimeComponents(schema);
        schema.Seal();
        return schema;
    }

    void DrainOnce()
    {
        if (TaskThreads_ == 0)
            Tasks_.PumpWork();
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Tasks_.DrainCompletions();
    }

    ZoneLoadRecipeFn MakeRecipe()
    {
        return [this](const ZoneHeader& header)
        {
            const int index =
                static_cast<int>(header.Id.Value - kZoneIdBase);
            BuildSequence_.push_back(header.Id);

            ZoneLoadRecipe recipe;
            const int entities = Spec_.EntitiesPerZone;
            const double span = Spec_.ZoneSpan;
            recipe.Build = [index, entities, span](ZoneLoadPackage& package)
            {
                for (int entity = 0; entity < entities; ++entity)
                {
                    const ZoneLocalEntityId id = package.CreateEntity();
                    Transform3f transform;
                    transform.Position =
                        Vec3d(static_cast<float>(static_cast<double>(index) * span
                                                 + (entity % 16)),
                              0.0f,
                              static_cast<float>(entity / 16));
                    package.AddComponent(id, LocalTransform{ transform });
                }
            };
            recipe.Finalize = [this](RuntimeWorld&, RuntimeZoneRecord&)
            {
                ++Attaches_;
                return true;
            };
            return recipe;
        };
    }

    TopologySpec Spec_;
    unsigned TaskThreads_ = 0;
    LoggingProvider Logging_;
    AsyncTaskQueue Tasks_;
    WorldComponentSchema Schema_;
    RuntimeWorld World_;
    ComponentSerializerRegistry Serializers_;
    SceneSerializationContext SceneContext_;
    RuntimeFrameLoop FrameLoop_;
    AsyncZoneLoader Loader_;
    WorldPartitionRuntime Partition_;
    std::vector<ZoneId> BuildSequence_;
    int Attaches_ = 0;
    int Detaches_ = 0;
};

// Edge count a spec implies, for tests that bound work against topology size
// rather than against a hard-coded number that drifts when a shape changes.
[[nodiscard]] inline int ExpectedEdgeCount(const TopologySpec& spec)
{
    switch (spec.Shape)
    {
        case TopologyShape::Chain:
        case TopologyShape::MultiGraph:
            return spec.ZoneCount > 0 ? spec.ZoneCount - 1 : 0;
        case TopologyShape::Hub:
            return spec.ZoneCount > 0 ? spec.ZoneCount - 1 : 0;
        case TopologyShape::Grid:
        {
            const int side = GridSide(spec.ZoneCount);
            int count = 0;
            for (int index = 0; index < spec.ZoneCount; ++index)
            {
                if (index % side + 1 < side && index + 1 < spec.ZoneCount)
                    ++count;
                if (index + side < spec.ZoneCount)
                    ++count;
            }
            return count;
        }
    }
    return 0;
}
}  // namespace ZoneTopologyStress
