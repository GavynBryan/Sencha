# Spatial Compilation: Subdivision, Evidence, Contacts, and Containment

Status: proposed design (2026-07-13), owner review before any stage starts.
Canonical: this document and `11-zone-runtime-model.md` together replace the
earlier 11 through 13 review chain (reasoning history lives in git). Doc 11
owns the runtime model this compiles for: zones, regions, demand composition,
promotion authority, and the containment contract. This document owns
everything the cook produces: residency subdivision, spatial evidence,
labeling, contact extraction, transition promotion mechanics, gate
association, the runtime containment artifact, the editor surface, and the
stage plan.

## Why

Two truths about the world are currently approximations that cannot check
each other: zone shape is one derived AABB (union of brush vertex bounds),
and zone connectivity is a hand-maintained edge list. Both fail around
curved, diagonal, L-shaped, stacked, and wrap-around structure, and neither
can say what two touching boxes mean: doorway, wall, corner graze, or
stacked-but-sealed rooms. Separately, a large authored place (a field, a
garden, a village exterior) is today one indivisible load unit, which is
wrong at scale.

The direction: **author semantics, compile space.** The designer authors
places, content ownership, gates, and rare hints. The world cook compiles
residency subdivision for large places, samples movement-blocking geometry
where shape is informative, recovers real zone shapes, compiles physical
contacts where traversable space crosses boundaries, and promotes into graph
edges only the contacts that demand semantics actually require. The AABB
survives as derived broad-phase data. Nothing infers topology from bounds
contact, and nothing hand-maintains geometric doorway links, ever again.

---

## 1. Grounding (verified against the tree)

- **Brush geometry** is indexed face-vertex polygons, explicitly non-convex
  (`editor/kyusu/src/brush/BrushMesh.h:11-34`), triangulated at cook time.
  No per-face or per-brush semantic flags exist; `BrushClustering.h:20`
  records "classify: future".
- **The level cook** (`DocumentCook.cpp:128-329`) collects world-space
  triangles per brush (`BrushCookInput.cpp:9-45`), clusters whole brushes
  into 16m cells by brush-center (`BrushClustering.cpp:11-18`, cvar
  `editor.cook.cell_size`), and bakes per cell one `.smesh` and one Jolt
  triangle-mesh `.scol` from the same triangles
  (`CollisionShapeCook.cpp:14-57`). Render and collision are one triangle
  stream, and props contribute no collision (the only production `Collider`
  emitter is `ZoneCollisionLoader.cpp:95-99`).
- **The world cook** (`WorldCook.cpp:13-139`) cooks each zone scene by file
  path and fills cooked-only manifest fields. Cook kernels are synchronous
  and pure by stated contract; the texture cook's
  `JobSystem::ParallelFor` (`TextureCook.cpp:52`) is the cook-parallelism
  precedent, and kyusu already links the job system
  (`EditorServices.cpp:612`).
- **Cooked binary artifacts** (`.smesh`, `.stex`, `.scol`) ship with
  magic-and-version headers and are restored directly; Track F's binary
  scene flip does not block new binary artifacts.
- **The runtime world path** loads the cooked manifest and the world scene
  synchronously at world start into `ZoneRuntime::Global()`
  (`TemplateGame.cpp:731-763`); this is the slot where world-level cooked
  artifacts load once.
- **Movement truth**: `CharacterController` (radius 0.3, height 1.8, slope
  50 degrees; `CharacterController.h:19-21`) drives Jolt `CharacterVirtual`
  with default stair handling (`CharacterMover.cpp:62`). No step-height
  field exists anywhere yet.
- **Math**: `Vec3d` is `Vec<3, float>` (`Vec.h:382`); no triangle-box test,
  no voxel, Morton, or SDF utility exists; `Grid3d<T>`
  (`math/spatial/Grid3d.h`) is a dense grid template with no consumers.
- **Navigation is docs-only**: a navmesh "cooked as a sibling artifact of
  the level cook, from the same cooked collision geometry" is the v1.0
  roadmap item; the hierarchical cross-zone planner is v2.0
  (`engine-roadmap.md:263-273`). Open-field cell streaming, HLOD, and
  impostors are Track C item 9, v3.0 (`engine-roadmap.md:391`).

---

## 2. The authored surface

Authored (first two exist today):

1. Places: zone documents with identity, names, and content ownership
   (structural, as always).
2. Logical links and annotations: `ConnectZones` for teleports and
   elevators; annotation records for tags, priorities, names, and contact
   promotion (Section 7.3), edited through the existing panel verbs.
3. `ResidencyCellSize` on a zone header: the one new value that turns a
   place into a subdivided residency source (Section 3).
4. Gates, when door content exists: an entity carrying `GatePassage`
   (Section 8).
5. Labeling hints, rare: an interior seed marker or an influence volume,
   added only where the ambiguity overlay shows the compiler cannot know
   (Section 5.3).

Never authored: portal planes, zone-bound boxes, hand-maintained doorway
links, low-poly streaming shells, per-cell anything.

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

The subdivision frame is **world-aligned with origin at the world origin**,
the same convention the cook cells already use: `cell = floor(pos / pitch)`
per horizontal axis. The frame deliberately depends on nothing that content
edits can move: not content bounds, not a westernmost brush, not any
recomputed local origin. Adding a brush to a subdivided garden never
renumbers its children; a brush landing in a previously empty cell mints
that one cell's child. Grid alignment against architecture is achieved the
way builders already align everything: by building on the world grid. An
authored per-place grid origin is rejected as an id-invalidating knob with
no earned need (recorded trigger: a real world that cannot align content to
the world grid).

Subdivision is horizontal (XZ cells). A place needing genuinely 3D
residency subdivision is not a v1.x case; vertical structure inside places
is handled by containment layering (Section 9), and 3D subdivision is a
recorded deferral.

### 3.2 Child identity and its stability contract

```text
childId = Hash64(sourceZoneId, cellX, cellZ)
```

re-salted deterministically on collision with any existing id. Stability
contract, stated for the save system and annotations that will key on
these ids:

- **Stable under**: all content edits, renames, region changes, cook-cell
  size changes, and bake-config changes. Editing the world never renumbers
  children.
- **Intentionally invalidated by**: changing `ResidencyCellSize` (every
  child of that source), deleting or re-minting the source zone id, and
  future coordinate-space migration (spaces get per-space frames; the
  artifact header carries a space id, invalid in v1).
- **Migration expectation**: pitch changes are content-migration events.
  When Track C item 5's persistent state lands, its migration tooling
  rebuckets state by position into the new children; annotations on
  promoted contacts (Section 7.3) survive independently because contact
  ids do not key on child pitch. Until then, pitch is treated like
  renaming a zone: a deliberate, rare content decision.

The cooked child header carries `SourceZone` (the authored place id,
doc 11 Section 1.1), an inherited `Region`, a derived name
("Garden 2,1"), per-child cooked scene and collision refs, and the two
bounds below. The source header itself does not appear as a loadable zone;
the compilation sidecar records source-to-children provenance for the
editor and telemetry.

### 3.3 Residency coverage versus content bounds

```cpp
// Cooked, per zone. Coverage is the stable spatial footprint: for a child,
// its cell box (XZ) crossed with the source's vertical band; for a bespoke
// zone, its envelope box. Spatial demand distance, analytic containment,
// and recovery all test coverage. ContentBounds is the AABB of actually
// cooked content: diagnostics, cost analysis, render-ish uses, tie-breaks.
Aabb3d ResidencyCoverage;
Aabb3d ContentBounds;
```

The distinction exists so a cell with one fence post near its far edge
does not load late because its bounds shrank around its assets: demand
tests the full cell footprint, always. The vertical band of a source is
its content's Y range plus a config margin, recomputed per cook; Y drift
from edits changes coverage slightly but never identity (ids are XZ).

**Emptiness**: a child exists if any content buckets into its cell: brush
geometry (which includes traversable floors by construction), collision,
or any passthrough entity. Cells with literally nothing produce no child,
no coverage, and resolve through recovery if something falls through
them. Recorded deferral: flying traversal across contentless air cells
may eventually want phantom coverage-only children; no current profile
needs it.

### 3.4 Content assignment (the re-bucketing rules, stated precisely)

The level cook's existing 16m cells are the mechanical substrate (every
cook cell maps wholly into one residency cell via the pitch-multiple
rule), but subdivision is only as correct as its assignment rules:

**Splittable static geometry (brushes).**

- Default: whole-brush assignment by center, the existing cook rule,
  legal while the brush fits within its cell plus a config tolerance.
- Oversized brushes (a ground slab spanning cells) are split at residency
  planes by a **cook split kernel**: a pure function that may reuse
  `BrushOps` clip internals but is specified and gated independently,
  because an editor verb is not automatically a safe cook primitive. Its
  contract: output pieces partition the input triangles exactly (no gaps,
  no overlaps), preserve per-face `FaceMaterial` and UV projection,
  preserve provenance (source brush id per piece), and produce sealed
  pieces where the input was sealed. Gate: for every fixture, the union
  of split cooked triangles hashes identical to the unsplit cook, and the
  per-cell collision baked from the pieces is exercised by the scripted
  mover with no behavioral difference.
- Future splittable data (terrain chunks, baked lighting, nav tiles)
  rides the same plane-partition contract when those systems land; none
  are built here.

**Non-splittable anchored content (passthrough entities).**

- Default: anchor ownership by transform position; the entity belongs to
  the child whose cell contains its origin. This is the whole rule for
  most props, lights, and emitters.
- Entities whose declared influence extent exceeds their cell (light
  range, audio range, trigger volume, large visual bound) keep anchor
  ownership and trip a cook diagnostic
  (`partition.entity.influence_exceeds_cell`, Warning, naming the entity
  and the overhang), because their effect will pop with their cell's
  residency. Designer resolutions, in order: shrink the influence, move
  the entity, or move it to the world scene (`Global()`, always
  resident), which is the supported escape hatch today.
- Recorded future mechanism, not built: multi-cell dependency retention
  (an entity demanding residency of the cells it influences). It shares
  machinery with the dynamic-entity work (doc 11 Section 6) and lands
  with it, not before.
- Gates and moving platforms are dynamic content and follow doc 11
  Section 6's boundary; the `GatePassage` frame content is static and
  bakes normally (Section 8).

---

## 4. Spatial evidence: shared kernels, per-product domains

### 4.1 What is genuinely shared

A kernel library beside the other cook kernels (same "pure: no logging,
no threads, no disk" contract), in `engine/include/assets/cook/` with data
types in `engine/{include,src}/spatial/`:

- zone-tagged triangle gathering (the existing per-zone collect path,
  file-based, header-only zones included),
- conservative rasterization: cell is Solid or Mixed if any triangle
  intersects its box, via a triangle-box separating-axis primitive added
  to `math/geometry/3d/` with its own tests (nothing like it exists yet,
  and any navmesh bake needs the identical function),
- clearance (chamfer distance transform, integer cell units, truncated),
- support and slope extraction (supported cells, floor offset, normal
  bucket),
- traversal-profile evaluation (Section 4.3),
- the sparse brick IR: sorted `Vec3i`-keyed brick map over dense 16^3
  payloads (`Grid3d<T>` finally earns a consumer), canonical key-order
  iteration, per-brick content hashing,
- the fixture worlds and golden-hash harness.

**There is no persistent world-wide shared field artifact.** Each compiler
invokes the kernels over its own domain and emits its own product: the
zone compiler samples bespoke envelopes, place frontiers, and gate
neighborhoods (Section 4.2) and emits the containment artifact plus
contact records; the future navmesh compiler rasterizes every navigable
surface at its own resolution (including open fields the zone compiler
never touches) and emits polygon tiles; map and query products, when they
exist, filter and compile their own artifacts. Runtime navigation never
queries the zone containment artifact, and zone lookup never touches a
navmesh. The gate that keeps this honest: the kernel API takes an explicit
domain, and a fixture drives the kernels over an open-field fixture
directly, with no zone bake involved, proving reuse without whole-world
zone sampling.

### 4.2 The zone compiler's domain

Sampled: bespoke place envelopes (geometry bounds plus a margin of
clearance cap and profile radius), frontier bands where any two places'
envelopes or coverages meet, and gate passage neighborhoods. Not sampled:
the interiors of subdivided places (containment there is analytic,
Section 9) and empty space beyond envelopes. A domain-size assertion per
fixture keeps this scoped (the field-scale fixture samples only its
exits).

### 4.3 Traversal profiles

```cpp
struct TraversalProfile
{
    float Radius;          // capsule radius, meters
    float Height;          // capsule height, meters
    float StepHeight;      // max climbable rise without a jump
    float MaxSlopeDegrees;
};
```

Bake-config data; v1 ships exactly one ("pawn"), seeded from
`CharacterController` defaults plus `StepHeight` 0.4 (Jolt's walk-stairs
default). This record is the first authoritative step-height in the
engine; the roadmap's `LoftSteps` and traversal-probe items should read it
when they land (their current text assumes a `MovementProfile` step height
that does not exist). The bake must mirror the mover: a scripted mover
walk over every fixture agreeing with baked traversability is a standing
gate, because "the pawn can cross here" is the one claim this system must
never get wrong. Per-cell products: standing room, horizontal clearance
against radius, slope acceptance, and face-adjacent step validity
(support delta within `StepHeight`; drops beyond it are one-way downward;
no diagonal corner cutting). Camera and projectile profiles are rejected
(raycasts and no-floor constraints respectively); additional creature
profiles are data additions.

### 4.4 Determinism regime

- Accumulating arithmetic (distance transforms, growth costs) runs in
  integer cell units; float appears only in order-independent geometric
  predicates against exactly computed cell boxes.
- One canonical iteration order everywhere (brick keys ascending, cell
  index order); the labeling queue is an indexed priority structure with
  total-order tie breaking (cost, zone id, brick key, cell index); no
  unordered container ever reaches an output.
- Parallelism is `JobSystem::ParallelFor` over bricks with per-brick
  outputs reduced in index order; `worker_count == 0` is the reference
  and must be byte-identical to the pool.
- Every artifact records its bake config (pitch rules, cell size,
  profiles, cost constants, format version) in its header and enters
  `CookedCacheIndex` by content hash; unchanged inputs skip the stage.
- The standing test: golden content hash per fixture, identical across
  reruns and worker counts.

---

## 5. Labeling (bespoke places and frontiers)

### 5.1 Sources

Free cells adjacent to solid cells seed with the solid's owner at cost
zero. In Sencha ownership is not a heuristic: every movement-blocking
triangle is a brush, every brush lives in exactly one place document, so
walls, floors, and ceilings are the influence field the designer already
authored. Props never seed (no collision today; excluded by rule if that
changes). Gate-passage cells never seed and carry the passage penalty so
labels do not bleed through open doorways. Explicit seed markers and
influence volumes are override sources only.

### 5.2 Growth and posts

Multi-source shortest path over free cells, integer chamfer steps, cost =
distance + narrowness penalty (throats are expensive, so fronts from two
sides stall and meet inside openings instead of drifting across rooms) +
gate-passage penalty. Constants are bake config. Post passes: enclosed
islands below a size threshold absorb into their surrounder; a place
labeling multiple disconnected components is legal but reported
(`spatial.zone.split_label`, Warning: usually a mis-owned brush); cells
with no path to any source within the cost cap stay Unassigned (outside
the playable envelope; not an error). Subdivided interiors are never
grown: their labels are the grid partition, and the two mechanisms meet
only in frontier bands, where sampled labels win (Section 9).

### 5.3 Ambiguity, kept low-stakes

Per cell, the margin between best and second-best source cost; low-margin
boundary runs flag Ambiguous, surface as an editor overlay and an Info
diagnostic (`spatial.label.ambiguous_boundary`) with the zone pair and
location. The result stays deterministic either way. Where two open
places meet without a bottleneck, the exact boundary meter is explicitly
low-stakes (demand there is spatial, focus has margin, place identity is
the source): the overlay and hints exist for the designer who cares, and
the architecture stops pretending that line was ever an important answer.

---

## 6. Contacts

### 6.1 Extraction

Wherever traversable free space crosses a boundary between different
resolved owners (bespoke-to-bespoke labels, bespoke-to-grid, or
grid-to-grid across place frontiers), face-adjacent traversable cell
pairs with different owners record boundary faces. Faces group into
connected components per unordered zone pair; each component is one
**contact**. Two doors between the same pair are two contacts (their
boundary faces do not connect). Grid-to-grid contacts between children of
the same source are not compiled (same place, no boundary of interest);
sibling adjacency is implicit in the grid.

### 6.2 The record

```cpp
struct ZoneContact
{
    ContactId  Id;            // deterministic, Section 6.3
    ZoneId     A, B;          // unordered pair, A = lower id
    Vec3d      Representative;// area-weighted boundary center
    Aabb3d     Bounds;        // of member boundary faces
    Vec3d      NormalHint;    // dominant face direction, quantized
    float      Area;
    float      MinWidth;      // narrowest traversable run across it
    float      FrontierLength;// lateral extent along the boundary
    float      Constriction;  // MinWidth against the joined bodies
    uint8_t    Profiles;      // which profiles cross
    uint8_t    Directions;    // A->B, B->A, both (drops are one-way)
    // gate binding when bound (Section 8)
};
```

Width, constriction, and frontier length are classification metadata and
prefetch evidence. They never decide topology (doc 11 Section 4).
Contacts ship in the compilation sidecar keyed by id; member cell runs
stay compiler-side for refinement and debug.

### 6.3 Identity

```text
ContactId = Hash64(worldSalt, zoneA, zoneB,
                   quantize(representative, 2m), ordinalInBucket)
```

with deterministic re-salt on collision. For contacts between compiled
children, the hash uses the SOURCE ids plus the representative, so
re-pitching a place does not churn contact identity at its bespoke
borders. Moving an opening under 2m keeps its id; larger remodels change
it and the cook emits a reconciliation report (unmatched old, unannotated
new, nearest-candidate suggestion) instead of guessing. Gate-bound
contacts key on the gate's stable identity once entities have one (Track
C item 5); until then the geometric signature stands. Annotations are
sparse by design, which keeps reconciliation a report, not a migration.

---

## 7. Promotion into transitions

### 7.1 The rule (authority lives in demand semantics)

A contact is promoted into `TransitionRecord`s (one per traversable
direction) when any of:

1. **A gate binds it** (Section 8): the opening is conditional content.
   Topology `Doorway`.
2. **Either endpoint's region has `JoinsSpatialDemand = false`** (doc 11
   Section 3.3): an ineligible zone's only demand path is topological,
   so every contact into it must be an edge or streaming can never reach
   it. Topology `Seam` (or `Doorway` when also gated).
3. **An authored annotation promotes it**: the designer wants topological
   preload, prefetch anchoring, or future transition timing across this
   specific contact. Topology `Seam`.

Nothing else promotes. Not width, not constriction, not indoor-ness. A
hundred-meter frontier between radius regions stays a contact (spatial
demand carries it); a narrow canyon between radius regions stays a
contact (an edge would add nothing); a forty-meter temple mouth into a
graph-only interior promotes despite its width. `OneWay` derives from
directionality (drops); `PreloadPriority`, `PreloadDepth`,
`RequiredTags`, and names attach via annotations keyed by contact id.

### 7.2 What non-promoted contacts still do

Reachability validation traverses edges, contacts, and radius-region
cliques, so open frontiers count for connectivity without being demand
edges; overlays draw them; approach-distance facts and future nav and
map products consume them. `partition.bounds.overlap` and the AABB
adjacency vocabulary retire with the shapes that made them necessary.

### 7.3 Annotations and authored records

The authored manifest carries logical links (Teleport, via `ConnectZones`,
random editor-minted ids, unchanged) and annotation records keyed by
contact id, edited with the existing inline editor and non-undoable verb
pattern. Compiled records live only in cook output; the authored `.sworld`
never contains them, so a stale bake is visible rather than silently
merged. Diagnostics: `spatial.annotation.orphaned` (Warning) when a key
matches no compiled contact.

### 7.4 Migration

One cook flag per world enables compilation. The migration cook diffs
authored geometric edges against compiled contacts: matched edges carry
their name, tags, priority, and depth into annotations (promoting the
contact per rule 3) and are deleted; authored edges with no matching
contact are reported (sealed doorway or fiction); contacts with no edge
are reported (connectivity the author never knew). After migration the
authored transition list contains only logical links and annotations, and
the `Connect To` submenu creates Teleports. The template fixture world
migrates first.

---

## 8. Gates

No door exists in the engine today; this contract is designed now and
consumed when door content lands. Nothing in stages 0 through 6 depends
on it.

```cpp
// Component on a gate entity (world scene or zone content). The FRAME is
// ordinary content and bakes normally; the leaf and the open passage are
// what the bake must know about.
struct GatePassage
{
    ConvexVolumeRef OpenPassage;   // carved from static occupancy so the
                                   // bake sees the potential connection
    Vec3d           PassageAxis;   // local, points through the opening
    uint8_t         Profiles;      // which profiles pass when open
    // The movable leaf geometry is dynamic state and is excluded from
    // the static bake by this component's presence on its entity.
};
```

Bake semantics: leaf excluded, frame baked, passage cells flagged and
penalized (Section 5.1). Association after extraction: candidate contacts
intersecting `OpenPassage`, scored by overlap fraction and
`NormalHint`-to-axis alignment; the best binds when it leads by a config
margin, otherwise `spatial.gate.ambiguous_binding` (Error) rather than a
guess. Diagnostics: gate with no candidate contact (sealed or interior to
one zone), passage still blocked with the leaf removed, bound contact
failing the declared profiles. The cook writes the bound id into the
gate's cooked component (content references topology, never the reverse,
the D1 direction), so the runtime door system gates the edge through
world tags or a future blocked flag; that runtime choice belongs to Track
C item 6's typed transition scopes. Durable gate identity wants Track C
item 5, recorded as the dependency.

---

## 9. The runtime containment artifact

One world-level binary artifact (`.cooked/worlds/<stem>.szfield`, magic
and version, restored directly, the `.scol` precedent), referenced from
the cooked manifest as `CookedSpatialFieldRef` plus hash beside the
existing world trio, loaded once at the world-start boundary, owned by
`WorldPartitionRuntime`. It contains:

- **Sampled space**: bricks over the zone compiler's domain only, each a
  zone-id palette, per-cell label indices (4 or 8 bits by palette size),
  and per-cell label depth (4 bits, saturating); homogeneous bricks are
  headers only.
- **Analytic space**: per subdivided source, its grid parameters (pitch,
  vertical band, origin space id) and child table.
- Nothing else ships. Occupancy detail, clearance, support, profiles,
  ambiguity, and member cells stay compiler-side until a runtime consumer
  exists; nav and query systems compile their own artifacts.

Lookup precedence, deterministic and explicit:

1. **Sampled**: if the sample's brick exists and its cell is assigned,
   that zone answers, with label-depth hysteresis. Unassigned sampled
   cells fall through.
2. **Analytic**: among subdivided sources whose coverage contains the
   sample (XZ cell within the source's child table, Y within its band),
   the answer is the cell's child. Overlapping candidates (stacked or
   abutting coverages) tie-break: containing coverage, then nearest
   `ContentBounds`, then source id. Grid-boundary hysteresis uses
   distance to the cell edge.
3. **Recovery**: nearest zone by `ResidencyCoverage` (ties: smaller
   coverage, then id), for airborne, falling, spawning, and
   out-of-envelope samples.

The vertical fixtures this order must satisfy (all in the stage-0 suite):
a cave under a subdivided garden resolves to the cave (sampled wins in
Y); a building interior inside a village cell resolves to the building;
a bridge deck belonging to a bespoke bridge zone resolves to the bridge
while the road below resolves to the road's cell; stacked interiors
resolve per floor; a traversable rooftop resolves to the roof's owner
within the sampled band and to the grid above it (deterministic, and the
semantics are documented rather than pretended away: an influence volume
flips it if a game cares); falling between vertically separated spaces
walks sampled bands then recovery without flapping (hysteresis plus
margin); XZ-coincident but Y-distinct places resolve by band. The sample
point is the pawn's capsule center, supplied by the game exactly as
`SetFocus(Vec3d)` does today.

Size expectations: bricks exist only near architecture and frontiers;
grid parameters are bytes. Fixture worlds land in tens of KB; a
content-scale mixed world is expected in hundreds of KB, measured and
gated at stage 6 before anything grows.

---

## 10. Editor surface

- **Overlays** (existing line and fill pipelines): label slices
  zone-tinted at a chosen height, contact markers with width and area
  labels, promotion state (edge versus contact-only), ambiguity heat,
  Unassigned regions, residency coverage grid for subdivided places, all
  reading the last bake with the existing stale-cook badge on hash
  mismatch.
- **Panel**: compiled contacts and promoted edges appear beside authored
  links in the connections list (compiled rows marked as such,
  annotations editable inline, jump-to in viewport); demand records and
  the streaming preview group children by `SourceZone`; diagnostics rows
  navigate like validation rows. The connections list gains the filter,
  region grouping, and hover-highlight affordances regardless of the
  rest.
- **Graph panel** (the long-deferred node-link view, whose recorded
  trigger has fired): nodes from compiled shapes (children grouped under
  their source), edges styled by provenance (contact, promoted, logical)
  and topology, built over the pure policy and cook products only (D18).
- **Hint authoring**: seed markers and influence volumes as ordinary
  zone-document content with inspector support, landing in this stage
  because the ambiguity overlay must exist before the override does.
- **Migration UX**: the Section 7.4 diff as a reviewable report with
  accept-per-row.

---

## 11. Streaming facts (compiled evidence for doc 11's policy)

- Contact and edge metrics (area, min width, directions, profiles) ship
  in the sidecar; first consumers are same-hop load ordering by
  distance-to-representative and the preview.
- Approach distances: per zone, coarse geodesic distance (brick
  resolution, quantized) from interior to each promoted edge and each
  region exit, enabling lead-time prefetch ("nine seconds from the north
  door at current speed") instead of hop guessing. Scoped to promoted
  edges and exits; open frontiers get frontier-distance summaries only.
- Residency cost: per-zone cooked byte and count facts on Track C item
  1's `ZoneBudgetRecord`, feeding doc 11's cost budget.
- Reachability facts: dead ends, articulation edges, gate-locked
  reachability with controlling tags (`spatial.zone.gate_locked`, Info).

The compiler emits measured facts only; every decision stays in the pure
demand policy, testable and previewable.

---

## 12. Fixtures, stages, gates

### 12.1 The fixture suite (stage 0, the acceptance vocabulary)

Indoor set: curved hallway, L-rooms, T-junction, stacked-sealed rooms,
stairwell link, wrap-around corridor with interior room, thin wall (5cm),
sub-pawn slot, drop ledge, two-doors-same-pair, wide-open two-owner
boundary. Place-scale set: castle courtyard (bespoke, gated arches),
palace garden (subdivided, with bespoke grotto and orangery inside),
field-scale region (subdivided, walled town gate exit, unwalled frontier
to a neighbor region), outdoor village (subdivided streets, graph-only
interiors), cliffside into cave. Assignment set: oversized ground slab
spanning cells, large-influence entity (light range across three cells),
canyon between radius regions, wide temple mouth into a graph-only
interior. Vertical set: the Section 9 list. Each fixture's expected
labels, contacts, promotions, and containment answers are written as the
test assertions.

### 12.2 Stages

Each stage is one execution-spec lane when accepted; suite green
throughout; the overview's binding rules apply.

- **Stage 0: fixtures and formats.** The suite above as authored assets;
  `spatial/` data types; brick IR; artifact header round-trip;
  golden-hash harness.
- **Stage 1: subdivision compilation.** `ResidencyCellSize`, world-grid
  bucketing, split kernel, child headers with `SourceZone`, coverage
  versus content bounds, emptiness, provenance sidecar, preview
  grouping. Gates: child ids stable under content edits that change
  neither pitch nor source (add, move, delete brushes; hash the id set);
  radius demand in the template game keys off coverage (the sparse-cell
  fixture loads its far-edge cell on approach exactly when a full cell
  would); the oversized slab splits with triangle-conservation and
  mover-equivalence; the large-influence entity warns and behaves as
  documented; children report their source across cook, demand records,
  and preview. The field-scale fixture streams by radius with zero
  transitions in the world and zero sampling run: the open-space path is
  proven before any field exists.
- **Stage 2: spatial eligibility.** `JoinsSpatialDemand` through config,
  policy, and preview (doc 11 Section 3.3). Gate: village interiors stay
  cold from street radius; the S-D3 signature amendment is recorded in
  doc 10's decision text.
- **Stage 3: evidence kernels.** Triangle-box primitive with its own
  math tests; rasterization, clearance, support, traversability over
  explicit domains; determinism gates (golden hashes, worker counts);
  the kernel-reuse gate (an open-field fixture rasterized directly
  through the kernel API with no zone bake).
- **Stage 4: labeling and contacts.** Ownership growth, islands,
  Unassigned, ambiguity; frontier bands including grid boundaries;
  contact extraction, metrics, ids. Gates: every indoor fixture labels
  as written; the grotto wins its shape against the garden grid; the
  decoy owned-island absorbs; contact counts and directionality exact
  (two doors, two contacts; drop ledge one-way); the wide-open boundary
  flags Ambiguous and nothing else does.
- **Stage 5: promotion, manifest compilation, migration.** The three
  promotion rules, per-direction records, annotation keying, migration
  diff, validation swap. Gates: the temple mouth promotes (graph-only
  side) and streams the interior on approach; the canyon does not
  promote and both sides stream correctly by radius alone; gate-bound
  fixtures promote as `Doorway`; a 1m door move keeps its contact id
  and annotations, a room remodel produces the reconciliation report;
  the migrated fixture world runs with zero authored geometric edges.
- **Stage 6: runtime containment.** `.szfield`, manifest refs,
  world-start load, layered lookup, hysteresis, recovery, preview
  parity, size budget. Gates: every vertical fixture resolves per
  Section 9; the traversal harness walks the mixed village and
  cliff-cave fixtures with focus changes exactly at expected boundaries
  and zero missed ticks; the scripted mover agrees with baked
  traversability everywhere (the profile-mirror gate); artifact sizes
  within budget.
- **Stage 7: editor surface.** Section 10, plus the manual walkthrough
  script (see shapes, see a contact, promote via annotation, place a
  seed, rebake, watch the boundary move).
- **Stage 8: streaming facts.** Section 11 emission and the policy
  consumption from doc 11 Section 3.4; ordering and lead-time gates in
  the preview and demand records.
- **Stage 9: gates.** Section 8, when door content exists; coordinated
  with Track C items 5 and 6.
- **Stage 10: incremental bake.** Brick-hash dirty tracking, margin
  expansion, frontier-outward relabel with the full-rebake backstop.
  Scheduled when measured full-bake time hurts the editor loop; the
  binding gate is byte-identity with a full bake across mixed edit
  scripts.

Recorded, not scheduled: aperture polygon refinement (lands with
see-through portals, v2.0), the navmesh back end (Track A item 5,
consuming stage-3 kernels), the spatial query facade (first AI
consumer), map surface extraction (filtered field to per-place display
meshes), 3D subdivision, phantom air cells for flying, per-region
artifact splitting, adaptive resolution.

---

## 13. Risks and open cases

1. **Split-kernel fidelity** is now a gated contract rather than an
   assumed-safe editor verb; the triangle-conservation and
   mover-equivalence gates are the guard. Residual risk: sealing
   guarantees on degenerate brushes; the fixture set includes them.
2. **Contact id churn** under remodels: mitigated by coarse
   quantization, source-keyed hashing for child borders, gate identity
   later, reconciliation reports, and sparse annotations. Promotion by
   eligibility (rule 2) needs no annotations at all, which keeps most
   worlds' annotation count near zero.
3. **Roaming entities** across children: owned honestly in doc 11
   Section 6; a prerequisite for shipping wanderers, not for this
   design.
4. **Pitch changes invalidate child ids**: a rare, deliberate content
   decision with migration expectations recorded (Section 3.2).
5. **Float determinism**: confined to order-independent predicates;
   golden hashes catch platform variance; predicates can go exact if it
   ever fires.
6. **Demand-scan scaling**: linear scans are fine at hundreds of zones;
   the spatial index trigger is v3 scale.
7. **Terrain stays brush-shaped in v1.x**: gardens, villages, and
   fields built from brushes work now; terrain assets, HLOD, and
   impostors are Track C item 9, for which this design reserves slots
   (pitch, proxy participation, per-child artifacts) and builds nothing.
8. **Rooftop and above-place semantics** are deterministic but may
   surprise (Section 9); the fixture documents them and the override
   exists.

## 14. Non-goals

- No third runtime tier, no hierarchical participation, no mode enums.
- No promotion by geometry alone; no topology from bounds contact.
- No whole-world sampling; no persistent shared field consumed by other
  systems; no runtime query facade, navmesh, or map meshes here.
- No dynamic-obstacle carving or runtime field mutation.
- No auto-bake daemon; the bake rides the existing cook actions.
- No genre vocabulary in identifiers.

## 15. Open questions for the owner

1. **Defaults**: residency pitch 64m (4 cook cells), sample cell 0.25m,
   pawn `StepHeight` 0.4, coverage vertical margin: confirm against
   intended content scale.
2. **Emptiness and flying**: accept no-child-for-empty-cells until a
   flying profile exists?
3. **Migration posture**: per-world cook flag with the reviewable diff,
   or hard cutover once fixtures pass?
4. **Ambiguity severity**: `ambiguous_boundary` stays Info forever, or
   per-world promotable to Error for teams wanting forced resolution?
5. **Annotation-promoted topology name**: promoted-by-annotation
   contacts read `Seam` here; confirm, or introduce a distinct label if
   transition timing later needs to distinguish authored intent from
   gate binding.
