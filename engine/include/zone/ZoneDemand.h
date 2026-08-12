#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/ZoneParticipation.h>

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
//
// A published record always carries at least one reason. One zone can be
// demanded several ways at once (pinned and a graph neighbor, say), and one
// kind can appear more than once when separate sources produced it, so Reasons
// is a list rather than a set of flags.
struct ZoneDemandRecord
{
    ZoneId            Zone;
    ZoneParticipation Desired;
    std::vector<ZoneDemandReasonRecord> Reasons;
};

// Whether the record carries this kind of reason at all. Compares the kind
// only: two SpatialRadius entries from different seeds are one kind.
[[nodiscard]] bool IsDemandedFor(const ZoneDemandRecord& record,
                                 ZoneDemandReason reason);

// The record's reason kinds as a "+"-joined display string, each kind named
// once, in a fixed order that does not depend on how the reasons accumulated.
[[nodiscard]] std::string DescribeZoneDemandReasons(const ZoneDemandRecord& record);

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

// Who a residency demand is on behalf of.
//
// A zone-layer identity on purpose: an authority streams around every player at
// once, but "player" is a networking word and this module does not have one. The
// net layer maps a PeerId onto a source id and the policy never learns which is
// which, so the same merge serves a server holding sixteen neighborhoods, an
// editor previewing one, and a split-screen game that has no session at all.
using FocusSourceId = StrongId<struct FocusSourceIdTag, uint32_t>;

// What a single-focus caller is: source one. Named so the single-source
// overloads and the span ones describe the same thing rather than two things
// that happen to agree.
inline constexpr FocusSourceId kPrimaryFocusSource{ 1 };

struct ZoneFocusSource
{
    FocusSourceId Source;
    ZoneId Focus;
    // Absent when position is not meaningful (a scripted warp, a menu). Spatial
    // radius demand applies only to sources that have one.
    std::optional<Vec3d> Position;
};

// Pure. The demand set for one focus: the focus zone at full participation,
// its graph neighbors within HopCount hops at the config's preload
// participation, zones within Radius of the focus position likewise (when a
// position is supplied), plus pins at their minimum. Linger is runtime state
// and is layered on by WorldPartitionRuntime::Update, never computed here.
// Deterministic: records ascend by zone id value.
[[nodiscard]] std::vector<ZoneDemandRecord>
ComputeZoneDemand(const WorldPartitionManifest& manifest,
                  const WorldPartitionIndex& index,
                  ZoneId focus,
                  std::span<const ZonePin> pins,
                  const WorldPartitionStreamingConfig& config,
                  const Vec3d* focusPosition = nullptr);

// The same policy over several focus sources at once, which is what an
// authority streaming around every connected player needs: its residency is the
// union of their neighborhoods, and linger absorbs their crossings exactly as it
// absorbs one player's today.
//
// The merge, stated because it is the whole contract:
//
// - A zone's hop rank is the *minimum* over the sources that demanded it, so a
//   zone one hop from anybody is treated as one hop away rather than as far as
//   the furthest player who can see it. The within-rank cost tiebreak merges the
//   same way, so nearer still survives longer.
// - Every source's focus zone gets full participation and immunity from
//   eviction. A player standing in a zone the cap would otherwise drop is a
//   player the authority stops simulating around.
// - Spatial radius applies per source, from that source's own position.
// - The cap and the eviction comparator are unchanged, applied once to the
//   merged set. Focus zones and pins may exceed it, exactly as focus does with
//   one source.
// - Reasons accumulate across sources rather than collapsing, so "why is this
//   zone resident, and for whom" stays answerable.
//
// Sources are expected sorted by source id; ties in the merge resolve toward the
// earlier source, which is what makes the result independent of the order peers
// happened to connect in.
//
// One source produces byte-identical records to the overload above. That is a
// property this is built to have rather than one it is tested into: a source's
// demand is accumulated by the same code either way, and the merge is a no-op
// when there is nothing to merge with.
[[nodiscard]] std::vector<ZoneDemandRecord>
ComputeZoneDemand(const WorldPartitionManifest& manifest,
                  const WorldPartitionIndex& index,
                  std::span<const ZoneFocusSource> sources,
                  std::span<const ZonePin> pins,
                  const WorldPartitionStreamingConfig& config);
