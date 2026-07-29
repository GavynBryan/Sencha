# World Graph Runtime Contract

Status: implemented corrective contract on `agent/world-graphs-and-docks`
(2026-07-15). Together with Plan 12, this supersedes the spatial-ownership and
connection-local streaming designs in the earlier plans.

This file owns runtime data, topology, demand, focus resolution, crossing, and
late-residency behavior. Plan 12 owns authoring, Kyusu affordances, cooking,
migration, validation, Graph Viewer scope, and rollout gates.

## 1. Invariants

1. A Zone is one streamed registry, belongs to one Graph, and has one coarse
   world-space AABB.
2. Zone AABBs may overlap, need not touch, do not tile the world, and never
   imply topology.
3. Initial placement, teleport, save restore, recovery, radius demand,
   diagnostics, framing, and suggestions may query AABBs. Ordinary movement
   changes Zone only through an explicit Dock.
4. One `DockId` or `LinkId` is one authored connection and one logical graph
   edge. Directionality is a property of that edge. Two cooked endpoint views
   are locality records, not two edges.
5. Different stable IDs may connect the same Zone pair and remain different
   selectable graph edges.
6. Gates control physical traversal. They do not remove topology or suppress
   residency demand.
7. Connections carry adjacency/crossing data only. Streaming policy belongs to
   Graphs and the residency layer.

The deleted spatial ownership chain must stay deleted: there is no polygonal or
cell-based Zone boundary product, containment mesh, height-band copy, split-cell
workflow, cooked ownership artifact, or minimap contour generated from one.

## 2. Runtime data model

The implemented Graph policy record is:

```cpp
struct GraphStreamingConfig
{
    std::optional<int32_t> HopCount;
    std::optional<double>  Radius;
    std::optional<int32_t> ResidentZoneCap;
};

struct GraphRecord
{
    GraphId Id;
    std::string Name;
    GraphStreamingConfig Streaming;
};
```

`ZoneHeader` is shared by authored and cooked manifests. Authored files use
`Id`, `Name`, `Graph`, `SceneRef`, `Bounds`, and `BoundsOverridden`. Cook fills
the cooked scene/collision/hash fields and endpoint arrays:

```cpp
struct ZoneHeader
{
    ZoneId Id;
    std::string Name;
    GraphId Graph;
    std::string SceneRef;
    Aabb3d Bounds;                 // the only Zone spatial metadata
    bool BoundsOverridden = false;

    std::string CookedSceneRef;
    std::string CookedCollisionRef;
    uint64_t CookedContentHash = 0;
    std::vector<DockEndpoint> Docks;
    std::vector<LinkEndpoint> Links;
};
```

The editor derives `Bounds` from boundable content by default and persists the
last valid derived value. A designer can explicitly override it. Editing a
derived AABB creates an override in the same undoable transaction. Empty
content does not manufacture an invalid box or silently erase the last value.

Cooked endpoint records are deliberately small:

```cpp
struct DockEndpoint
{
    DockId Id;
    ZoneId OwnerZone;
    ZoneId OtherZone;
    DockSide Side;
    Vec3d Origin;
    Vec3d Normal;
    Vec3d Right;
    Vec3d Up;
    Vec2d HalfExtents;
    uint32_t Directions;
};

struct LinkEndpoint
{
    LinkId Id;
    ZoneId OwnerZone;
    ZoneId OtherZone;
    DockSide Side;
    uint32_t Kind;
    uint32_t Directions;
};
```

Graph IDs are obtained from the referenced Zone headers; they are not copied
into every endpoint. No endpoint contains designer-authored loading policy,
tags, distances, or volumes.

## 3. Focus and coarse lookup

`ResolveZoneAt` collects every valid AABB containing a point and returns a
`ZoneContainmentResult { Chosen, Candidates, Ambiguous }`. A preferred Zone wins
while it remains a candidate; otherwise the smallest AABB wins, then lowest
`ZoneId`. Outside all boxes, nearest point-to-AABB distance wins with the same
volume/id tie breaks. The previous Zone survives only when no valid AABB exists.

This deterministic lookup is for placement and recovery. It is not an ordinary
transition path and does not claim spatial ownership.

## 4. Graph-driven residency

`ComputeZoneDemand` merges these independent reasons:

- focus Zone at full participation;
- same-Graph outgoing neighbors through the resolved hop count;
- same-Graph AABBs within point-to-box radius;
- the previous Zone through traversal grace and linger;
- explicit gameplay pins;
- a cross-Graph destination seed.

A cross-Graph Dock or Link seeds its destination Zone. The destination Graph's
own hop/radius/cap settings expand from that entry; the connection cannot
override the neighborhood. One demand pass does not recursively walk through a
third Graph.

Gate state is absent from the demand API. A closed door still leaves its edge
and destination demand visible. Eviction sorts non-focus, non-pinned candidates
by hop, then runtime-derived spatial cost, then stable Zone ID. More than two
Zones can be resident. Current per-Graph caps remain the repository's coarse
budget input; RAM/VRAM arbitration is a separate residency-layer extension and
must consume these demand records rather than reimplement Graph policy.

The supported reason vocabulary is `Focus`, `SameGraphHop`, `SpatialRadius`,
`CrossGraphEntry`, `ExplicitPin`, `Gameplay`, `TraversalGrace`, and `Linger`.

## 5. Bounded-plane crossing

`AdvanceZoneFocus` consumes previous/current focus positions and the current
Zone's endpoint views. A candidate succeeds only when:

1. directionality permits movement from the endpoint side;
2. signed plane distance moves from the owner side to the destination side;
3. the swept segment intersects the plane, including the case where one fixed
   sample lands exactly on it;
4. the intersection projected onto `Right`/`Up` is within `HalfExtents`;
5. destination physics participation is ready.

The implementation uses a fixed side epsilon and fixed hysteresis distance.
After a crossing, the same `DockId` is suppressed until the actor clears the
hysteresis band, preventing threshold jitter while allowing a later reversal.
Horizontal, diagonal, and vertical planes use the same basis math.

Capsule support is runtime-only: radius plus cylinder half-height projected on
the plane normal determines a safe source-center clamp. It is not serialized,
displayed, or interpreted as prediction policy. The implementation performs no
extra broad phase; profiling may justify a fixed, internally derived plane slab
later, but never an authored one.

## 6. Late destination contract

V1 selects a threshold clamp:

1. demand should normally make the destination physics-ready before arrival;
2. if it is not ready, crossing returns
   `BlockedDestinationNotReady`, keeps focus on the source, and supplies a
   capsule-aware `SafeSourcePosition`;
3. `WorldPartitionRuntime` increments `LateTraversalCount` for telemetry;
4. the game integration moves both `LocalTransform` and the underlying
   `CharacterMover` to that safe position, so the next physics tick cannot
   restore the invalid destination-side position;
5. the same sweep retries when residency becomes ready.

Clamping preserves coherent movement without requiring every Dock to have a
physical gate. Development tooling can alert on the counter, but the runtime
behavior remains defined in all builds.

## 7. Runtime validation and tests

Cooked validation requires finite, positive Zone AABBs; resolvable graph/Zone
references; valid direction bits; and exactly two reciprocal endpoint views per
stable connection ID. Dock frames must be finite, unit length, orthogonal, and
geometrically reciprocal. Endpoint disagreement is a cook/load error. AABB
overlap is legal and never creates an edge.

Headless coverage includes overlapping AABBs, deterministic ambiguous lookup,
point-to-AABB radius demand, more-than-two-Zone residency, graph-only hop depth,
cross-Graph policy seeding, a closed gate retaining topology, fast swept
crossing, exact-plane samples, bounded rejection, diagonal/horizontal planes,
jitter/reversal hysteresis, capsule-safe late clamps, reciprocal endpoint
views, parallel edges, and traversal determinism across task counts.

## 8. Permanent tombstones

The following former symbols/policies must remain absent from authored
components, endpoint records, manifests, inspectors, renderers, manipulators,
demand APIs, and tests: `SideAArmBounds`, `SideBArmBounds`, `ArmBounds`,
`DockApproach`, `PreloadPriority`, `PreloadDepth`, `DemandCondition`, connection
required-tags, endpoint depth overrides, and any renamed per-side approach,
arming, preload, or trigger payload.
