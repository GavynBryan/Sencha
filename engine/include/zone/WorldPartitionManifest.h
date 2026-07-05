#pragma once

#include <optional>
#include <string>
#include <vector>

#include <math/geometry/3d/Aabb3d.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZoneId.h>

class JsonValue;

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

// Per-region overrides of the streaming demand shape. Each field optional:
// absent inherits the world/global base (EngineRuntimeConfig). Graph versus
// radius character is derived from the values: Radius == 0 is graph-only.
struct RegionStreamingConfig
{
    std::optional<int32_t> HopCount;        // >= 0
    std::optional<double>  Radius;          // finite, >= 0
    std::optional<int32_t> ResidentZoneCap; // >= 1

    friend bool operator==(const RegionStreamingConfig&,
                           const RegionStreamingConfig&) = default;
};

struct RegionRecord
{
    RegionId              Id;
    std::string           Name;
    RegionStreamingConfig Streaming;

    friend bool operator==(const RegionRecord&, const RegionRecord&) = default;
};

struct ZoneHeader
{
    ZoneId      Id;
    std::string Name;
    RegionId    Region;                    // exactly one, validated
    std::string SceneRef;                  // project-relative authored scene path
    Aabb3d      Bounds;                    // world coordinates (single implicit space in v1.0)
    bool        BoundsOverridden = false;  // true: designer-set, cook must not recompute

    // Cooked-manifest-only fields; zero/empty in authored manifests. The world cook
    // fills them; the runtime loading policy consumes them.
    std::string CookedSceneRef;
    std::string CookedCollisionRef;
    uint64_t    CookedContentHash = 0;

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
    int32_t            PreloadPriority = 0; // higher loads earlier within the neighbor set
    // Authored reach: crossing this edge grants the BFS at least this many
    // further hops, letting one critical corridor preload deeper than the
    // global horizon. 0 = inherit the remaining budget.
    int32_t            PreloadDepth = 0;
    // Gate: ALL listed gameplay-tag names must be active in the world state for
    // this edge to exist for streaming; empty = always open. Stored as dotted
    // NAMES (tag ids are registration-order runtime values, never serialized).
    std::vector<std::string> RequiredTags;
    // No content reference: an edge is topological. Content that realizes it
    // (a door) references the edge, never the reverse.

    friend bool operator==(const TransitionRecord&, const TransitionRecord&) = default;
};

struct WorldPartitionManifest
{
    std::string Name;
    ZoneId      StartZone;   // optional; invalid means "not designated" (validation warns)
    std::vector<RegionRecord>     Regions;
    std::vector<ZoneHeader>       Zones;
    std::vector<TransitionRecord> Transitions;

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
