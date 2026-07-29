#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <math/geometry/3d/Aabb3d.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZoneId.h>

class JsonValue;

enum class DockSide : uint8_t
{
    A,
    B,
};

struct DockEndpoint
{
    DockId Id;
    ZoneId OwnerZone;
    ZoneId OtherZone;
    DockSide Side = DockSide::A;
    Vec3d Origin;
    Vec3d Normal = Vec3d::Forward();
    Vec3d Right = Vec3d::Right();
    Vec3d Up = Vec3d::Up();
    Vec2d HalfExtents{ 1.0f, 1.5f };
    uint32_t Directions = 3;

    friend bool operator==(const DockEndpoint&, const DockEndpoint&) = default;
};

struct LinkEndpoint
{
    LinkId Id;
    ZoneId OwnerZone;
    ZoneId OtherZone;
    DockSide Side = DockSide::A;
    uint32_t Kind = 0;
    uint32_t Directions = 3;

    friend bool operator==(const LinkEndpoint&, const LinkEndpoint&) = default;
};

struct DockDebugRecord
{
    DockId Id;
    uint64_t AuthoredEntity = 0;

    friend bool operator==(const DockDebugRecord&, const DockDebugRecord&) = default;
};

struct LinkDebugRecord
{
    LinkId Id;
    uint64_t AuthoredEntity = 0;

    friend bool operator==(const LinkDebugRecord&, const LinkDebugRecord&) = default;
};

enum class TransitionTopology : uint8_t
{
    Seam,       // contiguous geometry, no visual break
    Doorway,    // authored opening (a door)
    Teleport,   // discontinuous; no geometric relationship implied
};

struct TransitionFlags
{
    bool OneWay = false;

    friend bool operator==(const TransitionFlags&, const TransitionFlags&) = default;
};

// Per-graph overrides of the streaming demand shape. Each field optional:
// absent inherits the world/global base (EngineRuntimeConfig). Graph versus
// radius character is derived from the values: Radius == 0 is graph-only.
struct GraphStreamingConfig
{
    std::optional<int32_t> HopCount;        // >= 0
    std::optional<double>  Radius;          // finite, >= 0
    std::optional<int32_t> ResidentZoneCap; // >= 1

    friend bool operator==(const GraphStreamingConfig&,
                           const GraphStreamingConfig&) = default;
};

struct GraphRecord
{
    GraphId              Id;
    std::string          Name;
    GraphStreamingConfig Streaming;

    friend bool operator==(const GraphRecord&, const GraphRecord&) = default;
};

struct ZoneHeader
{
    ZoneId      Id;
    std::string Name;
    GraphId    Graph;                     // exactly one, validated
    std::string SceneRef;                  // project-relative authored scene path
    Aabb3d      Bounds;
    bool        BoundsOverridden = false;

    // Cooked-manifest-only fields; zero/empty in authored manifests. The world cook
    // fills them; the runtime loading policy consumes them.
    std::string CookedSceneRef;
    std::string CookedCollisionRef;
    uint64_t    CookedContentHash = 0;
    std::vector<DockEndpoint> Docks;
    std::vector<LinkEndpoint> Links;

    friend bool operator==(const ZoneHeader&, const ZoneHeader&) = default;
};

struct TransitionRecord
{
    TransitionId       Id;
    std::string        Name;   // optional; empty displays as "<From> -> <To>"
    ZoneId             From;
    ZoneId             To;
    TransitionTopology Topology = TransitionTopology::Doorway;
    TransitionFlags    Flags;
    // No content reference: an edge is topological. Content that realizes it
    // (a door) references the edge, never the reverse.

    friend bool operator==(const TransitionRecord&, const TransitionRecord&) = default;
};

struct WorldPartitionManifest
{
    std::string Name;
    ZoneId      StartZone;   // optional; invalid means "not designated" (validation warns)
    // The world scene: authored global content loaded once at world start and
    // never streamed. A project-relative scene path like a zone's SceneRef;
    // empty means the world has none. It is not a zone: no id, no bounds, no
    // graph, no participation in the index or the demand policy.
    std::string WorldSceneRef;
    // Cooked-manifest-only, the ZoneHeader trio's world-scene counterpart:
    // zero/empty in authored manifests, filled by the world cook, consumed by
    // the runtime's world-start load.
    std::string CookedWorldSceneRef;
    std::string CookedWorldCollisionRef;
    uint64_t    CookedWorldContentHash = 0;
    std::vector<GraphRecord>     Graphs;
    std::vector<ZoneHeader>       Zones;
    std::vector<TransitionRecord> Transitions;
    std::vector<DockDebugRecord> DockDebugMap;
    std::vector<LinkDebugRecord> LinkDebugMap;

    friend bool operator==(const WorldPartitionManifest&, const WorldPartitionManifest&) = default;
};

// Strict on identity and enums, tolerant on unknown keys (ignored, for forward
// compatibility). Returns nullopt with a message in *error on the first hard
// failure. Does not validate cross-references; that is WorldPartitionValidation.
[[nodiscard]] std::optional<WorldPartitionManifest>
ReadWorldPartitionManifest(const JsonValue& root, std::string* error);

// Writes every field, ids as hex strings, arrays in the order stored. Cooked-only
// fields are written only when nonzero/nonempty, so authored files stay clean.
[[nodiscard]] JsonValue WriteWorldPartitionManifest(const WorldPartitionManifest& manifest);
