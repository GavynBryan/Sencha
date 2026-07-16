# World Graph Authoring and Compilation: Zone AABBs, Docks, and Kyusu Affordances

Status: implemented corrective replacement (2026-07-15). This document records
the repository-grounded execution plan and acceptance contract used by the
implementation.

Canonical: this document and `11-zone-runtime-model.md` supersede every earlier
Zone Shape, exact containment, inferred contact, and spatial-compilation plan.
Document 11 owns runtime behavior. This document owns repository findings,
authored structures, Kyusu architecture, lifecycle, migration, validation,
tests, staged implementation, deletions, and non-goals.

The review target was the actual worktree on `agent/world-graphs-and-docks`,
including its then-uncommitted additions. Existing code was preserved only when
it supported the corrected model.

---

## 1. Repository-grounded findings

### 1.1 Working infrastructure that should remain

| Area | Current repository evidence | Decision |
| --- | --- | --- |
| Zone bounds | `editor/kyusu/src/document/ZoneBounds.cpp` has `ComputeZoneBounds`, which unions `EditorScene::TryGetWorldBounds`; the pre-branch `ZoneHeader::Bounds`/`BoundsOverridden` path refreshed non-overridden open zones and preserved explicit boxes. | Restore this derived-by-default plus explicit-override model. Keep one valid cached/default AABB when a zone is empty or closed. |
| Radius streaming | `engine/src/zone/ZoneDemand.cpp` measures closest-point distance to each zone AABB. | Preserve behavior; rename `BroadBounds` back to `Bounds`. |
| Graph policy | `GraphRecord`, `GraphStreamingConfig`, graph-local demand, cross-graph seeding, demand reasons, and endpoint indexes are implemented across `WorldPartitionManifest`, `ZoneDemand`, `WorldPartitionIndex`, and runtime tests. | Keep and adjust only shape/bounds references. |
| Transform gizmos | `ManipulatorSession` registers translate, rotate, scale, and bounds manipulators. Translate/rotate already work for any selected entity with a transform through `ManipulationSink`; rotation snap uses the shared grid-enabled path. | Reuse translate/rotate for Dock origin/orientation. Do not create Dock transform code. Disable entity scale for Docks; width/height are component values. |
| Interaction lifecycle | `InteractionHost` and `ViewportToolDispatcher` route pointer capture, preview on move, commit on release, and cancel on Escape/focus loss. `BrushManipulationSink` demonstrates live preview followed by one command. | Reuse the lifecycle through generic value edit bindings. |
| Snapping | `GridSettings`, `EditorViewport::GetGrid`, `GridPlane`, and `GizmoMath` already provide position/frame snapping; `BoundsManipulator` has reusable face-drag and minimum-thickness math. | Extract the AABB math from its brush-specific resolve/apply path; add only a bounded-rectangle equivalent. |
| Rendering | `EditorWideLinePipeline`, `EditorFillPipeline`, and `LabelRequest` already cover wide lines, translucent triangles, and world-space text. | Build affordance output for these pipelines; do not add a Dock renderer. |
| Selection | `SelectionService`, `SelectableRef`, `SelectCommand`, hierarchy selection, and Graph Viewer edge selection already use registry/entity identity. Viewport picking currently considers brush geometry only. | Add generic editor pick proxies so non-brush authoring entities can be selected normally. |
| Commands/snapshots | `RawComponentEditCommand`, `ValueCommand`, `EntitySnapshot`, `CreateSnapshotEntityCommand`, `DuplicateEntitiesCommand`, and delete/restore commands already provide undo and serializer-driven persistence. | Reuse them. Add a narrow duplicate-remap hook because logical ids are not entity ids. |
| Schemas/serialization | `TypeSchema`, `RuntimeFields`, `IComponentSerializer`, `EngineSceneComponents`, and `SceneSerializer` already serialize/inspect registered components without editor type switches. `Aabb3d` schema support was added on this branch. | Keep the AABB schema and `WorldDock` component serialization. Do not put Kyusu callbacks into the runtime serializer ABI. |
| Generic inspector | `InspectorPanel` renders every registered component from flattened schema fields and commits byte snapshots. It has no custom-section registry; strong ids are intentionally read-only and strings/enums are limited. | Add a type-keyed editor inspector adapter with schema fallback. Remove the separate Dock panel. |
| Static component visuals | `ComponentVisualRenderer` consumes `ComponentEditorVisual` mesh hints generically (currently Camera). | Keep it for static icons. It is insufficient for data-driven plane/AABB overlays and manipulation. |
| World organization | The branch implements `GraphViewerPanel` and removes the old World-panel Connections list. Graph nodes already use `BroadBounds.Center()` and edges retain distinct ids. | Keep, change to `Bounds.Center()`, and retain one edge per `DockId`/`LinkId`. |

`EditorScene::TryGetWorldBounds` currently returns bounds for live or baked brush
geometry, not arbitrary point components. The first correction must preserve that
known derivation rule. Expanding what counts as boundable content is a separate
decision and must not be hidden in this work.

### 1.2 Historical branch additions from the rejected Zone Shape design

The following were implemented in the reviewed worktree and were deleted rather
than retained merely because they existed:

- `engine/include/zone/ZoneShape.h` and `engine/src/zone/ZoneShape.cpp` add
  `ConvexPrism`, `AuthoredZoneShape`, exact containment/interpenetration,
  canonicalization, broad-bound derivation, and shape hashing.
- `ZoneHeader` now stores `Shape`, `BroadBounds`, `BroadBoundsOverridden`, and
  `CookedShapeHash`; v2 manifest JSON stores `shape.cells`, `broad_bounds`, and
  `shape_hash`.
- `WorldCook.cpp` validates/canonicalizes shapes and emits a cooked shape hash.
- `WorldPartitionValidation.cpp` rejects invalid cells, broad/shape mismatch,
  and shape overlap.
- `ZoneDemand.cpp`, `WorldPartitionRuntime.cpp`, `WorldDocument.cpp`,
  `DockPanel.cpp`, and `EditorEntityRecipe.cpp` combine broad AABB rejection with
  exact-shape tests.
- `ZoneShapePanel.*` implements footprint drawing, vertex editing, vertical
  ranges, adjacent cells, height duplication, merge, and X split.
- `ZoneBoundsRenderer.cpp` renders prism edges.
- `ZoneShapeTests.cpp` and multiple manifest/document/validation/cook tests make
  L halls, T junctions, diagonals, stacked cells, exact overlap, and stable shape
  hashes contractual.

All of that exact-shape surface is obsolete.

### 1.3 Branch Dock work: useful pieces and gaps found during review

Useful implemented pieces:

- `WorldConnectionComponents.h` defines `WorldDock`, `WorldLink`, and
  `DockGateBinding` and registers their schemas.
- `WorldTopologyCook.*` emits reciprocal Dock/Link endpoints and deterministic
  debug maps.
- `DockCrossing.*` performs swept bounded-plane crossings with direction and
  residency checks.
- `EditorEntityRecipe.*` creates snapshot-based Dock/Link entities and separates
  explicit creation from snapshot restoration.
- `GraphViewerPanel.*` represents one entity/id as one edge and keeps parallel
  edges distinct.
- legacy Transition retirement/migration, graph terminology, and graph-local
  streaming changes are substantially implemented.

Gaps or incorrect ownership identified before implementation:

- `ZoneBoundsRenderer.cpp` explicitly queries `WorldDock` and draws its plane,
  normal, and arm boxes. It is a hard-coded Dock renderer.
- `DockPanel.*` is a dedicated component editor with exact-shape probing and
  Dock-specific command logic outside the generic inspector.
- no Dock viewport handle exists; dimensions and arm boxes are numeric panel
  edits only;
- no world-space Side A/Side B labels, direction decoration, invalid-assignment
  decoration, or translucent plane fill exists;
- viewport picking cannot select a Dock because `PickingService` only intersects
  brush geometry;
- the current `BoundsManipulator` is reusable geometrically but resolves and
  writes only brush meshes;
- `EditorEntityRecipeRegistry` exists, but current callers instantiate
  `WorldDockRecipe`/`WorldLinkRecipe` directly instead of using the registry;
- duplication copies `DockId` byte-for-byte, producing duplicate ids; there is
  no entity copy/paste feature and no logical-id remap seam;
- raw component add correctly uses C++ defaults and does not run a recipe, but a
  manually added Dock has an invalid zero id and needs an explicit repair action;
- the current cook expands a rotated local arm AABB to a world AABB, losing its
  authored orientation;
- `Transform3f::Forward()` is local `-Z`, while current Dock arm defaults assume
  local `+Z` is A-to-B. The corrected contract below resolves that ambiguity.

### 1.4 What existed only in plans at review time

The previous plan described generic creation registration, direct Dock viewport
editing, labels/arrows, reusable side-AABB handles, and normal duplicate/paste
semantics. Only snapshot recipes are partially present. There is no component
affordance registry, dynamic overlay provider, pick-proxy provider, custom
inspector registry, generic rectangle handle, or component-value manipulation
transaction in the tree today.

---

## 2. Corrected authored and cooked data model

### 2.1 Zone AABB

The single zone metadata value is:

```cpp
struct ZoneHeader
{
    ZoneId      Id;
    std::string Name;
    GraphId     Graph;
    std::string SceneRef;
    Aabb3d      Bounds;
    bool        BoundsOverridden = false;

    // Cooked-only fields in the current combined header type.
    std::string CookedSceneRef;
    std::string CookedCollisionRef;
    uint64_t    CookedContentHash = 0;
    std::vector<DockEndpoint> Docks;
    std::vector<LinkEndpoint> Links;
};
```

There is no shape field or shape hash. `Bounds` is always a world-space AABB.
It is used only for:

- spatial-radius demand;
- Graph Viewer node placement at `Bounds.Center()`;
- coarse spawn/teleport/save/recovery/diagnostic queries;
- editor framing, labels, selection, and bounds display;
- initial Dock-side suggestions;
- other explicitly coarse world queries.

Derived mode and override mode are both required because they already solve two
real repository cases: ordinary zones follow brush content automatically, while
empty zones, intentionally generous recovery volumes, and cell-like zones need
stable manual bounds. A manual-only model would discard working derivation; a
derived-only model would make coarse recovery and radius policy hostage to
renderable geometry.

Rules:

1. New zones receive a valid starter `Bounds` and `BoundsOverridden=false`.
2. For an open non-overridden zone, `ComputeZoneBounds` refreshes the cached
   value when at least one boundable entity exists.
3. No boundable content means retain the last valid value.
4. Explicit editing sets `BoundsOverridden=true` in the same command.
5. `Use Derived Bounds` clears the flag and recomputes immediately when possible.
6. Save refreshes open derived zones before writing the manifest.
7. Cook validates and copies the saved AABB; it emits no exact geometry product.

### 2.2 WorldDock

The persistent scene component remains compact:

```cpp
struct WorldDock
{
    DockId       Id;
    ZoneId       ZoneA;
    ZoneId       ZoneB;
    Vec2d        HalfExtents{1.0f, 1.5f};
    Aabb3d       SideAArmBounds{{-1,-1.5f,-2}, {1,1.5f,0}};
    Aabb3d       SideBArmBounds{{-1,-1.5f, 0}, {1,1.5f,2}};
    uint32_t     Directions = DockDirectionBoth;
    int32_t      PreloadPriority = 0;
    int32_t      PreloadDepth = 0;
    WorldDemandCondition DemandCondition;
};
```

The entity's `LocalTransform` supplies origin and rotation. Scale must be exactly
one; the adapter disables scale manipulation and validation rejects non-unit
scale. Width and height live only in `HalfExtents`.

Coordinate convention:

- local +X is plane right;
- local +Y is plane up;
- local +Z is the authored normal from Zone A to Zone B;
- because Sencha's `Transform3f::Forward()` is local -Z, the Dock normal is
  `-transform.Forward()`;
- Side A arm bounds lie entirely at local `z <= 0`;
- Side B arm bounds lie entirely at local `z >= 0`.

This matches the component defaults and makes the visible normal unambiguous.
The recipe must rotate local +Z, not `Forward()`, onto a picked normal.

### 2.3 Cooked Dock endpoints

Use the exact `DockEndpoint` contract in document 11. In particular, keep the
arm AABB in an endpoint-local oriented frame rather than converting it to a
world AABB.

Cook A's endpoint with the authored frame and Side A box. Cook B's endpoint by
rotating the endpoint frame 180 degrees around local up (`Right=-A.Right`,
`Up=A.Up`, `Normal=-A.Normal`) and reflecting Side B's local X/Z bounds into
that frame. Both endpoint arm boxes then lie on local `z <= 0`, so runtime uses
one owner-to-other crossing algorithm.

The cook emits exactly two endpoint records with the same `DockId`; it does not
duplicate or reinterpret one Dock as two graph edges.

### 2.4 Editor-only data

No editor data is serialized. The proposed affordance output is rebuilt from
the selected document and live components each frame:

```cpp
struct ViewportAffordanceOutput
{
    std::vector<EditorLineSegment> Lines;
    std::vector<EditorLineVertex>  FillTriangles;
    std::vector<LabelRequest>      Labels;
    std::vector<EditorPickProxy>   PickProxies;
    std::vector<AabbEditTarget>    Aabbs;
    std::vector<RectEditTarget>    Rectangles;
};
```

These are transient Kyusu values. There is no serialized handle id, snap value,
label, color, hover, selected sub-target, adapter name, or validation decoration.

---

## 3. Narrow Kyusu affordance architecture

### 3.1 Why a Kyusu adapter registry fits this repository

Do not extend `IComponentSerializer` with ImGui, viewport, command, or world
document callbacks. That interface is engine/game-module serialization ABI and
already has a narrow static-mesh visual hint. Dynamic component editing needs
Kyusu services, selection, commands, validation, and per-frame transforms.

Add a process-local Kyusu registry keyed by the existing stable
`ComponentTypeId`:

```cpp
class IEditorComponentAdapter
{
public:
    virtual ComponentTypeId Type() const = 0;
    virtual void BuildViewport(
        const EditorComponentContext&, ViewportAffordanceOutput&) const = 0;

    // Return true when the adapter replaces the flat schema body.
    virtual bool DrawInspector(EditorComponentInspectorContext&) const
    { return false; }

    virtual ~IEditorComponentAdapter() = default;
};
```

`EditorComponentContext` provides the entity, registry/document, transform,
selection state, viewport, grid, validation records, and read-only world lookup.
It provides no runtime service. `EditorComponentInspectorContext` provides the
selected component, command stack, and the same document/world lookups.

Registration occurs in `EditorServices` beside other editor feature assembly.
The generic renderer, picking service, manipulator session, and inspector query
the registry by type id. None names or switches on `WorldDock`.

This is deliberately not a plugin ABI or declarative gizmo language. It has one
lookup key, one viewport output, and one optional inspector override.

### 3.2 Reusable presentation primitives

Use existing rendering types directly:

- lines and arrow shafts -> `EditorWideLinePipeline`;
- translucent plane/AABB faces -> `EditorFillPipeline`;
- world labels -> existing `LabelRequest` projection in `ViewportPanel`;
- arrowheads -> a small geometry helper that emits lines/triangles;
- invalid/warning state -> adapter-selected colors and labels based on normal
  `ContentRiskRecord` values;
- picking -> simple bounded rectangle/box/segment proxies carrying the owning
  entity `SelectableRef` and a hit distance.

`AffordanceRenderer` submits those arrays. It knows primitive types, depth/on-top
flags, and selection tint; it knows no component semantics.

`PickingService` merges affordance candidates with existing brush candidates and
returns the nearest candidate under the current pick priority. Marquee uses the
projected proxy bounds. A Dock is therefore selected with the same
`SelectionService`/`SelectCommand` path as every entity. The hierarchy and Graph
Viewer keep working without a second selection model.

### 3.3 Reusable manipulation targets

Do not make `BoundsManipulator` understand components. Extract its pure face
handle construction, hit testing, axis drag, snap, and minimum-thickness code,
then apply it to a target supplied by a provider:

```cpp
struct AabbEditTarget
{
    AffordanceKey Key;       // stable only for the live interaction
    Transform3f   LocalToWorld;
    Aabb3d        Value;
    IValueEdit<Aabb3d>* Edit;
};

struct RectEditTarget
{
    AffordanceKey Key;
    Transform3f   LocalToWorld;
    Vec2d         HalfExtents;
    IValueEdit<Vec2d>* Edit;
};

template <typename T>
struct IValueEdit
{
    virtual void Preview(const T&) = 0;
    virtual void Commit(const T&) = 0;
    virtual void Cancel() = 0;
};
```

The actual implementation may use value-owned callbacks rather than virtual
objects, but it must preserve this ownership: generic handles calculate values;
provider-created bindings decide what value is being edited.

`AffordanceManipulator` is one additional `IManipulator` registered in
`ManipulatorSession`. In Resize mode it asks the affordance service for targets,
draws/hit-tests them, and starts the normal interaction lifecycle. The session's
effective-resize query includes either a brush target or an affordance target.

For component values, a generic component edit binding:

1. captures the entire component byte snapshot on pointer down;
2. writes preview bytes live without dirtying or recording undo;
3. restores the original bytes on cancel;
4. commits one `RawComponentEditCommand` on release;
5. triggers the document's normal content-edited notification so world
   validation reruns on execute, undo, and redo.

For zone metadata, a zone-bounds binding previews the manifest AABB and commits
one `SetZoneBoundsCommand` carrying `{Bounds, BoundsOverridden}` before/after.
This proves the same AABB handles against a second current authoring consumer,
not a hypothetical future volume.

### 3.4 Shared snapping

All handles read the same `GridSettings` and viewport grid frame:

- entity position: existing Translate manipulator absolute grid snap;
- entity rotation: existing Rotate manipulator snap when grid snapping is on;
- plane width/height: snap each moved rectangle edge in Dock-local X/Y to grid
  spacing anchored at local zero, while enforcing a positive minimum full size;
- side AABBs: snap the moved local face coordinate to the same spacing anchored
  at local zero, then enforce minimum thickness and side-of-plane constraints;
- Zone override AABB: snap world/grid-frame face coordinates through the same
  extracted AABB kernel.

There is one snap toggle/spacing source. `WorldDock` stores no snap settings.

### 3.5 Inspector integration

`InspectorPanel` first looks for a registered adapter by the serializer's
`TypeId`. If its `DrawInspector` returns true, that body replaces the flat field
rows; otherwise the current schema inspector remains unchanged.

The `WorldDock` adapter owns zone-name combos, direction names, plane dimensions,
arm bounds, demand fields, validation evidence, active-handle selection, and
semantic buttons. It uses normal generic commands/edit bindings. The Inspector
does not name `WorldDock`.

Delete the standalone `DockPanel`. Keep `ComponentVisualRenderer` for simple
mesh hints such as Camera; do not force dynamic Dock geometry into
`ComponentEditorVisual`.

### 3.6 Explicit second consumer and generalization limit

The immediate second consumer is the Zone AABB override editor:

- it emits the existing AABB lines and zone label;
- when the user enters bounds-edit mode, it supplies one `AabbEditTarget`;
- it uses the same snap, preview/cancel, and face-handle math as Dock arm boxes;
- it commits through a manifest command instead of component bytes.

This is enough proof that AABB handles are reusable. Point-light range, trigger
volumes, audio volumes, probes, and nav links are credible later adapters, but
this plan does not add sphere handles, arbitrary polygons, splines, or a universal
property-binding language for them.

---

## 4. Dock viewport and semantic behavior

### 4.1 Presentation

`WorldDockEditorAdapter` builds the following from persistent data:

- bounded rectangle outline plus a translucent fill;
- Side A arrow from the plane toward local -Z, labeled
  `A: <zone name>` (or `A: unresolved`);
- Side B arrow toward local +Z, labeled `B: <zone name>`;
- direction arrows through the plane for enabled A->B and/or B->A travel;
- translucent Side A and Side B local boxes transformed to world space;
- selected/hovered handle highlighting;
- error color for missing/same-zone assignments, invalid dimensions, invalid
  arm side, duplicate id, or non-unit transform scale;
- warning/evidence text when an AABB suggestion is empty or ambiguous.

Unselected docks may use a low-clutter outline. Selected or hovered docks show
the full labels, directionality, boxes, and handles. Invalid docks remain visible
and selectable so they can be repaired.

### 4.2 Manipulation

- Move and Rotate use the existing transform gizmos.
- Scale is unavailable for an entity carrying a Dock adapter.
- Rectangle handles edit `HalfExtents` without changing entity scale.
- AABB face handles edit Side A and Side B independently in Dock-local space.
- Plane and AABB handles obey the shared grid.
- Horizontal and diagonal planes are ordinary rotations; no special mode exists.

The adapter may expose an inspector choice for which sub-targets show handles
when simultaneous boxes become visually crowded, but the choice is transient
editor state and must not serialize.

### 4.3 Swap Sides

`Swap Sides` is a Dock-specific pure operation invoked by the adapter:

1. swap `ZoneA` and `ZoneB`;
2. rotate the entity 180 degrees around Dock-local up so the normal reverses;
3. swap Side A/B arm boxes and reflect their local X and Z ranges so both world
   volumes remain in place;
4. exchange A->B and B->A direction bits;
5. keep `DockId`, plane dimensions, priority, depth, and condition;
6. commit component and transform together as one undoable command;
7. rerun validation.

Applying the operation twice must restore an equivalent authored state.

### 4.4 Suggestions are evidence, not topology

Contextual creation sets Zone A to the active zone. For Zone B, sample or search
just beyond Side B using zone AABBs only:

- exactly one candidate other than Zone A -> suggest it;
- none -> leave unresolved;
- more than one -> leave unresolved and show candidates.

The inspector may run the same coarse suggestion explicitly for either side.
It never silently overwrites an authored assignment, never examines a polygon
shape, and never creates an edge from AABB contact/overlap.

---

## 5. Lifecycle semantics

### 5.1 Explicit creation

Finish and use the existing `EditorEntityRecipeRegistry`. The World panel asks
the registry for the Dock recipe instead of constructing `WorldDockRecipe`
directly. The recipe receives `EditorCreateContext`, then exactly once:

1. mints `DockId`;
2. sets Zone A from `ActiveZone`;
3. computes a non-authoritative Zone B suggestion from AABBs;
4. builds a `LocalTransform` whose local +Z matches the placement normal;
5. initializes plane and arm defaults;
6. returns one complete `EntitySnapshot`;
7. the create command restores it, focuses the world scene, and selects it.

The recipe is the only contextual-default path.

### 5.2 Manual component add

The existing generic `RawComponentAddCommand` adds `WorldDock{}` bytes only. It
does not run a recipe, use the active zone, mint an id, or probe bounds. The
custom inspector displays the resulting validation errors and offers explicit
`Generate Dock ID` and zone assignment controls. Those are user commands, not
implicit initialization.

### 5.3 Editing and undo/redo

- Transform edits use existing transform commands.
- Plane/AABB drags preview live, cancel losslessly, and commit one command.
- Inspector scalar/semantic edits commit one command per gesture/action.
- `Swap Sides` commits transform plus component atomically.
- Execute, undo, and redo all notify `WorldDocument` to refresh validation.
- Undo/redo never invokes a recipe or mints a new logical id.

Add a generic content-edited callback to `EditorDocument` (or the equivalent
command notification) and bind world-scene edits to world validation. This
removes the current need for Dock-specific setters to call `World.Revalidate()`.

### 5.4 Duplicate

Current `DuplicateEntitiesCommand` correctly captures snapshots once but copies
logical ids unchanged. Add a narrow editor duplicate-remapper registry keyed by
component type. It runs once after first capture and before the duplicate
snapshots are retained:

- a duplicated `WorldDock` receives one newly minted `DockId` while every other
  persistent value is preserved;
- a duplicated `WorldLink` receives one new `LinkId`;
- a duplicated `DockGateBinding` alone continues to reference the original Dock;
- when a Dock and bound entities are duplicated in the same snapshot set,
  bindings are remapped through the old->new `DockId` map;
- redo reuses the already-remapped snapshots and id; it does not mint again.

This is identity remapping, not contextual reinitialization. Zone A/B, dimensions,
arm boxes, direction, conditions, and transform remain exact copies.

Keep this remapper separate from viewport adapters: snapshot identity lifecycle
is useful even in headless editor tests and must not depend on rendering.

### 5.5 Copy/paste

Kyusu currently has no entity copy/paste path (only face-projection clipboard
operations). When entity copy/paste is added, paste must run the same snapshot
duplicate-remapper exactly once. Copy is read-only. Paste does not run creation
recipes. Cross-world paste preserves Zone ids only when they resolve in the
destination; otherwise it leaves them unresolved and validation reports them.
Building a general entity clipboard is not part of this correction.

### 5.6 Load, import, save, and cook

- Load/import deserialize stored component and manifest values verbatim, subject
  only to explicit format migration below.
- No recipe, suggestion, id minting, or snapping runs on load/import.
- Save writes current persistent values and no editor affordance state.
- Cook reads world-scene Docks, validates them, and emits endpoint locality
  views; it never creates editor geometry or shape products.
- Loading a cooked world never registers or queries Kyusu adapters.

### 5.7 Delete and reference behavior

- Delete uses the existing snapshot command and removes the authored entity.
- Undo restores the same stored `DockId`; redo removes it again.
- Gate bindings are not cascade-deleted. A dangling binding is an error.
- Deleting a zone must be blocked or explicitly repair every Dock/Link reference;
  silent endpoint reassignment is forbidden.
- Graph Viewer derives its edge directly from the world entity and id; deletion
  removes the edge without a second graph model.

---

## 6. Manifest and scene migration

### 6.1 Canonical format

Increment world manifest format to v3 rather than changing v2 meaning in place.
Canonical v3 uses:

```json
{
  "format_version": 3,
  "graphs": [],
  "zones": [
    {
      "id": "...",
      "graph": "...",
      "scene": "...",
      "bounds": { "min": [0,0,0], "max": [1,1,1] },
      "bounds_overridden": false
    }
  ]
}
```

Cooked-only scene/collision/content hash and endpoint arrays remain as already
designed. There is no `shape`, `cells`, `broad_bounds`,
`broad_bounds_overridden`, or `shape_hash` in v3 output.

### 6.2 Read migration

- v1 (`regions`, `region`, `bounds`, `bounds_overridden`) maps to graph
  terminology and the same single bounds fields. Existing transition migration
  remains explicit.
- v2 (`graphs`, `graph`, `shape`, `broad_bounds`) takes `broad_bounds` as the
  single v3 `Bounds`, maps the override flag, and discards the entire `shape`
  object and `shape_hash`. V2 already required/stored the broad box, so no convex
  runtime type is needed to migrate it.
- if a v2 bounds value is invalid, migration fails with a repair diagnostic; it
  does not retain shape cells as fallback truth.
- the next save writes only v3. Unknown obsolete shape keys never round-trip.
- cooked v1/v2 manifests should be recooked; authored source migration is the
  supported path.

Emit a one-time migration summary listing zones converted and any invalid bounds.
There are no `.sworld` fixtures in the source tree beyond tests, so tests must
provide both v1 and v2 inputs explicitly.

### 6.3 WorldDock scene data

Keep the current serialized WorldDock fields. The corrected local +Z normal
convention matches the branch's existing Side A negative-Z and Side B
positive-Z defaults, so obsolete shape removal does not require rewriting Dock
arm values. The cook/recipe/adapter must consistently use `-Forward()` as +Z.

### 6.4 Plan migration

Update both canonical plans in the same change. Remove every statement that:

- calls a zone shape authored truth;
- describes convex prisms/cells, footprints, height bands, exact containment,
  minimap union, shape hashes, side probes against exact shapes, overlap
  rejection, shape draft generation, or generated exact cell contracts;
- requires the designer to draw L/T/stacked zone geometry;
- permits an exact zone product to return later under another representation.

Historical earlier documents may remain historical only if their headers point
to Plans 11/12 as superseding them. No active implementation checklist may
reference Zone Shape.

---

## 7. Validation and diagnostics

Validation runs live and again as a cook gate. Shared helpers must use finite,
strictly-positive extent checks rather than only `Aabb3d::IsValid()`.

### 7.1 Zone and graph rules

- every zone has a valid graph;
- graph policy values are in range;
- every zone has one finite AABB with positive X/Y/Z extent;
- derived refresh may warn when it has no boundable content and is retaining a
  starter/cached box, but this is not an exact-geometry failure;
- overlapping, nested, touching, or separated Zone AABBs are all legal;
- overlap produces no edge and no error/warning merely because it exists;
- placement/suggestion ambiguity is reported at the query/Dock that encountered
  it, not as a global pairwise overlap prohibition.

### 7.2 Authored Dock rules

- `DockId` is nonzero and unique;
- Zone A and Zone B both resolve and are distinct;
- transform position/rotation are finite and transform scale is unit;
- `HalfExtents` are finite and strictly positive;
- both local arm AABBs are finite and nondegenerate;
- Side A has `Max.Z <= epsilon`; Side B has `Min.Z >= -epsilon`;
- direction bits contain only A->B/B->A and at least one is set;
- preload depth is non-negative and demand tags parse/resolve;
- gate bindings reference a live Dock id.

Do not reject or merge Docks because they connect the same zone pair. Do not
retain the current pair/proximity-based `partition.dock.strong_overlap` warning;
duplicate `DockId` is the identity error. Two doors beside one another are two
legitimate edges.

### 7.3 Cooked endpoint rules

- every cooked `DockId` occurs exactly twice, one A and one B;
- the two records are reciprocal and name the correct owner graphs;
- endpoint basis vectors are finite, unit, orthogonal, and consistently handed;
- each endpoint arm is finite/nondegenerate and lies at local `z <= 0`;
- plane dimensions and directions agree between views;
- one endpoint pair counts as one logical edge in graph queries/diagnostics.

### 7.4 Required explicit cases

Tests and UI must distinguish:

- invalid AABB -> error;
- unresolved Zone A/B -> error plus red label;
- same-zone endpoints -> error;
- zero/negative/nonfinite plane dimensions -> error;
- arm bounds on the wrong side -> error;
- duplicate DockIds -> error;
- overlapping Zone AABBs -> legal, possibly ambiguous only for a specific
  coarse query;
- multiple distinct Docks between the same pair -> legal and individually
  selectable/cooked.

---

## 8. Tests and manual viewport gates

### 8.1 Pure/headless tests

Zone bounds and migration:

1. `ComputeZoneBounds` unions current boundable content.
2. non-overridden open zones refresh; overridden zones do not.
3. an empty derived zone retains its valid cached/starter AABB.
4. a bounds edit sets override and undo restores both value and mode.
5. v1 bounds and v2 shape manifests read into one v3 AABB.
6. v3 write/read is deterministic and writes no shape/cell/hash key.
7. invalid/nonfinite/degenerate AABBs fail validation.
8. overlapping AABBs validate cleanly and infer no adjacency.
9. placement resolution prefers the previous candidate, otherwise deterministic
   volume/id tie breaks.
10. spatial radius preserves point-to-AABB behavior.
11. Graph Viewer layout extraction places a node at `Bounds.Center()`.
12. cook output contains only the AABB and content hash, no exact shape product.

Generic affordances:

13. pure AABB handle geometry/hit tests work in world and rotated local frames.
14. rectangle edge drags snap to shared spacing and enforce minimum size.
15. AABB face drags snap and enforce minimum thickness.
16. component edit preview/cancel restores bytes; release creates one undo step.
17. Zone AABB and Dock arm targets use the same AABB kernel.
18. pick proxies return the owning entity and compete by hit distance without a
    component-type branch.

Dock lifecycle and semantics:

19. explicit creation sets Zone A, mints one id, and uses a unique AABB
    suggestion for Zone B.
20. manual component add, load, snapshot restore, undo, and redo do not run the
    recipe.
21. duplication preserves all authored values but remaps `DockId` once.
22. duplicating a Dock plus bindings remaps intra-set references; a binding alone
    remains attached to the original.
23. `Swap Sides` swaps zones/directions, reverses normal, preserves world arm
    volumes, and is involutive.
24. component serialization round-trips plane dimensions and both local AABBs.
25. deletion leaves a dangling binding diagnostic; undo restores the id.

Cook/runtime/validation:

26. one logical bilateral Dock cooks to two reciprocal endpoints with the same id.
27. two distinct Docks between one zone pair produce four endpoint records and
    two graph edges.
28. duplicate ids, unresolved zones, same-zone endpoints, degenerate plane,
    wrong-side arms, and non-unit scale each fire their specific rule.
29. vertical, horizontal, and diagonal Dock planes cross through the same code.
30. the oriented local arm rejects a sweep that only intersects its old expanded
    world AABB.
31. direction flags, fast sweep, residency gate, re-arm, and jitter behavior keep
    the current DockCrossing coverage.

### 8.2 Manual viewport gates

A reviewer must verify in a real Kyusu session:

1. select a Dock by clicking its plane/box in the world viewport;
2. move it with grid snap and rotate it with normal transform tools;
3. rotate it diagonally and horizontally without losing plane/box alignment;
4. resize width and height visually with grid snap;
5. resize all six faces of each side-local AABB independently with grid snap;
6. cancel every drag with Escape and see the exact original value;
7. complete a drag, undo, and redo it as one step;
8. see clear A/B arrows, zone-name labels, and one-way/bidirectional decoration;
9. see unresolved/same-zone/wrong-side/degenerate assignments in unmistakable
   error styling while the Dock remains selectable;
10. run `Swap Sides`, verify labels/directions/normal/boxes, then undo;
11. duplicate a Dock and verify values are unchanged except for a new id;
12. save/reload and verify no contextual initialization reruns;
13. add `WorldDock` manually and verify it remains unresolved until explicit
   repair rather than adopting the active zone;
14. delete and undo a Dock through normal entity commands;
15. edit a Zone AABB override with the same face-handle behavior, then return it
   to derived mode;
16. overlap two Zone AABBs without receiving a topology/overlap error;
17. display two Docks between one zone pair as separate selectable Graph Viewer
   edges;
18. confirm the World panel contains Graphs/Zones and no Connections section;
19. confirm spatial Docks are created/placed in the world viewport, never at a
   fabricated Graph Viewer midpoint;
20. inspect a cooked artifact and see one logical id in two zone-local endpoints.

---

## 9. Staged implementation order

Each stage is reviewable, keeps the repository building, and lands its own tests.
Do not combine these into a single rewrite.

### S0. Contract and safety fixtures

- land these corrected Plans 11/12;
- add characterization tests for current point-to-AABB radius policy, graph-local
  demand, reciprocal endpoint identity, and transition migration;
- do not change runtime/editor behavior yet.

Gate: baseline tests identify policy regressions independently of shape removal.

### S1. Contract Zone data to one AABB

- change `ZoneHeader` to `Bounds`/`BoundsOverridden` and remove shape/hash fields;
- restore derived refresh from `ComputeZoneBounds` with valid starter/cached
  behavior;
- remove exact containment from demand/runtime and implement deterministic
  AABB-only containment results;
- remove exact overlap validation;
- implement v1/v2 read migration and canonical v3 write;
- remove shape cook products and update Graph Viewer/preview references;
- delete Zone Shape code/UI/tests and build entries.

Temporary Dock rendering may remain visually unchanged in this stage, but any
shape-based suggestion button must be removed or changed to AABB evidence so the
tree compiles without shape types.

Gate: all zone, manifest, runtime, cook, migration, and radius tests are green;
no production symbol or canonical JSON contains Zone Shape.

### S2. Finish Zone AABB authoring

- add explicit bounds/mode WorldDocument verbs and `SetZoneBoundsCommand`;
- add World-panel controls for `Edit Override` and `Use Derived Bounds`;
- render one AABB and one label per zone;
- establish a transient active Zone bounds-edit target and headless command tests;
- wire generic document content-edited notification so derived refresh and
  validation run after relevant execute/undo/redo.

Gate: derived, override, save/reload, empty-zone fallback, and undo semantics are
complete without any Dock work.

### S3. Add the narrow affordance core

- add the Kyusu component adapter registry and transient output structures;
- add the generic renderer/label submission and pick-proxy merge;
- extract reusable AABB handle math from `BoundsManipulator`;
- add the rectangle handle kernel and component/manifest value edit bindings;
- add `AffordanceManipulator` to the existing session;
- adapt Zone bounds as the second AABB consumer while preserving brush Resize.

Gate: pure handle/snap/pick/transaction tests pass; generic hosts contain no
`WorldDock` include, query, or switch.

### S4. Move Dock presentation and selection into an adapter

- register `WorldDockEditorAdapter`;
- emit plane fill/outline, A/B arrows and labels, direction arrows, boxes, and
  validation decoration;
- make Dock pick proxies participate in normal click/marquee selection;
- remove all Dock drawing from `ZoneBoundsRenderer`;
- integrate the adapter with Graph Viewer/hierarchy selection.

Gate: Dock viewport selection and feedback work; searching generic renderer,
picking, interaction host, viewport panel, and inspector finds no Dock semantic
branch.

### S5. Land Dock manipulation and inspector semantics

- expose rectangle and two local AABB targets;
- share grid snap with transform tools and enforce constraints;
- disable scale capability through adapter-declared transform capabilities;
- replace `DockPanel` with the adapter's inspector body;
- implement atomic `Swap Sides`, id repair, and explicit AABB suggestions;
- delete `DockPanel.*` and its registration.

Gate: manual diagonal/horizontal, snap, cancel, undo, invalid feedback, and side
swap gates pass.

### S6. Complete creation and identity lifecycle

- route Dock/Link creation through `EditorEntityRecipeRegistry`;
- correct recipe rotation to local +Z and AABB-only suggestions;
- add type-keyed snapshot duplicate remapping for Dock/Link identities and gate
  references;
- cover explicit create vs add/load/duplicate/undo in headless tests;
- document future copy/paste use of the same remapper without building a new
  clipboard system.

Gate: duplicate never creates duplicate ids and no non-create path reruns
contextual defaults.

### S7. Correct cook and runtime endpoint frames

- cook owner-local oriented arm AABBs and reciprocal endpoint frames;
- update endpoint serialization/index validation;
- update crossing/approach tests to transform points into endpoint local space;
- preserve graph demand, direction, residency, and jitter behavior;
- remove the old transformed-world-AABB arm path.

Gate: one Dock -> two endpoints/one edge; horizontal/diagonal arm tests and all
streaming policy tests pass.

### S8. Validation, migration cleanup, and UX sign-off

- finish authored/cooked Dock rules and dangling binding diagnostics;
- prove overlapping zones and multiple same-pair Docks are legal;
- finish v1 transition and v2 shape migration reports/fixtures;
- remove obsolete plan text, commands, UI labels, tests, and CMake entries;
- run the full headless suite and every manual viewport gate.

Gate: source search finds no obsolete shape implementation or active plan text,
all tests are green, and owner UX review signs off.

---

## 10. Explicit deletions

Delete completely:

- `engine/include/zone/ZoneShape.h`;
- `engine/src/zone/ZoneShape.cpp`;
- `editor/kyusu/src/ui/ZoneShapePanel.h/.cpp` and panel registration;
- `test/runtime/ZoneShapeTests.cpp` and its build entry;
- `ConvexPrism`, `AuthoredZoneShape`, `IsValidConvexPrism`, both exact
  `ContainsPoint` overloads, `ZoneShapesInterpenetrate`,
  `DeriveZoneBroadBounds`, `CanonicalizeZoneShape`, `HashZoneShape`, and
  `MakeBoxZoneShape`;
- `ZoneHeader::Shape`, `BroadBounds`, `BroadBoundsOverridden`, and
  `CookedShapeHash` (replace the bounds names, do not wrap them);
- v2 shape writer and canonical shape parser; keep only a v2 migration reader
  that takes `broad_bounds` and ignores/discards shape data;
- shape validation/mismatch/overlap rules and all fixtures that require L/T,
  diagonal footprint, stacked-cell, or exact-overlap behavior;
- Zone Shape commands and UI for draw footprint, vertex edits, vertical range,
  split, adjacent cell, merge, duplicate height, and first-draft generation;
- prism rendering and every shape-based Dock probe/suggestion;
- cooked shape canonicalization/hash/product logic;
- Dock-specific drawing inside `ZoneBoundsRenderer`;
- `DockPanel.h/.cpp` after the adapter inspector lands;
- pair/proximity-based `partition.dock.strong_overlap` validation;
- every active-plan paragraph that invites exact Zone geometry back later.

Keep and correct:

- `ZoneBounds.cpp`, single-AABB rendering, zone labels, and world view toggles;
- `Aabb3d` schema support;
- `WorldConnectionComponents`, `WorldTopologyCook`, `DockCrossing`, endpoint
  indexes, graph demand, Graph Viewer, creation recipes, gate bindings, and
  transition migration;
- generic command, serializer, inspector fallback, selection, transform, grid,
  interaction, line/fill, and label infrastructure.

Do not invent compatibility wrappers around shape types. Migration reads old
JSON directly into the new AABB model and discards obsolete data.

---

## 11. Non-goals and generalization boundary

- no universal declarative gizmo or editor-plugin language;
- no runtime/editor shared adapter interface;
- no serializer ABI extension for viewport callbacks;
- no arbitrary polygon, convex volume, OBB, spline, or CSG handle in this work;
- no exact Zone containment/minimap product under another name;
- no topology inferred from bounds, collision, doors, or Graph Viewer layout;
- no stored Graph Viewer node positions;
- no physical Dock fabrication between Graph Viewer nodes;
- no automatic cascade deletion of gates or endpoint reassignment;
- no entity clipboard implementation; only its required future remap semantics;
- no PointLight/trigger/audio/probe/nav adapter implementation until a consumer
  is scheduled; the Zone AABB already proves reusable AABB editing;
- no expansion of derived zone content beyond current boundable brush/baked-brush
  behavior without a separate reviewed requirement.
