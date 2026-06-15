# Pillar 3a — Brush Representation: Editable Polygon-Mesh Brushes

**Status**: Working plan (2026-06-14). Editor-internal; can proceed in parallel with
Pillars 1–2 once capacity allows. Its output feeds texturing (04-) and cook (05-).
**Owns Stage S4a.**

> Product decision (2026-06-14): a brush is an **editable polygon mesh** (face-vertex /
> half-edge solid, convexity NOT required), in the ProBuilder / Source 2 Hammer lineage —
> *not* a convex plane-set CSG brush. Editing verbs: **extrude, face delete, clip**; carve
> (boolean) is an explicit stretch the model must not preclude. Brushes are **first-class
> geometry**, not greybox throwaway, and **cannot be instanced by design** (each is unique
> level geometry; instancing comes from baking a brush to a static-mesh asset — 05-§bake-out).

---

## 1. Current state (what we replace)

The brush is a box. `BrushComponent { Vec3d HalfExtents }`
([editor/level/LevelScene.h:15](../../editor/level/LevelScene.h)); all geometry derives from
a transform + half-extents:

- `BrushState { Transform3f, Vec3d HalfExtents }`,
  `BrushGeometry::ComputeCorners` (8), `ComputeFaceGeometry` (6 axis-aligned faces),
  `ResizeFace`, `Translate`
  ([editor/level/BrushGeometry.h](../../editor/level/BrushGeometry.h)).
- Face handles & interactions assume 6 axis-aligned faces
  ([editor/level/editmodes/BrushFaceHandle.*](../../editor/level/editmodes/),
  [editor/level/interactions/BrushResizeDragInteraction.*](../../editor/level/interactions/)).
- Serialization: `TypeSchema<BrushComponent>` with one `half_extents` field; FourCC `BRSH`
  ([editor/level/LevelSerialization.cpp](../../editor/level/LevelSerialization.cpp)).
- Wireframe renderer draws 12 edges of the box.

A box cannot represent a clipped, extruded, or otherwise non-box solid. So the box brush is
a **placeholder**; this pillar replaces the *data model* and the verbs, while keeping the
editor's command/selection/handle *architecture* (which is sound).

---

## 2. The data model

### 2.1 Mesh representation: indexed face-vertex with adjacency on demand

A brush is a **closed, orientable polygon mesh**. We store an **indexed face-vertex** mesh
(compact, trivially serializable, cache-friendly) and compute **half-edge adjacency on
demand** for editing ops that need neighbor queries (extrude, clip, weld). We do **not**
persist a half-edge structure — it is a derived, in-memory acceleration built when an edit
session opens and discarded after.

```cpp
// editor/level/brush/BrushMesh.h  (new)  — the authored brush geometry
struct BrushVertex { Vec3d Position; };           // position only; UVs are per-face (04-)

struct BrushFace {
    std::vector<uint32_t> Loop;     // CCW vertex indices, outward-facing (>=3, planar-ish)
    Vec3d   Normal = {};            // cached; recomputed on edit (Newell's method)
    // Per-face material + UV projection live in 04- (FaceMaterial). Kept separate so this
    // doc stays geometry-only and 04- layers texturing on without touching topology.
};

struct BrushMesh {
    std::vector<BrushVertex> Vertices;
    std::vector<BrushFace>   Faces;
    // Invariants (validated): closed (every edge shared by exactly 2 faces), outward
    // normals, no degenerate faces, no unreferenced vertices. Non-convex allowed.
};
```

Notes:
- **Faces are polygons, not triangles.** A box has 6 quad faces, not 12 tris. This matches
  how designers think (Hammer/ProBuilder faces) and is what per-face texturing (04-) and the
  face-selection model want. Triangulation happens only at *render* and *cook* time.
- **Faces should be planar** for clean texturing; the editor enforces/repairs planarity after
  edits (project loop to best-fit plane, or split into planar sub-faces if an op produced a
  non-planar quad). Recorded invariant; checked in validation.
- **Vertices are welded** (shared corners are one index) so edge adjacency is well-defined and
  extrude/clip behave. Welding tolerance is a brush/editor setting.

### 2.2 The component

`BrushComponent` changes from `{ HalfExtents }` to hold (or reference) a `BrushMesh`. Because
**components must be trivially copyable** (archetype memcpy — [World.h:92](../../engine/include/ecs/World.h)),
and a `BrushMesh` has `std::vector`s, the brush **cannot store the mesh inline in an archetype
component**. Two clean options; we pick the second:

1. ~~Store the mesh in the component~~ — violates trivial-copyability. Rejected.
2. **Component holds a stable `BrushId` (a handle); the meshes live in an editor-side
   `BrushMeshStore` keyed by `BrushId`.** The component is `{ BrushId Id; }` — trivially
   copyable. The store is a resource on the editor `LevelDocument` (not an archetype
   component). This mirrors how the engine keeps heavy data out of components (mesh handles,
   not mesh bytes).

```cpp
struct BrushComponent { BrushId Id; };            // trivially copyable; archetype-safe

// editor/level/brush/BrushMeshStore.h
class BrushMeshStore {
public:
    BrushId   Create(BrushMesh mesh);
    BrushMesh*       Find(BrushId);
    const BrushMesh* Find(BrushId) const;
    void      Destroy(BrushId);
    // Serialization walks this store alongside the registry (see §5).
};
```

> This is a small but important correction the box model dodged (a `Vec3d` is trivially
> copyable; a mesh is not). Getting it right now avoids an archetype-corruption class of bug.

### 2.3 `BrushState` / `BrushGeometry` become mesh-based

`BrushGeometry`'s box math is replaced by mesh queries that the rest of the editor consumes
through the *same* shape of API (so handles/interactions change behavior, not architecture):

- `ComputeBounds(mesh)` — AABB over vertices.
- `EnumerateFaces(mesh)` — returns face descriptors (index, centroid, normal, loop corners)
  for selection/handles, now N faces of arbitrary polygon, not a fixed 6.
- Face/vertex/edge **picking** by ray (replaces AABB-face picking).
- Edit ops (§3) operate on `BrushMesh` and produce a new `BrushMesh` (value semantics →
  trivial undo: command stores before/after meshes, or a compact op-delta).

---

## 3. Editing verbs (the crude mesh toolkit)

Each verb is a **pure function** `BrushMesh → BrushMesh` (plus parameters), wrapped by an
undoable command (`EditBrushMeshCommand` storing before/after, consistent with the existing
command stack). Pure functions are unit-tested with zero UI.

| Verb | Signature (sketch) | Notes |
|------|--------------------|-------|
| **Create box** | `MakeBox(halfExtents) → BrushMesh` | The default brush; preserves today's UX. 8 verts, 6 quad faces. |
| **Resize face** | `ResizeFace(mesh, face, planePos, minThickness) → BrushMesh` | Generalized from `BrushGeometry::ResizeFace`; moves a face's loop along its normal, keeps the solid closed. |
| **Translate** | `Translate(mesh, delta)` | Whole-brush move stays on the entity transform; mesh is local-space. |
| **Extrude face** | `ExtrudeFace(mesh, face, distance) → BrushMesh` | Duplicate the face loop, connect with side quads, offset along normal. The verb that *needs* adjacency. |
| **Delete face** | `DeleteFace(mesh, face) → BrushMesh` | Opens the solid (becomes an open mesh — allowed for authoring; cook caps/handles open meshes, 05-). |
| **Clip** | `Clip(mesh, plane, keepSide) → BrushMesh` | Slice by a plane: cut edges at the plane, drop the discarded side, cap the new opening with a face. The Hammer clip tool. |
| **Carve (stretch)** | `Carve(target, tool) → vector<BrushMesh>` | Boolean subtract. NOT committed this branch; the model permits it (general meshes). Trigger in 00-§8. |

Adjacency: ops needing neighbor traversal build a transient half-edge view over the
face-vertex mesh at op start (`BuildHalfEdge(mesh)`), operate, and emit a fresh face-vertex
mesh. The half-edge builder + its inverse are themselves pure and unit-tested.

### 3.1 Validation & repair

After every edit, run `ValidateAndRepair(mesh)`:
- Re-derive face normals (Newell), enforce outward orientation.
- Weld coincident vertices within tolerance; drop unreferenced.
- Reject/repair degenerate faces (<3 unique verts, zero area).
- Flag non-closed meshes (allowed during authoring, surfaced as a warning; cook decides).
- Re-fit faces to planes; split non-planar faces if needed.

A brush that fails repair is rejected with a clear editor message; the command does not
commit (no half-broken geometry enters the document).

---

## 4. Selection, handles, rendering

The existing selection/handle architecture (`SelectableRef`, `BrushEditSession`,
`BrushFaceHandle`, `BrushBodyHandle`, drag interactions) is **kept**; the geometry behind it
generalizes:

- **`SelectableRef`** gains sub-element addressing: select a **brush body**, a **face**, an
  **edge**, or a **vertex** (Hammer/ProBuilder modes). Extend `SelectableRef` with an element
  kind + index (the doc already anticipated "extend with registry tag"). Face id is now an
  index into `BrushMesh::Faces`, stable within an edit session.
- **Picking** (`PickingService`) replaces ray/AABB-per-box with ray vs. brush triangles
  (triangulate faces on the fly or keep a cached render mesh) for body/face hits, plus
  edge/vertex proximity picks in screen space for those modes.
- **Handles**: face handle drags map to `ResizeFace`/`ExtrudeFace`; body handle to
  `Translate`; new edge/vertex handles for direct manipulation (optional this branch — gate
  on time; resize+extrude+clip is the must-have).
- **Wireframe renderer** draws all face-loop edges of the brush mesh (was 12 box edges).
  Selection renderer highlights the selected element (face fill / edge / vertex).
- A **solid preview** (shaded, not just wireframe) is desirable so designers see real
  geometry; this is naturally the PIE bake (05-) or a lightweight editor solid pass. Gate on
  time; wireframe is the baseline.

---

## 5. Serialization (authored format)

The authored level JSON must round-trip arbitrary brush meshes, and must remain
**forward-compatible** so the cook step and any editor-unaware tool can pass it through
(pipeline.md Decision A's "keep the editor's save→load round trip intact").

- `TypeSchema<BrushComponent>` can no longer be one `half_extents` field. The brush's heavy
  data is the `BrushMesh` in the `BrushMeshStore`, keyed by `BrushId`. Serialize the
  component as its `BrushId`, and serialize the **store** as a sibling section of the scene
  (a `brush_meshes` block: `id → { vertices, faces[ {loop, material/uv (04-)} ] }`).
- Concretely: extend the editor's save/load (it uses engine `SaveSceneJson`/`LoadSceneJson`
  on its own registry) to also write/read the `BrushMeshStore`. The cleanest fit is a
  **document-level sidecar in the same JSON**: the scene's components carry `BrushId`s; a
  top-level `brush_meshes` map carries the geometry. Both saved/loaded by `LevelDocument`.
- Keep FourCC `BRSH` for the component's binary chunk; the mesh store gets its own chunk/
  section. Versioned, because the mesh format will evolve (edges/UV metadata).
- **Validation on load**: run `ValidateAndRepair`; a corrupt brush is reported, not silently
  accepted.

> Why a sidecar map keyed by `BrushId` rather than inlining the mesh per entity? It keeps the
> component trivially copyable (§2.2), it lets the cook walk all brush geometry in one place,
> and it mirrors the asset-ref-by-handle pattern the engine already uses. The authored JSON is
> still one file, still human-diffable (vertex lists diff cleanly on edits).

---

## 6. Stage S4a & gate

Work:
1. `BrushMesh`, `BrushMeshStore`, `BrushId`; component becomes `{ BrushId }`.
2. Pure verbs: `MakeBox`, `ResizeFace`, `Translate`, `ExtrudeFace`, `DeleteFace`, `Clip`
   (+ `BuildHalfEdge`/inverse, `ValidateAndRepair`). Unit-tested.
3. `BrushGeometry` mesh queries; picking; selection sub-element addressing.
4. Handles/interactions retargeted to the mesh verbs (resize, extrude, clip minimum).
5. Wireframe/selection rendering over arbitrary face loops.
6. Serialization: `BrushMeshStore` ↔ JSON sidecar; round-trip + repair.

**Gate (S4a):**
- Create a box, **extrude** a face, **clip** it with a plane, **delete** a face; the brush
  renders correctly each step.
- **Save and re-load** the level; the edited mesh round-trips byte-for-byte (after canonical
  repair) including the non-box topology.
- Pure-verb unit tests pin invariants: closedness preserved by extrude/resize/clip, vertex
  counts, outward normals, repair idempotence, half-edge build/inverse round-trip.
- No archetype corruption: brush components are trivially copyable; meshes live in the store.

---

## 6b. Progress log

- 2026-06-15 — **Phase 1: geometry kernel landed; 20 unit tests, full suite 899 green.**
  Pure, headless, math-only (no editor/UI deps) under [editor/level/brush/](../../editor/level/brush/):
  - `BrushId`, `BrushMesh` (+ Newell normals, centroids, bounds), `BrushMeshStore`.
  - Verbs in `BrushOps`: `MakeBox`, `Translate`, `ResizeFace` (normal-offset + min-thickness
    clamp), `ExtrudeFace` (cap + side walls), `DeleteFace`, `Clip` (Sutherland-Hodgman per
    face + cut-segment cap chaining). Each runs through `BrushValidateAndRepair`.
  - `BrushValidateAndRepair` + `BrushWeldVertices`: weld, drop unreferenced, drop degenerate,
    recompute normals, outward-orient (centroid heuristic — exact for convex/star-shaped),
    closedness report, idempotent.
  - `BrushBuildHalfEdge` / `BrushToFaceVertex` round-trip.
  - Tests (`test/brush/`, new `brush_tests` target): MakeBox closed/outward/6-quads; extrude
    keeps closed + correct vert/face counts; resize moves one face + clamps; delete opens;
    clip by axis plane (caps, 6 faces, 8 verts) and diagonal plane stay closed/outward; weld;
    repair orientation/idempotence/unreferenced/open/empty; half-edge twins + round-trip;
    store id semantics.
  - The box editor is untouched and still works — integration is Phase 2.
  - Known: outward-orientation is a centroid heuristic (per §7 risk); fine for the convex-ish
    brushes the verbs produce. A truly non-convex authored mesh may need per-face raycast
    orientation — revisit if/when carve (boolean) lands.
- 2026-06-15 — **Phase 2a: substrate swap landed (editor builds + launches; 899 green).**
  The data model is now the mesh kernel, with behavior unchanged (brushes are still authored
  and rendered as boxes — interactive mesh verbs are 2b):
  - `BrushComponent` is now `{ BrushId }` (trivially copyable; archetype-safe). The mesh lives
    in a `BrushMeshStore` owned by `LevelScene`. `CreateBrush` → `MakeBox` → store;
    `CreateBrushFromMesh` for restore/load; `SetBrushHalfExtents` rebuilds the box mesh;
    `SetBrushMesh` for future verbs; `TryGetBrushMesh` resolves id→mesh.
  - `BrushGeometry::TryGetState` derives the box from the mesh's local bounds, so every
    existing box handle/interaction (resize/move/create) keeps working unchanged.
  - **Serialization:** `SceneFieldCodec<BrushId>` persists the component's id; `LevelDocument`
    writes/reads a `brush_meshes` JSON sidecar keyed by `BrushId` (vertices + face loops),
    running `ValidateAndRepair` on load. The component carries the id, the sidecar carries the
    geometry — no fragile entity-matching.
  - `DeleteEntityCommand` saves/restores the `BrushMesh` (round-trips non-box geometry on undo).
  - Inspector shows the brush `id` as non-editable (BrushId is an `Unsupported` scalar in the
    runtime-field model) — correct: brushes are edited via handles, not the field list.
  - **Note (store ownership):** the plan placed `BrushMeshStore` on `LevelDocument`; it lives on
    `LevelScene` instead (the object already threaded through all brush code), with `LevelDocument`
    reaching it via `Scene.GetBrushMeshStore()` for the sidecar. Defensible; revisit if a
    document needs multiple scenes.
  - **Needs visual QA:** create a brush, save, reload — geometry should round-trip.
- 2026-06-15 — **Phase 2b (slice 1): mesh-edge rendering + verb invocation. 899 green,
  editor launches.** The verbs are now usable end-to-end and the true geometry renders:
  - **Wireframe + selection renderers draw the actual mesh** (every face loop, world-
    transformed) via `AppendBrushMesh`, instead of a bounding box — so extruded/clipped/
    face-deleted brushes render correctly. (`AppendBrush` stays only for the create-drag
    preview box.)
  - **`EditBrushMeshCommand`** (before/after `BrushMesh`) — the undoable wrapper for every
    mesh verb.
  - **Inspector "Brush Tools" section** (editor-owned, for the editor-only `BrushComponent`):
    Extrude / Delete Face (by face index) and Clip X/Y/Z, each running the real `BrushOps`
    verb through `EditBrushMeshCommand`. This exercises the whole chain — create box →
    extrude/clip/delete → render → undo → save/reload — i.e. the S4a gate's behavior.
  - **Interim, not final UX:** face selection is an index slider, not click-to-select; clip
    is axis-aligned through the local origin, not an interactive plane. The box face *handles*
    (resize/move drag) still operate on the box-derived state and are correct only for box
    brushes — once a brush is non-box, edit it via Brush Tools, not the drag handles.
  - **Needs visual QA:** create a brush → Brush Tools → Extrude/Clip/Delete → confirm it
    renders, undoes, and survives save/reload.
- **Remaining (Phase 2b, slice 2 — proper interaction):** general mesh face descriptors;
  ray picking vs. mesh triangles; click-to-select body/face/edge/vertex (`SelectableRef`
  already carries the element id); face/edge/vertex *handles* driving the verbs (drag-extrude,
  interactive clip plane); retire the box-axis face system. This is the polished UX over the
  now-working verb substrate.

## 7. Risks & mitigations

- **Robustness of mesh ops** (extrude/clip on awkward input) is the classic ProBuilder-class
  hazard. Mitigation: pure functions with a thorough unit corpus (degenerate, near-coplanar,
  thin, concave cases); `ValidateAndRepair` as a hard gate; ops reject rather than emit
  broken geometry.
- **Planarity drift** of polygon faces after edits. Mitigation: re-fit/split in repair; cook
  triangulates per-face anyway.
- **Selection-id stability** across edits (a face index changes when topology changes).
  Mitigation: selection is resolved within an edit session; after a topology-changing op,
  re-resolve selection to the nearest surviving element (Hammer behavior). Do not persist
  raw face indices across structural edits as if stable.
- **Scope creep toward full DCC modeling.** The verb set is deliberately crude (Hammer-level).
  Anything past extrude/delete/clip(/carve-stretch) is out of scope; record triggers, don't
  build.
- **Open meshes from face-delete** complicate the cook. Mitigation: cook policy for open
  meshes defined in 05- (cap, or emit single-sided with a warning); authoring allows them.
