# Spatial Field and Compiled Crossings: Zone Shape from Geometry, Topology from Traversability

Status: proposed design (2026-07-13). Owner review before any stage starts. This
document reverses and amends standing decisions on the record (Section 0.4); none
of those reversals are in force until this design is accepted. Read
`00-execution-overview.md`, `09-retire-portals-doors-as-world-content.md`,
`10-per-region-streaming-and-topology-labels.md`, and
`11-zone-architecture-review.md` first.

## Why

The zone model's spatial truth is one derived AABB per zone (union of brush
vertex bounds) and its topological truth is a hand-authored edge list. Both are
approximations of the same underlying fact (where free space is, and where free
space labeled A meets free space labeled B), and both fail in the same
places: curved and diagonal architecture, L-shapes, T-junctions, stairs,
wrap-around zones, and stacked floors. Worse, the two truths do not check each
other. Two boxes touching says nothing about whether a pawn can walk from one
zone to the other, and an authored edge says nothing about whether the doorway
it names is actually open, actually reachable, or actually where the author
remembers it.

The direction: **zones stay authored semantically; their shape and their
geometric connectivity become compiled.** The designer authors zone identity,
content ownership, gameplay gates, and rare hints. A cook stage samples the
world's movement-blocking geometry into a labeled free-space field, recovers
each zone's actual shape, extracts the places where actor-sized traversable
space crosses a label change, and compiles those crossings into the same
transition records the runtime already consumes. The AABB survives only as a
derived broad-phase artifact. Nothing infers connectivity from bounds contact
ever again.

---

## 0. Verdicts up front

### 0.1 Accepted (the core of the proposal fits Sencha unusually well)

- **Compiled spatial truth from a sampled field.** A sparse brick grid of cells
  carrying occupancy, clearance, and zone labels, baked by the world cook from
  the same triangle stream the level cook already produces. Sencha has no
  competing spatial representation to displace: no voxel, heightfield, BVH, or
  SDF utility exists anywhere in the tree, and `math/spatial/Grid3d.h` is a
  dense grid template with zero consumers waiting for exactly this.
- **Ownership is the primary zone influence, and Sencha already authors it.**
  Every movement-blocking triangle in a cooked world comes from a brush, every
  brush lives in exactly one zone document, and props contribute no collision
  at all today (the only production `Collider` emitter is the cooked brush
  cell path, `ZoneCollisionLoader.cpp:95-99`). Solid samples therefore carry a
  zone owner for free, and free space can be labeled by geodesic growth from
  owned surfaces. The proposal's "seeds or equivalent influence sources" reduce
  in Sencha to: **brush ownership first, explicit seeds only as the rare
  override.** This is the single biggest simplification this document makes to
  the proposal.
- **Crossings from traversable adjacency, not bounds contact.** Face-adjacent
  free cells with different labels, traversable under the pawn profile, grouped
  into connected components per zone pair: each component is one crossing.
  Edge and corner contact, walls, stacked-but-sealed rooms, and sub-pawn gaps
  never produce edges.
- **Crossings compile into `TransitionRecord`s.** The entire runtime policy
  stack (`ComputeZoneHopRanks`, `ComputeZoneDemand`, region shapes, tags, pins,
  linger, cap, the editor preview per D18) keeps working unchanged, because the
  cooked manifest's transition list remains the runtime contract. Only the
  provenance changes: geometric edges are compiled, non-geometric edges
  (teleports, elevators) stay authored, and authored annotations attach to
  compiled edges.
- **The field is also the navmesh front end.** The roadmap's navigation item
  already pins "a navmesh cooked as a sibling artifact of the level cook, from
  the same cooked collision geometry" (`engine-roadmap.md:263-268`). A
  walkable-span front end (rasterize, clearance, region) is the first half of
  any navmesh bake; building the field builds it once, and the navmesh becomes
  a second consumer of the same pass family rather than a second rasterizer.

### 0.2 Amended from the proposal (fitted or trimmed)

- **Seeds demoted to overrides.** See above. Authored influence volumes and
  seeds exist only as the escape hatch for ambiguous open boundaries, and they
  are added in the editor stage after the overlay exists to show where they
  are needed, not before.
- **One traversal profile in v1, not five.** Sencha has one kinematic capsule
  (`CharacterController.h:19-21`: radius 0.3, height 1.8, slope 50 degrees)
  and zero AI. The bake takes a profile list and bakes per-profile
  traversability so a second profile is a data addition, but v1 ships exactly
  one. Camera and projectile profiles are rejected outright: projectile
  reachability is a physics raycast question and the camera has no floor
  constraint; neither wants this field.
- **The five-term labeling cost trims to three.** Distance, narrowness, gate
  passage. Surface-ownership disagreement is not a cost term because ownership
  is the seed itself; a separate vertical-traversal penalty is deferred until a
  fixture proves the need. Fewer knobs, same watershed behavior.
- **Runtime zone changes use label depth, not crossing events.** The proposal's
  "commit on crossing volume traversal" needs runtime crossing volumes, per
  actor tracking, and an event path. The same jitter resistance falls out of
  one baked channel: per-cell distance-to-different-label ("label depth"). The
  focus switches only when the position sits at least K cells deep inside a
  different label. Crossing volumes stay a cook and metadata concept until a
  consumer actually needs the runtime event (transition timing, Track C item
  6, is scope-based and can bind to focus changes).
- **Aperture polygon refinement is deferred to its consumer.** v1 crossings
  carry centroid, bounds, area, min width, normal bucket, and member cells.
  Plane fitting and contour simplification land with the first consumer that
  needs a polygon (see-through portals, Track C item 7, v2.0). Streaming,
  focus, the graph panel, and diagnostics do not.
- **Residency cost integrates with the existing plan.** The proposal's
  `ZoneResidentCost` is Track C item 1's `ZoneBudgetRecord` (planned shape
  already includes asset bytes, GPU upload bytes, entity count, render items;
  `action-adventure-core-runtime.md` Decision A). Doc 11 Phase D already routes
  demand budgets through it. This document adds field-derived facts (approach
  distances, crossing metrics) beside it, not a parallel record family.

### 0.3 Rejected alternatives (argued in Section 15)

- Exact portal-and-cell decomposition (BSP-style) instead of a sampled field.
- Navmesh-first zone labeling (label walkable polys, skip the volume).
- Keeping authored geometric transitions as a parallel truth after migration.

### 0.4 Standing decisions reversed or amended (on the record)

- **P-D2 (doc 09, "connections are authored zone-to-zone, never derived")
  is narrowed, not repudiated.** P-D2 killed a marker object whose only job
  was deriving one edge from one hand-placed box, redundant with `ConnectZones`
  and owned by the wrong thing. This design derives edges from the actual
  cooked world geometry, with no marker, no per-connection authoring, and no
  ownership question. What P-D2 protected still holds where it was right:
  non-geometric connections (Teleport) are authored zone-to-zone through
  `ConnectZones`, and no entity ever creates topology. Geometric seams and
  doorways stop being authorable at all, which is a stronger version of the
  same discipline: the designer authors intent (ownership, gates), never the
  edge list.
- **`ZoneHeader.Bounds` is demoted from canonical shape to derived broad
  phase.** Authored bounds remain in the manifest as the live editor
  approximation; the cooked manifest's bounds are recomputed from the labeled
  field (aggregate of a zone's cells). `BoundsOverridden` survives for the
  broad phase only. The doc 11 open question about a bounds-override UI is
  mooted at that point.
- **`partition.bounds.overlap` (and doc 11's proposed containment split of it)
  retires when the field lands.** Real shapes do not overlap by construction;
  the replacement diagnostics are field-native (ambiguous boundary, unassigned
  island, unreachable label region).
- **Doc 11 supersessions.** The transition `Anchor` field (doc 11 Section 4.3)
  is superseded: crossing centroids are the derived, always-correct version of
  the hand-placed anchor, and the same-hop distance ordering it fed now reads
  crossing centroids. Doc 11's containment focus fix (Section 2.2) becomes an
  interim measure: worth shipping only if this design is deferred, because
  field lookup plus label depth replaces `ResolveFocusZone`'s AABB logic
  wholesale. Doc 11's cost budget (Phase D), Teleport dormant preload (Phase
  E), and graph panel (Phase B) stand, and the panel gains real shapes and
  real crossings to draw.
- **D4 (id minting is editor-side, random) gains a cook clause.** Compiled
  crossings need deterministic identity across rebakes; the world cook mints
  their `TransitionId`s as content-derived hashes (Section 7.4), never random,
  and never colliding with authored ids (rejected and re-salted
  deterministically on collision). Authored records keep random editor minting.
- **Doc 10 S-D5 ("topology labels do not affect streaming") ends as planned,
  honestly.** Seam and Doorway become derived classifications (Doorway = gate
  bound), and doc 11's Teleport-dormant-preload proposal gives Teleport its
  behavior. Labels in the UI change in the same commits that change behavior.

---

## 1. Grounding

Verified against the tree (2026-07-13):

- **Brush geometry** is indexed face-vertex polygons, explicitly non-convex
  (`editor/kyusu/src/brush/BrushMesh.h:11-34`), triangulated only at
  render/cook time. There are no per-face or per-brush semantic flags (no
  detail/structural/collision-only vocabulary); `BrushClustering.h:20` records
  "classify: future".
- **The level cook** (`DocumentCook.cpp:128-329`) collects world-space
  triangles per brush (`CollectCookBrushes`, `BrushCookInput.cpp:9-45`),
  clusters whole brushes into 16m cells (cvar `editor.cook.cell_size`,
  `PieDriver.cpp:195-205`), bakes one `.smesh` and one Jolt `.scol` triangle
  mesh blob per cell from the same triangles (`CollisionShapeCook.cpp:14-57`),
  and emits a passthrough scene. Render collision and physics collision are
  one triangle stream today, and props add no collision (no production
  `Collider` emitter outside `ZoneCollisionLoader.cpp:95-99`; no MeshCollider
  component exists).
- **The world cook** (`WorldCook.cpp:13-139`) cooks each zone's scene file by
  path, fills the cooked-only manifest fields, and writes
  `.cooked/worlds/<stem>.sworld.json`. All cook kernels are synchronous and
  pure by stated contract (`BrushGeometryCook.h:13`, `CollisionShapeCook.h:15`);
  the only parallel cook is texture mips via `JobSystem::ParallelFor`
  (`TextureCook.cpp:52`), which kyusu already links (`EditorServices.cpp:612`).
- **Cooked binary asset precedent** exists (`.smesh`/`.stex`/`.scol` with
  magic-and-version headers); cooked scenes and manifests are JSON, and Track
  F's binary scene flip is blocked on the field codec, which does not block
  new binary artifacts (the `.scol` blob ships today,
  `CollisionShapeCook.h:11-25`).
- **The runtime world path** loads the cooked manifest, builds per-zone
  recipes, and loads the world scene synchronously into
  `ZoneRuntime::Global()` at world start as "a loading-screen boundary, not a
  streaming moment" (`TemplateGame.cpp:731-763`). The pawn lives in
  `Global()`; `WorldPartitionUpdateSystem` pushes its transform into
  `SetFocus(Vec3d)` once per frame (`TemplateGame.cpp:259-271`), and
  `SetFocus(ZoneId)` covers spawn and console warps (`:765-815`).
- **Focus resolution and demand** are pure functions
  (`engine/src/zone/ZoneDemand.cpp`); the AABB containment/hysteresis defect
  for contained zones is documented in doc 11 Section 2.1.
- **Math**: `Vec3d` is `Vec<3, float>` (the suffix is dimension, not double;
  `Vec.h:382`). `Aabb3d` is float min/max with contains/intersect/expand
  (`Aabb3d.h`). There is no triangle-box intersection, no ray-triangle, no
  spatial hash, no Morton utility; `Grid3d<T>` (`math/spatial/Grid3d.h`) is a
  dense 3D array template with no consumers.
- **Movement truth**: the capsule is `CharacterController` (radius 0.3, height
  1.8, slope 50 degrees, `CharacterController.h:19-21`) driven through Jolt
  `CharacterVirtual` with default `ExtendedUpdate` stair handling
  (`CharacterMover.cpp:62`). **No step-height field exists anywhere**, though
  two roadmap items (`LoftSteps`, traversal probe overlays;
  `engine-roadmap.md:459,507-508`) already assume one. `MovementProfile` is
  acceleration/friction tuning only (`MovementProfile.h:22-27`).
- **Navigation, spatial queries, occlusion, minimap: zero code.** Navmesh is
  planned v1.0 as a cook sibling over the same collision; the hierarchical
  cross-zone planner is the one greenlit abstraction, v2.0
  (`engine-roadmap.md:263-273`). Audio explicitly plans distance/pan only,
  "occlusion almost certainly never" (`docs/audio/runtime.md` Decision F).

---

## 2. What stays, what is demoted, what is fighting (Q1, Q2)

**Stays, unchanged in role:**

- `ZoneRuntime`, per-zone registries, `ZoneParticipation` spans,
  `AsyncZoneLoader`, the recipe seam (D10), the pawn-in-`Global()` model.
- `WorldPartitionManifest` as the runtime contract, regions, per-region
  streaming shapes (doc 10), `RequiredTags` gating, pins, linger, cap, and the
  whole pure demand pipeline. Compiled crossings enter as `TransitionRecord`s,
  so `WorldPartitionIndex`, `ComputeZoneHopRanks`, and `ComputeZoneDemand` do
  not change shape for this design (doc 11's budget extension composes
  independently).
- `WorldDocument`, zone documents, structural content ownership, the partition
  panel, the D18 rule (the preview consumes pure policy plus cook products,
  never the runtime).
- The level cook's cells, `.smesh`/`.scol` artifacts, the cooked cache, and
  `ZoneCollisionLoader`.
- `ConnectZones` and transition verbs, re-scoped to non-geometric edges and
  annotations (Section 7.5).

**Demoted to derived artifacts:**

- `ZoneHeader.Bounds`: broad-phase and editor approximation only. Cooked
  bounds recomputed from the field.
- The AABB cluster (if the editor wants merged boxes for selection or debug
  draw): derivable from the field, editor-only, never a topology source.

**Fighting the direction (replaced or retired):**

- `ResolveFocusZone`'s AABB containment, smallest-volume, and
  nearest-fallback logic: replaced at runtime by field lookup with label-depth
  hysteresis (Section 9.2), retained only as the no-field editor fallback
  until the first bake exists.
- Authored geometric transitions as truth: replaced by compiled crossings
  plus a migration diff (Section 7.6).
- `partition.bounds.overlap` and the doc 11 containment-validation split:
  retired with the AABB-as-shape model.
- The absence of any face-level semantics on brushes: not blocking, but the
  recorded "classify: future" seam becomes the eventual home for
  non-blocking decorative brush classification if worlds need it
  (Section 15, risk 6).

---

## 3. The authoring model (what a designer touches)

Authored, all of which already exist except the last two:

1. Zone identity and names (manifest).
2. Content ownership: which brushes and entities live in which zone document.
   This is already structural and already the strongest semantic signal.
3. Non-geometric connections (Teleport) via `ConnectZones`, plus annotations
   on any edge: `RequiredTags`, `PreloadPriority`, `PreloadDepth`, names.
4. Gates, when door content exists: an entity carrying a gate-bake component
   (Section 8), authored as world-scene or zone content like any entity.
5. Rare labeling hints: an interior seed point or an influence volume, only
   where the ambiguity overlay shows the compiler genuinely cannot know
   (Section 6.4).

Not authored, ever: portal planes, zone-bound boxes, per-doorway links for
geometric openings, streaming shells. The `Connect To` submenu stops offering
geometric-pair creation once migration completes; it creates Teleport links.

---

## 4. The compiler IR: `SpatialField` (Q3, Q4)

### 4.1 Placement

- `engine/include/spatial/` and `engine/src/spatial/`: field data types
  (bricks, channels, ids), the runtime `ZoneLabelField` reader (Section 9),
  `TraversalProfile`, `ZoneCrossing` records. Runtime-linkable, no cook
  dependency, pure data plus lookup.
- `engine/include/assets/cook/SpatialFieldBake.h` (and src sibling): the bake
  passes, beside `BrushGeometryCook` and `CollisionShapeCook`, same "pure: no
  logging, no threads, no disk" contract, dev-only with the rest of the cook.
  `WorldCook` gains one stage that drives them.
- `test/spatial/`: the fixture suite (Section 14, stage 0).
- Editor overlays and hint authoring in kyusu, over the cook products, like
  every other preview surface.

One file per pass (occupancy, clearance, labeling, crossings), not a
`SpatialFieldBake.cpp` junk drawer.

### 4.2 Storage

A sparse brick map over dense bricks:

- Cell size: bake config, default 0.25m. Brick: 16x16x16 cells (4m), so 4x4x4
  bricks per existing 16m cook cell. Brick keys are integer `Vec3i` world
  brick coordinates (the `CellOf` flooring precedent,
  `BrushClustering.cpp:11-18`); the map is a sorted vector of (key, brick),
  iterated in key order everywhere. No unordered containers anywhere in the
  bake (binding rule 8).
- Brick payloads are dense arrays indexed x + 16y + 256z; `Grid3d<T>` gains
  its first consumer as the payload shape or the bricks use a fixed
  `std::array`; either is fine, decided at implementation.
- Only bricks that intersect geometry or labeled free space exist. Wholly
  solid-owned or wholly one-label bricks store a homogeneous header and no
  cell array (this is also the runtime compression, Section 9.1).

Compiler channels per cell (compiler-side only; the shipped subset is
Section 9.1):

- Occupancy: 2 bits (Solid, Free, Mixed). Mixed means the cell box intersects
  triangles but sampling did not classify it fully solid; Mixed is treated as
  Solid for traversal (conservative) and flagged for Phase-2 refinement.
- Owner: `uint16` palette index into a per-field zone table (solid cells:
  the zone owning the source brush; free cells: the label, once assigned).
- Clearance: `uint8`, distance to nearest solid in cells, truncated at 32
  (8m), from a chamfer distance transform over the brick map.
- Support: floor flag plus floor height offset within the cell (`uint8`
  quantized) and a slope-ok bit per profile, derived from the supporting
  triangle's normal.
- Per-profile traversable bit (Section 5).
- Flags: `NearBoundary` (within N cells of a different label), `Ambiguous`
  (Section 6.4), `NearGate` (Section 8), `Unassigned`.

### 4.3 Geometry sources and the conservative rule

Input is the cook's existing world-space triangle stream, collected per zone
with brush and zone identity attached (`CollectCookBrushes` already returns
per-brush geometry; the world cook loads each zone document by path exactly as
`CookDocument` does, so header-only zones bake without being open anywhere).
This is simultaneously render truth and collision truth today, which is the
correct source: the field must agree with what the capsule actually collides
with, and both derive from the same triangles.

Voxelization is conservative: a cell is Solid or Mixed if any triangle
intersects its box, using an exact triangle-box overlap test added to
`math/geometry/3d/` (separating axes; the tree has no such primitive yet and
the navmesh bake will need the identical function). Thin walls therefore
never vanish: a 5cm wall marks every cell it passes through, and a false
opening is impossible by construction. The reverse error (a real gap thinner
than one cell reads closed) is accepted at v1 resolution, is exactly what the
pawn profile would reject anyway for gaps under 0.6m, and is the Phase-2
local-refinement target for sub-pawn profiles.

Gate leaves (movable door geometry, when doors exist) are excluded from the
static bake by component contract (Section 8); everything else static bakes.
Props currently contribute no collision and therefore no occupancy; if prop
collision ever ships, prop triangles join occupancy but never labeling
influence (Section 6.1), which keeps set dressing from moving zone fronts.

### 4.4 Determinism

Binding rules for every pass:

- All bake arithmetic that accumulates (distance transforms, growth costs)
  runs in integer cell units (chamfer 3-4-5 metric, integer gate penalties).
  Float appears only in triangle-box tests against exactly computed cell
  boxes, which are order-independent predicates. `Vec3d` being float is fine
  for predicates; it is not allowed to carry accumulated cost.
- One canonical iteration order: brick keys ascending, cells by index. The
  labeling queue is an indexed priority structure with total-order tie
  breaking (cost, then zone id, then brick key, then cell index).
- Parallelism, when it comes, is `JobSystem::ParallelFor` over bricks with
  per-brick outputs written to preallocated slots and reduced in index order,
  so `worker_count == 0` and the pool produce identical bytes (the existing
  serial-reference invariant). Labeling stage runs serial in v1 (it is a
  single global priority walk; measure before splitting it).
- The field artifact records its full bake config (cell size, profile list,
  cost constants, format version) in its header, and its content hash enters
  the cooked cache like any artifact.

The determinism test is a golden content hash per fixture world, asserted
identical across two consecutive bakes and across worker counts.

---

## 5. Traversal profiles

```cpp
struct TraversalProfile
{
    float Radius;        // capsule radius, meters
    float Height;        // capsule height, meters
    float StepHeight;    // max climbable rise without a jump
    float MaxSlopeDegrees;
};
```

Plain data, part of the bake config, palette of named profiles with v1
shipping exactly one ("pawn"), seeded from `CharacterController` defaults
(0.3, 1.8, 50) plus `StepHeight` defaulting to Jolt's walk-stairs default
(0.4). Two coordination notes, both owner-visible:

- This record becomes the first authoritative home of a step height in the
  engine. The roadmap's `LoftSteps` and traversal-probe items currently claim
  to read a "MovementProfile step height" that does not exist
  (`engine-roadmap.md:459,507`); they should read this profile when they land,
  and the roadmap text should be corrected either way.
- The bake profile must mirror the mover's effective behavior
  (`CharacterMover.cpp` wraps Jolt defaults). If the controller ever exposes
  authored step height, both read the same field. A mismatch here produces
  crossings the pawn cannot actually use, which is the one lie this system
  must never tell; a fixture test pins bake traversability against a scripted
  mover walk (Section 14, stage 4 gate).

Per-cell, per-profile traversability precomputes: standing room (vertical free
run of `ceil(Height / cellSize)` cells above a supported cell), horizontal
clearance (`clearance >= ceil(Radius / cellSize)`), slope within limit.
Cell-to-cell steps check support delta against `StepHeight` and only
face-adjacency is connective; diagonal movement requires both shared faces
free (no corner cutting through a wall edge). A drop taller than `StepHeight`
is traversable downward only, which crossings inherit as derived one-way
edges (Section 7.2). Flying profiles (when one exists) skip support and slope
and use 3D face adjacency with clearance only; nothing else changes.

---

## 6. Zone labeling (Q5)

### 6.1 Influence sources, in priority order

1. **Brush ownership** (primary, always on): every Free cell face-adjacent to
   a Solid cell whose owner is zone Z becomes a growth source for Z at cost
   zero. In Sencha this is not a heuristic: the designer literally authored
   this assignment by building the room in that zone's document. Interior
   walls, floors, and ceilings are the influence field.
2. **Explicit seeds** (override): an authored point in a zone document
   (a marker component) that injects a source at cost zero. Needed only where
   a zone owns little or no geometry near contested space.
3. **Influence volumes** (override): an authored convex volume biasing cost
   for one zone inside it. The last resort for genuinely ambiguous open
   boundaries; expected to be rare, added only in the editor stage.

Prop geometry never seeds (props are set dressing and today have no collision
anyway). Gate-adjacent cells never seed (a door frame is usually owned by one
side; letting it pull labels through the opening would drag the boundary past
the gate; instead the gate's passage region gets the passage penalty and the
watershed settles inside it).

### 6.2 The growth

Multi-source shortest-path over Free cells (integer chamfer distances), one
global pass, deterministic queue. Step cost from cell a to face-adjacent b:

```text
cost = chamferStep
     + narrowness(b)      // k1 * max(0, clearCap - clearance(b))
     + gatePenalty(b)     // k2 if b is inside a gate passage region
```

Narrowness makes doorway throats and corridor necks expensive, so fronts from
two sides stall and meet inside the throat instead of drifting across an open
room (watershed behavior). Constants `k1`, `k2`, and `clearCap` are bake
config with defaults chosen against the fixture worlds; they are data, not
code branches. A cell's label is the source zone of its cheapest path; ties
break by zone id then discovery order (deterministic by the queue's total
order).

Where ownership is one-sided the cost shaping barely matters: a hallway built
in zone A's document carries A's label to exactly where B's geometry begins,
which is where the designer drew the boundary. The interesting cases are
throats between two zones' geometry and wide-open contact, handled next.

### 6.3 Post passes

- **Island absorption**: a labeled component smaller than a config threshold,
  fully enclosed (all non-solid neighbors one other label), relabels to the
  enclosing label. Kills bubbles from stray owned geometry.
- **Disconnected zone islands**: a zone labeling two or more mutually
  unreachable components is legal (a zone may intentionally have two lobes)
  but reported (`spatial.zone.split_label`, Warning) because it is usually a
  mis-owned brush.
- **Unassigned**: Free cells with no path to any source within a cost cap
  (outside the playable envelope, sealed voids) stay Unassigned. Not an
  error. The field only exists within a bounded envelope: bricks within a
  margin of any geometry or any seed; the envelope bound is bake config.

### 6.4 Ambiguity, honestly

Per Free cell, the bake retains the margin between the best and second-best
zone cost. Cells (and boundary components) whose margin falls under a
threshold are flagged Ambiguous, surfaced in the editor as a paintable
overlay, and listed as diagnostics (`spatial.label.ambiguous_boundary`, Info,
with zone pair and location). The compiler still produces a deterministic
answer; it just refuses to pretend the answer was informed. The designer
response, in order of preference: move brush ownership (rebuild the wall in
the right document), drop a seed, or add an influence volume. This is the
entire override surface, and all three are ordinary authored data.

### 6.5 The fixture cases (the acceptance vocabulary)

The golden worlds stage 0 builds, and what each proves:

- Curved hallway between rooms: labels follow the curve; one crossing at the
  authored throat; no AABB artifact anywhere.
- L-room beside a straight room: no false adjacency across the L's empty
  quadrant (the exact case AABBs cannot express).
- T-junction three ways: three crossings, three zone pairs, none merged.
- Stacked rooms, sealed floor: zero crossings (AABBs overlap today and warn;
  the field simply has no traversable adjacency).
- Stacked rooms joined by a stairwell: one crossing on the stairs, labeled
  where stair-brush ownership changes; slope and step checks hold.
- Wrap-around corridor (doc 11's garden-and-house): inner and outer label
  correctly; crossings at the actual doorways; the doc 11 focus defect is
  structurally impossible.
- Thin wall (5cm) between zones: no crossing (conservative solidity).
- Sub-pawn gap (0.5m wide slot): no pawn crossing; flagged aperture-below-
  profile diagnostic.
- Drop ledge: one-way crossing, downward.
- Wide-open two-owner field boundary: deterministic split, Ambiguous flagged.

---

## 7. Crossings (Q6)

### 7.1 Extraction

For every ordered pair of face-adjacent Free cells (a, b) with different,
assigned labels where the pawn profile can traverse a to b: record a boundary
face. Group boundary faces into connected components by (unordered zone pair,
face adjacency of the boundary cells). Each component is one
`ZoneCrossing`. Two doors between the same pair are two components (their
boundary cells are not connected), which preserves the doc 09 requirement
that distinct openings stay distinct.

### 7.2 The record (compiler product)

```cpp
struct ZoneCrossing
{
    CrossingId   Id;          // deterministic, Section 7.4
    ZoneId       A, B;        // unordered pair, A = lower id
    Vec3d        Centroid;    // area-weighted boundary center
    Aabb3d       Bounds;      // of member boundary faces
    Vec3d        NormalHint;  // dominant face direction, quantized
    float        Area;        // face count * cell face area
    float        MinWidth;    // min clearance along the component
    uint8_t      Profiles;    // which profiles can cross
    uint8_t      Directions;  // A->B, B->A, or both (drops are one-way)
    // gate binding, when bound (Section 8)
};
```

No polygon, no patches in v1 (Section 0.2). Member cell runs are kept
compiler-side for refinement and debug, not shipped.

### 7.3 Compilation into the manifest

The world cook emits, into the cooked manifest only:

- One `TransitionRecord` per crossing direction that is traversable:
  `From`/`To` per direction, `Topology = Seam` (or `Doorway` when
  gate-bound), `Flags.OneWay` when the reverse direction is absent.
  `TransitionId` = the crossing id (per direction salt).
- Authored records pass through: Teleports and any authored annotations
  matched to crossings (Section 7.5).

The runtime demand stack consumes the result unchanged. The crossing sidecar
(a cook JSON beside the manifest, later binary with the rest) carries the
geometric payload (centroid, bounds, area, min width) keyed by
`TransitionId`, for the streaming metadata consumers (Section 10) and the
editor. The authored `.sworld` never contains compiled records; compiled
truth lives only in cook output, so a stale bake is visible, never silently
merged.

### 7.4 Identity (the churn problem, faced directly)

`CrossingId` must survive rebakes or annotations rot. Minting:

```text
CrossingId = Hash64(worldSalt, zoneA, zoneB,
                    quantize(centroid, 2m),
                    ordinalAmongSamePairSameBucket)
```

plus deterministic re-salt on collision with any authored id. Stability
analysis: moving a doorway under 2m of centroid drift keeps the bucket and
the id; larger remodels change it, and the cook then emits a reconciliation
report (old crossing unmatched, new crossing unannotated, nearest-candidate
suggestion) instead of guessing. Gate-bound crossings are sturdier: their
identity keys on the gate's stable id once entities have one (Section 8's
dependency), falling back to the geometric signature until then. Annotations
are expected to be sparse (most seams need none), which keeps reconciliation
a report, not a migration.

This is the design's weakest joint and is called out as such in Section 15;
the mitigation is honesty (reports, never silent rebinds) plus sparse use.

### 7.5 Authoring against compiled records

Annotations (`RequiredTags`, `PreloadPriority`, `PreloadDepth`, `Name`,
participation hints if doc 11 Phase E lands) live in the authored manifest as
a keyed list: `TransitionAnnotation { CrossingId Key; ...fields }`. The panel
edits them exactly as it edits transitions today (same inline editor, same
verbs, same non-undoable pattern per D11). `ConnectZones` remains for
Teleport and future elevator-style logical links, which keep authored random
`TransitionId`s. The undirected pair display continues to collapse compiled
direction pairs.

### 7.6 Migration and validation swap

One cook flag enables crossing compilation per world. The migration cook
produces a diff: authored geometric edges with a matching compiled crossing
(auto-annotate: carry name, tags, priority, depth over; delete the authored
edge), authored edges with no crossing (report: sealed doorway or fiction,
owner resolves), crossings with no authored edge (report: connectivity the
author never knew or never wanted; usually the interesting list). After
migration the authored transition list contains only Teleports and
annotations. Validation changes:

- Retired: `partition.bounds.overlap`, `partition.transition.unpaired` for
  geometric edges (pairing is now derived reality).
- Added: `spatial.crossing.below_profile` (opening exists but no profile
  passes; Info), `spatial.label.ambiguous_boundary`,
  `spatial.zone.split_label`, `spatial.annotation.orphaned` (annotation key
  matches no compiled crossing; Warning), gate diagnostics (Section 8).
- Kept: reachability over the merged graph, region rules, id rules, tag-name
  rules, all unchanged.

---

## 8. Gates (Q7)

There is no door in the engine today (no interaction system, no door entity;
"Doorway" is an enum label). The contract is therefore designed now and
consumed when door content lands, and nothing in stages 1 through 5 depends
on it.

```cpp
// Component on a gate entity (world scene or zone content).
struct GatePassage
{
    // Volume the gate can open (local space): excluded from static
    // occupancy so the bake sees the potential connection.
    ConvexVolumeRef OpenPassage;
    // The leaf geometry that moves at runtime: also excluded from the
    // static bake (it is dynamic state, not world structure).
    // The FRAME is ordinary brush/mesh content and bakes normally.
    Vec3d PassageAxis;      // local, points through the opening
    uint8_t Profiles;       // which profiles the gate can pass when open
};
```

Bake semantics: leaf excluded, frame baked, cells inside `OpenPassage`
flagged NearGate and given the gate passage cost penalty (Section 6.2), so
labels do not bleed through an open doorway and the watershed sits inside the
frame. Crossing association after extraction: candidate crossings whose
member cells intersect `OpenPassage`, scored by overlap fraction and by
alignment of `NormalHint` with `PassageAxis`; best match binds if it leads by
a config margin, otherwise `spatial.gate.ambiguous_binding` (Error) rather
than a guess. Diagnostics, all cook-time: gate with no candidate crossing
(sealed by static geometry, or interior to one zone), gate whose passage
remains blocked with the leaf removed, gate whose bound crossing fails the
declared profiles.

Binding direction honors D1's principle: the cook writes the bound
`TransitionId` into the gate entity's cooked component data (content
references topology, never the reverse), so the runtime door system can gate
the edge through the existing world-tag mechanism (`RequiredTags` plus
`SetWorldTags`) or a future direct blocked flag; that runtime choice belongs
to Track C item 6's typed transition scopes, not here.

Dependency, stated plainly: durable gate identity (for crossing-id stability
and for the cooked component rewrite) wants the stable entity identity scheme
(Track C item 5). Until it lands, gate binding works but its identity
contribution is the geometric signature, and the cook rewrite targets the
entity by document identity within the cooked scene it is emitting anyway
(the cook owns that file end to end, so this is safe today, just not durable
across hand edits of cooked output, which nobody does).

---

## 9. Runtime products (Q8)

### 9.1 What ships

One world-level binary artifact, `.cooked/worlds/<stem>.szfield`, following
the `.scol` precedent (magic, version, restored directly). Referenced from
the cooked manifest as `CookedSpatialFieldRef` + `CookedSpatialFieldHash`
beside the existing world trio (`WorldPartitionManifest.h:98-104` pattern),
loaded once, synchronously, at the world-start loading boundary
(`TemplateGame.cpp:731-763` is the slot), owned by `WorldPartitionRuntime`.

Shipped channels, per brick: the zone palette, per-cell label indices (4
bits when the brick's palette has at most 15 labels plus Unassigned, else 8),
and per-cell label depth (4 bits, saturating: distance in cells to the
nearest different label). Homogeneous bricks ship as headers. Everything
else (occupancy detail, clearance, support, per-profile bits, ambiguity,
member cells) stays compiler-side; those channels ship later only when a
runtime consumer exists (the navmesh and query stages compile their OWN
products instead; Section 11).

Size sanity: a 200m x 200m x 30m fully-active envelope at 0.25m cells is
76.8M cells raw; real worlds are sparse shells of that (rooms plus margin),
homogeneous-brick compression collapses interiors, and 1 byte per cell only
near boundaries. The fixture worlds land in the tens to hundreds of KB; a
content-scale world is expected in single-digit MB, measured at stage 4 and
gated before anything grows (Section 14). Per-region field splitting is the
recorded fallback if a v3-scale world ever needs it; do not build it now.

The manifest stays O(zones + transitions); the field is an artifact beside
it, exactly as cooked collision is.

### 9.2 Lookup and focus

`ZoneLabelField::LabelAt(Vec3d) -> {ZoneId, labelDepth}`: brick hash (sorted
vector binary search or a flat map; measure), cell index, palette resolve.
Constant-time in practice, allocation-free, thread-agnostic const data.

`ResolveFocusZone` (same name, same pure-policy home, new inputs): the
previous focus wins unless the sampled position (pawn capsule center) sits in
a different assigned label with `labelDepth >= K` (config, default 2 cells =
0.5m), in which case the new label wins. Unassigned samples keep the previous
focus; an invalid previous focus falls back to the aggregate-bounds nearest
rule that exists today (recovery tier, unchanged code). Teleports, spawns,
and console warps keep `SetFocus(ZoneId)`; save restore and
falling-out-of-world resolve through `LabelAt` plus the recovery tier.

This retires the AABB containment defect (doc 11 Section 2.1) structurally:
the wrap-around zone's label simply is not present inside the house, so
there is nothing to stick to. Stairs, thresholds, and diagonal boundaries
are jitter-guarded by label depth instead of box hysteresis. The editor
preview resolves through the same function against the last bake, and shows
a "no bake / stale bake" state (content-hash mismatch) exactly like cook
status does today; `LoadManifest` at runtime refuses a cooked world with a
missing or hash-mismatched field, the same contract as missing
`CookedSceneRef`s.

Spaces note (design doc Section 8): a field is scoped to one coordinate
space by construction. When spaces land, each space bakes its own field, and
`LabelAt` takes the space implicitly from the querying context. Nothing in
the format precludes it (the header carries a space id, invalid in v1).

---

## 10. Streaming metadata (facts from the bake, policy in the runtime)

All cook-derived, all shipped in the crossing sidecar or `ZoneBudgetRecord`
family, all consumed by the pure demand policy as optional inputs (absent
data = today's behavior, the doc 11 pattern):

- **Crossing metrics** (already in the record): area, min width, directions,
  profiles. First consumer: doc 11's same-hop ordering (distance to crossing
  centroid replaces the superseded authored anchor); second: Teleport/seam
  participation decisions (doc 11 Phase E).
- **Approach distances**: per zone, per crossing, a coarse geodesic distance
  field at brick resolution (distance from each brick of the zone to that
  crossing, through the zone's own free space). Shipped quantized (uint8
  bricks); enables "player is 9 seconds from the north door at current speed,
  the far side costs 6 seconds to load" prefetch lead, replacing hop-count
  guessing. Policy change itself is a separate doc 11 Phase D style stage;
  this stage only makes the fact available.
- **Zone interior graph**: bricks cluster into interior nodes (basins split
  by narrowness, the same watershed machinery); nodes carry pairwise coarse
  distances between the zone's crossings. This is transition-to-transition
  travel time, and it is deliberately shaped as the coarse tier the greenlit
  hierarchical cross-zone planner needs (zone-graph planning refined per
  zone, `engine-roadmap.md:270-273`): when the planner lands, its top level
  reads this graph instead of inventing one.
- **Reachability facts**: dead-end zones (one crossing), articulation
  crossings (every A-to-B path uses it), gate-locked reachability (all paths
  from start pass gates with tags). Cheap graph analysis over compiled
  crossings at cook; consumed by eviction policy later and by validation
  (`spatial.zone.gate_locked` as Info, listing the controlling tags).

The compiler never decides policy: no "prefetch this" bits in cook output,
only measured facts. The demand policy remains the one place decisions
happen, testable and previewable.

---

## 11. One substrate, several compilers, no god object (Q10)

The field passes split into a reusable front half and product-specific back
halves:

```text
zone triangle streams (per zone, cook-owned)
        v
occupancy + clearance + support + per-profile traversability   (shared front)
        v
+-- zone labeling -> crossings -> cooked manifest + sidecar + ZoneLabelField
+-- navmesh back end (v1.0 roadmap item, when scheduled):
|     walkable spans from supported cells -> regions -> contours -> polys
|     per zone, cooked as the sibling artifact the roadmap already names
+-- spatial query products (only when an AI consumer exists):
      their own compiled artifacts over the same front-half data
```

Rules that keep this from congealing into a god object:

- The front half is pure functions over plain data with no knowledge of
  zones, navmeshes, or queries; it lives beside the other cook kernels and is
  tested alone.
- Every back end emits its own artifact with its own format and version.
  The navmesh is a polygon graph, not "the field with extra bits"; runtime
  navigation queries never touch `ZoneLabelField`, and zone lookup never
  touches the navmesh.
- No runtime `WorldSpatialQueries` facade ships in this plan. It is recorded
  as the natural API when an AI/EQS-shaped consumer exists (candidate
  methods: label, clearance, floor, traversable), and it would wrap shipped
  artifacts, not the compiler IR. Shipping it now would be a dead seam
  (directive 4); the design cost of adding it later is nil because the
  compiler channels already exist.
- Dynamic state never enters the bake. Gate open/closed is world-tag state
  over a static potential crossing; future dynamic obstacles are a runtime
  overlay for whatever system needs them (navmesh local avoidance), not a
  field rebake.

---

## 12. Incremental compilation (Q9)

Designed now, built after correctness, gated by measurement (the cook is
offline; the fixture worlds bake in negligible time, and even content-scale
full bakes are expected in low seconds; do not spend complexity before the
editor loop actually hurts).

- The brick map keys and per-brick content hashes make dirty tracking
  natural: a zone edit dirties the bricks its brush bounds touch, expanded by
  the dependency margin (clearance cap + profile radius + one brick).
- Occupancy, clearance, support, and traversability rebuild locally within
  the dirty set (their dependencies are bounded by the margin).
- Labeling is the honest problem: a wall edit can legally move a watershed
  arbitrarily far through open space. The scheme: relabel outward from the
  dirty set; if the recomputed labels at the expansion frontier match the
  prior bake, stop; else expand. Worst case degenerates to a full relabel,
  which is the correctness backstop, not a failure.
- The binding gate: **an incremental bake must be byte-identical to a full
  bake** of the same inputs (golden-hash test per fixture, mixed edit
  scripts). Any divergence is a defect, per the determinism rules.
- Crossing extraction and id minting rerun over affected zone pairs;
  unaffected crossing ids are stable by construction (their inputs did not
  change).

Until this stage lands, every world cook runs a full field bake, cached by
the world-level input hash exactly like other cook artifacts (unchanged
inputs skip the whole stage via `CookedCacheIndex`).

---

## 13. Editor experience

- **Overlays** (kyusu render, existing line/fill pipelines): label-colored
  free-space slices (a floor-height slab of cells, zone-tinted), boundary
  faces, crossing markers with area/width labels via
  `EditorOverlayState.Labels`, ambiguity heat, Unassigned regions. All read
  the last bake's compiler products from the cook directory; all show the
  stale badge on content-hash mismatch (the existing cook-status pattern).
- **Panel**: crossings appear in the connections list exactly as transitions
  do today (derived rows marked as compiled, annotations editable inline,
  jump-to in viewport); diagnostics rows navigate like validation rows do.
  The doc 11 graph panel, when built, draws compiled shapes and crossings
  instead of AABB centers, which is the version of that panel actually worth
  having.
- **Hints**: seed marker component and influence volume authoring land in
  this stage (after the ambiguity overlay exists to show where they are
  needed), as ordinary zone-document content with inspector support.
- **Bake trigger**: the field bakes with the world cook (the existing Cook
  action and PIE's live cook). No background auto-bake in v1; measure the
  full-bake time first (Section 12) before deciding one is needed.

---

## 14. Stages and gates (Q11)

Each stage is one execution-spec document when accepted, one lane of commits,
suite green throughout, per the overview's rules. Stage order is dependency
order; stages 5+ reorder freely.

- **Stage 0: fixtures and format skeleton.** The golden worlds (Section 6.5)
  as authored assets under `test/spatial/fixtures/`, the `SpatialField`
  types, brick map, and header round-trip. Gate: fixtures load and cook
  (level cook only); field container round-trips empty and toy payloads;
  golden-hash harness runs.
- **Stage 1: occupancy, clearance, support, traversability.** Triangle-box
  primitive in `math/geometry/3d` with its own tests; conservative
  rasterization from per-zone triangle streams; chamfer clearance; support
  and per-profile bits. Gate: thin-wall fixture shows no false opening;
  sub-pawn slot not traversable; stair fixture traversable; golden hashes
  stable across reruns; serial output defined as reference.
- **Stage 2: labeling.** Ownership sources, weighted growth, islands,
  Unassigned, ambiguity margins. Gate: every fixture labels as its comment
  says (curve, L, T, stacked-sealed, stairwell, wrap-around); prop-bubble
  test (a decoy owned brush island) absorbed; ambiguity flagged on the
  open-boundary fixture and nowhere else; determinism across worker counts.
- **Stage 3: crossings and manifest compilation.** Extraction, components,
  metrics, one-way drops, deterministic ids, cooked-manifest emission,
  sidecar, migration diff, validation swap. Gate: fixture crossing counts and
  directions exact; two-doors fixture yields two crossings; rebake with a 1m
  doorway shift keeps ids, a room remodel produces the reconciliation report;
  cooked worlds load and stream in the template game with zero authored
  geometric edges.
- **Stage 4: runtime field.** `.szfield` artifact, manifest refs, world-start
  load, `LabelAt`, label-depth focus resolution, recovery tiers, preview
  parity and staleness. Gate: the traversal-hitch harness walks every fixture
  with focus changes exactly at expected boundaries and zero missed ticks;
  the wrap-around fixture's inner zone gains Logic/Audio on entry (the doc 11
  defect test, now structural); a scripted mover walk agrees with baked
  traversability on every fixture (the profile-mirror gate); shipped field
  sizes recorded and within budget.
- **Stage 5: editor surface.** Overlays, diagnostics rows, annotation
  editing against crossing ids, seeds and influence volumes, migration UX.
  Gate: the manual walkthrough script (open fixture, see shapes, see
  ambiguity, place a seed, rebake, watch the boundary move).
- **Stage 6: streaming facts.** Approach-distance bricks, interior graph,
  reachability facts, crossing-distance load ordering (supersedes doc 11
  anchors), ZoneBudgetRecord coordination. Gate: prefetch-lead numbers appear
  in demand records and the preview; ordering test proves near-door-first.
- **Stage 7: gates.** `GatePassage` contract, bake semantics, binding,
  diagnostics; lands with or after the first door content, coordinated with
  Track C items 5 and 6. Gate: the door fixture binds one crossing, reports
  the ambiguous-binding fixture, and a world-tag flip closes the edge in the
  preview.
- **Stage 8: incremental bake.** Dirty bricks, frontier relabel, id
  stability, byte-identity gate. Scheduled when measured full-bake time hurts
  the editor loop, not before.
- **Recorded, not scheduled here**: aperture polygon refinement (with
  see-through portals, v2.0), navmesh back end (Track A item 5 consumes the
  stage-1 passes), spatial query facade (first AI consumer), map-mesh
  extraction (a filtered field to per-zone display meshes; a Track D editor
  feature when a game wants it), per-region field splitting, adaptive
  resolution and compression beyond palette bricks.

Relation to the user-facing phase list this design was asked to fit: proposal
phases 1 and 2 map to stages 0 through 5 (with polygon refinement deferred
into the recorded list), phase 3 to stage 6, phase 4 to the recorded navmesh
and query items (the substrate exists from stage 1; the refactor is not
needed because it is built shared-first), phase 5 and 6 to the recorded list
and stage 8.

---

## 15. Risks, ambiguous cases, alternatives (Q12)

1. **Label placement versus intent on open boundaries.** Irreducible
   (Section 6.4); the mitigations are determinism, the ambiguity surface, and
   three ordinary override mechanisms. Residual risk: designers ignoring the
   overlay; the countermeasure is the diagnostic list at cook, which is where
   they already look.
2. **Crossing id churn.** The weakest joint (Section 7.4). Mitigations:
   coarse quantization, gate identity when available, reconciliation reports,
   sparse annotations. Alternative considered and rejected: authored
   "annotation anchors" placed in the world to key annotations spatially;
   that re-introduces hand-placed markers (the portal smell) to solve a
   bookkeeping problem reports handle.
3. **Float nondeterminism.** Handled by integer-cost rules (Section 4.4);
   the residual float surface (triangle-box predicates) is order-independent.
   Risk shrinks to compiler-flag variance across platforms; the golden-hash
   suite catches it, and predicates can go exact-arithmetic if it ever fires.
4. **Bake cost on content-scale worlds.** Unmeasured until stage 4. Bounded
   by sparsity (envelope bricks only), brick parallelism headroom
   (`ParallelFor` precedent exists in the cook), and the cache (unchanged
   inputs skip). Fallback: coarser default cell with per-region override,
   before any adaptive-resolution machinery.
5. **Profile-versus-mover drift.** The bake claims "the pawn can cross
   here"; the mover decides reality. Pinned by the stage-4 scripted-walk
   gate and by sourcing profile numbers from the controller component.
   Residual: future mover changes must rerun that gate (it is in CI via the
   fixture suite).
6. **Brush-ownership assumptions.** Labeling leans on "solid geometry is
   zone-owned brushes." Two futures bend it: prop collision (excluded from
   influence by rule already) and non-brush imported architecture (would
   carry zone ownership via its owning document the same way; only per-face
   ownership inside one mesh is unrepresentable, and that is the recorded
   trigger for the `classify: future` seam in `BrushClustering.h:20`).
7. **Two-truth window during migration.** Between accepting this design and
   completing migration, authored geometric edges and compiled crossings
   coexist per world behind the cook flag. The diff report is the bridge;
   the flag flips per world, not globally, and the template fixture world
   migrates first.
8. **Alternative: exact cell-and-portal decomposition** (BSP over brush
   planes, portals from open shared faces). Rejected: Sencha brushes are
   explicitly non-convex polygon meshes (`BrushMesh.h:12-15`), robust exact
   decomposition over that input is a known tar pit, it yields no clearance,
   support, or profile data (so the navmesh front end gets built anyway),
   and it cannot answer "can the pawn fit." Its one advantage (exact planar
   apertures) is exactly the deferred refinement pass, appliable locally
   later against brush faces near a crossing.
9. **Alternative: navmesh-first labeling** (build walkable polys, label
   them, derive crossings from labeled poly edges). Rejected for ordering
   and generality: the navmesh itself needs the rasterized front end, so
   this saves nothing; walkable-only surfaces cannot express flying or
   volumetric membership (focus for an airborne pawn, falling recovery,
   future flying profiles); and zone correctness would inherit every navmesh
   parameter quirk. The dependency points the other way: the navmesh is a
   consumer.
10. **Alternative: keep authored edges as assertions over compiled ones.**
    Rejected: it reintroduces hand-maintained doorway links as a shadow
    truth, which is this design's central deletion. The migration diff and
    the fixture suite carry the safety role instead.

---

## 16. Non-goals

- No runtime navmesh, planner, or spatial-query service in these stages; the
  substrate is built shared-first, consumers land on their own roadmap items.
- No see-through portals, no aperture polygons, no renderer coupling.
- No dynamic-obstacle carving or runtime field mutation; dynamic state stays
  layered (tags, future overlays).
- No nested zones, no per-entity zone membership changes; ownership stays
  structural.
- No genre vocabulary anywhere in identifiers; the map-mesh idea, when it
  comes, is "per-zone map surface extraction," not a franchise reference.
- No new concurrency lane; the bake is cook-side, serial-reference,
  brick-parallel via the existing `JobSystem` only.
- No auto-bake daemon, no background cook in v1.

## 17. Open questions for the owner

1. **Cell size default and world envelope.** 0.25m cells and a
   geometry-margin envelope are proposed; confirm against intended content
   scale (the 16m cook cell suggests interiors-plus-yards, where these
   numbers are comfortable).
2. **Step height default.** 0.4 (Jolt walk-stairs default) proposed for the
   pawn profile; confirm, and decide whether `CharacterController` should
   grow the explicit field now or keep relying on Jolt defaults that the
   profile mirrors.
3. **Migration posture.** Per-world cook flag with a reconciliation report is
   proposed. Alternative: hard cutover once fixtures pass (smaller code
   surface, no dual window). Preference?
4. **Doorway topology naming.** With crossings derived, `Seam` versus
   `Doorway` becomes "unbound versus gate-bound." Keep both labels (useful in
   UI and future timing policy) or collapse to one until gates exist?
5. **Where ambiguity blocks.** Should `spatial.label.ambiguous_boundary`
   ever be promotable to Error (blocking cook) per world, for teams that
   want forced resolution, or stay Info forever?
6. **Doc 11 Phase A.** Ship the interim AABB containment focus fix now
   (small, immediate relief) knowing stage 4 deletes it, or hold for the
   field? Recommendation: ship it only if this design's acceptance or
   scheduling slips a milestone.
