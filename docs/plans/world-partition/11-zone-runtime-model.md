# World Graph Runtime: Graphs, Zones, Docks, Demand, and Focus

Status: proposed replacement design (2026-07-15), owner review before implementation.
Canonical: this document and `12-spatial-compilation.md` replace the previous
zone topology design. This document owns the runtime model. Doc 12 owns Kyusu
authoring, cooking, validation, and migration.

## Why this plan replaces the previous one

The previous design asked a spatial compiler to recover exact zone shape,
discover contacts, evaluate moving configurations, and become a general
queryable topology store. That was technically ambitious but authoring-hostile,
difficult to validate, and too far ahead of Sencha's actual consumers.

The replacement takes the useful parts of Metroid Prime's area model and makes
them easier to author:

- zones remain the residency atom;
- explicit dock boundaries decide ordinary zone crossings;
- topology remains resident even while zone contents are unloaded;
- doors and gates do not define topology;
- loading remains policy-driven and normally keeps more than two zones resident;
- the designer authors one bilateral world dock, while the cook emits two
  zone-local endpoints.

The other correction is terminology. Sencha does not need regions. It needs
**graphs**. A graph is a topology and residency-policy domain containing zones.
Graphs may connect through edges between specific zones, which allows a future
cell-partitioned exterior to connect to a hand-authored interior without making
either use the other's partition policy.

---

## 1. Verdicts

Accepted:

1. A world contains one or more graphs.
2. Every zone belongs to exactly one graph.
3. A zone is still one streamed registry and one participation state.
4. A dock is authored once at world level and compiled into two zone endpoint
   records.
5. Ordinary focus changes are caused by crossing dock geometry, not by AABB
   containment.
6. Zone shapes remain useful for ownership queries, minimap geometry, spawn and
   teleport resolution, and recovery.
7. Graph policy decides residency. A dock can contribute priority, but it does
   not issue raw load or unload commands.
8. Cross-graph travel is an edge between two zone endpoints. Graphs never link
   as anonymous wholes.
9. The topology needed by streaming stays resident in the world header.
10. Door and gate entities are ordinary gameplay entities that may bind to a
    dock, but they never become the topology source of truth.

Rejected:

- inferred doorway topology from zone bounds;
- automatic contact discovery from world collision;
- a runtime voxel label field for normal focus changes;
- a universal spatial-configuration compiler for doors, elevators, rotating
  halls, destructibles, and every future mechanism;
- making the door component own Zone A and Zone B;
- a global `if (just created a dock)` editor special case;
- treating the current focus graph's policy as the policy for every graph in a
  hybrid world;
- requiring hand-authored preload volumes for every connection.

Deferred until a real consumer lands:

- dynamic nav links and route-cache invalidation;
- visibility and audio capabilities on connections;
- runtime-generated graphs;
- boundary-resident entity compilation;
- cell and terrain graph generation;
- non-Euclidean coordinate-space transforms.

---

## 2. Vocabulary

| Concept | Meaning |
| --- | --- |
| **World** | Owns graph records, zone headers, endpoint records, world-scene content, and the residency runtime. |
| **Graph** | A set of zones evaluated under one residency-policy configuration. It is a policy and topology domain, not a spatial territory. |
| **Zone** | The residency and entity-ownership atom: one registry, one participation state, one cooked content package. |
| **Zone shape** | Authored ownership geometry for containment, minimap, diagnostics, and broad spatial queries. It is not the ordinary crossing mechanism. |
| **Dock** | A bounded spatial boundary connecting two zones. Authored once in the world scene and compiled into reciprocal endpoints. |
| **Link** | A non-spatial edge between zone endpoints: teleport, scripted relocation, instance entrance, world-map travel. |
| **Endpoint** | The zone-local cooked view of a dock or link. Endpoint records stay resident in zone headers. |
| **Gate binding** | Optional gameplay binding between one or more entities and a dock id. It controls passage, not topology identity. |
| **Focus zone** | The zone currently containing the primary focus source for participation and policy. |
| **Demand** | A reasoned request that a zone become or remain resident. Multiple reasons merge before budget resolution. |

### 2.1 Identity

```cpp
using GraphId = StrongId<struct GraphIdTag, uint64_t>;
using ZoneId  = StrongId<struct ZoneIdTag,  uint64_t>;
using DockId  = StrongId<struct DockIdTag,  uint64_t>;
using LinkId  = StrongId<struct LinkIdTag,  uint64_t>;
```

Authored ids are editor-minted, nonzero, and stable across renames. A compiled
cell graph may derive zone ids deterministically from the source graph and cell
coordinate when that compiler eventually exists.

---

## 3. Manifest and resident topology

The current `RegionRecord` becomes `GraphRecord`. This is a semantic rename of
the implemented policy grouping, not a second grouping layer.

```cpp
struct GraphStreamingConfig
{
    std::optional<int32_t> HopCount;
    std::optional<double>  Radius;
    std::optional<int32_t> ResidentZoneCap;
};

struct GraphRecord
{
    GraphId               Id;
    std::string           Name;
    GraphStreamingConfig  Streaming;
};

struct ZoneHeader
{
    ZoneId                      Id;
    GraphId                     Graph;
    std::string                 Name;
    Aabb3d                      BroadBounds;
    CookedZoneShapeRef          Shape;
    CookedZoneContentRef        Content;
    std::vector<DockEndpoint>   Docks;
    std::vector<LinkEndpoint>   Links;
};
```

The graph table and every zone header load at world start. Zone content remains
streamed. Endpoint records must be header data because policy needs adjacency
before either side's registry is resident.

### 3.1 Dock endpoints

```cpp
struct DockEndpoint
{
    DockId       Id;              // same id in both endpoints
    ZoneId       OwnerZone;
    GraphId      OwnerGraph;
    ZoneId       OtherZone;
    GraphId      OtherGraph;
    DockSide     Side;            // A or B
    Plane3d      Plane;           // world or graph-space plane
    Rect2d       SurfaceBounds;   // bounded area on the plane
    Aabb3d       ArmBounds;       // editable side-local volume
    uint8_t      Directions;      // A->B, B->A, or both
    int32_t      PreloadPriority;
    int32_t      PreloadDepth;
    TagQueryRef  DemandCondition; // optional, existing tag-gate semantics
};
```

`OwnerZone` is redundant inside a zone header but retained in debug builds and
artifact inspection. The cook verifies that the two endpoint records agree.

### 3.2 Links

A link uses the same endpoint pattern without crossing geometry. A one-way link
emits one outgoing endpoint and one incoming reference if needed for queries. A
bidirectional link emits reciprocal endpoints.

There is no graph-to-graph connection table. A cross-graph edge is simply a
dock or link whose endpoint records name zones in different graphs.

---

## 4. Focus and crossing

### 4.1 Ordinary movement

Containment is not polled every frame to guess whether the pawn changed zones.
The current zone's dock endpoints are the authoritative transition surface.

For each moving focus source, runtime state retains:

```cpp
struct ZoneFocusState
{
    ZoneId    Current;
    ZoneId    Previous;
    DockId    ArmedDock;
    Vec3d     PreviousPosition;
};
```

A dock crossing succeeds when:

1. the swept focus bounds intersect the endpoint's `ArmBounds`;
2. the previous and current positions lie on opposite signed sides of the
   plane with an epsilon band;
3. the swept intersection point lies inside `SurfaceBounds`;
4. the endpoint direction permits the crossing;
5. the destination is resident enough for the transition contract.

On success the runtime:

1. emits a transient `ZoneCrossingRecord`;
2. changes `Current` to `OtherZone`;
3. retains the source through linger or an explicit traversal-grace reason;
4. does not directly evict or load arbitrary zones.

`ZoneCrossingRecord` remains a frame-local event record. It is not topology and
is never serialized.

### 4.2 Non-ordinary placement

Zone-shape lookup remains necessary for:

- world start and save restore;
- editor placement;
- teleport and scripted relocation;
- falling out of the world and recovery;
- spatial queries and minimap location.

The lookup uses each zone's broad AABB first and exact cooked shape second. An
ambiguous or unassigned point reports a diagnostic and uses the existing
explicit fallback path. It never silently rewrites topology.

---

## 5. Demand is graph policy, not dock ownership

Sencha assumes more than two zones may be resident. The baseline policy is:

- focus zone: pinned and fully active;
- same-graph neighbors: resident according to hop, radius, participation, and
  budget;
- previous zone: retained for traversal grace;
- explicit pins and gameplay demands: merged;
- cross-graph destination entry: preloaded when the connecting endpoint enters
  the current demand frontier;
- dock approach: a priority boost, not the only reason the destination loads.

### 5.1 Per-graph evaluation

The implemented per-region policy becomes per-graph policy. Each graph expands
its own seeds using its own resolved config. The current graph's config is not
applied to every graph in the world.

A demand pass conceptually performs:

```text
focus sources and pins
    -> seed zones
    -> graph-local expansion using that graph's config
    -> cross-graph endpoint seeds, bounded by cross-graph preload depth
    -> destination graph-local entry expansion
    -> merge reasons
    -> resolve global RAM and VRAM budgets
```

The destination graph decides how much of itself to preload. A cell exterior
can use radius demand while an interior graph uses hop demand. Crossing between
them does not force either graph to adopt the other's partition rules.

Cross-graph propagation is bounded. Default depth is one graph boundary from a
focus source. An endpoint may raise or lower that depth explicitly. The demand
pass never recursively wakes every graph reachable in the world.

### 5.2 Demand reasons

```cpp
enum class ZoneDemandReason : uint8_t
{
    Focus,
    SameGraphHop,
    SpatialRadius,
    DockApproach,
    CrossGraphEntry,
    ExplicitPin,
    Gameplay,
    TraversalGrace,
    Linger,
};
```

Every candidate records its reason, source zone or endpoint, rank, and optional
cost. This preserves the existing "why is this zone resident" requirement.

### 5.3 Approach bounds

The dock's side AABBs serve three purposes:

- reliable arming of the crossing test;
- editor visualization of the physical transition neighborhood;
- optional predictive priority for expensive destinations.

They are not mandatory preload shells. A normal graph with `HopCount >= 1`
already keeps its immediate neighbors resident. Approach only improves ordering
when several candidates compete or a cross-graph destination is expensive.

---

## 6. Doors and gates

Metroid Prime stores doors as area-local script objects. Each loaded area
instantiates its own door. Opposite-side doors are separate objects and find
each other through reciprocal dock topology rather than a shared persistent
door id.

Sencha keeps the useful separation but does not require Prime's duplication as
a topology rule:

- a door is an ordinary gameplay entity in exactly one registry;
- a door may carry `DockGateBinding { DockId }`;
- one or more entities may bind to the same dock;
- physics decides whether the actor can physically cross;
- the dock decides which zone crossing means;
- demand decides which zones stay resident.

An ordinary door may live in either endpoint zone because immediate neighbors
are normally resident from both approaches. A content author may use two
zone-local door views sharing gameplay state when independent side residency is
required. A truly global mechanism may live in the world scene deliberately.
Neither choice changes the dock record.

The first door implementation must not invent a global door registry, shared
entity id, or mandatory paired-door abstraction. If repeated content proves
paired authoring painful, Kyusu may add a paired-gate creation recipe over the
same `DockId` binding.

---

## 7. Runtime query surface

The initial query surface stays small:

```cpp
std::span<const DockEndpoint> DocksFrom(ZoneId zone) const;
std::span<const LinkEndpoint> LinksFrom(ZoneId zone) const;
const GraphRecord*            FindGraph(GraphId graph) const;
const ZoneHeader*             FindZone(ZoneId zone) const;
std::optional<ZoneId>         ZoneAt(Vec3d position) const;
```

Graph algorithms remain explicit pure functions:

```cpp
ComputeGraphHopRanks(...);
ComputeWorldDemand(...);
```

Navigation, visibility, map, and audio add their own endpoint products and
queries when their consumers land. The runtime does not ship speculative
capability masks now.

---

## 8. Hybrid-world consequence

A future exterior cell graph can compile terrain cells into ordinary zone
headers. Its cells use spatial-radius policy. A facility interior remains a
hand-authored graph using dock-hop policy.

```text
ExteriorGraph / Cell 412
    <-> Dock 71
FacilityGraph / Entrance
```

Approaching Dock 71 keeps the exterior ring according to `ExteriorGraph`, seeds
the facility entrance, and lets `FacilityGraph` preload its own configured
entry neighborhood. On crossing, the facility becomes the focus graph while
traversal grace and linger retain the exterior long enough for retreat.

No runtime mode switch is required. The graphs differ by data and by the zone
producer used at cook time.

---

## 9. Migration from the current implementation

The source branch currently has `RegionRecord`, `TransitionRecord`, region
streaming overrides, graph BFS, spatial-radius demand, and a world-level
connection editor. Preserve the working machinery and migrate its vocabulary
and ownership.

1. `RegionId` -> `GraphId`.
2. `RegionRecord` -> `GraphRecord`.
3. `ZoneHeader.Region` -> `ZoneHeader.Graph`.
4. `RegionStreamingConfig` -> `GraphStreamingConfig`.
5. Keep the value-driven hop/radius/cap model. Do not add a graph-mode enum.
6. Replace authored geometric `TransitionRecord`s with world docks.
7. Replace teleports and scripted transitions with world links.
8. Cook docks and links into zone header endpoints.
9. Replace containment-driven focus changes with dock crossing.
10. Keep legacy JSON readable for one migration window; save writes the new
    graph and endpoint vocabulary.

The implemented doc 10 remains historical evidence for the existing policy.
Its `Region` name is superseded by this plan.

---

## 10. Implementation stages

Each stage is one reviewable commit with runtime and editor tests kept green.

### R1. Graph vocabulary migration

Rename manifest, ids, serializers, validation, panel labels, and pure policy
helpers. Preserve behavior byte-for-byte. Add legacy read coverage.

Gate: existing per-region streaming fixtures pass under graph names.

### R2. Endpoint data model

Add authored dock and link records to the world document, cooked endpoint
records to zone headers, resident indexes, serialization, and validation.
Keep current transitions readable but no longer writable.

Gate: one authored bilateral dock cooks to exactly two reciprocal endpoints;
one cross-graph dock indexes correctly from both zones.

### R3. Policy migration

Make demand evaluate graph-local configs and seed immediate cross-graph entry
policy without unbounded transitive propagation.

Gate: a radius exterior connected to a hop interior keeps the correct sets on
both sides and preloads the interior entrance before crossing.

### R4. Dock crossing

Implement swept arm-volume plus bounded-plane crossing and transient crossing
records. Retain shape lookup for placement and recovery.

Gate: vertical doorway, horizontal floor opening, diagonal hallway, fast swept
crossing, reversal, and plane-jitter fixtures.

### R5. Transition retirement

Migrate existing geometric transitions to docks, teleports to links, remove the
legacy transition authoring surface and runtime branch.

Gate: no ordinary focus change depends on AABB containment or transition labels.

### R6. Gate binding seam

Add `DockGateBinding` as a narrow gameplay-facing component and a test door
fixture. Do not build paired-door editor sugar yet.

Gate: a closed door prevents physical crossing while its destination remains
resident by graph policy; opening it permits the existing dock crossing without
changing topology.

---

## 11. Fitness tests

The design is accepted only if these remain simple:

1. A two-room doorway needs one authored dock and no hand-maintained reverse
   record.
2. A hole in the floor uses a horizontal dock with the same runtime code.
3. A diagonal hallway uses an oriented plane without inflating the zone shape.
4. A curved hallway uses one chosen cross-section and ordinary graph adjacency.
5. A locked door does not define or delete its connection.
6. Three or more neighboring zones may remain resident.
7. A radius cell graph preloads a hop interior through one cross-graph dock.
8. Duplicate, paste, undo, load, and component-add never rerun contextual dock
   initialization.
9. Teleports do not pretend to be planes.
10. Topology remains queryable while all connected zone registries are unloaded.

---

## 12. Non-goals

- no automatic topology inference from collision;
- no recursive zones;
- no graph object containing another graph;
- no global door ownership rule;
- no requirement that zone broad AABBs touch;
- no streaming policy embedded in door gameplay code;
- no runtime topology rebake;
- no spreadsheet-style adjacency authoring;
- no navmesh, PVS, map, or audio implementation in these stages.
