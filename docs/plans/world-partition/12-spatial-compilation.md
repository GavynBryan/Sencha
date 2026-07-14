# Spatial Compilation: Subdivision, Evidence, Contacts, Configurations, and Artifacts

Status: proposed design (2026-07-13), owner review before any stage starts.
Canonical: this document and `11-zone-runtime-model.md` are the current zone
architecture (reasoning history lives in git). Doc 11 owns the runtime model
this compiles for: the topology store, capabilities, evaluation, mutation
rules, demand, queries, and the reconfiguration lifecycle. This document owns
everything the cook produces and the editor authors: residency subdivision,
spatial evidence, contact records, capability compilation, predicates,
spatial-configuration variants, artifacts and indexes, the editor surface,
fixtures, and stages.

## Why

Zone shape and zone connectivity are today hand-maintained approximations (a
derived AABB and an authored edge list) that cannot check each other and
cannot describe a world that moves. The compiler below recovers real shapes,
discovers every potential physical relationship (including every declared
arrangement of moving architecture), and emits them as data the runtime
evaluates under world state. The designer authors places, content, the
gameplay objects that actually exist, and declared arrangements of the ones
that move; Sencha derives the topology consequences. Nothing infers
connectivity from bounds contact, nothing duplicates gameplay objects with
portal-like proxies, and nothing rebakes at runtime.

---

## 1. Grounding (verified against the tree)

- **Brush geometry** is indexed face-vertex polygons, explicitly non-convex
  (`editor/kyusu/src/brush/BrushMesh.h:11-34`), triangulated at cook time;
  no per-face semantic flags exist (`BrushClustering.h:20` records
  "classify: future").
- **The level cook** (`DocumentCook.cpp:128-329`) collects world-space
  triangles per brush (`BrushCookInput.cpp:9-45`), clusters whole brushes
  into 16m cells by center (`BrushClustering.cpp:11-18`, cvar
  `editor.cook.cell_size`), and bakes per cell one `.smesh` and one Jolt
  triangle-mesh `.scol` from the same triangles
  (`CollisionShapeCook.cpp:14-57`). Render and collision are one triangle
  stream; props contribute no collision (`ZoneCollisionLoader.cpp:95-99` is
  the only production `Collider` emitter).
- **The world cook** (`WorldCook.cpp:13-139`) cooks each zone scene by file
  path and fills cooked-only manifest fields. Cook kernels are synchronous
  and pure by stated contract; `JobSystem::ParallelFor` in the texture cook
  (`TextureCook.cpp:52`) is the cook-parallelism precedent, and kyusu links
  the job system (`EditorServices.cpp:612`).
- **Cooked binary artifacts** (`.smesh`, `.stex`, `.scol`) ship with
  magic-and-version headers and are restored directly; Track F's binary
  scene work does not block new binary artifacts.
- **The runtime world path** loads the cooked manifest and world scene
  synchronously at world start into `ZoneRuntime::Global()`
  (`TemplateGame.cpp:731-763`): the slot where world-level cooked artifacts
  load once. World tags already flow `SetWorldTags` to the pure demand
  policy (`WorldPartitionRuntime.cpp:117-136`).
- **Movement truth**: `CharacterController` (radius 0.3, height 1.8, slope
  50; `CharacterController.h:19-21`) over Jolt `CharacterVirtual` with
  default stair handling (`CharacterMover.cpp:62`). No step-height field
  exists anywhere yet.
- **Math**: `Vec3d` is `Vec<3, float>` (`Vec.h:382`); no triangle-box test,
  voxel, Morton, or SDF utility exists; `Grid3d<T>`
  (`math/spatial/Grid3d.h`) is a dense grid template with no consumers.
- **Navigation is docs-only** (navmesh v1.0 as a cook sibling over the same
  collision; cross-zone planner v2.0; `engine-roadmap.md:263-273`).
  Open-field cell streaming, HLOD, impostors are Track C item 9, v3.0
  (`engine-roadmap.md:391`).

---

## 2. The authored surface

Authored (the first two exist today):

1. **Places**: zone documents with identity, names, and structural content
   ownership.
2. **Logical links and annotations**: `ConnectZones` mints teleport-kind
   contacts; annotation records attach names, priorities, depths, and
   predicates to compiled contacts by contact id.
3. **`ResidencyCellSize`** on a zone header: the one value that turns a
   place into a subdivided residency source (Section 3).
4. **`SpatialConfigurations`** on an assembly root entity: the one
   mechanism for everything whose arrangement changes topology: doors,
   gates, drawbridges, rotating halls, elevators, floodgates, destructible
   walls (Section 8). There is no separate gate component and no portal
   proxy object; the designer authors the gameplay object that exists and
   captures its arrangements.
5. **Labeling hints**, rare: an interior seed marker or influence volume,
   added only where the ambiguity overlay shows the compiler cannot know
   (Section 5.3).
6. **Preview scenarios**: named, game-authored bundles of tag and
   configuration overrides for editor evaluation (Section 10.1). The
   engine never interprets the names.

Never authored: portal planes, zone-bound boxes, hand-maintained doorway
links, streaming shells, per-cell anything, runtime edges.

---

## 3. Residency subdivision

### 3.1 The field and the frame

```cpp
// On ZoneHeader, authored, optional. Present: the cook expands this zone
// into independently resident child zones on the world-aligned horizontal
// residency grid of this pitch (meters). Must be a positive multiple of
// the cook cell size. Absent: the zone compiles as one residency unit.
std::optional<double> ResidencyCellSize;
```

The frame is world-aligned with origin at the world origin, the cook-cell
convention (`cell = floor(pos / pitch)` per horizontal axis). It depends on
nothing content edits can move: adding a brush never renumbers children.
An authored per-place grid origin is rejected as an id-invalidating knob
(builders align content to the world grid, which snapping already serves);
recorded trigger: a real world that cannot. Subdivision is horizontal;
vertical structure resolves through containment layering (Section 9), and
3D subdivision is a recorded deferral.

### 3.2 Child identity and stability

```text
childId = Hash64(sourceZoneId, cellX, cellZ)
```

re-salted deterministically on collision. **Stable under** all content
edits, renames, region changes, and bake-config changes. **Intentionally
invalidated by** pitch changes, source re-minting, and future
coordinate-space migration (the artifact header carries a space id,
invalid in v1). Pitch changes are content-migration events: when Track C
item 5's persistent state lands, its tooling rebuckets state by position;
contact annotations survive independently (contact ids do not key on
pitch). Child headers carry `SourceZone`, inherited `Region`, derived
names ("Garden 2,1"), per-child cooked refs, and both bounds below; the
source header is not itself loadable, and the compilation sidecar records
provenance.

### 3.3 Residency coverage versus content bounds

```cpp
Aabb3d ResidencyCoverage; // stable footprint: cell box (XZ) crossed with
                          // the source's vertical band; bespoke zones use
                          // their envelope. Spatial demand, analytic
                          // containment, and recovery test this.
Aabb3d ContentBounds;     // AABB of actually cooked content: diagnostics,
                          // cost analysis, tie-breaks.
```

A cell with one fence post near its far edge still loads on approach,
because demand tests the full footprint. **Emptiness**: a child exists iff
any content buckets into its cell (brush geometry, which includes floors
by construction; collision; any passthrough entity). Contentless air cells
produce no child and resolve through recovery; phantom coverage-only
children for flying profiles are a recorded deferral.

### 3.4 Content assignment

Nothing is ever hand-split: authoring keeps every object whole, and the
cook's default keeps it whole too. Assignment is by span, not by editing.

**Fits within one cell.** Assigned to the cell containing its anchor (a
brush by its center, an entity by its transform), the existing cook rule.
The common case; nothing further.

**Spans several cells** (a long wall, a bridge, a big rock, a floor slab,
a light or trigger whose influence reaches past its cell): kept whole,
assigned to one owning cell, with every cell it overlaps declaring a
**residency dependency** on that owner: whenever an overlapped cell is
demanded, the owner is demanded too, so the object is resident and
complete from every cell it touches. No split, no seam, no duplicated
geometry, one registry per object (the flat-zone invariant holds). The
cost is co-residency of the owner with its overlappers, bounded and cheap
while the span is a handful of cells. This is the dependency-retention
mechanism the dynamic-entity work also needs (doc 11 Section 9);
subdivision is its first consumer, and it replaces the earlier
influence-exceeds-cell warning: an object reaching past its cell is a
retention fact, not a diagnostic.

**Spans many cells** (a region-scale terrain surface) is not a
subdivision problem and must not become one: retention would pin the
whole region resident, and plane-splitting a continuous surface buys
render seams (tangent and normal discontinuities, T-junctions) for no win
its own tiling would not do better. True terrain is its own residency
citizen with its own LOD and tiling (Track C item 9, v3.0); this design
reserves the slot and builds none of it. A moderate v1.x ground built
from brushes rides the retention rule above, and a designer is never
asked to tile a floor to match the grid.

**The split kernel is deferred, not default.** Splitting an oversized
object at cell planes stays a valid cook optimization for the narrow case
where co-residency is measurably too expensive and seams are acceptable
(collision-only geometry, which does not render; or a future consumer
that tolerates cuts). When it lands it is a pure cook function with a
gated contract (exact triangle partition, `FaceMaterial` and UV
preservation, source provenance, sealed-where-sealed, mover-equivalent
across the seam). It is out of v1: retention makes it unnecessary for
correctness, and adding it early would buy seams for no capability.

**Configuration assemblies** (Section 8.3) are never split and never
bucketed away from their assembly root regardless of span.

---

## 4. Spatial evidence: shared kernels, per-product domains

### 4.1 The kernel library

Beside the other cook kernels (same "pure: no logging, no threads, no
disk" contract), data types in `engine/{include,src}/spatial/`, kernels in
`engine/include/assets/cook/`:

- zone-tagged triangle gathering (file-based, header-only zones included),
- conservative rasterization (cell Solid or Mixed if any triangle
  intersects its box; a triangle-box separating-axis primitive joins
  `math/geometry/3d/` with its own tests),
- clearance (integer chamfer transform, truncated),
- support and slope extraction,
- traversal-profile evaluation (Section 4.3),
- the sparse brick IR (sorted `Vec3i` keys over dense 16^3 payloads;
  `Grid3d<T>` earns its first consumer), canonical iteration, per-brick
  hashing,
- fixtures and the golden-hash harness.

**No persistent world-wide shared field artifact exists.** Each compiler
drives the kernels over its own domain and emits its own product: this
compiler samples bespoke envelopes, place frontiers, and configuration
assembly domains; the future navmesh compiler rasterizes every navigable
surface at its own resolution and emits polygon tiles; map and query
products filter their own. Runtime navigation never queries the zone
containment artifact and zone lookup never touches a navmesh. Standing
gate: the kernel API takes an explicit domain, and a fixture rasterizes an
open field through the kernels with no zone bake involved.

### 4.2 The zone compiler's domains

Bespoke place envelopes (geometry plus clearance-and-radius margin),
frontier bands where places meet, and, per spatial configuration state,
the assembly's affected bounds (Section 8.4). Not sampled: subdivided
interiors (analytic containment) and empty space beyond envelopes. A
domain-size assertion per fixture keeps it scoped.

### 4.3 Traversal profiles

```cpp
struct TraversalProfile
{
    float Radius;
    float Height;
    float StepHeight;
    float MaxSlopeDegrees;
};
```

Bake-config data; v1 ships one ("pawn"), seeded from `CharacterController`
plus `StepHeight` 0.4 (Jolt's walk-stairs default). This is the engine's
first authoritative step height; the roadmap's `LoftSteps` and
traversal-probe items read it when they land (their current text assumes a
`MovementProfile` field that does not exist). Standing gate: a scripted
mover walk agrees with baked traversability on every fixture. Per-cell
products: standing room, radius clearance, slope acceptance, step validity
(drops beyond `StepHeight` are one-way downward), no diagonal corner
cutting. Camera and projectile profiles are rejected; creature profiles
are data additions.

### 4.4 Determinism regime

Accumulating arithmetic in integer cell units; float only in
order-independent predicates. Canonical iteration everywhere; the labeling
queue is an indexed priority structure with total-order ties; no unordered
container reaches an output. Parallelism is `ParallelFor` over bricks with
index-order reduction; `worker_count == 0` is the byte-identical
reference. Artifacts record their full bake config and enter
`CookedCacheIndex` by content hash. Standing test: golden hash per
fixture, identical across reruns and worker counts.

---

## 5. Labeling (bespoke places and frontiers)

### 5.1 Sources

Free cells adjacent to solid cells seed with the solid's owner at cost
zero; ownership is authored fact, not heuristic (every blocking triangle
is a brush in exactly one place document). Props never seed. Cells inside
any configuration assembly's affected bounds seed only from geometry
present in the state under evaluation (Section 8.4). Seed markers and
influence volumes are override sources only.

### 5.2 Growth and posts

Multi-source shortest path over free cells: cost = integer chamfer step +
narrowness penalty (fronts meet inside throats, not across rooms) +
passage penalty inside assembly openings. Post passes: sub-threshold
enclosed islands absorb; a place labeling disconnected components reports
`spatial.zone.split_label` (Warning); unreached cells stay Unassigned
(not an error). Subdivided interiors are never grown; sampled labels win
over grids in frontier bands (Section 9).

### 5.3 Ambiguity, kept low-stakes

Per-cell margin between best and second-best source; low-margin boundary
runs flag Ambiguous (overlay plus Info diagnostic
`spatial.label.ambiguous_boundary`). Deterministic either way. Where open
places meet without a bottleneck the exact meter is explicitly
low-stakes: demand is spatial there, focus has margin, place identity is
the source. Hints exist for the designer who cares.

---

## 6. Contacts: one record family

### 6.1 Extraction

Wherever traversable free space crosses a boundary between different
resolved owners (bespoke-to-bespoke, bespoke-to-grid, grid-to-grid across
place frontiers), boundary faces group into connected components per
unordered zone pair; each component is one contact. Two doors between the
same pair are two contacts. Same-source sibling adjacency is implicit in
the grid and compiles nothing. Extraction runs per spatial-configuration
state within assembly domains (Section 8.4), so a contact knows which
states it exists in.

### 6.2 The record

```cpp
enum class ContactKind : uint8_t { Compiled, Link };
// Compiled: a physical relationship the spatial compiler found where two
//   zones' traversable free space meets. A narrow doorway and a broad
//   open frontier are the SAME kind; their geometric character lives in
//   Metrics (constriction, width), read by each consumer at its own
//   threshold, never frozen into a stored opening-versus-frontier enum
//   that would force one threshold on every consumer.
// Link: an authored logical relationship (teleport, scripted route),
//   no geometry, no metrics. Compiled-versus-authored is the one real
//   provenance boundary and the only reason a kind field survives.

struct WorldContact
{
    ContactId          Id;
    ZoneId             A, B;            // unordered, A = lower id
    ContactKind        Kind;
    uint8_t            Directions;      // A->B, B->A, both (drops one-way)
    uint8_t            Profiles;        // traversal profiles that cross
    CapabilityMask     Potential;       // compiled capability potentials
    ConfigurationSetId Controller;      // invalid = uncontrolled
    uint32_t           StateMask;       // controller states containing it
    PredicateRef       Predicates;      // per-capability predicate rows
    Vec3d              Representative;  // area-weighted boundary center
    Aabb3d             Bounds;
    ContactMetrics     Metrics;         // area, min width, frontier
                                        // length, constriction, normal
};
```

SoA in the artifact and the runtime store (hot evaluation reads
predicates and masks; metrics are cold), AoS in this document for
legibility. The former `TransitionRecord` array dissolves into this
family: graph demand traverses contacts, and the manifest carries no
separate transition list (Section 9.2). A compiled contact between two
spatially eligible zones demands nothing by default (Section 7.1); it
still serves reachability validation, overlays, approach metadata, and
future nav and map products. Width and constriction are metadata, never
topology authority: opening-versus-frontier is a display and
future-consumer reading of those metrics (Section 7.3), not a compiled
decision, so no threshold is baked and no runtime behavior turns on it.

### 6.3 Identity

```text
ContactId = Hash64(worldSalt, zoneA, zoneB,
                   quantize(representative, 2m), ordinalInBucket)
```

with deterministic re-salt on collision. Contacts between compiled
children hash source ids, so re-pitching does not churn bespoke-border
contacts. A contact existing in several configuration states is one
record (same location, same id, `StateMask` union); different docks of a
rotating hall are different locations and therefore different contacts.
Sub-2m moves keep ids; remodels produce a reconciliation report
(unmatched old, unannotated new, nearest suggestion), never a silent
rebind. Controlled contacts additionally key on their controller id,
which is editor-minted and survives every remodel (Section 8.2), making
gate-adjacent annotation churn rare by construction.

---

## 7. Capability compilation and predicates

### 7.1 Potentials

Each contact compiles which capabilities it can ever offer:

- **`Traversal` potential**: the contact has traversable cells for a
  profile in at least one configuration state. A barred grate whose gaps
  are below the pawn radius never compiles pawn traversal potential in
  any state.
- **`Demand` potential** (which contacts graph demand may traverse), the
  successor of the previous promotion rules, verbatim in effect:
  1. the contact has a controller (a governed opening is conditional
     content; the room behind a door preloads by default),
  2. either endpoint's region has `JoinsSpatialDemand = false` (the
     ineligible side's only demand path is topological, regardless of
     the opening's width),
  3. an annotation grants it (authored topological preload across a
     specific contact),
  4. `Link` (authored logical relationships always participate).
  A compiled contact between two spatially eligible zones with no
  controller and no annotation compiles no `Demand` potential: proximity
  carries them, and hop counts keep meaning "rooms away through real
  openings." This is width-independent (a wide temple mouth into a
  graph-only interior promotes by rule 2; a narrow canyon between two
  radius regions does not), which is exactly why opening-versus-frontier
  is not a compiled kind.

Future capabilities (Navigation, Visibility, Audio, Map) compile in the
change that lands their consumer, per doc 11 Section 3.3's growth rule;
the mask and tables are shaped for additive growth.

### 7.2 Predicates

```cpp
struct ContactPredicate            // one row per (contact, capability)
{                                  // that needs gating; absent row =
    TagSetRef  AllOf;              // unconditionally active when
    TagSetRef  NoneOf;             // potential exists
    ConfigurationRequirementRef Config; // list of (set id, state mask),
                                        // usually length zero or one
};
```

Compiled defaults, overridable by annotation:

- Controlled `Traversal`: requires the controller in a state whose
  geometry opens the contact (`StateMask` membership), plus any authored
  tags. A powered security door authors
  `AllOf {power.on}, NoneOf {security.lockdown}` and inherits the
  configuration requirement.
- Controlled `Demand`: unconditional by default (preload behind closed
  doors); an authored predicate expresses "do not preload the sealed
  wing until quest tags arrive," which preserves the shipped
  `RequiredTags` semantics as the authored case rather than the only
  case.
- Uncontrolled contacts: no predicate rows unless authored.

Tag names intern to ids at artifact load (registration-order tag ids are
never serialized, per the `core/gameplay_tags` rule; the artifact stores
dotted names in a string table). Evaluation is a linear pass: for each
predicate row, membership tests against the tag set and the
configuration map (doc 11 Section 3.4). Multi-mechanism contacts carry a
requirement list (Section 8.4); combined-alignment paths are route
composition at query time, never compiled products.

### 7.3 Kind and label vocabulary

Display labels are derived editor-side from data, never stored, and they
read along two different axes that must not be flattened into one peer
list of Doorway/Seam/Frontier (that flattening would imply the width
split is the same kind of distinction as the controller split; it is
not):

- **The mechanical axis is the controller.** A `Compiled` contact with a
  controller reads Controlled (the inspector names it; "Doorway" is the
  one-word badge when a graph node wants one); without one it reads
  Uncontrolled. This is the real line: it is exactly the
  controlled-versus-uncontrolled split that drives `Demand` and
  `Traversal` (Sections 7.1, 7.2), and it is queryable
  (`ContactFilter.Controller`).
- **The cosmetic axis is width, and only on uncontrolled contacts.** A
  narrow one (constriction below the editor's display threshold) reads
  Seam, a broad one Frontier. This turns nothing: the threshold is a
  cosmetic editor setting, and a Seam and a Frontier are the same record
  with the same behavior.

So the surface leads with controlled-versus-uncontrolled and treats width
as a secondary readout on the uncontrolled ones; a `Link` reads by its
authored label (Teleport first). When a real consumer (visibility leaks,
audio transmission, a map door icon) needs the narrow-versus-broad
decision it reads `Metrics.constriction` at the threshold its own need
dictates; if several consumers converge on one threshold, that is when a
stored classification is earned (directive 4), not before.

### 7.4 Migration

One cook flag per world. The migration cook maps the authored transition
list onto compiled contacts: matched geometric edges become annotations
(name, priority, depth carried over; `RequiredTags` become `Demand`
predicates, preserving shipped behavior exactly) and are deleted;
Teleport edges become `Link` contacts; unmatched authored edges and
unannotated discovered contacts are reported. After migration the
authored file contains places, links, annotations, scenarios, and
hints. Validation swaps accordingly: `partition.bounds.overlap` and
unpaired-edge rules retire; reachability floods edges, contacts, and
radius cliques under the authored-default scenario;
`spatial.annotation.orphaned`, `spatial.zone.split_label`, and the
configuration diagnostics (Section 8.5) join.

---

## 8. Spatial configurations

### 8.1 The mechanism

One authored component covers every topology-relevant arrangement
change: doors, gates, portcullises, drawbridges, rotating corridors,
elevators, floodgates, docking platforms, destructible walls. The
designer authors the gameplay object that exists, adds the component to
its assembly root, and captures arrangements; Sencha derives the
topology consequences. Only connectivity-relevant motion declares
states: fans, pistons, and decorative movers never appear here.

```cpp
struct SpatialConfigurationState
{
    ConfigurationStateId Id;        // small index, stable per set
    std::string          Name;      // "Open", "NorthSouth", "Raised"
    // Captured arrangement of the assembly subtree:
    std::vector<MemberPose>  Poses;    // local transforms per member
    std::vector<MemberFlag>  Included; // per-member inclusion (present
                                       // or absent in this state);
                                       // destructibles use this
    bool                 Transit = false; // an arrangement that docks
                                          // nothing (mid-motion truth)
};

struct SpatialConfigurations       // component on the assembly root
{
    ConfigurationSetId   Id;       // editor-minted StrongId, durable
    std::vector<SpatialConfigurationState> States;
    ConfigurationStateId Default;
};
```

Capture is transform-and-inclusion snapshotting of the subtree: robust,
serializable, previewable, and cook-evaluable. Component-state-driven or
callback-driven variants are rejected for the cook (nondeterministic,
game code in the bake); gameplay that wants logic-driven geometry
resolves it into inclusion flags and poses.

### 8.2 Controller identity without entity identity

`ConfigurationSetId` is minted by the editor into the component data
(the zone-id precedent) and survives every remodel, rename, and move.
Contacts carry the controlling id; the runtime gameplay system that owns
the entity pushes state by that id (`SetConfiguration(id, state)`); the
editor and queries find controlled contacts through the
controller-to-contacts index. No serialized entity reference exists
anywhere in the topology data, so gate binding does not wait on Track C
item 5. Where the controller entity resides (world scene first,
boundary residency later) is doc 11 Section 9.

### 8.3 Assemblies and residency

Assembly members are excluded from the static bake (their geometry
enters per-state evaluation instead) and are never split or re-bucketed
away from their root by subdivision. The assembly's entities live where
authored (typically the world scene for boundary-straddling mechanisms,
per doc 11 Section 9); their zone residency is independent of which
configuration is active.

### 8.4 Cooking states

Per set, the cook computes the **affected bounds**: the union of member
geometry bounds across all states plus the standard margin. Within that
domain, per state: member geometry is posed and included per the
capture, evidence passes rerun, labels resolve, and contacts extract.
Results merge into the one contact family: a contact discovered at the
same location across states is one record whose `StateMask` accumulates;
dock contacts unique to a state carry that state alone; a `Transit`
state contributes no dock contacts by construction.

Interacting mechanisms: when two sets' affected bounds overlap, the cook
evaluates the cross product of only those overlapping sets' states
within the intersection, and a contact needing both carries a
requirement list entry per set. Cartesian explosion is avoided because
evaluation is per overlapping cluster, clusters are small and rare, and
independent mechanisms never multiply: a path that exists only when two
independent mechanisms align is two contacts and a route-time
conjunction, not a compiled product (fixture 9).

Cost note: per-state domains are local (a door's domain is meters), so
state count multiplies small bakes, not the world. The domain-size
assertion covers assemblies too.

### 8.5 Diagnostics

`spatial.configuration.no_contacts` (a set whose states never produce a
contact: probably not topology-relevant; Info),
`spatial.configuration.state_unreachable_geometry` (a state whose posed
members collide with static world geometry; Warning),
`spatial.configuration.dock_unresident_risk` is not a cook diagnostic
(the runtime prepare lifecycle owns it, doc 11 Section 7), and
`spatial.annotation.orphaned` covers stale references as everywhere
else.

---

## 9. Artifacts and what ships

### 9.1 The topology artifact

`.cooked/worlds/<stem>.sztopo` (binary, magic and version, the `.scol`
restored-directly precedent), referenced from the cooked manifest with a
content hash, loaded once at world start into the topology store
(doc 11 Section 3): contact records (SoA), predicate rows, the tag-name
string table, configuration-set table (ids, state ids, names, defaults),
annotation results, and the indexes (zone to incident contacts, zone
pair, controller to contacts, source to children). Evaluated state is
runtime-only and never ships.

### 9.2 The manifest

Zones (with `SourceZone`, `ResidencyCoverage`, `ContentBounds`, cooked
refs), regions (shape config plus `JoinsSpatialDemand`), scenarios, and
artifact references. **No transition array**: the authored `.sworld`
carries links, annotations, hints, and scenarios in authored form; the
cook compiles all relationship data into the topology artifact. The
manifest stays O(zones); relationships are O(contacts) in the artifact.

### 9.3 The containment artifact

`.cooked/worlds/<stem>.szfield`, unchanged in role: sampled bricks
(palette labels, label depth) over bespoke and frontier domains, grid
parameters and child tables per subdivided source. Lookup precedence,
deterministic: sampled assigned cells first (label-depth hysteresis),
analytic grids second (coverage containment; ties: containing coverage,
nearest `ContentBounds`, source id; cell-edge margin hysteresis),
recovery third (nearest `ResidencyCoverage`). The vertical fixture set
(cave under garden, interior in village cell, bridge over road, stacked
floors, rooftop, falling, XZ-coincident Y-distinct places) pins the
precedence; the rooftop's answer is deterministic and documented, with
an influence volume as the override. Containment is not
state-dependent in v1 (doc 11 Section 8); assembly interiors label from
their default state.

Size expectations: bricks near architecture and frontiers only; fixture
worlds in tens of KB, content-scale mixed worlds in hundreds of KB,
measured and gated before anything grows.

---

## 10. Editor surface

Everything below runs the same pure evaluator as the runtime against the
last bake, with the existing stale-cook badge on hash mismatch. No PIE
required for any of it.

### 10.1 Preview scenarios and the state inspector

The world authors named scenarios (tag overrides plus configuration
overrides; engine-opaque names): "Authored Default", "All Open",
"Power Offline", whatever the game means by them. The partition panel
gains a scenario selector (plus "Custom" scratch state, and a recorded
future "Follow PIE" once telemetry streams live state). The state
inspector lists world tags (toggle) and every configuration set with its
states (dropdown); edits re-evaluate immediately and every surface
reflows: topology overlays, streaming preview, demand records, contact
rows, reachability diagnostics.

### 10.2 The contact inspector

Selecting a contact (list, graph panel, or viewport marker) shows: kind,
endpoints (and their sources), controller and its current state,
potential and active capabilities, per-capability predicate rendered as
requirements with pass or fail marks ("requires power.on: present;
SecurityGate14 = Open: currently Closed; blocked by security.lockdown:
absent"), directions, profiles, metrics, demand consequences ("counts
toward Demand: yes; East Hall resident because of this contact"), and
the annotation editor. It answers, in place: why is this zone resident,
why is this contact not traversable, what would activate it, which
entity controls it.

### 10.3 Configuration authoring

Select the assembly root, add `SpatialConfigurations`, Capture State
(snapshots subtree poses and inclusion), arrange with the ordinary
gizmos, capture again, mark a transit state, request a local rebake
(the assembly domain only; seconds, not a world cook), and step through
states in the preview watching contacts appear and disappear with their
capability readouts. Per-state contact deltas display beside the state
list.

### 10.4 Scenario comparison (v1 scope)

Pick scenarios A and B: the panel lists contacts activated and
deactivated per capability, and zones newly demanded or released (the
demand policy run under both states with the same focus). Deterministic,
cheap, and the fixture for state-diff testing. Route diffs, budget
deltas, and the reconfiguration timeline simulator (prepare, ready,
transit, commit, release with memory peaks) are recorded editor features
for when routes and budgets exist as consumers.

### 10.5 Overlays and panel hygiene

Label slices, contact markers styled by kind and active state,
capability badges, ambiguity heat, coverage grids, and the graph panel
(nodes from compiled shapes grouped by source; edges styled by kind,
controller, and current capability; the long-deferred node-link view,
whose recorded trigger has fired, now drawing evaluated truth). Demand
records and lists group children by `SourceZone`.

---

## 11. Streaming facts

Contact metrics ship with the records; approach distances (coarse
per-zone geodesic to each `Demand`-potential contact and region exit)
ship quantized for lead-time prefetch; residency costs ship on Track C
item 1's `ZoneBudgetRecord`. Facts only; every decision stays in the
demand policy (doc 11 Section 5).

---

## 12. Fixtures and stages

### 12.1 Fixture suite (stage 0, the acceptance vocabulary)

Indoor set: curved hallway, L-rooms, T-junction, stacked-sealed,
stairwell, wrap-around with interior room, thin wall, sub-pawn slot,
drop ledge, two-doors-same-pair, wide-open two-owner boundary. Place
set: courtyard (bespoke, controlled arches), palace garden (subdivided,
bespoke grotto and orangery inside), field region (subdivided, walled
gate exit, unwalled open boundary), village (subdivided streets,
graph-only interiors), cliffside into cave. Assignment set: a multi-cell
slab and a multi-cell bridge (dependency retention, whole, no split, no
seam), a large-influence light (retention), a canyon between two radius
regions (compiled contact, no `Demand`), a wide temple mouth into a
graph-only interior (`Demand` regardless of width), one continuous room
split into two bespoke zones (a wide-open contact on a deterministic
ambiguous boundary). Vertical set: Section 9.3's list.
Topology set: (1) closed gate: `Demand` active, `Traversal` inactive,
opening activates without recook; (2) barred grate: no pawn `Traversal`
potential in any state (visibility recorded for its future capability);
(3) rotating corridor: three states, per-state dock contacts, transit
exposes none, prepare-before-commit; (4) elevator: cab local, dock
contacts per state; (5) drawbridge: raised blocks traversal, lowered
creates it, opposite side preloadable before lowering; (6) destructible
wall: inclusion-flag states, intact and destroyed; (7) powered security
door: tag-plus-configuration predicate, inspector explains the failure;
(8) wide graph-only entrance: `Demand` potential regardless of width;
(9) two overlapping mechanisms: pairwise state evaluation, no global
product, combined path answered by route conjunction; (10) scenario
diff: two scenarios produce a deterministic contact and demand diff.

### 12.2 Stages

Each stage one execution-spec lane when accepted; suite green
throughout; overview binding rules apply.

- **Stage 0: fixtures and formats.** The suite above; `spatial/` types;
  brick IR; artifact headers round-trip; golden-hash harness.
- **Stage 1: subdivision compilation.** Section 3 complete. Gates:
  child-id stability under content edits; coverage-based radius demand
  (the sparse-cell fixture loads on approach); dependency retention keeps
  the multi-cell slab and bridge whole and resident from every overlapped
  cell, with no split and no seam; `SourceZone` across cook, records,
  preview; the field fixture streams by radius with zero contacts
  compiled and zero sampling run.
- **Stage 2: spatial eligibility.** `JoinsSpatialDemand` through
  config, policy, preview. Gate: village interiors cold from street
  radius; S-D3 amendment recorded.
- **Stage 3: evidence kernels.** Section 4 over explicit domains;
  determinism gates; the open-field kernel-reuse gate.
- **Stage 4: labeling and contacts.** Sections 5 and 6 for static
  geometry (no configurations yet). Gates: every indoor fixture labels
  and contacts as written; grotto beats garden grid; decoy island
  absorbs; drop ledge one-way; ambiguity flags exactly once.
- **Stage 5: topology artifact, capabilities, evaluator, demand.**
  Sections 7 and 9.1, doc 11 Sections 3 through 5: potentials,
  predicates, `.sztopo`, the store, `EvaluateWorldTopology`, revision,
  demand over `Demand`-active contacts, `RequiredTags` migration, the
  selection API and `ComputeReachableZones`, validation swap. Gates:
  the temple mouth carries `Demand` and streams the interior; the
  canyon compiles a plain contact with no `Demand` and streams by
  radius; the room split into two bespoke zones yields one wide-open
  contact on a deterministic boundary; fixture 1's
  gate flips `Traversal` by state with zero recook; fixture 10's diff
  is deterministic; the migrated fixture world runs with no authored
  geometric edges and byte-identical demand under authored-default
  state.
- **Stage 6: runtime containment.** Section 9.3, doc 11 Section 8.
  Gates: the vertical set resolves as written; the traversal harness
  crosses village and cliff-cave fixtures with focus flips exactly at
  boundaries and zero missed ticks; the mover-agreement gate; artifact
  sizes in budget.
- **Stage 7: spatial configurations.** Section 8 and doc 11 Section 7:
  component, capture, per-state cooking, controller ids, state input,
  prepare/commit lifecycle. Gates: fixtures 3 through 7 and 9 as
  written; the rotating corridor refuses motion until prepared and
  commits dock adjacency and demand at one evaluation point; pairwise
  overlap evaluation bounded (assert evaluated state-pair count).
- **Stage 8: editor surface.** Section 10. Gate: the manual
  walkthrough (select scenario, flip a tag, watch reflow; inspect a
  blocked contact and read why; capture a two-state door and preview
  both; compare two scenarios).
- **Stage 9: streaming facts.** Section 11 emission and doc 11
  Section 5.4 consumption; lead-time and ordering gates in preview and
  records.
- **Stage 10: incremental bake.** Brick-hash dirty tracking, margin
  expansion, frontier-outward relabel, full-rebake backstop; scheduled
  when measured bake time hurts; byte-identity gate.

Recorded, not scheduled: navigation capability and navmesh back end
(Track A item 5 over the stage-3 kernels), visibility capability
(portal work, v2.0), audio and map capabilities with their systems,
route and blocking-set algorithms with caches (planner, AI), the
reconfiguration timeline simulator, "Follow PIE" preview state,
aperture polygon refinement, 3D subdivision, phantom air cells,
per-region artifact splitting, adaptive resolution, runtime compilation
for procedural worlds.

---

## 13. Risks

1. **Scope gravity.** A topology store invites every system to demand
   features early. The guards are the capability growth rule (doc 11
   Section 3.3), the one-algorithm v1, and the recorded-not-scheduled
   list; enforcement is review against this document.
2. **Pose-capture drift**: a captured state can go stale against
   remodeled assembly geometry. The per-state local rebake is cheap and
   the state-unreachable-geometry diagnostic catches collisions;
   re-capture is one click. Residual risk accepted and documented.
3. **Split-kernel fidelity**: gated, not assumed (Section 3.4).
4. **Contact id churn**: mitigated by coarse quantization, source-keyed
   child borders, controller-keyed controlled contacts, reconciliation
   reports, sparse annotations.
5. **Roaming entities and controller residency**: owned honestly in
   doc 11 Section 9.
6. **Pitch changes invalidate child ids**: rare, deliberate, with
   recorded migration expectations.
7. **Float determinism**: confined to order-independent predicates;
   golden hashes watch it.
8. **Terrain stays brush-shaped in v1.x**; Track C item 9 owns the
   rest and this design reserves its slots.
9. **Evaluation cost**: linear in predicate rows; the incremental
   indexing optimization has a recorded trigger (measured cost), not a
   v1 structure.

## 14. Non-goals

- No third runtime tier, no hierarchical participation, no mode enums,
  no genre vocabulary.
- No topology from bounds contact; no promotion by geometry alone; no
  runtime edge minting; no scripts inside evaluation.
- No whole-world sampling; no persistent shared field artifact; no
  runtime query facade beyond doc 11 Section 6; no navmesh, no map
  meshes, no route caches here.
- No dictated gameplay: the engine never animates a door, times a
  bridge, or decides when motion starts.
- No auto-bake daemon.

## 15. Open questions for the owner

1. **Defaults**: residency pitch 64m, sample cell 0.25m, pawn
   `StepHeight` 0.4, coverage vertical margin, prepare participation
   `{Visible, Physics}`: confirm against content scale.
2. **Scenario storage**: authored in the `.sworld` (shared, versioned)
   as proposed, or a sibling authored file?
3. **Migration posture**: per-world cook flag with the reviewable diff,
   or hard cutover once fixtures pass?
4. **Ambiguity severity**: Info forever, or per-world promotable to
   Error?
5. **Transit capture**: require an explicit authored transit state for
   every multi-dock set (proposed: yes, validation warns when absent),
   or synthesize an implicit no-dock state?
6. **Emptiness and flying**: accept no-child-for-empty-cells until a
   flying profile exists?
