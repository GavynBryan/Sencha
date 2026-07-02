# Sencha Engine Roadmap

Status: standing master plan (2026-07-02). Supersedes `docs/plans/real-engine-roadmap.md`;
that plan's phase spine (0 through 4) is absorbed here. Its Phase 0 (the SDK boundary)
and most of Phase 1 (the editor product loop) are recorded as shipped; Phases 2 through 4
map into the v1.0 tracks below. Companion execution docs remain authoritative for their
own detail (see Section 12).

Audience: whoever implements any item here (human or agent), and reviewers. This document
owns versions, gates, and sequencing for the whole engine. Execution detail lives in the
specialist docs it references.

---

## 0. Thesis

Sencha v1.0 is the release where an indie team ships a real 3D action-adventure game on
installed Sencha binaries and never touches engine source. Measured against shipping the
same game on a general-purpose commercial engine, the reasons to choose Sencha are the
workflow: the brush-to-cook-to-play loop, gameplay authored as data over uniform systems,
scripting for content logic, and streaming that does not hitch. The bar is that a team
nine months into a comparable engine sees v1.0 and regrets not waiting.

Sencha v2.0 is the release where a small studio ships a bigger and prettier game and
iterates fast enough that the tooling disappears from their critical path.

Sencha v3.0 is the release that is credible for a first-party, large-scale, open-field
action-adventure production: the streaming, rendering, animation, and editor stability
hold up at content-team scale.

Two internal target games are the forcing functions: Loss Function (an FPS-Metroidvania)
and a 3rd-person Zelda-like. Per CLAUDE.md directive 1, those names and genres appear in
planning prose only. Every item below is a shape-neutral mechanism the two games
configure with data; the genre is never a type.

---

## 1. Invariants

The CLAUDE.md prime directives bind every item in this plan: name mechanisms, never
intents; requirements are user-facing, not engineering directives; behavior comes from
data, not branches; earn every abstraction. So do the concurrency rules (two lanes, no
third), the determinism rules (serial equals parallel where it matters), and the layering
rules (editor and cook code never link into the runtime).

Carried forward from the superseded roadmap, still load-bearing:

- **The engine, editor, and runtime are a versioned binary product. A game developer
  never rebuilds them.** The published contract is the module ABI (the fingerprint
  handshake) plus the cooked-content formats. Anything that would force a game developer
  to recompile the engine to change gameplay is a defect.

One invariant this document adds:

- **Every roadmap item names the seam or code it builds on.** An item that names no
  existing seam is a design task, not a build task, and is labeled as such. This is the
  containment for the vocabulary-vs-code problem in Section 3.

---

## 2. Where we are (the honest starting line)

Verified against the tree at the time of writing, not against older docs' claims.

Done and load-bearing:

- ECS: archetype SoA storage in 16KB chunks, `CommandBuffer` structural changes,
  `Changed<T>` chunk-conservative change detection, cached queries, `ComponentTraits`
  lifecycle hooks, generational entity ids, roughly 852 green tests.
- Concurrency: `JobSystem`/`ThreadPoolJobSystem` fork-join plus `AsyncTaskQueue`,
  with `worker_count == 0` as the deterministic reference path.
- Frame hosting: `FrameDriver` with the fixed ten-phase frame, `EngineSchedule`
  topologically ordered systems, fixed-tick simulation with presentation-only wall time.
- Renderer: Vulkan forward pass for static meshes, `.smat`/`.stex` materials and
  textures, point lights (max 64, unshadowed), extraction by copy behind the
  render-domain vs `graphics/vulkan` split.
- World: `ZoneRuntime` over per-zone registries, `AsyncZoneLoader` publish-by-handoff
  streaming, `ZoneParticipation` compiled to Visible/Physics/Logic/Audio frame spans.
- Assets: `IAssetLoader` staged-load contract, content-hashed `CookedCacheIndex`,
  `.cooked/` overlay, source importers, asset hot reload for materials and textures.
- SDK boundary: engine as a shared library, `install()`/`find_package(Sencha)`,
  `sencha_game_module()`, the ABI fingerprint handshake, the out-of-tree `template/`
  project. (The superseded roadmap's Phase 0, shipped.)
- Reflection: module-stable `ComponentTypeId` and `TypeSchema`/`Field` metadata shared
  by serialization, the inspector, and the module boundary.
- Physics: Jolt behind a PIMPL firewall (`engine/src/physics/`), `PhysicsScene` per
  registry, `CharacterControllerSystem`, `PhysicsQueries` (raycast/shapecast/overlap),
  `ZoneCollisionLoader`, enforced by the physics-isolation fitness test.
- Gameplay framework: AbilityKit (gameplay tags, attributes, effects, abilities as POD
  components plus data plus uniform systems), the data-driven movement stack
  (`MovementProfile`, ground/air locomotion, jump execution, `MovementIntent`), and
  `CameraRig` with first-person/third-person/fixed as data.
- Editors: kyusu, chakin, and kettle over `editor_common` (docking UI, themes, keymap
  rebinding, `CommandStack` undo, reflection-driven inspector that picks up game-module
  components, out-of-process PIE spawning the real `app` host, material hot reload).
  The brush half-edge kernel with pure tested `BrushOps` verbs including `CarveFaceRect`,
  four deep tools (Select, Brush, EdgeCut, FaceCarve), four-way viewports, gizmos with
  working-grid frames, and the level cook clustering brushes into per-cell static meshes.
  (Most of the superseded roadmap's Phase 1, shipped.)
- Fitness functions: layering, module ABI, physics isolation, framework isolation,
  mesh-edit dependency, and UI color checks exist as ctests and scripts.

Absent. This list is the roadmap's backlog:

- Animation runtime: clips and skeletons load, but nothing samples, blends, or poses.
  Skinned meshes cannot be drawn at all (no `SkinnedMeshComponent`, no GPU skinning).
- Input action mapping: raw `InputFrame` only, no data-driven bindings.
- Game-facing UI/HUD: ImGui is debug and editor only.
- Navigation: none. AI: none. Save games: none. Localization: none.
- Rendering: no shadows, no transparency (opaque fallback with a warning), no
  post-processing (phase reserved, empty), no particles, no decals, no skybox, no GI.
- Cinematics/sequencer: none.
- World partition metadata: no cells, adjacency, manifests, or streaming budgets beyond
  `AsyncCommitBudgetMs`.
- Scripting runtime: none (in scope for v1.0 by owner decision, Section 5 item 3).
- Shipping: no packaging step, no dev-vs-shipping build configurations, no incremental
  cook, binary cooked scenes blocked on the asset-handle codec.
- Editor: no prefab assets, no placement palette, no general asset browser, no
  collision or navigation view modes, no autosave, no game-module hot reload.
- Audio: no spatialization, no streamed music.
- Tooling: no memory tracking, no sampling profiler (a timing panel exists).
- CI: none. The fitness scripts exist and nothing runs them.
- Platforms: Linux plus SDL3 plus Vulkan only; no Windows bring-up yet, no headless
  render build.

---

## 3. Vocabulary vs code

Some terms used by CLAUDE.md and older docs read as if they name existing code. They do
not. This table is the containment: a reader must never mistake prose for substrate, and
an implementer must never "helpfully" build the named interface before its second
implementation exists. When a term lands as code, delete its row.

| Term | Appears in | Status today | Becomes code |
| --- | --- | --- | --- |
| `WorldPartitionRuntime` | CLAUDE.md, superseded roadmap | Stance only. No such type. `ZoneRuntime` and `AsyncZoneLoader` exist; no partition cells, adjacency, manifests, or budgets beyond `AsyncCommitBudgetMs`. | Track C, v1.0 |
| `IZonePopulationStrategy` | CLAUDE.md | Stance only. No interface, no strategy. Not to be built until a second population policy exists (directive 4). | Deferred, Section 11 |
| `IPoseModifier` | CLAUDE.md, superseded roadmap | Stance only. No animation runtime exists to modify poses. The superseded roadmap's "skinned mesh exists" claim was about asset data; nothing renders skinned meshes. | Track A, v1.0; seam added only with the second modifier |
| Navigation (area classification, cost biasing, hierarchical cross-zone planner) | CLAUDE.md directive 4 | A design decision on the record, zero code. | Track A, v1.0 core; planner v2.0 |
| Binary cooked scenes | core-systems-map.md | Codec seam exists (`SceneFieldCodec::IsText()`); the asset-handle binary path asserts unimplemented. | Track F, v1.0 |
| Transparency and post-processing phases | render pipeline | Reserved phase slots; transparency falls back to opaque with a warning, post is empty. | Track B, v1.0 |

---

## 4. The version arc and gates

Three versions, each with a demonstrable gate. Track detail follows in Sections 5
through 10; this section is the cut.

### v1.0: an indie team ships a real 3D action-adventure

The honest cut of what "ship a real game" requires. In: skinned animation, directional
shadows, transparency, skybox, minimal post (tonemap and exposure), CPU particles, HUD,
save games, input action mapping, in-zone navigation, a minimal AI substrate, spatial
audio and streamed music, streaming with budgets and state memory, the scripting runtime,
packaging for Linux and Windows, binary cooked content, CI with determinism gates, and
the designer editor loop (prefab assets, placement palette, asset browser, the first
batch of face-carve-class tools, collision and navigation view modes, play-from-here,
autosave, the validation dashboard).

Explicitly not v1.0, each recorded with a revisit trigger in Section 11: GI, decals,
cinematics, full localization (string tables only in v1.0), game-module hot reload, the
hierarchical cross-zone planner, the sampling profiler, a second RHI or platform beyond
Windows.

Gate: the FPS-Metroidvania vertical slice (three interconnected zones authored in kyusu
with brushes, prefabs, and volume brushes; a skinned animated player and enemies; enemies
that perceive, path, and attack through data-authored abilities; a HUD bound to
`AttributeSet` values; remappable input from data; script-driven entity behavior; save
and restore mid-progression with world state intact) is cooked, packaged, and completes a
scripted playthrough on clean Linux and Windows machines containing no engine source and
no editor, with zero missed fixed ticks during a declared traversal corridor, and CI is
green including the determinism gate.

### v2.0: a small studio ships bigger, prettier, faster

In: baked GI, decals, the full post stack, GPU particles, point and spot shadows,
cinematics/sequencer, visibility and vista proxies, the full earned set of participation
tiers, the hierarchical cross-zone planner, a grown AI substrate, full localization,
game-module hot reload, the sampling profiler, mature memory tracking, content version
migration with upgrade-on-load, deeper incremental cook, and the editor's partition map
view and sequencer timeline.

Gate: the 3rd-person target's slice (seamless indoor and outdoor traversal past vista
proxies, one data-authored cinematic, a second language selected by cvar, and a gameplay
change iterated via game-module hot reload without an editor restart) runs at the
configured frame budget as verified by the sampling profiler, and a v1.0-era cooked
project loads through migration with no authoring change.

### v3.0: credible for first-party scale

In: open-field cell streaming with HLOD and impostors, terrain, water, weather and
time-of-day as data, dynamic GI at scale, animation at production depth (full-body IK
layers through the earned pose-modifier seam), a second platform or RHI (trigger-driven,
Section 11), distributed cook, GPU profiling and frame capture tooling, and editor
stability at content-team scale (mass validation, soak-tested sessions, concurrent
authoring without data loss).

Gate shape (concrete numbers to be pinned when v2.0 ships): an open-field region of
stated size streams hundreds of cells through the traversal-hitch contract at 60 fps on
stated target hardware, authored by more than one person concurrently without data loss.

---

## 5. Track A: gameplay runtime

Each item states its mechanism, version, the seam it builds on, and its gate.

1. **Input action mapping (v1.0).** A data-driven binding asset maps device inputs to
   named actions with edge, held, and axis semantics, resolved at fixed-tick consumption.
   Builds on `input/InputFrame.h` (held state plus edge lists, edges preserved across
   zero-tick frames) with the editor's `keybinds.json` as the authoring precedent.
   Actions feed AbilityKit intents and `movement/MovementIntent.h`, so player input and
   AI share one activation path. Gate: the template game rebinds jump and fire from data
   with no recompile, and an input edge arriving on a zero-tick frame fires on the next
   fixed tick.

2. **Animation runtime (v1.0).** Clip sampling into pose buffers plus a data-authored
   blend/state graph asset (states, transitions, blend parameters), evaluated by an
   animation system in the fixed and post-fixed phases. Builds on `anim/AnimationClip.h`,
   `anim/Skeleton.h`, and their caches, which load today and are consumed by nothing.
   The `IPoseModifier` seam is added only when the second modifier exists (the graph
   output plus one procedural modifier such as look-at); until then the concrete system
   stands alone (directive 4). Blocked by Track B item 1 (skinned rendering). Gate: a
   data-authored locomotion graph drives a skinned character on a cooked level, and the
   serial and parallel paths produce bit-identical poses.

3. **Scripting runtime (v1.0; owner decision 2026-07-02, recorded in Section 11).** An
   embedded Lua-family VM hosted as an engine service. The design constraints that keep
   it inside the invariants:

   - Scripts are assets: they flow through the `IAssetLoader` staged-load contract and
     the cooked cache, and they hot-reload like materials do. Script iteration is the
     cheapest loop in the engine.
   - Scripts drive behavior only through existing seams: `CommandBuffer` for structural
     change, gameplay tags, AbilityKit definitions and intents, cvars, and component
     reads/writes through the reflection registry. No raw registry mutation surface, no
     parallel flag system.
   - Script-defined components register through the same `TypeSchema`/`ComponentTypeId`
     path as game-module components, so the kyusu inspector picks them up with zero
     editor changes.
   - Execution stays inside scheduled systems on the two existing lanes. A script
     callback is a system detail, not a third concurrency lane.
   - Determinism holds: no wall-clock time, no address-derived identity, no unseeded
     randomness exposed to scripts by default.

   Positioning: native game modules for systems-heavy code, scripts for entity behavior
   and content glue. Neither replaces the data path; a request that data vocabulary can
   express still lands as data. Guardrails ship as roadmap items alongside the runtime:
   a fitness ctest asserting the script API surface exposes only the seams above, and
   the determinism gate (Track E) extended over script-driven fixed-tick systems. The
   track's first task is a recorded design task: VM selection (recommendation: Lua
   through a minimal binding layer owned by the engine, no framework dependency).
   Gate: the template game's entity behavior is authored in a script asset, edits
   hot-reload in PIE without a restart, and the determinism gate passes with scripts
   in the loop.

4. **AbilityKit world sinks (v1.0).** Wire the framework's stubbed sinks to the world:
   `IImpulseSink` and `IHitQuery` to Jolt via `PhysicsQueries`, `IMontageSink` to the
   animation runtime. The framework itself is landed; this item is integration only.
   Gate: the Dash and Fireball fixtures from `docs/gameplay/abilitykit.md` Stage 6 run
   against real physics and animation.

5. **Navigation core (v1.0).** A navmesh cooked as a sibling artifact of the level cook,
   from the same cooked collision geometry `ZoneCollisionLoader` consumes. Area
   classification sourced from volume brushes and surface tags; cost biasing as data; a
   runtime path and nav-raycast query service. No authored NavPath primitive (the
   decision already on the record in CLAUDE.md). Gate: an agent paths around cooked
   level geometry, and an area cost change re-routes it with a data change only.

6. **Hierarchical cross-zone planner (v2.0).** The one greenlit irreducible abstraction:
   zone-graph planning over partition adjacency (Track C metadata), refined per zone by
   the navmesh query service. Pull-in trigger: the v1.0 slice needs an AI that paths
   across a zone boundary.

7. **AI substrate (v1.0 minimal, v2.0 grown).** A perception system (sight cones via
   `PhysicsQueries` raycasts, hearing from event queues) writes gameplay tags and facts;
   behavior selection is data (utility and consideration tables) producing AbilityKit
   intents. No behavior-tree object model and no per-entity behavior object, consistent
   with the posture recorded in `docs/gameplay/abilitykit.md`. Gate: an enemy authored
   entirely as data patrols, perceives the player, and attacks through an ability.

8. **Save system (v1.0).** Gameplay persistence as the zone state overlay (Track C
   stateful detach and `ZoneStateRecord`) plus a global record, serialized through the
   existing `TypeSchema` machinery, distinct from scene serialization. Gate: save
   mid-zone, kill the process, restore; opened doors and taken pickups stay opened and
   taken.

9. **Game UI/HUD (v1.0 HUD, v2.0 menus).** A runtime screen-space layer: quad and text
   batches through the renderer (reusing the Track B transparency pass), layout documents
   as data, values bound to component fields via the existing reflection. Explicitly not
   ImGui and not linked to debug UI. v1.0 ships HUD primitives; a full widget toolkit is
   deferred until menus demand it (directive 4). Gate: a HUD showing `AttributeSet`-bound
   health renders in a shipping-config build with debug UI compiled out.

10. **Audio spatialization and streamed music (v1.0).** A listener component and
    distance/pan attenuation in `AudioSystem` over the audio participation span; streamed
    long-form playback through the async lane. Builds on `AudioService` voices,
    `AudioClipCache`, and `CaptionRuntime`. Gate: a positioned emitter attenuates and
    pans as the listener moves, and a music stream plays without a resident full decode.

11. **Localization seam (v1.0 string tables, v2.0 full).** String-table assets keyed by
    a locale cvar; captions and the HUD read through the table. The full pipeline (font
    fallback, localized audio routing) lands in v2.0.

12. **Cinematics/sequencer (v2.0).** Keyed tracks over component fields, camera, and
    animation as a data asset, integrated with the Track C transition model. Deferral
    trigger in Section 11.

---

## 6. Track B: rendering

Ordering inside this track matters: item 1 blocks the animation runtime, which blocks
the 3rd-person target. Everything stays behind the existing render-domain vs
`graphics/vulkan` split (extraction copies, the renderer never reads live ECS); that
split is also what keeps the second-RHI decision cheap to revisit (Section 11).

1. **`SkinnedMeshComponent` plus GPU skinning (v1.0, first).** Skinning matrices from
   the animation pose buffer, palette upload, vertex-shader skinning. `MeshGeometry`
   already splits skinning streams from base vertex streams by design, so the mesh
   format seam exists; `render/skinned_mesh/` caches load the data today. Gate: a
   skinned character renders and animates in a cooked level.

2. **Shadows (v1.0 directional cascaded shadow maps; spot in v1.0 if the pass structure
   makes it incidental, point in v2.0).** A depth-pass feature in the existing
   render-feature structure; the `GpuLight` record already reserves the type enum and
   `ShadowIndex` field. Gate: sun shadows in the template game at a stated cascade
   configuration, toggled by data.

3. **Transparency pass (v1.0).** A sorted blended pass filling the reserved pipeline
   slot; retires the opaque-fallback warning in `MaterialAssetLoader`. Also the
   substrate for the HUD and particles.

4. **Skybox (v1.0).** A cubemap background pass.

5. **Post-processing (v1.0 minimal, v2.0 stack).** v1.0 fills the reserved post phase
   with tonemap and exposure. v2.0 grows the stack (bloom, SSAO, color grading) as
   data-ordered passes, one pipeline, features toggled by data.

6. **Particles (v1.0 CPU, v2.0 GPU).** v1.0: a CPU-simulated emitter component and a
   billboard batch through the transparency pass. v2.0: GPU simulation.

7. **Decals (v2.0).**

8. **GI (v2.0 baked, v3.0 dynamic at scale).** Explicitly not v1.0; hemispheric ambient
   remains the stand-in. Trigger in Section 11.

9. **Terrain, water, volumetrics, weather and time-of-day as data (v3.0).**

---

## 7. Track C: world at scale

This track absorbs `docs/action-adventure-core-runtime.md`; that document remains the
execution spec (its stages are referenced below), this roadmap assigns versions and
gates.

1. **Streaming metrics and risk records (v1.0).** `ZoneBudgetRecord`,
   `TraversalBudgetRecord`, `StreamingTelemetryRecord`, `ContentRiskRecord` and their
   JSON writer (spec stage 1). Records first, UI second: these feed the editor
   validation dashboard (Track D).

2. **Traversal-hitch harness (v1.0).** A headless scripted traversal replay asserting
   zero missed fixed ticks and no synchronous fallback loads (spec stage 2). Wired into
   CI as a gate (Track E).

3. **`WorldPartitionRuntime` (v1.0).** The metadata and policy layer over `ZoneRuntime`:
   cell identity, bounds, adjacency, traversal edges, and priority hints; it issues load
   and unload requests to `AsyncZoneLoader` and participation changes to the zone set
   (spec stage 3). This converts the CLAUDE.md stance term into code; delete its
   Section 3 row when it lands.

4. **Participation tiers (v1.0 core subset, v2.0 full).** Grow `ZoneParticipation` only
   into the tiers the two targets measurably need first (Dormant, PhysicsShell,
   LogicHot; VisibleProxy arrives with the v2.0 visibility work). Tiers compile down to
   the existing frame spans (spec stage 4). Each tier is earned from a measured need,
   never adopted wholesale from the candidate list.

5. **Stateful detach (v1.0).** `DetachZone` by ownership handoff, off-thread overlay
   serialization, `ZoneStateRecord` (spec stage 5). Blocks the save system (Track A).

6. **Transition model (v1.0).** Typed transition scopes replacing ad hoc discontinuity
   handling (spec stage 6). v2.0 cinematics ride on it.

7. **Visibility, portals, vista proxies (v2.0).** Spec stage 8, paired with the
   VisibleProxy tier.

8. **Content risk dashboard (v1.0 minimal in debug UI, v2.0 as an editor panel over the
   same records).** Spec stage 9.

9. **Open-field cell streaming, HLOD and impostors (v3.0).**

---

## 8. Track D: editor and designer empathy

Two halves: the product loop, then the designer tool suite. The editor hardening backlog
(`docs/architecture/hardening-and-consolidation.md`, workstreams W1 through W6) is
absorbed here as v1.0 items with that document as the execution spec.

### 8a. Product loop

1. **Prefab assets (v1.0).** Entity templates as first-class cooked assets with
   instanced spawning and per-instance overrides. The deferral trigger recorded in
   `docs/plans/sencha-level-editor/00-overview.md` ("the first content need for a
   shared, overridable instance") has fired: placement UX needs it. Builds on the
   reflection and serialization path and the brush-instancing precedent.

2. **Placement palette and drag-place (v1.0).** A palette panel of prefabs and assets;
   drag into the viewport with surface snap via the picking service and the working-grid
   frames. Retires hierarchy-add-at-origin as the only placement path.

3. **General asset browser (v1.0).** Thumbnails and search over `AssetRegistry`;
   `MaterialLibrary` is the recorded seed. Requires a thumbnail render path (the feature
   dependency recorded in `docs/plans/sencha-level-editor/10-editor-ui-look-and-feel.md`).

4. **Collision and navigation view modes (v1.0).** Viewport overlays rendering cooked
   collision and the cooked navmesh through the consolidated editor line batch
   (hardening W5).

5. **Play-from-here (v1.0).** A PIE spawn override argument passed to the out-of-process
   `app` host.

6. **Autosave and crash recovery (v1.0).** Timed document snapshots plus a recovery
   prompt on next open.

7. **Content validation dashboard (v1.0 minimal).** An editor panel consuming Track C
   `ContentRiskRecord`s at save and cook time.

8. **Traversal probe overlays (v1.0).** Tool 9 in the suite below.

9. **Game-module hot reload (v2.0).** The recorded trigger ("gameplay-iteration pain on
   a real title") is expected to fire while dogfooding the two targets; honor it in
   v2.0. Script hot reload (Track A item 3) covers most iteration in v1.0.

10. **Sequencer timeline panel (v2.0), partition map view (v2.0), mass validation and
    concurrent-authoring stability (v3.0).**

11. **Editor hardening (v1.0).** W1 tier 2 (POD component descriptors, engine-owned
    serializer vtables), W2 (headless gizmo and picking math tests), W3 (whole-event
    routing), W4 (the unified live-edit transaction), W5 (consolidations), W6 (fitness
    suite growth), per the hardening doc.

### 8b. The face-carve-class tool suite

`FaceCarveTool` is the pattern: a tool born from a common level-design need (draw a
rectangle on a wall, decide it is a doorway, extrude), built as a pure tested `BrushOps`
verb (`CarveFaceRect` over `RectFaceFrame`) wrapped in an undoable command, restricted to
an honest domain, validated by `BrushValidateAndRepair`. Ten more tools in that mold,
mechanically named. Batch 1 (tools 1, 2, 3, 5, 8, 9, 10) is v1.0; batch 2 (tools 4, 6, 7)
is v2.0. Every batch-2 tool states its restricted domain up front, the way
`CarveFaceRect` restricts itself to flat rectangular quads.

1. **`LoftSteps` (v1.0).** Stairs and ramps from a dragged rect plus rise-run data.
   Pattern: vertical traversal is the most common blockout task. Mechanism: a
   construction verb beside `MakeBox`/`MakeCylinder` taking step parameters (rise-run or
   count, steps or ramp) as data; a wedge primitive falls out for free. The default rise
   reads the project's `MovementProfile` step height through reflection, so stairs are
   climbable by construction.

2. **`PierceFaceRect` (v1.0).** Through-openings: doorways and windows. Mechanism: a
   composition verb applying `CarveFaceRect` to the picked face and the matching
   projected rect on the opposite parallel face, `DeleteFace` on both inset rects, and
   four bridging interior wall quads inheriting the host `FaceMaterial`. Domain:
   rectangular-quad opposite-face pairs.

3. **`FrameFaceRect` (v1.0).** Opening framing: door and window trim, recessed panels,
   inset fixtures. Mechanism: two concentric `CarveFaceRect` calls; the ring between
   them becomes its own face set carrying a designer-picked material; an optional
   `ExtrudeFace` of the ring gives raised or recessed trim.

4. **`ChamferEdgeLoop` (v2.0).** Ledge and trim strips along edge loops. Mechanism:
   `TraceEdgeLoop` (exists) supplies the loop; a new verb replaces each loop edge with a
   flat strip of stated width carrying its own material. Domain restricted to convex
   quad-face loops in v1 of the verb.

5. **`HollowBrush` (v1.0).** A room shell from a solid: block a room as one box, hollow
   it to walls of stated thickness. Mechanism: a pure one-to-many function producing
   wall slabs via per-face inward `Clip`; because it is one-mesh-in, many-meshes-out, it
   lives in a `BrushCompose` family beside `BrushOps`, and the document-level command
   mints the sibling brush entities under one undo step.

6. **`MirrorBrush` and radial array (v2.0).** Symmetric arenas and radial rooms.
   Mechanism: a pure `Mirror(mesh, plane)` verb (reflect positions, reverse windings,
   re-project UVs); array and radial duplication are document-level commands over brush
   instances through `CommandStack`.

7. **`AlignFacePlane` and `FitBrushToGrid` (v2.0).** Modular alignment so corridors and
   rooms meet flush and the per-cell cook welds cleanly. Mechanism: set a face coplanar
   to a picked reference face on another brush (eyedropper interaction); quantize face
   planes to the working grid (`GridSettings` is the single grid source).

8. **`FaceUvHotspotFit` (v1.0).** Trim-sheet and hotspot texture alignment, the largest
   texture-workflow accelerator in brush pipelines. Mechanism: a hotspot atlas (named
   rects with physical sizes) as sidecar data on the `.smat`; a pure function over
   `FaceMaterial` plus `RectFaceFrame` picks the best-fitting hotspot for the face's
   physical dimensions and writes the projection UV scale, offset, and rotation. It is a
   face-material op, not a topology op, so it lives in a `FaceMaterialOps` sibling with
   the same pure-tested discipline, plus a small `.smat` cook extension for the atlas.

9. **`TraversalProbeOverlay` (v1.0).** Answers "can the player make this jump, fit
   through this gap, climb this ledge" while authoring instead of in playtest. Mechanism:
   an editor overlay reading `MovementProfile`, `CharacterController`, and jump data from
   the loaded game module via the existing reflection; pure headless-tested math (jump
   arc from gravity and jump velocity and run speed, clearance boxes, step-height
   gauges) rendered through the consolidated editor line batch; probes placeable on faces
   and edges. Not a `BrushOps` verb, and correctly so: it follows the hardening W2
   discipline (pure math decoupled from the GUI, tested headless). The highest
   empathy-per-effort item in the suite.

10. **`BrushRole` volumes (v1.0).** Triggers, kill volumes, reverb zones, streaming hint
    volumes, and navigation area paint. Mechanism: a role field (data) on the brush
    entity: Solid (today's path), Volume (cooks to a convex physics sensor plus a volume
    component, no render mesh), AreaTag (contributes area classification to the navmesh
    cook). One cook pipeline, behavior selected by data (directive 3). Feeds Track A
    navigation and Track C streaming directly.

---

## 9. Track E: performance and determinism

1. **CI (v1.0, immediate, the first item in this entire roadmap).** A workflow that
   builds, runs ctest (llvmpipe for render-dependent tests), and runs the fitness
   scripts that today nothing executes, on a Linux and Windows matrix. Every gate in
   this document is prose until this exists.

2. **Determinism gates in CI (v1.0).** Serial (`worker_count == 0`) vs parallel
   state-hash comparison over the fixed-tick systems, extended over script-driven
   systems when Track A item 3 lands. Extends the existing fitness ctest family.

3. **Traversal-hitch harness as a CI gate (v1.0).** Track C item 2, wired in.

4. **Frame timing capture and export (v1.0); sampling profiler (v2.0).** v1.0
   serializes `TimingHistory` data per run for regression comparison. v2.0 adds a
   sampling profiler with flame output.

5. **Memory tracking (v1.0 counters per system and asset type, v2.0 tagged
   allocations).**

6. **Chunk-parallel queries (profile-gated, not version-scheduled).** They stay behind
   the roughly 1ms profile gate from CLAUDE.md. Revisit trigger: the profiler shows a
   single system exceeding the gate. Measurement drives this, not the calendar.

7. **GPU profiling integration and frame capture tooling (v3.0).**

---

## 10. Track F: product and shipping

1. **Windows platform bring-up (v1.0).** Build system, platform glue, and the CI matrix
   entry. Explicitly not a new RHI: SDL3 and Vulkan already run on Windows; the work is
   toolchain, filesystem and process details, and packaging. Gate: the full test suite
   and the template game pass on the Windows CI leg.

2. **Packaging (v1.0).** A bundle step producing app plus game module plus cooked
   content (no editor, no cook code) for Linux and Windows, and dev vs shipping build
   configurations (shipping strips `SENCHA_ENABLE_COOK`, the dev console, and asserts).
   Gate: the packaged directory runs standalone on a clean machine of each platform.

3. **Binary cooked scenes (v1.0).** Finish the asset-handle codec behind the existing
   `SceneFieldCodec::IsText()` seam; cooked content flips to binary, authoring stays
   JSON. Gate: a cooked binary scene round-trips and loads identically to its JSON twin.

4. **Incremental cook (v1.0 minimal, v3.0 distributed).** A dependency graph over
   sources so only affected artifacts re-cook; `CookedCacheIndex` is the seed.

5. **Graceful degradation (v1.0).** Missing, malformed, or stale cooked content logs,
   substitutes a placeholder, and never crashes the runtime.

6. **Content versioning (v1.0 stamps plus refuse-with-message, v2.0 upgrade-on-load
   migration).**

7. **Module ABI tier 2 (v1.0).** POD component descriptors plus C-ABI callbacks; the
   engine owns all serializer vtables (hardening W1). A changed serializer interface can
   then never break a shipped module.

8. **External-team documentation (v1.0).** SDK getting-started, component and script
   authoring, cook and packaging guides, versioned with the SDK install.

9. **SDK compatibility policy (v2.0).** A stable module ABI across minor engine
   versions, enforced by the fingerprint fitness test.

---

## 11. Recorded decisions and deferrals

The repo's deferral pattern: every deferral records the concrete trigger that revives it.

- **Scripting: in v1.0, by owner decision (2026-07-02).** This supersedes the earlier
  no-VM stance. The tension with directive 3 (a VM is a second behavior-entry channel
  beside the data path) is real and is contained structurally: the script API exposes
  only existing seams (Track A item 3), a fitness test enforces that surface, and
  requests expressible as data vocabulary still land as data. Scripts are for entity
  behavior and content glue; native modules remain the home of systems-heavy code.
- **Second RHI and further platforms: single Vulkan backend through v2.0, Windows and
  Linux via that backend.** The render-domain vs `graphics/vulkan` split already
  isolates the backend, so the option stays cheap without building a speculative
  abstraction now (directive 4). Revisit trigger: the first shipping target (console or
  platform deal) that cannot run this backend. Sub-item that may pull in earlier: a
  headless or null-render build if llvmpipe proves insufficient for CI.
- **macOS: out of scope.** Trigger: a shipping target that requires it (implies the
  second-RHI trigger via a Metal path or a translation layer decision).
- **`IZonePopulationStrategy`: not until a second population policy exists.**
  `WorldPartitionRuntime` ships with one concrete policy.
- **`IPoseModifier`: not until the second modifier exists.** The state graph plus one
  procedural modifier earn the seam.
- **GI: v2.0 baked.** Trigger to accelerate: the 3rd-person target's look development
  demands bounce light.
- **Cinematics: v2.0.** Trigger: the first authored scene that cannot be expressed as
  gameplay.
- **Game-module hot reload: v2.0.** Trigger already recorded ("gameplay-iteration pain
  on a real title"); script hot reload covers most v1.0 iteration.
- **Chunk-parallel queries: profile-gated** (Track E item 6), never speculative.
- **Participation tiers: earned per tier**, never adopted wholesale from the candidate
  list in the streaming spec.
- **Widget toolkit: deferred until menus demand it.** v1.0 HUD is bound primitives.

---

## 12. Relationship to other plan documents

This roadmap owns versions, gates, and sequencing. Execution detail stays in the
specialist docs. Single source of truth is structural: if a specialist doc and this one
disagree on version or priority, this one wins; if they disagree on mechanism detail,
the specialist doc wins.

| Doc | Relationship |
| --- | --- |
| `docs/action-adventure-core-runtime.md` | Execution spec for Track C; its stages map to Track C items as listed. |
| `docs/gameplay/abilitykit.md` | Execution spec for Track A items 1, 4, and 7. |
| `docs/architecture/hardening-and-consolidation.md` | Execution spec for Track D item 11 and Track F item 7. |
| `docs/plans/sencha-level-editor/*` | Shipped-branch record plus execution detail for the editor substrate Track D builds on. |
| `docs/assets/pipeline.md` | Execution record and deferral register for the asset pipeline items in Track F. |
| `docs/core-systems-map.md` | Reader's map of the current tree; not a plan. |

---

## 13. Sequencing rationale

CI comes first in absolute terms: it gates nothing functionally and everything
procedurally, it is entirely absent, and every determinism and fitness claim in this
document is unenforced prose until it exists.

The critical path to v1.0 runs through skinned rendering, because it blocks the
animation runtime, which blocks both the 3rd-person target and the AbilityKit montage
sink. Input action mapping lands early because it blocks AbilityKit intents, which block
the AI substrate. The scripting runtime lands early enough to be load-bearing in the
v1.0 gate. Everything else orders by the edges below.

Dependency edges:

- CI blocks the determinism gates, the traversal harness as a gate, and honest fitness
  enforcement.
- Skinned mesh rendering blocks the animation runtime, which blocks `IMontageSink`
  wiring, which blocks combat feel, which blocks the 3rd-person target.
- Input action mapping blocks AbilityKit intents, which block the AI substrate (one
  shared intent path).
- The level cook's collision output (exists) blocks the navmesh cook, which blocks
  navigation, which blocks AI movement.
- Partition metadata blocks participation tiers, which block stateful detach, which
  blocks the save system.
- Stateful detach also blocks the world-state half of the v1.0 gate.
- Prefab assets block the placement palette, which blocks the designer authoring loop.
- The transparency pass blocks the HUD and particles.
- The transition model blocks cinematics (v2.0).
- `ContentRiskRecord`s (runtime) block the editor validation dashboard (records first,
  UI second).
- The binary handle codec blocks binary cooked scenes, which block packaging at
  shippable size.
- The scripting runtime blocks the script-driven behavior clause of the v1.0 gate.

---

## 14. Rules for this document

- No em dashes, here or in anything this document causes to be written.
- Mechanism names, never intents or genre words, in any identifier this document
  proposes. The target games appear in prose only.
- Every item names the seam or code it builds on; an item that cannot is a design task
  and says so.
- Every gate is an objective, demonstrable condition.
- Every deferral records its revisit trigger.
- When a stance term becomes code, delete its Section 3 row.
- When a gate is met, mark the item shipped with the date. Do not delete it; this
  document is also the record.
