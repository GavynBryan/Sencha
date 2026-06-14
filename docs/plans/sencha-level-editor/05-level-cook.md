# Pillar 4 — Level Cook: Brushes → Static Mesh, PIE, and No-Garbage Guarantees

**Status**: Working plan (2026-06-14). Retargets and absorbs the original
`editor-brush-geometry-cook.md` (now superseded), corrected for editable-mesh brushes
(03-), material-aware sections (04-), and the module topology (02-).
**Owns Stages S6 (offline cook) and S7 (PIE).**

> Original-plan corrections baked in here: **one mesh per level with one section per
> material** (not "one section, MaterialSlot 0"); **UVs baked from projections** (04-);
> **`AssetSourceKind::Generated` is mandatory, not optional**, because "no garbage" is a
> stated requirement and a distinct, prune-owned source kind is what makes it auditable.

---

## 1. The authored ↔ cooked boundary (sacred)

- **Authored** (repo, editor reads/writes): level `.json` = transforms + `BrushComponent`
  (a `BrushId`) + the `brush_meshes` sidecar (mesh geometry + per-face `FaceMaterial`/
  `UvProjection`, 03-/04-) + game components (02-). Human-edited, VCS-diffable.
- **Cooked** (derived, under `.cooked/`, never committed): a stamped scene JSON with
  `StaticMeshComponent`s (no brush anything), one or more generated `.smesh` artifacts, the
  zone manifest, AssetId-map entries.
- The editor **never** writes `StaticMeshComponent` for world brushes during authoring. The
  cook is the *only* emitter. Brushes are never a runtime concept (the runtime has no
  `BrushComponent` registered — and after 02-, the editor's brush types aren't even in the
  runtime host's module set).

This mirrors the existing authored-glTF→cooked-.smesh model; here the "source" is the level
JSON + brush sidecar.

---

## 2. The bake: `BakeBrushesToStaticMesh` (pure)

```cpp
// engine/include/assets/cook/BrushGeometryCook.h  (SENCHA_ENABLE_COOK; pure)
#include <render/static_mesh/StaticMeshData.h>

// A POD copy of one brush's cookable geometry — decoupled from editor types so the engine
// cook lib never depends on editor/. The editor (PIE) and the offline cook both fill this.
struct CookBrush {
    std::vector<Vec3d>          Vertices;         // brush-local
    Transform3f                 WorldTransform;   // entity transform
    std::vector<CookBrushFace>  Faces;            // loop indices + material AssetRef + UvProjection
};

// Produces ONE StaticMeshData: faces across all brushes triangulated, grouped into one
// StaticMeshSection per distinct material AssetRef. UVs baked by evaluating each face's
// projection; tangents generated (MikkTSpace, the existing cook path). Bounds computed.
StaticMeshData BakeBrushesToStaticMesh(std::span<const CookBrush> brushes,
                                       std::span<const AssetRef> materialOrder /*slot order*/);
```

Steps (pure; only the writer touches disk):
1. Collect brushes (cook: from authored JSON + sidecar; PIE: from the live `BrushMeshStore`).
2. For each face: triangulate the (planar) loop; transform vertices brush-local → world (or
   keep a level root — §5); evaluate `ProjectUv` per vertex for baked UVs (04-§5).
3. **Group faces by material `AssetRef`** → one `StaticMeshSection` per material, `MaterialSlot`
   = the section ordinal. The level's distinct materials define the slot order.
4. Generate tangents from baked UVs (MikkTSpace; de-index/re-weld like the glTF path,
   pipeline.md Stage 4c).
5. Output `StaticMeshData` (the shared `MeshGeometry` core + sections + bounds).

Open meshes (from face-delete, 03-): policy = emit the existing faces single-sided with a cook
warning (designer chose to delete the face); do **not** auto-cap silently. Recorded; revisit
if content wants auto-cap.

This helper is the single source of truth used by **both** PIE and the offline cook — the
"one path, two schedulings" discipline (pipeline.md Decision C).

---

## 3. Generated mesh identity (answers "do we mint assets every cook?")

**No — by construction.**

- Virtual path is a function of **level identity**, not the cook invocation or content hash:
  `levels/hub_room.json` → `asset://levels/hub_room.geometry.smesh`. Stable across cooks.
- Physical artifact: `<assets-root>/.cooked/levels/hub_room.geometry.smesh`.
- The `CookedCache` source key is the **level source path**; freshness uses a **brush-geometry
  hash** (hash of the brush sidecar + transforms + per-face material/UV + level bake settings)
  so unrelated edits to game components don't force a re-bake, and a real geometry change does.
- Re-bake **overwrites the same artifact** and updates the same cache entry; the AssetId map
  gives the same id for the same stable path. No path churn, no id churn.

Rejected (as in the original): per-brush meshes, UUID/hash-named meshes. One mesh per level
(N sections) is the playable-milestone shape; splitting into multiple meshes for
streaming/visibility is a later, justified move.

---

## 4. `AssetSourceKind::Generated` — mandatory

The generated level mesh registers with **`AssetSourceKind::Generated`**, not `File`. Today
`Generated` is an enum value with a stubbed load path
([AssetSystem](../../engine/src/assets/) — the stub the pipeline doc flags). We implement it:

- A `Generated` static-mesh record carries the `.cooked/...` physical path; its loader reads
  bytes from there exactly like `File` (delegate to the byte source, pipeline.md Decision I).
  The *only* behavioral difference from `File` is **provenance**: a `Generated` asset is
  *owned by a level source* and is the prune pass's responsibility (§6).
- Why mandatory and not "path of least resistance = File" (the original's hedge): the stated
  requirement is **no garbage assets, ever**. A distinct source kind makes "this mesh was
  generated from level X and dies with it" a queryable fact instead of a convention. It is a
  few lines (delegate-to-file load) and it is the right shape. We pay it.

---

## 5. Scene rewrite (what the runtime sees)

The cook transforms the authored scene into a cooked scene:
1. Remove every brush entity / strip `BrushComponent`; drop the `brush_meshes` sidecar.
2. Emit **one consolidated entity** with `LocalTransform` (identity or a level root) +
   `StaticMeshComponent { mesh = asset://levels/<stem>.geometry.smesh, material = <slot 0> }`.
   Multi-section meshes carry their per-section materials via the mesh's slots; the component's
   material set references the level's materials in slot order.
3. **Game components pass through unchanged** (player start, player controller, etc. — they are
   already plain engine/game components; the cook does not touch them). This is why the cooked
   scene "just works" in the host with the game module loaded.
4. Run normal scene asset collection + AssetId stamping + manifest emission. **The manifest
   walk must include the brush-sidecar material refs** (04-§4) so the cooked level preloads
   every face material — even though those refs lived in the sidecar, not a component field.
5. Record source→artifacts in `CookedCacheIndex` (source = level path).

Cooked scene shape:
```json
{ "entities": [
  { "components": {
      "local_transform": { ... },
      "static_mesh": { "mesh": {"id":"...","path":"asset://levels/hub_room.geometry.smesh"},
                       "materials": [ {"id":"...","path":"asset://materials/dev/gray.smat"},
                                      {"id":"...","path":"asset://materials/dev/brick.smat"} ] } } },
  { "components": { "local_transform": {...}, "acme.player_controller": { ... } } }
] }
```
No `brush` key anywhere; no brush sidecar.

---

## 6. No garbage: the prune guarantee (answers "does stale geometry linger?")

Without action a deleted `levels/old.json` leaves `.cooked/levels/old.geometry.smesh` + an
orphan cache entry + an id-map entry. We close it:

- **Source-keyed cache is the single source of truth**: every generated artifact is recorded
  under its level source path in `CookedCacheIndex`.
- **`PruneOrphanedGeneratedArtifacts(...)`** (new): walk the cache; for any entry whose source
  file no longer exists (or no longer contains brushes), delete the `Generated` artifact files
  and remove the entry. Because the kind is `Generated`, the prune knows exactly which records
  it owns and may delete — it never touches authored or `File`-imported assets.
- Exposed in: dev `ImportAssetsOnDemand` (optional strict mode), the batch cook tool, and a
  standalone `sencha-cook --prune` / `validate-cooked-cache`.
- **AssetId hygiene**: a `Generated` id whose level source disappeared is reported removable
  (the id map's `pathIsLive` predicate, already present in the proto-cook, extended to know
  `Generated` ids are reapable, unlike authored ids which we keep for rename history).
- `.cooked/` stays gitignored, derived, reproducible from a clean checkout + cook.

Net: delete a level + run cook/prune → its generated geometry, cache entry, and id are gone.
Audited by a test (§9).

---

## 7. Play-In-Editor (PIE) — in-memory, zero disk

The iteration path; does not require an offline cook.

- The editor builds `CookBrush`es from the live `BrushMeshStore` + registry, calls
  `BakeBrushesToStaticMesh`, and registers the result **transiently** via the existing
  procedural/`Generated` registration in `AssetSystem`/`StaticMeshCache` — no persistent
  `AssetRecord`, no `.cooked/` write.
- It constructs a runtime-ready scene fragment: the consolidated `StaticMeshComponent` entity
  + the game components (player start, player controller) instantiated from the level. The
  **game module's systems** (loaded per 02-) drive gameplay — this is the one place the editor
  ticks game logic, sandboxed to the PIE session.
- It loads/attaches this into an **editor preview zone** reusing the normal zone path as much
  as possible.
- On Stop: release transient handles; destroy the preview zone. No disk touched. Works with
  `SENCHA_ENABLE_COOK=OFF`.

Success: edit a brush, hit Play, see solid lit geometry in the real renderer, driven by the
real game player controller, zero asset disk I/O.

---

## 8. Asset bake-out (brush → first-class static-mesh asset)

A distinct operation from the per-level cook (different lifecycle, recorded for completeness;
gate it in S8 polish): a designer selects brush(es) and **bakes them into a committed
`.smesh` asset** (optionally exporting to glTF/`.blend` for DCC round-trips). Unlike the
level cook's `Generated` per-level mesh, a bake-out produces an **authored, first-class,
instanceable** static mesh that is severed from the brush — this is *how* level geometry
becomes instanceable (brushes themselves can't be instanced by design, 03-).

- Reuse `BakeBrushesToStaticMesh` → `MeshSerializer::WriteToFile` to a chosen authored path.
- glTF/`.blend` export reuses the cook's mesh export inverse (a new exporter; the import path
  exists, the export path is new) — gate on real need; the `.smesh` bake-out is the must-have.
- After bake-out the designer may delete the source brushes and place the new mesh as a normal
  `StaticMeshComponent` (which *can* be instanced) — the "static meshes are viable level
  geometry too" future the product owner named.

---

## 9. Stages & gates

**S6 — offline cook:**
- `BrushGeometryCook` (pure bake, sections-per-material, baked UVs, tangents); `CookBrush` POD.
- `AssetSourceKind::Generated` load path implemented.
- Level cook module: authored JSON + sidecar → cooked scene + `.smesh` + manifest (with
  sidecar material refs) + AssetId stamping; `CookedCache` participation (source = level,
  brush-geometry hash).
- `PruneOrphanedGeneratedArtifacts`.
- *Gate:* cook a 3–5-brush textured level → cooked scene + `.smesh`; **LevelDemo host (no
  editor symbols) loads and renders it** correctly with per-face materials. Re-cook unchanged
  → cache hit, **no new ids/paths**. Delete the level + prune → artifact + cache entry + id
  gone. Pinned by tests: pure geometry (vert/index counts, bounds, winding, tangent
  handedness), serializer round-trip, cook integration (memory writer + fake cache asserting
  artifacts + cooked-scene shape + section-per-material), prune removes orphan.

**S7 — PIE:**
- In-memory bake + transient registration + preview zone + game-module systems driving play.
- *Gate:* from an editor brush level, Play → visible solid lit geometry driven by the game's
  player controller; Stop releases cleanly; **zero `.cooked/` writes** (asserted); runs with
  `SENCHA_ENABLE_COOK=OFF`.

---

## 10. File/module changes (approximate)

**Engine (under `SENCHA_ENABLE_COOK`, pure where possible):**
- `engine/include/assets/cook/BrushGeometryCook.{h,cpp}` — `CookBrush`, `BakeBrushesToStaticMesh`.
- `engine/.../assets/cook/LevelCook.{h,cpp}` — authored level → cooked scene + manifest +
  cache participation; or extend the proto-cook (`GenerateCubeDemoAssets` lineage) into a
  shared `CookScene` lib.
- `AssetSystem` — implement the `Generated` static-mesh load branch.
- `assets/cook/Prune*.{h,cpp}` — `PruneOrphanedGeneratedArtifacts`.
- Consider `render/static_mesh/StaticMeshPrimitives` — a shared `BuildTransformedPolygon`/quad
  emitter to avoid duplicating triangulation.

**Editor:**
- `editor/level/PieSession.{h,cpp}` (or similar) — build `CookBrush`es, transient bake, preview
  zone, Play/Stop, game-module system driving.
- UI: "Play"/"Stop", "Cook Level", "Bake Selection to Mesh", default-material setting.

**Tests:** `test/core/BrushGeometryCookTests.cpp` (+ integration in runtime/engine_feature).
All cook code builds with `SENCHA_ENABLE_COOK=OFF` (gated) and links clean in the editor when ON.

---

## 11. Open questions & future hooks

- Generated-mesh naming (`*.geometry.smesh` vs `*.world.smesh`) — pick `*.geometry.smesh`,
  minor.
- One mesh vs. brush-groups/layers → separate sections/meshes for visibility/streaming — defer
  until a use case (the model already supports sections).
- **Collision** from brushes (pipeline.md Decision O) — same cook step, different output
  artifact type; the playable-milestone collision question is escalated in 00-§8.
- Hot-reload of generated level geometry — a watcher on the level file could trigger targeted
  re-bake + slot-swap (pipeline.md Decision H mechanism). Nice, not required this branch.
- AssetId behavior on **level rename** — stable path means a rename produces a "new" asset and
  the old id goes dead; prune + id-map maintenance document and handle it.
