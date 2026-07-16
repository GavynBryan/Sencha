# World Graph Runtime: Graphs, Zone AABBs, Docks, Demand, and Focus

Status: implemented corrective replacement (2026-07-15). This remains the
canonical runtime contract for the implementation.

Canonical: this document and `12-spatial-compilation.md` replace every earlier
zone-shape and inferred-topology design. This document owns runtime data,
crossing, containment fallback, and streaming policy. Document 12 owns authored
data, Kyusu affordances, cooking, migration, validation, tests, and execution
order.

The branch previously contained a partial implementation of the superseded
design. It was treated as evidence, not a compatibility constraint.
`AuthoredZoneShape`, convex-prism cells, exact-shape containment, and the cooked
shape hash/product were rejected and removed.

---

## 1. Decisions

Accepted:

1. A world contains one or more graphs.
2. Every zone belongs to exactly one graph.
3. A zone is one streamed registry and one participation state.
4. Every zone has exactly one finite, nondegenerate world-space AABB.
5. Zone AABBs are coarse data. They may overlap and need not tile the world.
6. One authored world dock is one logical graph edge.
7. A bilateral dock is authored once; the cook emits two zone-local endpoint
   views carrying the same `DockId`.
8. Ordinary movement changes zones only by crossing an explicit dock.
9. AABB containment is reserved for initial placement, teleport, save restore,
   recovery, diagnostics, editor framing, suggestions, and coarse queries.
10. Graph policy decides residency and normally retains more than two zones.
11. Doors and gates may bind to a `DockId` but do not define topology.
12. Spatial-radius demand continues to measure point-to-AABB distance. This is
    the already-working streaming policy and must remain behaviorally stable.

Rejected:

- convex-cell lists, editable polygon footprints, height bands, split/merge cell
  workflows, and adjacent-cell authoring;
- exact cooked zone geometry, exact-shape hashes, minimap contours derived from
  a zone ownership shape, and any renamed equivalent of those products;
- requiring an AABB to trace an L-shaped hallway or form a perfect partition;
- inferring a graph edge from touching, intersecting, or overlapping bounds;
- polling AABB containment to perform ordinary zone transitions;
- reciprocal authored dock records;
- making a door or gate the topology owner;
- editor handles, labels, snapping state, or adapter data in runtime components
  or cooked artifacts.

Deferred until a concrete consumer exists:

- nav, visibility, audio, or minimap products on graph edges;
- runtime-generated graphs and exterior cell compilation;
- dynamic topology rebakes;
- non-Euclidean coordinate transforms;
- boundary-resident entity compilation.

---

## 2. Vocabulary and identity

| Term | Meaning |
| --- | --- |
| World | Owns graph records, zone headers, world-scene content, and resident topology. |
| Graph | A topology and residency-policy domain containing zones; not a spatial territory. |
| Zone | Residency and entity-ownership atom with exactly one coarse AABB. |
| Zone AABB | Coarse world-space bounds used for radius demand, placement fallback, diagnostics, editor framing, and graph-node placement. |
| Dock | Bounded oriented plane connecting two explicit zones. Authored once in the world scene. |
| Link | Non-spatial edge such as a teleport. It has no fake plane or arm volume. |
| Endpoint | Cooked zone-local view of one authored dock or link. Two views are not two logical edges. |
| Focus zone | Current topology/residency focus. Ordinary changes occur through docks. |
| Demand | A reasoned request for zone residency or participation. |

```cpp
using GraphId = StrongId<struct GraphIdTag, uint64_t>;
using ZoneId  = StrongId<struct ZoneIdTag,  uint64_t>;
using DockId  = StrongId<struct DockIdTag,  uint64_t>;
using LinkId  = StrongId<struct LinkIdTag,  uint64_t>;
```

Authored ids are nonzero, editor-minted, stable across renames, and validated for
uniqueness within their own identity domain.

---

## 3. Authored and cooked zone headers

The exact authored contract is:

```cpp
struct AuthoredZoneHeader
{
    ZoneId      Id;
    GraphId     Graph;
    std::string Name;
    std::string SceneRef;
    Aabb3d      Bounds;            // exactly one world-space AABB
    bool        BoundsOverridden;  // false = derived cache, true = explicit value
};
```

`BoundsOverridden == false` means Kyusu refreshes `Bounds` from boundable zone
content when that content is available. `Bounds` is still persisted: it is the
cached value for closed zones and the fallback for an empty zone. A new zone gets
a valid starter AABB. If a derived zone has no boundable entities, refresh keeps
its last valid value rather than manufacturing an empty/invalid box.

`BoundsOverridden == true` means content edits do not recompute the value. The
designer can return to derived mode explicitly. Editing a derived box in the
viewport first creates an override using the current box; this mode change and
the edit are one undoable operation.

The cooked header contains the same single AABB and no shape product:

```cpp
struct CookedZoneHeader
{
    ZoneId                    Id;
    GraphId                   Graph;
    std::string               Name;
    Aabb3d                    Bounds;
    std::string               CookedSceneRef;
    std::string               CookedCollisionRef;
    uint64_t                  CookedContentHash;
    std::vector<DockEndpoint> Docks;
    std::vector<LinkEndpoint> Links;
};
```

The repository may continue using one `ZoneHeader` C++ type with authored-only
and cooked-only fields, as it does now, but serialization must preserve the
separation above. There is no `Shape`, `BroadBounds`, `CookedShapeHash`, shape
reference, or exact containment payload.

### 3.1 AABB validity and overlap

An authored/cooked zone AABB is valid only when every coordinate is finite and
each full extent is greater than the shared authoring epsilon. `Aabb3d::IsValid()`
alone is insufficient because it permits zero thickness and infinities.

Overlapping zone AABBs are valid. No overlap, contact, nearest-neighbor, or
containment relation creates adjacency. Validation must have an explicit test
that overlapping AABBs produce no topology record and no overlap error.

---

## 4. Resident topology and dock endpoints

Graph records retain the implemented value-driven policy configuration:

```cpp
struct GraphStreamingConfig
{
    std::optional<int32_t> HopCount;
    std::optional<double>  Radius;
    std::optional<int32_t> ResidentZoneCap;
};

struct GraphRecord
{
    GraphId              Id;
    std::string          Name;
    GraphStreamingConfig Streaming;
};
```

One valid authored dock always cooks to exactly two endpoint records:

```cpp
enum class DockSide : uint8_t { A, B };

struct DockEndpoint
{
    DockId       Id;                 // identical in both endpoint views
    ZoneId       OwnerZone;
    GraphId      OwnerGraph;
    ZoneId       OtherZone;
    GraphId      OtherGraph;
    DockSide     Side;

    Vec3d        Origin;
    Vec3d        Normal;             // unit vector: owner -> other
    Vec3d        Right;              // unit endpoint-frame +X
    Vec3d        Up;                 // unit endpoint-frame +Y
    Vec2d        HalfExtents;        // bounded plane in Right/Up
    Aabb3d       OwnerArmBoundsLocal;// endpoint-frame box, owner side is z <= 0

    uint32_t     Directions;         // authored A->B/B->A bits, unchanged
    int32_t      PreloadPriority;
    int32_t      PreloadDepth;
    std::vector<std::string> RequiredTags;
};
```

For an endpoint, a world point maps to local coordinates with dot products
against `Right`, `Up`, and `Normal`. The endpoint's owner arm is always on local
negative Z. The B endpoint reverses normal and right and transforms the authored
Side B box into that owner-local frame. Keeping the local AABB avoids the current
prototype's loss of orientation when it expands a rotated box into a world AABB.

The endpoint pair is deterministic and reciprocal:

- A owns A's view and names B as `OtherZone`;
- B owns B's view and names A as `OtherZone`;
- both use the same `DockId`, half extents, policy hints, and direction bits;
- the two endpoint frames face out of their owner toward the other zone;
- endpoint records are locality views, never counted as two edges.

Multiple different `DockId` values may connect the same ordered or unordered zone
pair. Indexing and graph display must retain every one.

Links use the same identity and reciprocal locality pattern but omit origin,
frame, plane extents, and arm bounds.

---

## 5. Focus and containment

### 5.1 Ordinary movement

Ordinary movement is authoritative through the current zone's dock endpoints:

```cpp
struct ZoneFocusState
{
    ZoneId Current;
    ZoneId Previous;
    DockId ArmedDock;
    Vec3d  PreviousPosition;
};
```

A crossing from an endpoint succeeds when:

1. the swept focus bounds intersect that endpoint's local owner arm;
2. the previous point is on the owner's side and the current point is on the
   other side of the plane, with the existing epsilon/jitter rules;
3. the swept plane intersection lies within both plane half extents;
4. the authored direction permits travel from this endpoint;
5. the destination meets the existing residency/physics contract.

On success runtime emits one transient `ZoneCrossingRecord`, changes focus to
`OtherZone`, arms the same `DockId` against jitter, and layers traversal grace
through demand. It does not issue arbitrary loads or unloads.

Horizontal, diagonal, and vertical docks use the same endpoint-frame math. No
axis or upright assumption is allowed.

### 5.2 Placement, teleport, save restore, and recovery

These exceptional paths may query zone AABBs. Because overlaps are legal, the
query must expose ambiguity internally rather than pretending containment is
unique:

```cpp
struct ZoneContainmentResult
{
    ZoneId              Chosen;
    std::vector<ZoneId> Candidates; // ascending id
    bool                Ambiguous;
};

ZoneContainmentResult ResolveZoneAt(
    const WorldPartitionManifest&, Vec3d position, ZoneId preferred);
```

Resolution is deterministic:

1. collect every valid AABB containing the point;
2. retain `preferred` if it is a candidate;
3. otherwise choose smallest volume, then lowest `ZoneId`;
4. if no AABB contains the point, use the existing nearest-AABB fallback with
   the same volume/id tie breaks;
5. report ambiguity or out-of-bounds recovery through diagnostics/telemetry.

The public convenience `ZoneAt` may return `Chosen`, but callers that make
placement decisions must retain/report `Ambiguous`. This is coarse recovery, not
a route for ordinary focus changes and not a promise of exact spatial ownership.

---

## 6. Demand policy remains graph-driven

The implemented policy remains the baseline:

- focus zone is pinned and fully active;
- same-graph neighbors are demanded by hop policy;
- zones in a proximity graph are demanded by point-to-AABB radius;
- the previous zone receives traversal grace/linger;
- explicit pins and gameplay demands merge;
- a cross-graph endpoint seeds the destination graph under the destination
  graph's configuration;
- a dock arm may add prediction/priority but is not the sole preload mechanism.

Each graph resolves its own `HopCount`, `Radius`, and `ResidentZoneCap`. A radius
exterior connected to a hop interior does not make either graph inherit the
other's policy.

Radius behavior must keep using closest-point distance from the focus position to
`ZoneHeader::Bounds`; it must not regress to center distance. Graph Viewer nodes,
by contrast, are deliberately placed at `Bounds.Center()`.

Demand reasons remain explicit (`Focus`, `SameGraphHop`, `SpatialRadius`,
`DockApproach`, `CrossGraphEntry`, `ExplicitPin`, `Gameplay`,
`TraversalGrace`, and `Linger`) so residency remains explainable.

---

## 7. Gates and runtime boundaries

```cpp
struct DockGateBinding
{
    DockId Id;
};
```

A door, force field, breakable, or other gameplay entity may bind to a dock.
Several entities may share one `DockId`. Physics controls whether traversal is
physically possible; the dock controls what a crossing means; demand controls
residency.

Deleting a gate does not delete a dock. Deleting a dock leaves bindings dangling
and validation reports them; it does not cascade-delete gameplay content.

No runtime editor adapter, gizmo, label, snap value, selected sub-handle, or
creation context is permitted in `WorldDock`, `DockEndpoint`, or runtime systems.

---

## 8. Runtime query surface

The initial query surface stays narrow:

```cpp
std::span<const DockEndpoint> DocksFrom(ZoneId zone) const;
std::span<const LinkEndpoint> LinksFrom(ZoneId zone) const;
const GraphRecord*            FindGraph(GraphId graph) const;
const CookedZoneHeader*       FindZone(ZoneId zone) const;
std::optional<ZoneId>         ZoneAt(Vec3d position) const;
ZoneContainmentResult         ResolveZoneAt(Vec3d position, ZoneId preferred) const;
```

Graph algorithms remain pure functions over headers and endpoint indexes.
Navigation, visibility, map, and audio add their own products only when their
consumers land.

---

## 9. Runtime acceptance tests

The runtime/data correction is accepted only when headless tests prove:

1. overlapping AABBs are legal and create no edge;
2. previous-focus containment wins in an overlap and all other tie breaks are
   deterministic;
3. initial placement and relocation use only the AABB path;
4. ordinary movement cannot change focus without crossing a dock;
5. spatial-radius demand still uses point-to-AABB distance;
6. a bounded plane rejects a crossing outside its width/height;
7. vertical, horizontal, and diagonal docks share one crossing path;
8. local arm AABBs remain correctly oriented for rotated docks;
9. one authored bilateral dock produces two reciprocal endpoints with one id;
10. two distinct docks between the same zones remain two logical edges;
11. a closed door can block physics without removing demand/topology;
12. topology remains queryable when connected zone registries are unloaded.

---

## 10. Runtime non-goals

- no exact zone geometry under a new type name;
- no AABB contact graph;
- no perfect spatial partition requirement;
- no containment-driven ordinary transitions;
- no per-door streaming policy;
- no runtime topology authoring or rebake;
- no generic capability mask for future subsystems;
- no editor code or editor state in runtime artifacts.
