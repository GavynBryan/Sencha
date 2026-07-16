# World Graph Authoring, Cook, and Editor Contract

Status: implemented corrective contract on `agent/world-graphs-and-docks`
(2026-07-15). This is the canonical editor/cook companion to Plan 11.

## 1. Ownership boundaries

| Owner | Responsibilities |
| --- | --- |
| `WorldPartitionManifest` | Graph records, one-AABB Zone headers, cooked endpoint locality views. |
| world scene | Persistent `WorldDock`, `WorldLink`, and optional `DockGateBinding` components. |
| `WorldTopologyCook` | Validate authored connections and emit two reciprocal endpoint views per stable ID. |
| `ZoneDemand` | Pure graph/radius/pin policy; no editor or gate state. |
| `DockCrossing` | Swept bounded-plane crossing and late-residency result. |
| `WorldDocument` | lifecycle, derived/override AABBs, shared Zone selection, validation, migration. |
| component adapter registry | component-associated viewport overlays, picking, manipulators, and inspector UI. |
| creation recipe registry | explicit-create defaults only. |
| Graph Viewer | read-only topology presentation and shared selection/focus. |

Generic Kyusu code does not branch on `WorldDock`. It asks the registered
component adapter to contribute primitives, pick proxies, edit targets, and
inspector content.

## 2. Authored components

```cpp
struct WorldDock
{
    DockId Id;
    ZoneId ZoneA;
    ZoneId ZoneB;
    Vec2d HalfExtents{1.0f, 1.5f}; // width/height are twice these values
    uint32_t Directions = DockDirectionBoth;
    // LocalTransform supplies origin, normal, right, and up.
};

struct WorldLink
{
    LinkId Id;
    ZoneId ZoneA;
    ZoneId ZoneB;
    LinkKind Kind = LinkKind::Teleport;
    uint32_t Directions = DockDirectionBoth;
};

struct DockGateBinding { DockId Id; };
```

`WorldLink` has no fake transform. A gate binding is a gameplay reference to a
Dock; deleting or closing the gate does not change topology or demand.

Authored scene ID codecs accept the all-zero invalid value so unresolved
references survive save, load, copy, paste, and undo for editor repair.
Cooked manifests remain strict and reject invalid stable IDs.

## 3. Zone AABB authoring

Each Zone header stores exactly one AABB and `BoundsOverridden`.

- Derived mode refreshes the cached AABB from boundable Zone content during
  save/cook while retaining the last valid value for empty content.
- Override mode stores the directly manipulated box.
- Dragging a derived box creates an override as part of the same command.
- “Use Derived AABB” restores derived mode through an undoable command.
- The generic AABB affordance remains available for Zones and future trigger or
  audio-volume components; Docks never register an AABB target.

Overlaps are legal. Suggestions and probe diagnostics may report ambiguity,
but overlap is not a hard validation failure and never creates adjacency.

## 4. Dock presentation and manipulation

`WorldDockEditorAdapter` contributes:

- a bounded rectangular outline and translucent fill;
- Side A/Side B arrows and Zone-name labels;
- direction arrows;
- invalid-reference/error decoration;
- a rectangle pick proxy;
- width/height edit targets;
- Dock inspector fields and the semantic `Swap Sides` command.

The ordinary transform gizmo supplies translation and rotation. Dock scaling is
disabled because dimensions are explicit. Rectangle and AABB manipulators use
the shared absolute grid-snap math. Each rectangle edge is a real independent
edge: dragging it preserves the opposite edge, shifts the plane transform by
half the edge delta, and updates the matching half extent. Transform and
dimensions preview and commit as one atomic transaction. Commit restores the
before value and executes one undoable value command; cancel restores the before
value. Inspector edits use the same command stack.

The narrow reusable affordance vocabulary is `Lines`, `FillTriangles`,
`Labels`, validation colors, rectangle pick proxies, `RectEditTarget`, and
`AabbEditTarget`. This is sufficient; no universal editor plug-in framework is
introduced.

## 5. Creation and suggestions

Only explicit creation invokes `WorldDockRecipe`:

1. mint a fresh `DockId`;
2. set Zone A from `EditorCreateContext::ActiveZone`;
3. place/orient the Dock from the explicit placement point and normal;
4. sample one point across the plane for a coarse Zone B suggestion.

Sampling uses Zone AABBs and excludes Zone A. One result is applied; zero leaves
Zone B unresolved; multiple leave it unresolved. The inspector's suggestion
command displays every overlapping candidate in an explicit-choice modal.
References stored on the Dock are always authoritative. Probe disagreement is
only a warning.

The recipe is never invoked by component addition, scene load, duplicate,
paste, undo restore, import, or migration.

Teleport authoring is a separate contextual command in the World panel. Focus
the source Zone, choose **Teleport Link**, select the destination Zone and
directionality, and create one `WorldLink` in the world scene. There is no
"teleport-eligible room" flag and no spatial transform or AABB edit for a
teleport. The Graph Viewer remains read-only; it does not own this workflow.

Legacy-transition resolution states this distinction directly. Existing
Teleport records migrate to WorldLinks. Doorway/Seam records normally require a
new, explicitly placed Dock followed by discarding the replaced legacy rows. A
per-row explicit conversion is available only for the designer to declare that
an old geometric record was actually a non-spatial teleport; reciprocal rows
collapse into one bidirectional WorldLink.

## 6. Lifecycle semantics

| Operation | Contract |
| --- | --- |
| explicit create | Run contextual recipe once; mint identity and apply active-Zone default. |
| manual component addition | Add schema defaults only; validation exposes unresolved identity/references. |
| edit | Preview then commit one command; revalidate on commit/undo/redo. |
| duplicate/paste | Capture snapshots, mint each connection ID once per batch, remap copied gate bindings, then restore snapshots. |
| undo/redo | Restore stored IDs, references, transforms, and dimensions without rerunning defaults. |
| load/save | Round-trip authored values including invalid zero references; never synthesize side data. |
| cook | Reject errors, emit two reciprocal endpoint views with one ID, and write debug ID-to-entity maps. |
| delete Dock | Remove one authored edge; dangling gate bindings become validation errors, not cascade deletes. |
| legacy migration | Reciprocal non-geometric transition pairs collapse into one `WorldLink`. Geometric records remain unresolved until replaced by an authored Dock and discarded, or explicitly reclassified by the designer as a Teleport Link. Obsolete connection policy is discarded. |

Creation focuses the world scene before executing the create command so the
focus-change reset cannot erase the newly-created undo entry.

## 7. Cook contract

For every valid Dock, `WorldTopologyCook` emits exactly two `DockEndpoint`
records with the same `DockId`:

- Side A owns Zone A and points to Zone B;
- Side B owns Zone B and points to Zone A;
- origin and half extents match;
- normals and right axes are opposite; up and direction bits match.

Links follow the same two-view identity rule without geometry. Output ordering
is deterministic by owning Zone and stable connection ID. Multiple IDs between
the same Zone pair remain multiple records and multiple logical graph edges.
Manifest format version 4 is the canonical writer. Readers tolerate unknown
legacy fields only to migrate old content; writers never reproduce them.

## 8. Validation

Editor validation reports:

- invalid/non-finite/degenerate Zone AABB;
- missing Graph or Zone references;
- invalid or duplicate Dock/Link IDs;
- same-Zone Dock endpoints;
- missing/invalid transform, non-unit Dock entity scale, degenerate plane, or
  invalid direction bits;
- Dock AABB probe disagreement as a warning only;
- invalid Link kind/direction/reference data;
- dangling `DockGateBinding` after Dock deletion.

Cooked validation additionally requires exactly two reciprocal endpoint views
per stable ID and rejects endpoint geometry or semantic disagreement. Distinct
IDs connecting the same Zone pair are valid. Late destination readiness is a
runtime telemetry event and threshold clamp, not a content validation error.

## 9. Graph Viewer v1

Mandatory v1 is a separate dockable 3D panel with:

- one sphere per Zone at `Bounds.Center()`;
- one individually pickable edge per Dock or Link;
- direction arrows and stable offsets for parallel edges;
- filters for graph, text, edge kind, cross-Graph, one-way, and validation;
- frame-all/frame-selected and basic camera projection controls;
- one shared Zone selection with the World panel and Zone AABB viewport
  affordance;
- entity selection for Dock/Link edges;
- double-click Zone focus/frame and physical Dock frame.

V1 is read-only topology presentation. RAM/VRAM overlays, residency simulation,
gate-state overlays, reachability analysis, Graph Viewer link creation, direction editing,
edge deletion, and runtime-preview filters are deferred. Those features are not
nearly free in the current repository and would turn the panel into another
world editor or policy engine. Graph Viewer consumes validation records and
document selection; it does not recompute either.

The World panel continues to own Graph/Zone organization, Graph policy fields,
document state, and validation badges. It has no flat connection list.

## 10. Deletion ledger

Removed from runtime/editor components, schemas, manifest writers, cook output,
demand, inspectors, viewport rendering, handles, validators, migration, and
tests:

- fields/types: `SideAArmBounds`, `SideBArmBounds`, `ArmBounds`,
  `PreloadPriority`, `PreloadDepth`, `DemandCondition`, connection required-tags;
- UI/commands: Reset Arm Bounds, per-side boxes, side-box handles, world-tag
  preview editing;
- runtime/cook: `DockApproach`, approach sampling, arm crossing, wrong-side arm
  checks, endpoint graph duplication, endpoint policy payloads;
- streaming semantics: tag-gated loading and connection-specific neighborhood
  depth/priority;
- Zone products: editable cell/prism ownership geometry, containment products,
  split/adjacent-cell workflows, generated ownership artifacts, and map contours
  derived from them;
- Graph Viewer scope: policy simulation and topology editing.

The deleted `WorldTagList` editor helper and its test/CMake registration must
remain absent. Legacy JSON keys may appear only in migration-read fixtures that
prove they are ignored and never written.

## 11. Verification matrix

Headless tests cover one cached/override Zone AABB, invalid AABBs, legal overlap,
no generated ownership artifact, contextual Dock and Teleport Link creation,
ambiguous suggestions, unresolved-reference round trip, fixed-opposite-edge
rectangle and AABB manipulation math,
grid-snapped edits, Swap Sides, duplicate/paste ID remap, undo/redo preservation,
two endpoint views for one edge, parallel edges, deterministic cook, no
serialized side payload, more-than-two-Zone graph demand, cross-Graph seeding,
closed-gate topology, bounded/fast/diagonal/horizontal crossing,
jitter/reversal, late clamps, and shared Zone selection state.

Manual Kyusu gates before release:

1. create, transform, rotate, independently resize every edge while the opposite
   edge remains fixed, snap, undo, redo, duplicate, paste, save, reload, Swap
   Sides, and delete a Dock;
2. confirm only the plane/arrows/labels render—never side boxes;
3. edit a Zone AABB in derived and override modes;
4. verify ambiguous suggestion modal and warning decoration;
5. verify Graph Viewer/World/viewport selection synchronization, parallel edge
   picking, filters, framing, and Dock double-click focus;
6. run a late-residency traversal and verify the character remains coherently
   clamped at the source threshold until physics residency is ready.
7. create bidirectional and one-way Teleport Links from the World panel and
   resolve legacy Teleport/Doorway/Seam rows without any room eligibility flag.

## 12. Reviewable implementation stages

The corrective work is partitioned so each stage builds and tests:

1. remove authored/cooked connection policy and side payloads; bump canonical
   manifest writer;
2. replace demand and crossing with graph policy and swept bounded-plane logic;
3. add defined late-residency clamp/telemetry and real character integration;
4. narrow Dock adapter/affordances and repair creation/duplicate/load lifecycle;
5. add explicit ambiguous suggestions and shared Zone selection;
6. reduce Graph Viewer to read-only v1 and support parallel edges/framing;
7. replace Plans 11/12 and mark earlier contradictory plans superseded;
8. run focused suites, the full build, full tests, and manual viewport gates.

No stage may temporarily serialize hidden side payloads, infer topology from
AABB contact, or preserve obsolete policy under a new name.
