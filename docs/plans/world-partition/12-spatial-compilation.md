# World Graph Authoring and Compilation: Zone Shapes, Docks, Links, and Kyusu

Status: proposed replacement design (2026-07-15), owner review before implementation.
Canonical: this document and `11-zone-runtime-model.md` replace the previous
spatial-compilation design. Doc 11 owns runtime graphs, endpoint records,
focus, demand, and gate boundaries. This document owns the authored surface,
Kyusu behavior, cooking, diagnostics, migration, and execution order.

## Why this plan replaces the previous one

The previous plan tried to infer exact ownership and physical contacts by
rasterizing world geometry, growing labels through free space, evaluating
captured mechanism states, and reconciling generated contact ids after
remodels. That moved ordinary level authorship into a large compiler whose
mistakes would be difficult to predict or repair.

The replacement is explicit where intent matters and derived where derivation
is safe:

- the designer authors zone ownership geometry;
- the designer authors a dock where crossing changes zones;
- Kyusu helps resolve the zones on each side;
- the cook emits reciprocal endpoint records and broad-phase data;
- runtime graph policy decides residency;
- doors and gates may bind to docks but do not define topology.

This keeps Metroid Prime's useful separation between area, dock, door, and
streaming while removing reciprocal dock editing and raw index management from
the designer.

---

## 1. Authored surface

The normal designer touches four things:

1. **Graphs** in the world partition panel.
2. **Zones** assigned to one graph and owning their content documents.
3. **Zone shapes** describing ownership and minimap geometry.
4. **World docks or world links** connecting specific zone endpoints.

Connections are viewed and selected primarily through a dedicated dockable 3D
Graph Viewer, not through a long Connections section in the World panel.

Optional gameplay objects may bind to a dock as gates. They are not part of the
world topology authoring transaction.

Never authored:

- reciprocal endpoint records;
- raw dock indices;
- graph adjacency spreadsheets;
- preload shells around every doorway;
- containment voxels;
- contacts inferred from touching AABBs;
- duplicate transitions for A to B and B to A.

---

## 2. Kyusu document ownership

### 2.1 World scene

World docks and links are world-scene entities because they express
relationships between zone documents. They remain available while the world is
open in Kyusu and do not structurally belong to either zone.

```cpp
struct WorldDock
{
    DockId       Id;
    ZoneId       ZoneA;
    ZoneId       ZoneB;
    Vec2d        HalfExtents;       // bounded plane rectangle
    Aabb3d       SideAArmBounds;    // dock-local
    Aabb3d       SideBArmBounds;    // dock-local
    uint8_t      Directions;
    int32_t      PreloadPriority;
    int32_t      PreloadDepth;
    TagQuery     DemandCondition;
};

struct WorldLink
{
    LinkId       Id;
    ZoneId       ZoneA;
    ZoneId       ZoneB;
    LinkKind     Kind;              // Teleport first
    uint8_t      Directions;
    int32_t      PreloadPriority;
    int32_t      PreloadDepth;
    TagQuery     DemandCondition;
};
```

The entity transform supplies the dock plane origin and orientation. Its normal
points from Side A toward Side B. `HalfExtents` bound the actual crossing
surface. The side arm bounds are editable AABBs and may have different depth
and size.

### 2.2 Zone document

A zone document owns ordinary entities and brushes plus its authored zone
shape. The shape belongs to zone metadata rather than a gameplay entity so it
cannot be duplicated accidentally.

```cpp
struct AuthoredZoneShape
{
    std::vector<ConvexPrism> Cells;
};

struct ConvexPrism
{
    std::vector<Vec2d> Footprint; // convex and ordered
    double             MinY;
    double             MaxY;
};
```

A zone shape is the union of its cells. Convex prisms handle diagonal halls,
L and T layouts through multiple cells, stacked areas through height ranges,
and a useful minimap footprint. A later general convex-polyhedron format is
permitted only if real sloped-volume cases prove prisms insufficient. The
runtime contract is union of convex cells, not prisms forever.

---

## 3. Generic editor creation recipes

`ZoneA = active zone` is contextual authoring behavior. It must not live in a
component constructor, runtime system, or dock-specific branch in Kyusu.

Kyusu gains a generic creation-recipe seam:

```cpp
struct EditorCreateContext
{
    WorldDocument* World;
    ZoneId         ActiveZone;
    GraphId        ActiveGraph;
    Vec3d          PlacementPoint;
    Vec3d          PlacementNormal;
    SelectionView  Selection;
};

class IEditorEntityRecipe
{
public:
    virtual EntitySnapshot Build(const EditorCreateContext&) const = 0;
};
```

The create tool invokes a registered recipe, receives a complete entity
snapshot, and commits it through the command stack. Kyusu does not inspect the
component type to apply contextual defaults.

### 3.1 World dock recipe

The dock recipe:

1. mints a `DockId`;
2. creates the entity in the world scene;
3. initializes Zone A from `context.ActiveZone`;
4. probes from the placement plane toward Side B and suggests Zone B;
5. derives an initial orientation from the picked surface or camera;
6. initializes a bounded plane and two conservative arm AABBs;
7. leaves Zone B unresolved when probing is ambiguous;
8. returns one snapshot for one undoable create command.

Creation behavior runs only for explicit recipe creation. It does not run when
loading, duplicating, pasting, restoring undo, importing, or manually adding a
`WorldDock` component. Those operations preserve or remap stored data through
the normal snapshot pipeline.

The recipe seam is intentionally general. Future recipes may use the same
context for lights, nav links, spawn points, generated graph cells, or paired
gate views.

---

## 4. Dock viewport and inspector

A selected dock renders:

- a filled or outlined bounded plane;
- a labeled arrow on Side A and Side B;
- the resolved Zone A and Zone B names beside the arrows;
- a translucent editable AABB on each side;
- warnings for unresolved or geometrically inconsistent assignments;
- an optional line to selected gate bindings.

The dock supports:

- transform gizmo for position and rotation;
- plane width and height handles;
- independent bounds manipulation for each side AABB;
- `Swap Sides`, which swaps zone references, flips the normal, swaps bounds,
  and remaps one-way flags;
- `Fit Plane To Selection`;
- `Reset Arm Bounds`;
- `Resolve Zones From Shape`.

Horizontal and diagonal docks are ordinary rotations. No code may assume a
dock is vertical or axis-aligned.

The inspector presents direct zone editing and separates evidence from truth:

```text
Dock
  Id                 0x...
  Zone A             West Corridor
  Zone B             Reactor Hall
  Directions         Both
  Surface            2.0 x 3.0 m
  Side A Arm Bounds  [edit]
  Side B Arm Bounds  [edit]

Residency Hints
  Priority           0
  Preload Depth      inherit
  Demand Condition   none

Validation
  Side A probe       West Corridor
  Side B probe       Reactor Hall
  Gate bindings      1
```

Zone A and Zone B remain editable even when probes disagree. Probe results are
validation evidence, not authority.

---

## 5. Dockable 3D Graph Viewer

Kyusu gains a dedicated `Graph Viewer` panel that can be docked or tabbed like
other editor panels. It is the primary overview and connection-selection
surface. The World panel no longer contains a Connections section.

### 5.1 Spatial layout

Each zone is displayed as a sphere positioned at the center of its derived
broad bounds:

```cpp
Vec3d nodePosition = zone.BroadBounds.Center();
```

This is a derived view, not stored layout data. Moving a graph node does not
move the zone, change topology, or edit world content.

Node spheres use a fixed or clamped screen-space presentation size so a tiny
zone and a huge exterior cell remain equally selectable. Optional translucent
bounds or shape outlines may be displayed around a selected node, but the
sphere remains the main graph glyph.

Labels show zone name and optionally graph name. Graph membership controls
node tint and filtering, but graph colors are presentation state only.

### 5.2 Edge presentation

Every world dock and link produces one selectable graph edge:

- bidirectional dock: line or curve with arrows at both ends;
- one-way dock: one directional arrow;
- teleport or other link: visually distinct dashed or styled curve;
- cross-graph edge: graph-boundary styling at the edge or endpoints;
- invalid or unresolved connection: warning style and incomplete endpoint.

Multiple docks between the same zone pair must remain individually selectable.
The viewer offsets them as shallow parallel curves rather than collapsing them
into one pair record. At distant zoom it may group them visually with a count,
but selection or zoom expands the individual edges.

Edge identity is the underlying `DockId` or `LinkId`. The viewer owns no second
connection model.

### 5.3 Selection and navigation

Selection is shared with the rest of Kyusu:

- select a node: select and focus the zone in the World panel;
- double-click a node: open or focus its zone document;
- select a dock edge: select the world dock entity and show its inspector;
- double-click a dock edge: focus the physical dock in the 3D world viewport;
- select a link edge: select its world link entity;
- selection made in the World panel or viewport highlights the corresponding
  node or edge in the Graph Viewer.

The panel supports orbit, pan, zoom, frame selection, frame graph, and orthographic
view presets. It may reuse editor camera and line-rendering infrastructure, but
it is a separate scene/view from the level viewport.

### 5.4 Filters and overlays

Minimum useful filters:

- all graphs or selected graphs;
- loaded, resident, active, or unloaded state during preview;
- docks, links, or both;
- one-way only;
- cross-graph only;
- validation severity;
- zone-name and connection-name search.

Useful overlays:

- preview residency around a selected focus zone;
- hop rank or spatial-radius membership;
- estimated RAM and VRAM cost;
- graph island and reachability diagnostics;
- gate state when runtime preview is connected.

These consume the same pure demand and validation results as the existing
preview. The viewer does not implement its own streaming simulation.

### 5.5 Editing boundary

The first version is primarily a viewer and selector. It may offer these narrow
world-document verbs:

- select two nodes and create a `WorldLink`;
- select an edge and reverse or change direction;
- delete the underlying dock or link through the normal command path.

Creating a spatial dock still happens in the world viewport because the dock
requires physical plane and arm geometry. The graph viewer may initiate the
operation and prefill Zone A and Zone B, then hand placement to the world
viewport. It must not invent a dock at the midpoint between graph nodes.

---

## 6. Zone shape authoring

A zone begins with one convex prism. The designer edits its top-down footprint
and vertical range, then adds cells around corners, branches, or stacked
sections.

Required tools:

- draw convex footprint;
- edit vertices with grid snap;
- edit vertical range;
- split a cell;
- add adjacent cell;
- merge cells when the union remains convex;
- duplicate a height band;
- select and focus shapes from the partition panel;
- ghost neighboring shapes while editing one.

Adjacent cells do not need identical vertex storage. The cook canonicalizes
planes and may union their minimap projection.

### 6.1 Optional first draft

Kyusu may generate a first draft from selected zone geometry. The result is
ordinary editable cells and never hidden compiler truth.

A safe first version gathers selected zone geometry, projects coarse occupancy
through useful height bands, traces outlines, convex-decomposes them, and
presents an uncommitted preview. The designer accepts, simplifies, edits, or
discards it. Generator failure cannot block manual authoring or cooking.

### 6.2 Broad bounds

The cook derives one AABB over the complete exact shape. It is used for:

- broad-phase containment rejection;
- editor framing and selection;
- graph-node placement;
- spatial-radius demand;
- coarse diagnostics.

It never decides adjacency. Overlapping broad AABBs are legal when exact shapes
do not overlap.

---

## 7. Side resolution

A dock stores explicit Zone A and Zone B references. Kyusu may suggest and
validate them by sampling just beyond the plane:

```text
A sample = origin - normal * distance
B sample = origin + normal * distance
```

Each sample first rejects by broad AABB, then tests exact convex-cell
containment.

- exactly one zone: suggest or validate that side;
- no zone: unresolved warning;
- several zones: ambiguity error with a candidate list.

Sample points may be adjusted within their side arm bounds and are visible in a
debug overlay.

Explicit references remain authoritative because a dock may sit in deliberately
unowned air, broad bounds may overlap, generated destination cells may not be
open in the editor, and non-spatial links cannot be discovered by probing.

---

## 8. Cooking

### 8.1 Inputs

The world cook consumes:

- graph records;
- zone headers and authored shapes;
- world dock entities;
- world link entities;
- existing zone-content cooks;
- optional demand conditions and residency hints.

It does not rasterize world collision or flood free space to discover topology.

### 8.2 Zone-shape product

Each shape cooks into:

- canonical convex-cell data;
- one broad AABB;
- optional merged 2D minimap contours;
- a content hash.

This product loads independently of zone entity content because containment,
graph layout, and map queries may target unloaded zones.

### 8.3 Dock compilation

For every valid world dock, the cook:

1. resolves Zone A and Zone B headers;
2. reads each zone's GraphId;
3. transforms plane, surface, and arm geometry into the chosen artifact space;
4. emits a Side A endpoint into Zone A's header;
5. emits a Side B endpoint into Zone B's header;
6. copies the shared DockId and reciprocal zone and graph ids;
7. reverses direction and orientation for the B endpoint;
8. builds deterministic incident-endpoint indexes;
9. records a debug map from DockId to authored world entity id.

The designer never edits endpoint records directly.

World links use the same identity and endpoint-index path without geometric
fields. Teleport is the first supported link kind.

Outputs sort by stable graph id, zone id, endpoint id, and side. Authored array
order never becomes identity. An unchanged cook produces byte-identical shape
and endpoint products.

---

## 9. Validation

Validation runs live in Kyusu and again as a hard cook gate.

### 9.1 Graph and zone rules

- every zone references one valid graph;
- graph streaming values are valid;
- every exact zone shape contains at least one valid convex cell;
- every prism footprint is convex and non-self-intersecting;
- every prism has `MinY < MaxY`;
- broad bounds contain all exact cells;
- exact zone shapes in the same coordinate space do not overlap unless a
  narrow explicit exception exists.

### 9.2 Dock rules

- DockId is valid and unique;
- Zone A and Zone B are valid and distinct;
- surface extents are finite and nondegenerate;
- each arm AABB is finite and lies on the expected side;
- direction flags are valid;
- probe results agree with explicit assignments unless a narrow override is
  acknowledged;
- strongly overlapping duplicate docks for the same pair warn;
- gate bindings reference a live DockId;
- demand conditions resolve registered tag names.

### 9.3 Link and graph checks

- link endpoint zones are valid;
- one-way and two-way semantics are valid;
- unsupported link kinds fail cooking;
- graph-local and cross-graph reachability run over explicit endpoints;
- one-way sinks and graph islands report clearly;
- resident-cap and destination-cost risks report against the same pure demand
  policy used by runtime.

Reachability never invents edges from proximity.

---

## 10. Door and gate boundary

The initial gameplay seam is:

```cpp
struct DockGateBinding
{
    DockId Id;
};
```

It may live on a door, force field, breakable wall, or other gameplay entity.
More than one entity may share a DockId. The world dock stores no entity
reference, avoiding cross-document lifetime and identity problems.

A gate entity is owned by one zone or deliberately by the world scene. If two
side-local presentations are required, two entities may bind to the same dock
and share gameplay state. Kyusu may later provide a paired-gate recipe, but the
runtime and topology model do not require duplicate doors.

The cook validates bindings but does not duplicate, migrate, or globally retain
gate entities in the first implementation.

---

## 11. World panel, Graph Viewer, and hybrid worlds

The World panel returns to structural organization:

```text
World
  Graphs
    Exterior
      Cell 411
      Cell 412
    Facility
      Entrance
      Lobby
      Reactor Hall
```

It owns graph creation, graph policy, zone membership, zone document state, and
validation badges. It does not repeat a flat Connections list.

The separate Graph Viewer displays the same world spatially:

```text
Cell 411  o----o  Cell 412  o====o  Entrance  o----o  Lobby
             ExteriorGraph              FacilityGraph
```

A graph row edits hop count, radius, resident cap, and future policy values. The
old Region UI migrates rather than coexisting.

A future cell compiler may generate zones under `Exterior`. Generated cells use
the same zone header, exact-shape, graph-node, and endpoint contracts. A world
dock can connect a generated cell to an authored interior zone without either
graph adopting the other's policy.

---

## 12. Migration from existing transitions

For each current `TransitionRecord`:

1. `Teleport` becomes a world link with the same direction, tags, priority,
   depth, and name.
2. A geometric transition with a linked marker becomes a world dock at the
   marker transform.
3. A reverse pair collapses into one bilateral dock.
4. An unpaired geometric transition becomes a one-way dock.
5. A transition without sufficient geometry becomes an unresolved migration
   item rather than a guessed plane.
6. Stable gate associations become `DockGateBinding`.
7. The report lists created docks, links, collapsed pairs, unresolved records,
   and orphaned markers.

Migration is an explicit world command. Saving afterward writes only graphs,
docks, and links.

---

## 13. Implementation stages

### A1. Graph terminology

Rename Region UI, manifest fields, serializers, validation, and document verbs
to Graph while preserving implemented behavior. Read legacy manifests for one
migration window.

### A2. Zone shapes

Add convex-cell metadata, viewport rendering, basic editing, exact containment,
broad-bound derivation, serialization, and fixtures for L halls, diagonal
corridors, T junctions, and stacked rooms.

### A3. Creation recipes

Add generic recipe registration, `EditorCreateContext`, snapshot creation, and
undo integration. Prove contextual defaults run only during explicit creation.

### A4. World docks

Add component, recipe, plane and arrow rendering, side-AABB manipulators,
inspector, side swap, probes, and validation.

### A5. 3D Graph Viewer

Add the dockable panel, derived sphere nodes at zone-bounds centers, individual
selectable dock and link edges, shared selection, viewport focus, graph filters,
and validation overlays. Remove the World panel's Connections section.

Gate: multiple docks between the same pair remain individually selectable and
selecting an edge focuses the underlying world object.

### A6. World links

Add non-spatial connections and Teleport without fake plane controls. Display
them through the same Graph Viewer edge path.

### A7. Cooked products

Cook exact shapes, broad bounds, reciprocal dock endpoints, link endpoints,
indexes, debug maps, and deterministic hashes.

### A8. Runtime integration

Land doc 11's graph-local policy and dock crossing against the cooked products.
Kyusu preview and the Graph Viewer consume the same pure policy kernels.

### A9. Migration and retirement

Add transition migration, migrate fixtures, remove legacy geometric transition
authoring, delete the Connections section, and delete obsolete automatic-contact
plans and code.

### A10. First-draft shape generation

Only after manual shape authoring is proven usable, add preview-only draft
generation from selected geometry.

---

## 14. Manual UX gates

A level designer must be able to do all of the following without editing JSON:

1. Create a graph and assign zones.
2. Draw an L-shaped zone with multiple convex cells.
3. Open the Graph Viewer as a docked tab and see zones at their bounds centers.
4. Select a graph node and focus the corresponding zone document.
5. Create a dock while Zone A is active and see Zone A filled automatically.
6. Rotate the dock diagonally and horizontally.
7. Resize the plane and both side AABBs independently.
8. Swap sides without repairing direction flags manually.
9. Override an incorrect Zone B suggestion and understand the warning.
10. See two separate docks between the same zone pair as separate graph edges.
11. Select a graph edge and focus its physical dock in the world viewport.
12. Create a teleport without plane controls.
13. Preview residency from either side of a cross-graph dock in the Graph Viewer.
14. Filter the viewer to one graph or cross-graph connections only.
15. Delete a door without deleting its dock.
16. Manage graphs and zones in the World panel without an ugly Connections list.

---

## 15. Non-goals

- no invisible adjacency inference;
- no spreadsheet editor for endpoint ids;
- no stored freeform graph-node layout in the first version;
- no dragging graph nodes to move zone content;
- no requirement that broad AABBs touch;
- no runtime constructive-solid-geometry union;
- no automatic door duplication;
- no per-door streaming implementation;
- no world-wide voxel field;
- no dynamic topology compiler;
- no navmesh, PVS, or minimap renderer implementation here;
- no assumption that all graphs share one policy.
