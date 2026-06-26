# Select Tool v2 — Manipulator Framework, Box Select, Bounds Handles

Status: plan (not yet implemented). Successor to the Phase A–D mesh-edit work.

## Context

Phases A–D delivered: a generic mesh-edit subsystem (`editor/meshedit/`), an
`IMeshEditTarget` seam (brush target today, static mesh proven target-only in
`07-static-mesh-edit-target-sketch.md`), per-mode picking, ordered multi-select,
the `IGizmo`/`TranslateGizmo` + `MeshEditSession`, and a dependency-direction
ctest guarding the layering.

What's missing / clunky, in the user's words: selection is click-only (no visible
box select), brushes can't be box-resized in object mode, and the visuals are
"ugly and clunky" — manipulators are world-scaled (not screen-constant), have no
hover feedback, and can be occluded by geometry.

This plan adds **visible marquee selection**, **ortho-view AABB resize handles**,
and a **visual pass** — but routes them through a small **manipulator framework**
refactor, because adding them ad-hoc would ossify the editor's input/draw topology
around translate-only assumptions.

Source 2 Hammer informs the UX (not Source 1): one Selection Tool whose
*granularity* is a mode (vertices/edges/faces/objects, cycled with Space, jumped
with number keys); marquee box-select in every view with modifier add/remove; a
bounding box with corner/edge **handles** for resize in the ortho views and an
**axis gizmo** in the perspective view. References:
- https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Hammer_Overview
- https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Basic_Construction/Mesh_Editing_1

## Guiding principle

Optimize for *cheap change later*, not just cheap addition now. The editor's
manipulation has three orthogonal axes of change:

1. **Manipulators** — translate, bounds-resize, rotate, scale, clip.
2. **Verbs** — extrude, delete, split, resize-face, …
3. **Edit targets** — brush, static mesh, …

Adding along any axis must be **one new file with zero edits to the others.**
Verbs (one `MeshEditService` method) and targets (`IMeshEditTarget`) already meet
this. Manipulators do not — `IGizmo` is routed bespoke and only models a rigid
`GizmoDelta` (T/R/S), which bounds-resize and clip do not fit. This plan makes the
manipulator a first-class seam so the remaining axis becomes additive too. Doing
it now is cheap (only translate exists); doing it after rotate/scale/clip is not.

## Goals

1. Visible marquee/box selection in all viewports, per element mode, with
   modifier add/remove, undoable.
2. Ortho-view AABB resize handles ("white corner squares") for object-mode brush
   selection; perspective keeps the axis gizmo.
3. A manipulator framework (`IManipulator` + `ManipulationSink`) that makes new
   manipulators additive and collapses scene mutation to one seam.
4. Consolidate duplicated viewport projection into one place.
5. A visual language: screen-constant sizing, hover highlight, draw-on-top,
   consistent colors.
6. Source 2-style mode hotkeys (Space cycle, 1–4 direct).

## Non-goals

1. Rotate / Scale / Clip manipulators (the framework must make them additive, but
   they are not built here).
2. Shear, group selection, double-click-all-faces / Alt-coplanar.
3. Renaming `BrushMesh` → `EditMesh` (still deferred; see 07).
4. A real static-mesh edit target (still target-only future work).

## Collateral effects on topology & dependency graph

Deliberately surfaced, because the point of the refactor is to *reduce* coupling,
not add it:

- **Consolidation (fewer edges):** three copies of `BuildRay`/`ProjectToScreen`
  (Picking, `TranslateGizmo`, the deleted `BrushBodyHandle`) collapse into one
  `editor/viewport/ViewportProjection`. Picking, manipulators, and the renderer
  depend on that one unit instead of each carrying their own.
- **Scene mutation collapses to one seam:** today the translate handlers in
  `editor/level/editmodes/MeshEditSession.cpp` touch `LevelScene` directly.
  After this, all manipulator preview/commit goes through `ManipulationSink`,
  implemented once in `editor/level` (`BrushManipulationSink`). `editor/editmodes`
  gains *no* new scene dependency — it depends only on the sink interface.
- **The session becomes generic.** `MeshEditSession` (currently in
  `editor/level/editmodes`, touching `LevelScene`) becomes a generic
  `ManipulatorSession` in `editor/editmodes` that owns an ordered
  `std::vector<std::unique_ptr<IManipulator>>` + a `ManipulationSink&`. The brush
  knowledge moves entirely into the sink.
- **The renderer stops knowing manipulator shapes.** `SelectionRenderer` already
  draws the gizmo via `AppendGeometry`; it generalizes to draw whatever the active
  manipulators emit plus the marquee rect — adding a manipulator never edits it.
- **`IGizmo` is superseded** by `IManipulator`. `TranslateGizmo` is ported to it
  (its `GizmoDelta`+handler become an internal detail of transform manipulators,
  not the session-facing contract).
- **Dependency rule strengthened** (new ctest assertions): `editor/editmodes`
  must not include `LevelScene`/`LevelDocument`; the only implementor of
  `ManipulationSink` lives under `editor/level`.

## Target dependency graph (updated)

```text
editor/level/brush/        pure kernel (BrushMesh, BrushOps, validation)
        ^
editor/meshedit/           MeshElements, MeshEditService, IMeshEditTarget,
                           MeshElementKind, ManipulationSink (interface),
                           IManipulator (interface). No scene/UI/render/viewport.
        ^
editor/viewport/           ViewportProjection (ray/screen/pixel math), Picking,
                           EditorViewport. (projection used by all overlay code)
        ^
editor/editmodes/          ManipulatorSession, TranslateManipulator,
                           BoundsManipulator, MarqueeInteraction. Generic:
                           depend on meshedit + viewport + projection + the sink
                           INTERFACE. No LevelScene.
        ^
editor/level/              BrushManipulationSink (the sink impl), BrushEditTarget,
                           LevelScene, commands, SelectTool.
        ^
editor/ui/  editor/render/ MeshEditPanel, ViewportPanel, Selection/overlay
                           renderer. Consume the above; never call BrushOps.
```

Note: `ManipulationSink` and `IManipulator` interfaces sit at the `meshedit`
altitude (generic), so both `editmodes` (consumers) and `editor/level` (the impl)
can depend on them without an upward edge.

## Architecture

### ViewportProjection (consolidation, do first)

`editor/viewport/ViewportProjection.{h,cpp}` — one home for the math currently
duplicated:

```cpp
struct ViewportProjection {
    explicit ViewportProjection(const EditorViewport& viewport);
    Ray3d                RayThroughPixel(ImVec2 px) const;
    std::optional<ImVec2> WorldToPixel(Vec3d world) const;   // nullopt if behind
    float                WorldSizeForPixels(Vec3d at, float pixels) const; // screen-constant
    // ...region/grid passthroughs as needed
};
```

Picking, manipulators, and the renderer all use it. Kills the 4th copy this spike
would otherwise add.

### IManipulator framework

```cpp
struct ManipulatorContext {
    const SelectionSnapshot& Selection;
    MeshElementKind          Mode;
    IMeshEditTarget&         Target;   // resolve mesh/transform/bounds (generic)
    ManipulationSink&        Sink;     // preview + commit (generic)
};

struct ManipulatorVisual { std::vector<GizmoLine> Lines; /* + handle quads */ };

struct IManipulator {
    virtual bool AppliesTo(const ManipulatorContext&, const EditorViewport&) const = 0;
    virtual void BuildVisual(const ManipulatorContext&, const EditorViewport&,
                             ManipulatorVisual& out) const = 0;
    virtual int  HitTest(const ManipulatorContext&, const EditorViewport&, ImVec2 px) const = 0;
    virtual std::unique_ptr<IInteraction> BeginDrag(
        int part, const ManipulatorContext&, const EditorViewport&, ImVec2 px) const = 0;
    virtual ~IManipulator() = default;
};
```

`AppliesTo` is what lets BoundsManipulator say "ortho views, object mode, brush
selected" and TranslateManipulator say "any view, any non-empty selection" without
the session hardcoding either. The session routes pointer-down to the first
applicable manipulator whose `HitTest` hits.

### ManipulationSink — the single scene-mutation seam

```cpp
struct ManipulationSink {
    // Live preview during a drag (no command):
    virtual void PreviewTransform(EntityId, const Transform3f&) = 0;
    virtual void PreviewMesh(EntityId, const BrushMesh&) = 0;
    virtual void Revert(EntityId) = 0;                  // restore pre-drag state
    // Commit one undoable command at drag end:
    virtual void CommitTransform(EntityId, Transform3f before, Transform3f after) = 0;
    virtual void CommitMesh(EntityId, BrushMesh before, BrushMesh after) = 0;
    virtual ~ManipulationSink() = default;
};
```

`BrushManipulationSink` (editor/level) implements this against `LevelScene` +
`MoveEntityCommand`/`EditBrushMeshCommand`. This is the *only* class in the
manipulation path that knows `LevelScene`. Manipulators and the session never do.

### Overlay model & single overlay renderer

`SelectionRenderer` becomes the editor overlay renderer. Each frame it draws:
1. selection highlights (from `SelectionService` + `MeshElements`, as today),
2. the active manipulators' `BuildVisual` output (it asks the session),
3. the marquee rect (screen-space) if a marquee is active.

It gains a **second line pipeline variant with depth-test off** for draw-on-top
manipulators/handles. Adding a manipulator never edits this file.

### Marquee selection

`MarqueeInteraction` (editmodes) drives a transient `MarqueeState { active,
startPx, curPx, viewportId }` (a small value the renderer reads to draw the rect).
On release it calls `PickingService::PickInRect(viewport, rect, scene, mode)` →
refs, applies modifier semantics (**Shift = add, Ctrl = remove**, none = replace),
and commits one `SelectCommand`. `SelectTool` starts the marquee when pointer-down
hits no manipulator. Gather rule: entities by projected-bounds overlap; faces,
edges, vertices by projected center inside the rect.

### Bounds resize manipulator

`BoundsManipulator` (editmodes). `AppliesTo` = ortho view + object mode + a
resolvable mesh. `BuildVisual` projects the world AABB and emits the box plus 8
screen-constant white handle quads (4 corners + 4 edge mids). `HitTest` picks a
handle by pixel distance. `BeginDrag` returns an interaction that, per move:
computes new world bounds (drag axis/axes only, opposite handle anchored,
grid-snapped, min-thickness clamp), asks `MeshEditService::ResizeBounds(baseMesh,
transform, oldBounds, newBounds)` for the resized mesh, and calls
`Sink.PreviewMesh`; on release `Sink.CommitMesh`. `ResizeBounds` is a new pure
`MeshEditService` op (scale local verts about the anchor; validate). Rotated
brushes: handles operate in world axes, baked back to local — may shear; clamp and
accept for the common axis-aligned case.

### Visual language

`editor/EditorTheme.h` constants: axis X=red/Y=green/Z=blue, selection=amber,
hover=white, handle=white square + thin dark outline; pixel sizes for handles
(~8px), vertex dots (~6px), gizmo length/arrowheads. All sizing via
`ViewportProjection::WorldSizeForPixels` (screen-constant). Hover: manipulators
take the current hovered part in `BuildVisual` to brighten it. Vertex highlight
changes from 0.05-world crosses to screen-constant squares. Object selection draws
a clean amber AABB instead of the full wireframe.

### Hotkeys

Add to `ShortcutRegistry`: `1/2/3/4` → Object/Vertex/Edge/Face, `Space` cycles
(guard against camera/UI conflicts). Keep `Shift+V`.

## Dependency rules (additions to the ctest)

Extend `scripts/check_meshedit_deps.sh`:
- `editor/editmodes` must not include `LevelScene`/`LevelDocument`.
- The only `: public ManipulationSink` / sink implementation lives under
  `editor/level`.
- Existing rules unchanged (no BrushOps in ui/viewport/render/editmodes; no
  scene/UI/render deps in meshedit).

## Phasing (each phase builds, tests, and keeps the editor usable)

- **P1 — Projection + framework seam (no visible change).** Add
  `ViewportProjection`; repoint Picking + TranslateGizmo to it. Introduce
  `IManipulator`, `ManipulatorContext`, `ManipulationSink`. Port `TranslateGizmo`
  → `TranslateManipulator`; implement `BrushManipulationSink`; turn
  `MeshEditSession` into the generic `ManipulatorSession` holding a manipulator
  list. Behavior identical; proves the seam. Dependency ctest extended and green.
- **P2 — Marquee selection.** `MarqueeInteraction` + `MarqueeState`,
  `PickingService::PickInRect`, renderer draws the rect, `SelectTool` starts it,
  modifier semantics, undoable. Headless tests for `PickInRect`.
- **P3 — Bounds handles.** `MeshEditService::ResizeBounds` (+ tests),
  `BoundsManipulator`, ortho-only via `AppliesTo`. Drive-test resize.
- **P4 — Visual pass + hotkeys.** `EditorTheme`, screen-constant everything, hover,
  draw-on-top pipeline, cleaner object highlight; Space/number-key modes.

## Verification

- **Headless:** `ViewportProjection` round-trips (world→pixel→ray); `PickInRect`
  gather counts for object/face/edge/vertex over a known brush; `ResizeBounds`
  vertex math + validation + min-thickness clamp; marquee rect normalization.
- **Dependency ctest:** extended rules green.
- **Drive-test (flagged, GUI-only):** marquee feel + modifiers; ortho corner/edge
  resize with snapping; gizmo + handles screen-constant and hover-highlighted;
  manipulators draw on top; mode hotkeys.

## Risks

- **Reworking `IGizmo` so soon.** Mitigation: it's young (only translate), and the
  port is mechanical; P1 is behavior-preserving and test/ctest-gated.
- **Draw-on-top pipeline.** New depth-off line pipeline variant — small, known.
- **Sink preview lifetime.** Preview mutates live scene state; `Revert`/cancel must
  restore pre-drag state (mirror current handler Cancel). Covered by the sink API.
- **Bounds resize on rotated brushes** can shear; clamp + accept for axis-aligned,
  document. A true OBB resize is future work.
- **Marquee state ownership.** Kept a tiny single-writer value (`MarqueeState`);
  if it grows, fold into an explicit overlay-state object.

## Done definition

1. Marquee box-select works in all views, per mode, Shift-add/Ctrl-remove,
   undoable.
2. Object-mode brushes show ortho corner/edge handles that resize (grid-snapped,
   clamped, undoable); perspective shows the gizmo.
3. Manipulators are added by implementing `IManipulator` only — session, renderer,
   picking, and sink unchanged (demonstrated by translate + bounds being the only
   two, with rotate/scale/clip clearly additive).
4. All manipulator scene mutation flows through `ManipulationSink`; `editor/editmodes`
   has no `LevelScene` dependency (ctest-enforced).
5. Projection math exists once (`ViewportProjection`).
6. Manipulators/handles/vertex-dots are screen-constant, hover-highlighted, and
   draw on top; colors come from `EditorTheme`.
7. Mode hotkeys (Space, 1–4) work alongside Shift+V.
8. Headless tests + dependency ctest green; drive-test checklist passes.

## Deferred / future (now additive, by design)

Rotate / Scale / Clip manipulators (each: one `IManipulator` file), group
selection, double-click-all-faces / Alt-coplanar, OBB (rotated) bounds resize,
`StaticMeshEditTarget` (a second `IMeshEditTarget` + sink reuse), `BrushMesh` →
`EditMesh` rename.
```
