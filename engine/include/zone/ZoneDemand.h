#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/ZoneParticipation.h>

// Why a zone is demanded. Flags, not a single enum: one zone can be demanded
// for several reasons at once (pinned and a neighbor, say), and the demand
// inspector wants all of them.
struct ZoneDemandSources
{
    bool Focus = false;
    bool Pinned = false;
    bool Neighbor = false;
    bool Spatial = false;
    bool Lingering = false;
    bool SameGraphHop = false;
    bool SpatialRadius = false;
    bool CrossGraphEntry = false;
    bool ExplicitPin = false;
    bool Gameplay = false;
    bool TraversalGrace = false;
    bool Linger = false;
};

enum class ZoneDemandReason : uint8_t
{
    Focus,
    SameGraphHop,
    SpatialRadius,
    CrossGraphEntry,
    ExplicitPin,
    Gameplay,
    TraversalGrace,
    Linger,
};

struct ZoneDemandReasonRecord
{
    ZoneDemandReason Reason = ZoneDemandReason::Focus;
    ZoneId SourceZone;
    uint64_t SourceEndpoint = 0;
    int32_t Rank = 0;
    std::optional<double> Cost;

    friend bool operator==(const ZoneDemandReasonRecord&,
                           const ZoneDemandReasonRecord&) = default;
};

// One zone's desired residency this update. The data contract the kyusu demand
// inspector and the streaming telemetry read; records first, UI second.
struct ZoneDemandRecord
{
    ZoneId            Zone;
    ZoneParticipation Desired;
    ZoneDemandSources Sources;
    std::vector<ZoneDemandReasonRecord> Reasons;
};

// An explicit script- or gameplay-driven residency demand beyond policy. Data, not
// subclasses.
struct ZonePin
{
    ZoneId            Zone;
    ZoneParticipation Minimum;
};

// Mirrors the EngineRuntimeConfig streaming fields; plain data so the policy
// stays pure and testable without config plumbing.
struct WorldPartitionStreamingConfig
{
    int32_t HopCount = 1;
    double  LingerSeconds = 3.0;
    int32_t ResidentZoneCap = 8;
    // Preloaded zones render (and carry static collision) so doorways read as
    // real space and threshold crossings land on resident colliders.
    bool    NeighborVisible = true;
    bool    NeighborPhysics = true;
    // Proximity demand: zones whose bounds lie within this distance of the
    // focus position join the demand set. 0 = graph hops only.
    double  Radius = 0.0;

    friend bool operator==(const WorldPartitionStreamingConfig&,
                           const WorldPartitionStreamingConfig&) = default;
};

// The streaming config in force while `focus` is resident: the focus zone's
// graph overrides applied over `base`, field by field. Pure; the runtime and
// the editor preview both resolve through it. Invalid or graph-less focus
// returns base unchanged.
[[nodiscard]] WorldPartitionStreamingConfig
ResolveGraphStreamingConfig(const WorldPartitionManifest& manifest, ZoneId focus,
                             const WorldPartitionStreamingConfig& base);

// One zone's BFS rank from the focus. Ordering is graph-policy evidence, never
// an authored per-connection priority.
struct ZoneHopRank
{
    ZoneId  Zone;
    int32_t Hop = 0;
    double Cost = 0.0; // runtime-derived distance/cost within one policy rank
    ZoneDemandReason Reason = ZoneDemandReason::Focus;
    ZoneId SourceZone;
    uint64_t SourceEndpoint = 0;
};

// Pure. BFS over outgoing edges only, up to hopCount hops from the focus. Gate
// state never removes topology or residency demand. Empty for an invalid or
// unknown focus. Ascending zone id.
[[nodiscard]] std::vector<ZoneHopRank>
ComputeZoneHopRanks(const WorldPartitionManifest& manifest,
                    const WorldPartitionIndex& index,
                    ZoneId focus,
                    int32_t hopCount);

struct ZoneContainmentResult
{
    ZoneId Chosen;
    std::vector<ZoneId> Candidates;
    bool Ambiguous = false;
};

// Pure placement and recovery resolution from a position. The preferred zone
// wins while it remains an AABB candidate; otherwise the smallest-volume candidate,
// ties by ascending zone id. A position inside no zone resolves to the
// NEAREST zone by closest-point distance (ties: smaller volume, then id):
// derived bounds hug authored geometry, so a pawn standing on a floor slab or
// airborne routinely sits outside its zone's box, and keeping the previous
// focus would freeze streaming on whatever zone was entered last. `previous`
// survives only when no zone has valid bounds. Candidates contain only actual
// containing AABBs, sorted by id, so callers can diagnose overlap ambiguity.
// Shared by
// WorldPartitionRuntime and the editor's streaming preview.
[[nodiscard]] ZoneContainmentResult ResolveZoneAt(
    const WorldPartitionManifest& manifest, Vec3d position, ZoneId preferred);

[[nodiscard]] ZoneId ResolveFocusZone(const WorldPartitionManifest& manifest,
                                      Vec3d position, ZoneId previous);

// Pure. The demand set for one focus: the focus zone at full participation,
// its graph neighbors within HopCount hops at the config's preload
// participation, zones within Radius of the focus position likewise (when a
// position is supplied), plus pins at their minimum. Lingering is runtime
// state and is layered on by WorldPartitionRuntime::Update, never computed
// here. Deterministic: records ascend by zone id value.
[[nodiscard]] std::vector<ZoneDemandRecord>
ComputeZoneDemand(const WorldPartitionManifest& manifest,
                  const WorldPartitionIndex& index,
                  ZoneId focus,
                  std::span<const ZonePin> pins,
                  const WorldPartitionStreamingConfig& config,
                  const Vec3d* focusPosition = nullptr);
