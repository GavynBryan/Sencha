# Level spatial model and authoring operations

Status: **revision 3, approved 2026-10-11, with decisions D1 to D8 as
recommended in section 10. Implementation starts with M0.** Revision 1 was
discussed in conversation; its review is dispositioned in section 11. The
second review, including where this plan pushes back on it, is dispositioned
in section 12. Section 6.7 draws on the decision records of SINR's room-logic
editor (`../sinr-2d/sinr/docs/decisions/`).

Goal: a level author, human or automated, can inspect a level, refer to its
spaces, propose connected changes, review them, and keep editing with Kyusu's
ordinary tools without the model breaking. Agents are one client of the same
public mechanisms Kyusu's tools use. Nothing here is built for agents only.

Status tags used throughout:

- **[exists]** verified in source on `claude/kyusu-shoji` at 7080fcb5, with the
  file named.
- **[planned]** described in a plan document, not built. The document is named.
- **[new]** proposed by this plan.

The plan has two parts with separate code ownership. Part A, authoring
operations, is editor-wide infrastructure in `editor/common`; nothing in it
knows what a space is. Part B, the spatial model, is level-editor
functionality in `editor/level_editor` that consumes Part A.

---

## 1. Invariants and ownership

1. **One owner per authored fact. Relationships derivable from authored facts
   are derived, never stored.** Which spaces an opening joins, which spaces are
   adjacent, and what is reachable are computed, not saved.
2. **Spatial identity is independent of how geometry was produced.** A space
   keeps its identity, name and relationships whether its walls are generated,
   hand-modelled, or both. Removing generation never invalidates a space.
3. **Generated geometry is never canonical.** It is a pure function of declared
   inputs, is never saved, and nothing stores a reference to it.
4. **Architecture, navigation, streaming and gameplay keep their own owners.**
   No layer infers another layer's authoritative facts. Analysis may *propose*
   authored facts. A proposal becomes authored only when accepted.
5. **Zone connectivity stays explicitly authored** (world-partition plan 09,
   P-D2). Geometry can help place a dock; it never creates or binds one.
6. **Every mutation is a change set.** An operation or a batch applies
   completely or not at all, and applying it is one undoable command.
7. **Queries never mutate. Mutations check their preconditions** against the
   records they touch.
8. **Analysis results carry provenance.** Each result says whether it was
   measured from geometry or navigation, assumed from a scenario input, or
   inferred by a heuristic. These are never presented as one kind.
9. **A proposal is a document, not a gesture.** It holds no live-scene state
   until accepted, so focus loss, tool use and other workspaces cannot disturb
   it.
10. **The runtime is untouched** unless a named runtime consumer adopts a
    compact cooked representation, which would be separate from authoring data.
11. **Derive measurements; store intent.** A fact that follows
    deterministically from geometry may be derived, such as which volumes an
    opening touches or whether a capsule fits. Anything a designer *means* is
    stored where they said it: that this region is "the hallway", that these
    brushes move together. The editor never infers intent and never invents
    vocabulary the data doesn't contain. SINR's derived logic graph broke on
    exactly this point (decisions 27, 35, 39).

| Concern | Owner | Status |
|---|---|---|
| Solid level geometry | brush entities and the brush sidecar | [exists] `EditorScene`, `BrushMeshStore` |
| Space identity and extent | a volume brush entity carrying the `space` tag | [new] |
| Generated walls | the `shell` generator, derived at evaluation | [new] |
| Architectural openings | `opening` entities | [new] |
| Walkable surface, traversal kinds | navigation (`NavLink`, `NavQueryRequest::Capabilities`) | [exists] `engine/include/navigation/` |
| Streaming topology | `WorldDock`, `WorldLink` in the world scene | [exists] `WorldConnectionComponents.h` |
| Gameplay conditions | LevelFlow and gameplay authoring | [planned] `docs/plans/common-authored-api.md` |
| Reachability, diagnostics, floor plans | analysis services, derived on demand | [new] |
| Project authoring conventions | the `level.conventions` data asset | [new] |
| Proposals, analysis caches, client sessions | editor services, never documents | [new] |

---

## 2. Data model

### 2.1 What is authored

**Space: a volume brush tagged `space`.** A space is a named entity whose
brush describes the *air*, the region a person would call "the hallway". It is
edited with every existing half-edge tool. That makes stepped floors,
mezzanines, irregular ceilings, shafts and courtyards ordinary shapes rather
than special cases. Stacked spaces are separate volumes, and a balcony inside
a hall is a nested volume.

- `space` is a zero-size tag component, following the ECS rule that markers
  are tags. Its name is the existing `name` component. It needs no fields;
  every field considered so far either duplicated geometry or belonged to
  generation.
- A brush is either solid or a volume. Today every brush is solid. The roadmap
  already plans this split as `BrushRole` volumes ([planned]
  `docs/plans/engine-roadmap.md` §8b tool 10: triggers, reverb, streaming
  hints, nav area paint). This plan proposes reducing that role to a closed
  `Solid | Volume` distinction. *What a volume means* comes from the
  components on its entity (`space`, and later trigger or reverb components),
  not from a role enum that grows with each use. Volumes never reach solid
  cook cells. Kyusu draws them translucent.

**Shell: generated walls around a volume.** `shell` is a component on the
same entity with a single field, `thickness`, defaulting from conventions. It
derives solid wall slabs around the volume (section 5). Wall materials come
from the volume's own face materials, which are otherwise unused because a
volume never renders. A face painted with the project's designated open-face
material gets no slab. That is how a courtyard leaves its top open, or a room
leaves a side open to a balcony. Designers already paint face materials, so no
new per-face editing surface is needed (decision D3).

**Opening: a cut through generated walls.** `opening` is an entity whose
transform is the opening's frame, with fields for size and shape (rectangle or
arch, reusing [exists] `CarveShapeOutline` in `brush/CarveShape.h`).

- It cuts every generated slab its prism crosses.
- It stores no reference to the spaces it joins. Which spaces lie on either
  side is derived, and so cannot go stale.
- It is also a referent: "the north door" resolves to it.
- It does not cut hand-modelled brushes. Those keep the existing destructive
  carve tool.

**Ownership and containment are different relations, and neither is new
data.** "Spatial membership" splits in two:
- **Ownership** is what moves, hides and duplicates with the space. It is the
  existing entity hierarchy ([exists] `Parent`, `EditorScene::SetParent`,
  `ReparentEntitiesCommand`). Detached walls, an opening created from a room,
  and props placed in it are its children by authored choice.
- **Containment** is what lies inside the volume. It is measured on demand.

A pillar can be owned by one space and contained in another, and both answers
are correct. Storing a membership list on the space would duplicate one of
these relations and drift from it.

**A corridor is a space.** It is a long volume with openings at its ends. It
has no separate generator kind and no path primitive.

**Note.** `note` is an editor-only component with `text`, `author` and
`state` (open or resolved). Parenting the note to an entity is how it refers
to that entity.

**Conventions.** `level.conventions` is a project data asset ([exists]
mechanism: `DataAssetTypeRegistry`, `DataSchemaRegistry`, `.sdata`). It holds
only fields with a named consumer: shell thickness, opening default size,
default space height, the open-face material, and default wall, floor and
ceiling materials.

### 2.2 What is derived (never saved)

| Derived fact | Computed from | Provenance |
|---|---|---|
| Shell slabs | volume, `shell`, neighbouring volumes, intersecting openings | measured |
| Which spaces an opening joins | opening frame against volumes | measured |
| Walkable surface, clearance, reachability | in-memory navigation build of solid geometry | measured, plus the scenario's assumptions |
| Space proposals for unannotated levels | navigation segmentation | inferred (output is a proposal) |
| Floor plans | horizontal section of solid geometry and volumes | measured |
| Diagnostics | the above | per rule |

### 2.3 What is deliberately not modelled

- **Space types** (room, corridor, hall). The name, and whoever reads the level,
  supply that meaning; the model stays one concept.
- **Parametric footprints and path corridors.** Revision 1 proposed a prism
  space with polygon and path footprint forms. That is replaced by the volume
  brush, which the existing tools already edit.
- **Persistent spatial constraints** ("the corridor stays attached"). Stretching
  a corridor is an operation that resizes its volume's end faces, not a live
  constraint solver.
- **Harvesting edits on generated geometry.** The volume is the control
  surface. An edit that cannot be expressed on the volume detaches the shell
  (section 5.5).
- **Gameplay conditions in the spatial model.** These belong to LevelFlow and
  gameplay authoring (section 6.5).
- **A persisted capability-profile asset.** Scenarios are query parameters
  (section 6.4).

### 2.4 Is the ECS component model the right owner?

Only for entity-local authored facts. `space`, `shell`, `opening` and `note`
describe one entity each, and as components they inherit what Kyusu already
provides for entities: persistent identity, hierarchy, transform composition,
the inspector, selection, undo and serialization. Geometry stays in the
existing brush sidecar. Project policy goes in a data asset. Relationships are
derived and stored nowhere. Session state goes in editor services.

**These are authoring components, not runtime components.** The level
editor's registry is an authoring store already: `name`, `brush` and
`baked_brush` live there and never reach the runtime ([exists]
`DocumentSerialization.cpp:63-66`). The concern that this forces document
semantics into a runtime model doesn't apply. The editor's ECS is the document
model, and it gives these facts identity, hierarchy, the inspector, undo and
serialization for free.

**Rejected alternative: per-document sets keyed by persistent id**, the way
hide and lock work today ([exists] `EditorScene.h:274-275`, serialized as
entity-record flags). This suits a one-bit editing affordance. It would need
its own serialization, undo, inspector and copy-paste handling, all of which
components already have, and it would make `space` the one semantic fact that
didn't behave like every other.

**Rejected alternative: a separate spatial document** holding a graph of
spaces and openings, like `.sworld` holds zones. It would duplicate entity
identity, transforms, undo and selection, and it would split a room's identity
from its geometry.

Everything new is editor-only, and the cook strips it by declaration, not by a
hard-coded list (section 9, item 5).

---

## 3. Alternatives considered

| Decision | Chosen | Rejected and why |
|---|---|---|
| Space extent | authored volume brush | **Prism or path** (rev. 1): too restrictive for general architecture. **The bounding brushes**: shared walls belong to two spaces, and the space is the air, not the walls. **A named group entity with no volume** (identity plus children): the smallest option, but its extent is only the union of its members' bounds, which is not the air. Containment ("which space is the camera in") becomes a guess, overlap and nesting can't be expressed, and generation has no input shape. **Seed point plus derived extent**: boundaries are heuristic and move as unrelated geometry changes. Kept only as the inference method, which outputs proposals. |
| References to generated geometry | none; references resolve at execution through provenance | **Stable names for generated faces**: this is CAD's topological naming problem. Every regeneration can renumber, split or merge faces, and persistent references to them are a known long-term trap. Nothing stores one. |
| Draft storage | full clone of the affected zone documents | **Copy-on-write over the live registry**: needs an overlay at the ECS storage level that nothing else would use. Adopt it only if R1 measurements fail. |
| Delivering a proposal to the editor | proposal file first, live socket later | **Socket first**: forces the transport, the sessions and the event stream into the first slice before the model is proven. A proposal is already a replayable operation list, so a file is its natural form. |
| Editing generated geometry | edit the volume, or detach | **Harvest**: mapping arbitrary half-edge edits back into parameters is the hard, unsolved part, and the volume already gives direct control. |
| Proposal transactions | draft document plus change set | **Live scene plus accumulated inverses**: the save path force-commits pending edits ([exists] `ResolvePendingEdits`, `EditorWorkspace.cpp:141`), only one pending edit can be open, an undo in any workspace cancels it (`DocumentSourceSet.cpp:55`), and regeneration caches would see transient state. **Live scene plus snapshots**: fixes rollback but keeps every other coupling. |
| Opening relationships | derived from placement | **Stored space ids**: duplicated state that goes stale whenever a volume moves. |
| Gameplay conditions | scenario inputs now; LevelFlow later | **Condition fields on openings or spaces**: a parallel gameplay-logic system. |
| Shared walls | gap rule (section 5.3) | **A CSG kernel**: a major subsystem the editor has never needed. Revisit if the gap rule fails real levels (R3). |
| Result format | schema-typed JSON5 values | **Prose `ConsoleResult` entries** ([exists] `ConsoleTypes.h:152`): clients would have to parse text. |
| Change tracking | record digests | **Instrumenting every command**: `ICommand` has no affected-object reporting, and adding it everywhere touches every command in the tree. |
| Overview image | CPU floor-plan section | **GPU screenshot only**: needs a live window and isn't deterministic. Screenshots stay available live ([exists] `editor.window.screenshot`). |
| Transport | console commands plus a local socket plus a CLI | **MCP**: tool schemas sit in agent context every session. A thin MCP client can be added later over the same socket. |

---

## 4. Part A: authoring operations

### 4.1 Operation contract

An operation is a definition in an `AuthoredCatalog<EditorOperationTraits>`
([exists] `engine/include/authored/AuthoredCatalog.h`: name grammar, atomic
per-provider registration, provider retirement, contract revisions and
handles). The catalog is generic, so the editor instantiates it with its own
traits and the engine is unchanged. Each definition holds:

- **id**: dotted and mechanical, e.g. `brush.box`, `space.create`,
  `query.near`.
- **kind**: `query` or `mutation`.
- **parameter schema and result schema**: `DataFieldSchema` records ([exists]
  `engine/include/core/metadata/DataSchema.h`), which provide constraints,
  enum choices, units and descriptions. Arguments are checked and their
  defaults filled by `NormalizeDataValue`, the one JSON-against-schema check
  that data assets and verb bindings also use.
- **executor**:
  - a query reads a document view and returns a result value;
  - a mutation writes through a change recorder and returns a result value.

The executor never knows whether its target is the live document or a draft.

Operations share kernels and command machinery with the tools; they are not a
second implementation:

- Mutations call the same pure functions the tools call: `BrushOps`,
  `CarvePolygon`, `CarveShape`, `MeshEditService` ([exists]).
- Kyusu's tools gather parameters interactively and commit through an
  operation. M1 ports the move gizmo and the Space tool as the proof (R5), and
  M2 ports `BrushTool`. Other tools
  migrate when their commit path is next changed, recorded in
  `docs/deferred.md`.

### 4.2 Subjects and identity

- **Stored references are always persistent ids** ([exists] 16-hex
  `EntityId`, `engine/include/core/identity/Id.h`).
- **Subject references accepted by operations:**
  - `#<id>`;
  - `@<name>` (names aren't unique, so ambiguity is an error that lists the
    candidates);
  - `selection`, `aim`;
  - `box(min, max)`;
  - `face(<subject>, point, direction)`.
- **Results always return the resolved persistent ids**, so a client can switch
  to ids immediately.
- **A face is never stored by index.** `face(...)` resolves at execution time.
  A face of generated geometry resolves to the volume face that produced it,
  using the slab's provenance (section 5.2).

### 4.3 Results

Every operation returns a value that conforms to its result schema. A mutation
result:

```
{ status: "ok" | "refused" | "conflict" | "error",
  code?, message?, subject?, candidates?,            // when not ok
  revision: { before, after },
  changes: { created: [{id, name, kind}], modified: [...], deleted: [...] },
  derived: [{id, what}],                 // e.g. shells regenerated because of this change
  diagnostics: { added: [ContentRiskRecord], resolved: [rule, subject] },
  data?: { ...operation-specific, e.g. resolved ids, measured values } }
```

- **Diagnostics** use [exists] `ContentRiskRecord`
  (`engine/include/zone/ContentRiskRecord.h`: severity, dotted rule id,
  message, subjects). The world-partition rules already emit it, and the Graph
  Viewer is designed to consume it.
- **`derived`** lists the generated outputs whose inputs the change touched,
  read from the evaluator's dependency sets (5.6). It is deliberately not a
  transitive impact analysis. A client that wants more asks `deps`.
- **The console** prints each result as one line of JSON. **The socket and the
  headless host** pass the same JSON through. `ConsoleResult` keeps its
  layout: game modules register console commands, so it crosses the module
  boundary.

### 4.4 Change sets, revisions and preconditions

- **Change recorder.** It snapshots an entity's records (entity record plus any
  brush records it references) before the operation first touches it, and
  again after.
- **Change set.** The list of `{id, before?, after?}`.
- **`ApplyChangeSetCommand`** writes `after` on execute and `before` on undo.
  Kyusu already has entity snapshot machinery ([exists]
  `CreateSnapshotEntityCommand`, `EditorEntityRecipe`). One generic command
  replaces bespoke inverses for every mutation.
- **Revision.** `EditorDocument` gains a revision counter, bumped on every
  execute, undo and redo. Today it has only a dirty flag (`EditorDocument.h:61`).
- **Precondition.** Applying a change set requires each touched entity's
  current record digest to equal its `before` digest. A mismatch refuses with
  `conflict` and the conflicting ids. The check is per entity rather than a
  whole-document revision check, so a designer can keep working elsewhere
  while a proposal waits.
- **Determinism.** The same base, operations and parameters produce the same
  change set, including newly minted ids. Id minting inside an operation draws
  from a per-transaction seeded source instead of `EditorScene`'s ambient RNG
  (`EditorScene.cpp:111`).

### 4.5 Drafts (proposal transactions)

- **A draft is a clone of the affected documents** (the focus zone, or the
  zones a batch touches), cloned from the live document's records at a
  recorded base.
- **Operations execute against the draft.** A failure discards nothing live,
  because nothing live was touched.
- **Diff:** the draft's change set against its base, plus the change in
  diagnostics. Draft and base are analysed only over the affected region.
- **Accept:** the precondition check, then one labelled
  `ApplyChangeSetCommand` on the live stack. Undo and redo then behave like any
  other command.
- **Reject:** discard the draft.
- **Conflict:** refuse, and offer **rebase**, which replays the draft's
  operation list on a fresh clone. This works because every step is a
  parameterized operation.
- **Lifetime:**
  - survives focus loss, tool use, other workspaces' undo, and saving or
    cooking the live document (the draft is not part of it);
  - discarded, with a notice, when its base document closes or is replaced,
    and at shutdown.
- **Several drafts may exist**, which is how variations work. One is previewed
  at a time.
- **Preview:** the renderer draws the draft's created and modified entities in
  a ghost style and marks deleted ones. It already draws several documents in
  one view, with a tinted wash on context zones ([exists]
  `EditorRenderFeature.cpp:270-278, 575-620`). Picking ignores draft entities.
- **The same mechanism serves humans.** A console batch runs `draft.begin`,
  then operations, then `draft.diff`, then `draft.accept` or `draft.discard`.
  Kyusu shows a proposal tray with each draft's label, change counts and
  diagnostics delta. `proposal.accept` and `proposal.reject` are catalog
  actions with keybinds.

**Immediate mutations** (a tool commit, a single console operation) run the
same executor directly against the live document through the recorder. The
code path is the same; the only difference is the target.

**Batches are atomic.** If step *k* of a draft fails, the draft records the
failure at *k* and keeps its state up to *k − 1*. The client either discards
the draft or appends corrected steps. A failed draft cannot be accepted.

**A proposal file** is the serialized draft:
```
{ label, rationale, author,
  base: { document: <asset id>, digests: { <entity id>: <digest>, ... } },
  operations: [ { op: "space.create", params: { ... } }, ... ] }
```
- Opening the file in Kyusu replays its operations into a draft and shows it
  in the proposal tray.
- `rationale` is free text, the "why". The operation list is the "how". The
  change set and diagnostics delta are computed on load, never stored, so
  they can't disagree with the operations.
- This is how a headless client hands work to a designer before any live
  connection exists. It is also how a human saves and shares a batch.

**Failure modes from the review, and what handles each:**

| Failure | Handled by |
|---|---|
| Derived geometry regenerates mid-transaction | it regenerates inside the draft; the live document never sees it |
| One operation invalidates a reference another relies on | references are persistent ids resolved at execution; a missing id fails that step (batch atomicity) |
| A rebuild fails halfway | the draft fails at that step; nothing live was touched |
| The user edits something a proposal affects | per-entity digest precondition at accept, then `conflict` and rebase |
| The document closes or reloads | the draft is discarded with a notice; its proposal file, if saved, can be replayed later |
| A client works from an outdated revision | the same digest precondition; `changes.since` tells the client what moved |
| Undo restores geometry but not caches or relationships | caches are keyed by content hash and relationships are derived, so restored records restore everything; there is no separate state to restore |

### 4.6 Queries and context

The goal is that a client never re-reads a whole document.

| Query | Returns |
|---|---|
| `level.summary` | one line per space, plus counts per entity kind |
| `query.box`, `query.near` | entities and spaces in a region, with bounds and distance |
| `entity.describe`, `space.describe` | components, bounds, hierarchy; for a space: footprint section, floor and ceiling elevations, openings, measured neighbours |
| `deps` | what depends on a subject: generated slabs, openings cutting it, children, docks and gate bindings that name it |
| `changes.since <token>` | ids changed since the token (digest snapshot held per client session), each with a one-line summary |
| `view.context` | live only: camera pose and compass facing, aim hit, selection, the space containing the camera, visible spaces |
| `plan.section <elevation> [box]` | floor plan as text polygons per space, optionally a PNG ([exists] `stb_image_write`, used in `FrameImageCapture.cpp`) |
| `measure.clearance <opening or point>` | width and height against the navigation profile's radius and height |
| `nav.reachable <from> <to> [scenario]` | yes, no or unknown, with a route summary and the assumptions used |
| `conventions.get`, `diagnostics [region or ids]` | as named |

**Compass:** north is −Z and east is +X. This matches [exists] `Vec.h:234`
(forward is −Z, right is +X).

### 4.7 Transports and hosts

- **Kyusu console:** operations register as console commands. [exists]
  `ConsoleRegistry`/`ConsoleService` are standalone core types (`test/core/ConsoleTests.cpp`).
  They register into the **level workspace's** lifetime, so they are
  unregistered on close, and they target that workspace explicitly. Today's
  level commands are never unregistered (section 9, item 1).
- **Headless host (M0):**
  - a binary that links `level_authoring` (no UI, no Vulkan);
  - opens a project and a level, runs an operation script, and prints results;
  - writes proposal files (4.5) for a designer to open;
  - precedent: [exists] `test/level_cook/TraversalWorldGen.cpp`.
- **Proposal files (M1):** the hand-off from any client to a designer in Kyusu,
  with no live connection needed.
- **Local socket (M2):**
  - polled from Kyusu's workspace tick on the owner thread;
  - off by default, with a per-user path;
  - line requests carry a request id; replies carry the result value;
  - an event stream reports selection changes ([exists]
    `SelectionService::Subscribe`), saves ([exists]
    `DocumentSourceSet::SubscribeReport`), and draft accept or reject.

  The engine's stdin feed cannot serve this role. It is polled only when there
  is no window (`EngineFramePhases.cpp:108-116`), and it has no reply path.
- **A small CLI client (M2)** for the socket, which the skill calls through
  Bash.

---

## 5. Part B: geometry generation

### 5.1 Supported shapes, version 1

Any closed volume with planar faces whose slabs are non-degenerate at the
requested thickness. That covers boxes, L and U plans, stepped floors,
mezzanines, sloped and irregular ceilings, shafts and courtyards.

A volume that fails is **refused, not approximated**:
- the generator emits `shell.unsupported` naming the face and the reason (open
  volume, non-planar face, slab inverted because the thickness exceeds a local
  feature);
- no slabs are produced for that space;
- the space itself stays fully valid.

### 5.2 Slabs and joins

- **One slab per non-open face.** The face polygon is extruded outward by
  `thickness`. Each slab carries its source face (the provenance used by
  subject resolution).
- **Two join rules bound each slab at its edges:**
  - **Wall to wall, or floor to floor** (both faces in the same orientation
    class): cut at the bisector plane of the two faces.
  - **Floor or ceiling to wall:** the horizontal-class slab runs under or over
    the wall slab, and the wall slab is capped on the horizontal face's plane.
    This keeps a wall's inner and outer faces sharing a bottom edge, which an
    opening at floor level needs in order to become a doorway notch rather than
    a window with a sill (5.4).
  - Orientation class comes from the face normal against world up.
- **An edge next to an open face** caps the slab on the open face's plane.
- **Where more than three faces meet at a vertex**, bisector cuts may leave a
  small gap or overlap at the corner. This is reported as a `shell.vertex_join`
  note, an approximation. It is never silent.

### 5.3 Shared walls, intersections and overlaps

- **Shared walls (the gap rule).** Two volumes whose faces are antiparallel,
  separated by exactly `thickness` (within tolerance), and overlapping in
  projection share a wall over the overlap region:
  - one slab is emitted for the overlap, owned by the lower persistent id;
  - each side keeps its own face material;
  - the rest of each face gets its own slab.
  - The overlap region is a polygon intersection in the face plane. Partial
    overlaps and different floor elevations need no special handling.
- **T-junctions.** A wall of one space meeting the shared wall of two others is
  an ordinary partial overlap plus the join rules.
- **Volumes closer than `thickness` apart:** slabs would interpenetrate. They
  are generated, and `shell.overlap` is emitted, a warning that visible
  z-fighting is possible.
- **Shelled volumes that intersect each other:** `space.overlap` is emitted,
  and **both shells are refused** until the overlap is resolved.
  - A space *without* a shell may sit inside another; that is how a balcony or
    a sub-area is expressed.
  - Separating `space` from `shell` is what makes this case simple.

### 5.4 Openings

After slabs are built:
- every slab crossed by an opening's prism is carved with [exists]
  `CarveFacePolygonThrough` (`brush/CarvePolygon.h:87`), with the slab as host;
- a slab's inner and outer faces are exactly the opposite pair that kernel
  expects;
- an outline lying on the slab's rim becomes a notch, which is how a doorway
  differs from a window;
- a shared wall is one slab, so it is cut once;
- a carve that refuses (`CarveStatus`) emits `opening.refused` with the
  kernel's reason.

### 5.5 Edits, detach, and staying in agreement

- **Generated slabs are never edit targets.** Kyusu already treats generated
  pieces this way (`viewport/Picking.cpp:330-371`). Clicking one selects the
  space. A face edit on a slab resolves to the volume face it came from, so
  pushing a wall moves the volume face and the shell follows.
- **An edit the volume can't express** (bevelling only the outside of one wall,
  say) needs **detach**:
  - one command materialises the current slabs, with openings already carved,
    as ordinary brush entities parented under the space, and removes `shell`;
  - the space, its name, its openings and every derived relationship survive;
  - undo restores the shell;
  - a tool asked to edit generated geometry says so instead of doing something
    surprising.
- **While a shell exists, geometry and space cannot disagree:** the walls are a
  function of the space. After detach they can, and that is allowed. Analysis
  reports measured facts such as "space volume intersects solid geometry" or
  "space volume is not enclosed" as warnings, not errors.

### 5.6 Invalidation and evaluation

- **Inputs:** the volume's mesh and world transform, `shell`, the volumes
  within `thickness` plus tolerance (found by bounds broadphase), and the
  openings whose prisms intersect the shell.
- **Cache key:** a content hash of those inputs, using the existing mesh
  signatures. It does **not** use the brush store's revision-keyed single slot
  (`BrushMeshStore.cpp:105-118`). That key ignores external inputs, and cook
  and preview policies evict each other from it.
- **Work** is proportional to the spaces whose input hash changed, times their
  neighbours.
- **Evaluation is in world space through composed transforms.** This depends
  on the transform-propagation defect being settled first (section 9, item 4).
- **Cook:** `CollectCookBrushes` ([exists] `BrushCookInput.cpp:67`) gathers
  shell slabs for shelled volumes and skips volumes themselves.
  - Fingerprints, cell clustering, collision, navigation and lighting then
    follow unchanged, because they all consume `snapshot.Brushes`.
  - The zone halo collects through the same function.
  - Hidden spaces don't cook, consistent with hidden brushes today
    (`BrushCookInput.cpp:84`).

### 5.7 Moving a room with a connected corridor

This is the review's key case. There is no constraint solver. Instead, an
operation does one predictable thing, and the derived relationships report
the result.

- **`space.move <space> <offset> [carry]`** moves the space and its owned
  children, including openings it owns. With `carry`, for each opening the
  space owns that joins a neighbouring space:
  - it finds the neighbour's volume face that forms the shared wall at that
    opening;
  - it translates that face by the offset's component along the face normal,
    using [exists] `MeshEditService` face translation on the neighbour's
    volume.

  A corridor therefore stretches or shrinks when the room moves toward or away
  from it, and the gap rule keeps the shared wall.
- **Movement along the wall** can't be absorbed by moving one face. The
  opening moves with its room; if it slides off the corridor's end face,
  `opening.joins_one_space` is reported. Carry never shears, bends or reroutes
  a corridor. Rerouting is a new proposal, not a side effect.
- **A neighbour whose shell was detached** has nothing parametric to carry.
  Carry skips it and reports `carry.skipped_detached`, and its hand-modelled
  walls are left alone. Silently editing freeform geometry would break
  invariant 2.
- **The gizmo uses the same rule.** If Kyusu's move gizmo did not commit
  through this operation, a designer and an agent moving the same room would
  get different results. Porting the gizmo's commit is part of milestone 1
  (decision D5).

**Acceptance (milestone 1):**
1. Move a room that a corridor joins: the corridor stretches, and the shared
   wall and opening stay valid.
2. Detach the corridor's shell and bevel it by hand: the space, its openings
   and reachability survive.
3. Move the room again: carry skips the detached corridor and reports it, and
   the diagnostics name the misaligned opening.
4. Undo every step: the original state, including regenerated shells, comes
   back.

---

## 6. Spaces, navigation, zones and gameplay

### 6.1 Architecture and navigation

- **Navigation is built from solid geometry**, including shell slabs, by the
  existing cook functions in memory ([exists] `CookZoneNavigation`,
  `ZoneNavigation::Load`; the test fixture does exactly this,
  `test/navigation/NavigationFixture.h:180-209`).
- **An opening never creates navigability.** Geometry does. Per opening,
  analysis reports measured clearance against the profile and whether the
  navmesh actually crosses it.
- **Non-walking traversal stays authored `NavLink`s.**

### 6.2 Zones and docks

- **Docks remain authored and authoritative.**
- **Analysis may report** `spatial.crossing_without_dock`: a navigable
  crossing or opening straddles zone bounds and no dock covers it.
- **The operation `dock.place_at <opening>`** proposes a dock fitted to an
  opening's frame. After acceptance the dock keeps no binding to the opening.
  Moving the opening later does not move the dock; it may raise a diagnostic.
  Geometry helps placement and never determines topology.
- **A space belongs to the zone document that holds it**, like any entity.

### 6.3 Four kinds of state, four owners

The review asks for SINR's distinction between structure, conditions,
persistent facts and transient state. In Sencha:

| Kind | Owner | Status | What spatial analysis does with it |
|---|---|---|---|
| Structure | spatial model (volumes, openings), navigation (`NavLink`), zones (`WorldDock`, `WorldLink`), LevelFlow graphs | spatial [new]; navigation and zones [exists]; LevelFlow [planned] | reads it as authored fact |
| Conditions | LevelFlow and game logic | [planned]; nothing authorable as data today (6.5) | reads declared conditions once they exist; until then, scenario inputs |
| Persistent facts | `ZoneStateStore` for per-zone changes; no save system | [exists] `engine/include/zone/ZoneStateStore.h` | scenario inputs only |
| Transient runtime state | ECS state at runtime: tags on entities, `NavLinkState` | [exists] | scenario inputs only |

The spatial model owns only the first row, and only its architectural part.
Everything else enters analysis as a declared assumption.

### 6.4 Reachability and scenarios

A reachability query takes a **scenario**, and every input is an existing
runtime concept:

- capabilities: [exists] `NavQueryRequest::Capabilities`, matched
  hierarchically against `NavLink.traversal`;
- link states: [exists] `ZoneNavigation::SetLinkState`; defaults come from
  authored `NavLinkState`;
- gated docks assumed open or closed: [exists] `DockGateBinding` marks which
  docks are gated.

The result lists the assumptions it relied on. Analysis defines no traversal
semantics of its own, so it has none to duplicate.

**Two walks, not one answer.** Every reachability query runs twice over the
same scenario:
- a **sure** walk, where anything unknown or unassumed counts as blocked;
- a **possible** walk, where it counts as open.

| sure | possible | Meaning |
|---|---|---|
| reachable | reachable | **reached:** verified under the stated assumptions |
| not reachable | reachable | **depends on:** the result lists which unknowns decide it |
| not reachable | not reachable | **unreached:** verified under the stated assumptions |

A gated region that the sure walk reaches without its key is a **bypass**.
That answers the review's point that a capability tag isn't evidence of what
happens at runtime: an unknown stays unknown and is never resolved
optimistically.

SINR learned this late. Its progression check answered only the generous
question, missed bypassable locks, and treated a bolt as open once reached
(`../sinr-2d/sinr/docs/decisions/59-locks-that-can-be-got-round.md`).

Named scenario presets are not persisted until someone needs them. Analysis
does not model jumps that have no authored link: inferred jumps would be a
heuristic, and they stay out of scope.

### 6.5 Gameplay conditions: verified state and integration point

- **No gameplay condition can be authored as data today.**
  - "Door opens with tag X", "lever enables bridge" and "ledge needs ability Y"
    have no representation.
  - **LevelFlow is [planned]** (`docs/gameplay/authored-api.md:4`,
    `docs/plans/common-authored-api.md:9-13`) with no code.
  - **T scripting is on the unmerged branch `claude/sencha-t-language-vm-bdrinb`**,
    not on this base.
  - **Abilities and effects are registered in C++**, not as data
    (`AbilityRegistry`).
  - **`GameplayTagQuery`** has no accessors and no serializer
    (`GameplayTagQuery.h:42-45`).
- **The spatial model will not fill this gap.** Entity-local conditions (a door
  that needs a tag), expressed without building a graph, are a requirement on
  LevelFlow and gameplay authoring.
- **When declared conditions exist, analysis reads them as scenario sources.**
  That is the integration point. Until then, conditions enter only as explicit
  scenario assumptions and are labelled as such.
- **Deferred entry:**
  - *what*: a static condition representation readable by analysis;
  - *trigger*: LevelFlow's condition or binding format landing;
  - *consumer*: scenario reachability.

### 6.6 Runtime

None in this plan, but the way a game asks for one is already settled.

- **The request mechanism exists.** Cook profiles select optional steps
  ([exists] `CookStepIds` in `editor/common/src/project/CookProfile.h`). A
  runtime spatial representation would be one more profile-selectable step,
  turned on by the projects that want it. Projects that don't want it pay
  nothing.
- **Its contents wait for a named consumer** (Loss Function's map screen,
  which-room queries, per-space audio). The consumer decides what the shape
  must be: space ids and names; extents as convex pieces or as
  navigation-region labels. Designing it now would be designing for an
  imagined consumer.
- **One caveat:** the cook's step list is a fixed array wired by hand
  ([exists] `CookGraph.cpp:29`, `DocumentCook.cpp`), so adding a step edits
  two central places. If that consumer arrives, the step list should become
  registered in the same change.
- **Deferred entry:** the trigger is a runtime consumer appearing in a plan.
- Authoring components stay editor-only and are stripped.

### 6.7 Lessons from SINR's room-logic editor

Source: `../sinr-2d/sinr/docs/decisions/`. SINR's logic authoring was
rewritten five times. The lessons that bear on this plan:

| SINR decision | What went wrong | Rule here |
|---|---|---|
| 27, 35 | A graph derived from scattered flags "showed the data's structure, not the design" | invariant 11: derive measurements, store intent |
| 39 | The editor kept explaining scattered conditions after the fact and invented words the data didn't contain | no condition fields on spatial objects; conditions live in one declared place (LevelFlow); analysis never names what the data doesn't |
| 36 | Facts had names but no owners | every fact has one owner (section 1 table); ownership of geometry is the hierarchy |
| 59 | Progression checked only the generous case and missed bypasses | sure and possible walks (6.4) |
| 58 | An implicit crossing between transient and persistent state caused a replay bug | drafts are transient, documents persistent; accept is the only crossing |
| 33 | Placed ids live in saved references, so renames had to rewrite them; engine defaults copied into the editor schema drifted | references are persistent ids, names are lookup only; conventions hold authoring choices and never copy capsule, step or navigation values from their owners |
| the editor skill | Its `SKILL.md` still describes a removed system | the skill's operation reference is generated from the registry, and a test fails when they diverge |

---

## 7. Roadmap: milestones proven end to end

Each milestone is a capability someone can demonstrate, not a set of systems.
A milestone ends when its acceptance passes as automated tests, plus an owner
walkthrough wherever interaction is involved. The next milestone starts only
after that. Each milestone builds only the operations its acceptance needs,
and the catalogue grows later.

### M0: see an existing level

**Capability:** a headless client inspects a real level, without changing it,
and gets structured, verifiable answers.

**Work:**
- Defect fixes (section 9).
- The editor-only declaration replacing the cook's strip list.
- One JSON-against-schema check (`NormalizeDataValue`) shared by data assets,
  verb bindings and operations.
- Operation registry core: an `AuthoredCatalog` of editor operations,
  parameter and result contracts, subject resolver, structured results,
  console adapter.
- The headless host.
- Queries: `level.summary`, `query.box`, `query.near`, `entity.describe`,
  `plan.section` (text and PNG), `measure.clearance`, `nav.reachable`
  (both walks), `diagnostics`.
- An in-memory navigation build.
- `level.conventions` as a data asset with its schema, read through
  `conventions.get`.

**Acceptance:**
- On `templates/fps` (`traversal3` and its rooms), a script returns
  schema-valid results: summary, floor plan PNG, and sure and possible
  reachability from player start to each dock.
- Running it twice gives byte-identical results.
- Loss Function's `test.sscene` gets the same treatment as an owner
  walkthrough. Its project isn't under this repository's tests, so a copy
  becomes a fixture if it proves useful.
- Malformed brush fixtures load with diagnostics instead of crashing.
- The parented-brush cook test settles section 9, item 4.
- Closing and reopening the level workspace leaves every level command
  registered once and live.

**Outcome (2026-10-12):** built, with these differences from the work list.
- `deps` moved to M1. In M0 nothing derives from an entity but its children,
  which `entity.describe` already reports; its purpose is derived outputs
  (shells, openings), which arrive with M1.
- `diagnostics` reports the world's existing validation records
  (`ContentRiskRecord`). No new rules were written for M0.
- Reachability is per zone. Navigation answers `CrossZoneUnsupported` across
  zones, so the acceptance asked it inside each zone, up to the docks that
  `world.summary` locates.
- `world.summary` was added so a client can find zones and docks at all; the
  zone queries read the focused zone only.
- `kyusu_batch` opens worlds with their user state ignored, so answers do not
  depend on, or write, anyone's `.sworld.user.json`.

The acceptance run on `traversal3` answered with structured data throughout
and found three real things: Room 2's Player Start stands on Zone 1's floor,
so reachability from it inside Room 2 is refused as off the walkable surface;
Zone 1's floor slab extends below its zone's bounds; and the editor's own
validation called Room 2 and Room 3 unreachable although docks join them
(section 9, item 8). The Loss Function walkthrough produced a floor plan whose
open runs show brushes with open surfaces.

### M1: propose connected spaces, review them, survive hand edits

**Capability:** the review's nine steps. The thinnest path through every layer
of the real model, with spaces included from the start.

**Work:**
- `deps`: the derived outputs, openings and children that depend on a subject.
- Command labels and document revision with record digests. Their consumers
  (the labelled accept and the precondition check) arrive here, so they do too.
- Volume brushes; `space`, `shell`, `opening`.
- Shell generation: join rules, open faces, the gap rule (shared faces convex
  in version 1, using [exists] `ClipPolygonToConvex2D`), openings through
  `CarveFacePolygonThrough`.
- Generated face to volume face resolution, and detach.
- The change recorder and `ApplyChangeSetCommand`.
- Drafts, batch atomicity, proposal files, the proposal tray and ghost preview
  in Kyusu.
- Mutations, and only these: `space.create`, `space.move` (with carry),
  `space.resize_face`, `opening.create`, `opening.move`, `shell.detach`,
  `entity.rename`.
- Kyusu's move gizmo and the Space tool commit through these operations.
- Diagnostics: `shell.unsupported`, `shell.overlap`, `space.overlap`,
  `opening.refused`, `opening.joins_one_space`, opening clearance,
  `carry.skipped_detached`.
- A minimal skill (v0): conventions, the operation reference generated from
  the registry, and the write-proposal-then-review loop.

**Acceptance experiment:**
1. A headless client inspects a zone and reads its conventions.
2. It writes a proposal file: three rooms, a corridor and openings.
3. Kyusu opens it as a draft. The designer sees ghosts, the change list and the
   diagnostics delta.
4. A second proposal moves a room with carry. The corridor stretches; a
   sideways move reports the misaligned opening.
5. Clearance and connectivity are checked by both walks.
6. The designer rejects one proposal and accepts the other as one undoable
   step. Undo and redo both work.
7. The designer detaches the corridor shell and bevels it by hand. The space,
   openings and reachability survive (5.7).
8. Save, reload, cook. The cooked level matches the accepted proposal, and
   navigation reaches every room.
9. Replaying the same proposal file on the same base gives an identical change
   set and identical ids.

**Tests:**
- A competing edit forces a conflict, and rebase succeeds.
- A failing step leaves the live document byte-identical.
- Focus loss leaves the draft intact; closing the document discards it.
- The shape matrix: mezzanine, courtyard, stacked spaces, shaft, partial and
  offset shared walls, T-junction, different floor elevations, and one
  deliberately unsupported volume.

**Progress notes (in flight):**
- One `LevelTarget` is what every level operation reads and writes: the draft
  being written, else the focus level. `Mutate` makes each mutation atomic (a
  refusal puts back what it had changed) and adds the change summary, so no
  operation repeats either. Queries read the draft too, so a batch can measure
  what it just proposed.
- `draft.begin` makes the new draft the one written; `draft.write` switches,
  or returns to the level. A refused step leaves the draft `failed` with the
  steps before it; `draft.resume` is the explicit way back to `open`. Draft
  state is a plain enum.
- A draft names its level by the document's instance (minted once per
  `EditorDocument`, never reused) and resolves it again at accept and rebase,
  refusing with `draft.base_gone` when it is no longer open; the asset path is
  its identity only in proposal files. The level workspace drops a draft whose
  level has closed or been replaced, reload included, with a notice, checking
  each frame rather than holding another document listener.
- Replayed ids: a draft seeds its copy's id minter from a 32-bit `id_seed`
  (exact in JSON), so the same seed on the same base mints the same ids.
- R1: copying `bench_stress` (77 entities, 10 brushes) costs about 13 ms.
- Proposal files carry `type: "level.proposal"` and `version: 1`, the id seed,
  the base level's asset path, the digest of every entity the draft changes
  or relies on, and the steps as `{op, params}`. `draft.save` writes where it
  is told, like
  `plan.section`; `<project>/.proposals/` (D8) is the convention clients and
  the tray follow. `draft.open` refuses a proposal for another level, names the
  entities changed since it was written (`moved_since`), and leaves the draft
  `failed` at a step that no longer applies. R8 is handled in M2 (below).
- The closed `Solid | Volume` distinction is a `volume` tag component on the
  brush entity rather than a role field: a two-valued role is a marker, and
  markers are tags. Nothing about the `brush` component's format changes.
  `space`, `shell { thickness }` and `opening { width, height, shape,
  arch_rise }` are declared editor-only; their defaults are the conventions'
  defaults.
- Volumes are air for every geometry consumer: one admission rule
  (`BrushRoleFilter`) keeps them out of the cook (so navigation, collision and
  lighting), the surface ray cast, the floor plan and the solid draw. Kyusu
  draws them translucent with their edges from the retained draw set, in every
  shading mode, for the focus level; context zones do not draw their volumes
  yet. Floor plans show spaces once the space operations land.
- `GenerateShell` is the pure kernel: slab corners are three-plane
  intersections of the face's inner or outer plane with the join planes of its
  two edges, so non-convex faces need no special case. A refusal clears every
  slab. Integration (next) mints the slabs as meshes of the space's own brush
  evaluation, each with a solid role and a face map whose inner face points at
  its volume face, so baking, drawing, picking ("a slab click selects the
  space; a face edit resolves to the volume face") and the cook reuse the piece
  machinery. The shell stage is cached per space by a content hash of its own
  inputs, never by the brush store's revision slot.
- Built that way. The role lives on each evaluated mesh, so a space's air and
  its walls share one evaluation; the walls are generated in world space (so
  up and the thickness are world facts whatever the space's scale) and stored
  in its frame. The open-face material comes from the project's conventions
  when the document learns its content roots; an edit to the conventions
  reaches documents opened after it.
- The gap rule partitions the face rather than carving the slab: the shared
  overlap and the convex pieces around it (clipped against the overlap's edge
  half-planes) each become a slab, with the face's own join planes on its own
  edges and square caps on the seams. Only the owner (lower persistent id)
  builds the shared piece, its outside in the neighbour's material. A carve
  could not do this: a shared region spanning the wall's height would split
  the slab in two, which the carve kernel refuses. Neighbours are the shelled
  volumes within a thickness of the space's bounds, and their identity,
  record, transform and thickness are among its inputs, so moving either room
  rebuilds both. `shell.overlap` and `space.overlap` arrive with the step 5
  diagnostics.
- Openings cut through `CarveAcrossSurface` with pierce, so a second opening
  in a wall an earlier one already split still lands; every outline is first
  clipped to the slab's uncut face, and one overlapping an earlier cut is
  refused (`opening.refused`). An opening cuts a slab that faces along its
  through axis and whose thickness, half a metre either way, holds its sill:
  the prism is not infinite, or a door would cut every wall in line with it.
  A full-height opening that would split a wall in two is refused by the
  carve; it is a gap between two spaces, not an opening.
- `shell.detach` is an operation, so it is atomic, undoable and draftable
  like every mutation: the walls the evaluation holds (openings cut) become
  brushes parented under the space at an identity transform, named after it,
  and `shell` goes. It refuses a space whose shell is refused, since there are
  no walls to keep.
- `space.create` takes a name (unique, so `@name` always resolves) and two
  corners; its faces take the conventions' floor, ceiling and wall materials,
  and its shell the conventions' thickness. `opening.create` takes a point
  in or near a generated wall rather than a space and a face: the wall at the
  point decides the space that owns it, its through axis and its floor, so a
  client names a door by where it is. A sill of zero is a doorway.
- `space.move` finds the far side of each door the space owns before moving
  (a space with a face looking back along the door's axis, over which the
  door's middle lies), then pulls that face by the move's part along its
  normal through `MeshEditService::TranslateElements`. A sideways move pulls
  nothing; a detached far side is skipped and named in `skipped`; a pull that
  would turn a space inside out refuses the whole move. Every mutation now
  refreshes world transforms on both sides of its edit: an operation is the
  "mutation finished" point the document otherwise lacks, and without it a
  batch read positions from before its own previous step.
- Diagnostics are `ContentRiskRecord`s, the world validation's record, with a
  new `Entity` source kind whose id is the persistent id; `level.diagnostics`
  reports them for the target level (a draft included) in the same shape as
  the world's `diagnostics`. Rules: `shell.unsupported`, `space.overlap`
  (both shells refused; judged by face-plane separation, exact for convex
  volumes, so a space in an L's notch is not refused, and a non-convex pair is
  not judged), `shell.overlap` (walls nearer than their thickness),
  `shell.vertex_join`, `shell.shared_not_convex`, `opening.refused` (about the
  opening), `opening.no_wall`, `opening.joins_one_space`, and opening clearance
  as `opening.too_narrow` and `opening.too_low` against the first navigation
  profile's capsule. `carry.skipped_detached` stays in `space.move`'s answer,
  since it is about one move rather than the level.
- `deps` answers what would change with an entity, from facts the evaluation
  already holds: a space's wall count, the openings that cut it and the
  spaces it shares walls with (the shell records both as it builds), and the
  spaces its doors lead into; an opening's cut spaces and the two it joins.

**Acceptance, headless half (2026-10-14)**, run as an agent would, on the
fixture project with a humanoid navigation profile added:
1. Inspected: `conventions.get`, `level.summary`, `plan.section` read as a PNG.
2. Wrote `east-wing.sproposal` in one `kyusu_batch` run: a hall, a corridor
   and a store a wall apart, and two doors by point. `level.diagnostics` came
   back empty; `deps` named the shared walls and where each door leads.
3. Kyusu replayed it with `draft.open` from the console. The ghost preview and
   proposal tray are step 6, not yet built.
4. Carry, a sideways move and a detached far side are covered by
   `CarryTest`; a door left off a corridor is `opening.joins_one_space`.
5. Clearance measured 2 m across the corridor. Reachability hall to store:
   unreached with no doors, unreached with one, reached with both.
6. Accept and discard as one undo step each are covered by tests.
7. Detach is covered; bevelling a detached wall by hand is live work for the
   designer.
8. Kyusu accepted, saved and cooked it (mesh and collision cells and
   `navigation.snav`); a fresh batch reloaded the saved file, found all five
   entities, no diagnostics, and the store still reachable.
9. Replaying the proposal in a fresh process created the same entities with
   the same ids (`id_seed` 42), and
   `AProposalReplaysToTheSameEntitiesWithTheSameIds` holds that.

Step 6, tools through the level target: `EditorWorkspace` owns the
`LevelTarget`. `MutateLevel` runs a tool's edit like `Mutate` runs an
operation's, but always against the level itself, so a designer's drag never
lands in a draft an agent is writing. The gizmo's transform commit puts the
dragged entities back where the drag began and records the move through
`MoveEntities`, the function `space.move` uses, so carry is one rule for both;
a space dragged together with the space it would pull carries nothing.
The Space tool (R) drags a footprint on a horizontal plane, the grid or a
surface facing up, previews the box at the conventions' space height, and
commits on release through `CreateSpace`, named `Space`, `Space 1`, and so on.

The ghost preview stages the previewed draft (the newest begun, or the one
`draft.preview` names) in two scratch documents: its change set's after-side
for what it adds or changes, and the before-side of its deletions. Each staged
entity takes its world transform from the draft (or, for a deletion, from the
level), so a door slid along a room the draft did not change stands where it
will. `DocumentStage`, extracted from the placement ghost's stage, gives each
its own bakes and queues; nothing in a stage is picked. Shells are evaluated
from the staged entities alone, so a staged room's wall shared with an
unchanged one shows twice in the preview, not in the result.

The proposal tray is an explorer (Shift+D), the shell's existing list with a
current item and page actions, not a new panel. It lists each open draft as
its label, its counts of new, changed and removed entities, and how many more
or fewer space diagnostics the level would have, the shown one current; then
the `.proposals` files not yet open. Picking a draft shows it; picking a file
runs `draft.open` and leaves mutations writing the level, since the designer
opened it to review. `proposal.accept` (Ctrl+Enter) and `proposal.reject`
(Ctrl+Backspace) run `draft.accept` and `draft.discard` on the shown draft and
post a notice. Command labels are not built: nothing shows undo history, so
a label would have no reader (docs/deferred.md).

Found on the way: `deps` counted only the doors a space owns as leading out
of it; it now counts every opening in its walls. Skill v0 is
`.claude/skills/kyusu-level/` (`SKILL.md` and a `reference.md` generated by
`kyusu_batch --reference`, held current by `kyusu_batch.SkillReferenceIsCurrent`);
`.gitignore` keeps `.claude/skills/` tracked and the rest of `.claude/` ignored.

### M2: work live, and widen the vocabulary

**Capability:** the agent works beside the designer in a running Kyusu and can
answer "this", "here" and "where I'm looking".

**Work:**
- Local socket, CLI client, event stream.
- `view.context`, select and frame back, `changes.since`.
- The remaining mutation catalogue over existing kernels: box, clip, carve,
  extrude, duplicate, delete, reparent, apply material, set component field,
  place instance.
- `BrushTool` commits through an operation.
- Skill v1.

**Acceptance:** the designer aims at a wall and asks for a doorway "here". The
agent resolves `aim` and proposes it live. The designer edits elsewhere
meanwhile, and accept succeeds. An edit to the same wall makes accept report a
conflict instead.

**Progress notes (in flight):**
- The socket is a Unix domain socket (`ipc/LineSocket`) in a directory only
  the user can enter (`$XDG_RUNTIME_DIR/sencha/kyusu.sock`, or a private
  temp directory), opened while `editor.operations.socket` is on and polled
  from Kyusu's workspace tick. A line is a request in the script form or a
  JSON object `{id, op, args}` whose answer carries the id back. A path in use
  by a live editor is refused; a dead editor's socket file is replaced. The
  serving state is `Off`, `On` or `Refused`, so a failed open is not retried
  every frame.
- Events go through `EditorOperations::Emit`, so workspaces never know a socket
  exists: `selection` (persistent ids, names and elements), `saved` (the
  document and how it went), `draft.accepted` and `draft.discarded`.
- `kyusu_client` sends one operation or stdin's lines and prints answers,
  skipping events; `--events [n]` prints them.
- The aim is the ray through the middle of the active view, not the pointer:
  the designer turns to face a wall and then types in another window, which
  takes the pointer away from the view. `view.context` reports the first solid
  surface on that ray (a space's generated wall reports the space), the space
  it faces into, the space holding the eye (the smallest whose volume winds
  around it, so a non-convex space counts) and the spaces in the view's
  frustum, nearest first. `view.*` reads the live document, never a draft.
  `kyusu_batch` has no view and doesn't register them, but its reference
  lists them.
- `changes.since` compares the level, never a draft, against a mark of
  per-entity digests, so it reports undo, accepted drafts and the designer's
  edits alike, and each answer carries the mark to ask from next. Marks are
  not per client (the operations don't know who asks): the newest 16 are held,
  an older one refuses with `changes.unknown_mark`, and a reloaded or replaced
  level refuses with `changes.level_replaced`. When the document's revision
  hasn't moved the answer is empty without digesting anything.
- Found on the way: an edit applied as a change set (an operation, an
  accepted draft, their undo and redo) could delete a selected entity and
  leave its dead handle selected. Applying a change set now fires the same
  handle observer a projection rebuild does.
- `entity.delete`, `entity.duplicate` and `entity.reparent` take a list of
  subjects. An entity means its branch, so a space's doors go and come with
  it. The branch rules (`document/EntityBranches`) and the reparent rules
  (`ResolveReparentTargets`) moved out of the commands, which the designer's
  verbs still commit through; the operations and the commands share them. A
  copy claims the next free name, so space names stay unique.
- The brush operations name a face by the world direction it looks toward
  (`brush.extrude`) or by a point on it (`brush.carve`), never by index: an
  index is opaque to a client and changes with every edit. They reshape solid
  brushes only and refuse a space, whose faces move with `space.resize_face`.
  `brush.clip` and the clip tool judge a cut through one kernel
  (`brush/BrushClipCut`). `brush.carve` is for brushes (a detached wall, a
  floor); a door in a generated wall stays `opening.create`.
- The Brush tool commits its pending brush through the level target, as the
  Space tool does: the commit adopts the brush the designer sees as a created
  entity. Undo no longer restores the selection from before the drag; the
  handle observer only drops the brush from it.
- `material.apply` takes a material's asset path and refuses one the
  project does not hold, so a typo can't paint a face missing. `assets.list`
  is how a client finds paths: the asset shelf's own query (`t:material
  stone`, `in:folder`) over the project's files.
  `component.set` edits a field through the entity's serialized record, the
  form `entity.describe` already shows, so it covers every registered
  component, editor-only ones included (`shell.thickness`). It refuses a value
  of another kind than the field holds and answers with what was stored.
- A placed scene is an instance record whose entities the projection
  recreates, so a change set made of entity snapshots could not place one:
  undo would leave the record to bring the entities back. Change sets now
  carry placements as records (`InstanceChange`), restored before the entity
  changes and removed after them, so undo, drafts, accept, conflicts and the
  ghost preview treat `instance.place` like any other edit. Change sets are
  never persisted (a proposal file stores steps), so no format changes.
- The accept precondition covers what an edit relies on, not only what it
  changes. A door changes nothing of its wall's space, so an edit to that
  wall used to leave the proposal acceptable, with the door left where the
  wall had been. An operation now declares the entities it reads
  (`EntityChangeRecorder::Read`: a door's wall, a copy's sources, a reparent's
  new parent), the change set keeps their digests, and accept refuses with
  `draft.conflict` when one moved. A proposal file lists them among its base
  digests, so `moved_since` names them too; the file's shape is unchanged.
- R8: a proposal file records a digest of each operation's contract (its
  reference entry: description, parameters, answer keys) under `contracts`,
  rather than one registry version, so an unrelated operation added since
  does not taint every proposal. `draft.open` names the operations whose
  contract changed in `changed_operations` and replays anyway. A file
  without `contracts` opens as before; the key is optional and the version
  stays 1.
- M2 acceptance, headless (`AimedProposalTests`): the camera faces a wall,
  `view.context`'s aim proposes a doorway there in a draft, a designer edit to
  another room leaves accept succeeding, and a designer edit to that wall
  makes accept report `draft.conflict`.

### M3: analysis depth

**Capability:** answer progression questions for hypothetical capability sets
and world states, and annotate levels that were built by hand.

**Work:**
- A space graph query with measured connectivity per scenario, plus bypass
  reports.
- `spatial.crossing_without_dock` and `dock.place_at`.
- A read-only public navigation polygon and edge iteration API. This is an
  engine boundary change: generic, development-only, zero runtime cost.
- Space proposals for unannotated levels.
- Notes.

**Acceptance:**
- On `traversal3`, the jump-capability scenario changes the result exactly at
  the authored jump link, and both walks report it.
- A gated dock reachable without its gate is reported as a bypass.
- A crossing without a dock is reported, and `dock.place_at` proposes one.
- Proposed spaces for the unannotated `room_2` are accepted through a draft.

### Not on the critical path

Recorded in `docs/deferred.md` with triggers:
- design heuristics (pacing, getting lost, encounter fairness), labelled
  heuristic if ever built;
- graph-first generation;
- playtest telemetry;
- the runtime spatial step;
- migrating the remaining tools onto operations;
- general 2D polygon booleans for non-convex shared faces (trigger: a shared
  face the convex clip refuses in real content).

---

## 8. Risk register

### Architectural uncertainty

These could invalidate a design choice.

| Id | Risk | Proven or retired by |
|---|---|---|
| R1 | Draft cloning is too slow, or loses fidelity, on large world documents (projection, brush sidecar, unknown keys) | M1: measure clone and diff on `bench_stress`; round-trip fidelity tests. Fallback: clone only the touched zones; copy-on-write only if that also fails. |
| R2 | Drawing a draft over the live scene in the shared renderer breaks picking or draw sets | M1 acceptance |
| R3 | The gap rule and join rules don't cover the shapes designers build, or designers can't hit the gap reliably | M1 shape matrix and owner walkthrough. Fallback: a snapping operation that sets the gap. CSG is the last resort. |
| R4 | Designers don't accept editing the air instead of the walls | M1 owner walkthrough. This is the core bet (D2), so it is tested first. |
| R5 | Operation results and change sets are clumsy for native tools | M1 gizmo and Space-tool ports; M2 `BrushTool` |
| R6 | Per-entity digest preconditions are too coarse (two edits to one big brush conflict) or too fine | M1 conflict tests |
| R7 | Carry (normal-only face translation) is too weak for how designers rearrange rooms | M1 walkthrough. Fallback: more carry cases as operations. Never a live constraint system. |
| R8 | A proposal file replays differently after an operation's semantics change between editor versions | Proposal files record the operation registry version; a mismatch replays with a warning, and the base digests still catch document drift. Measured in M2. |

### Implementation difficulty

The approach is known; it is just work.

- Shared walls between non-convex faces need a general 2D polygon
  intersection. [exists] clipping handles only convex clip polygons
  (`CarveSurround.h:55`), so M1 supports convex shared faces and reports the
  rest as `shell.unsupported`.
- The socket on Windows (AF_UNIX on Windows 10 and later, or a named pipe).
- Id minting injected per transaction.
- `LevelWorkspace` to `DataDocumentStore` plumbing.
- Navigation buffer limits for whole-zone floods (`NavReachableBuffer` 256,
  `MaxNodes` 2048).
- Moving `SimulateJumpArc` out of `data_editor` if jump checks are wanted
  (layering rule E).

---

## 9. Defects found during investigation

These are fixed in M0, whatever the outcome of the decisions in
section 10.

1. **Level console commands outlive their workspace.** `editor.open`,
   `editor.select`, `editor.mode`, `editor.brush.counters`, `cook`, `play`,
   `playserver`, `stop` and `project` register under owner `"editor"`, capture
   `this`, and are never unregistered (`LevelWorkspace.cpp:242`;
   `DocumentFileActions.cpp:57+`; `CookSession.cpp:416`; `PieDriver.cpp:206+`).
   The `editor.pie.host_port` cvar's change callback captures the play driver
   the same way, and only mirrors the cvar into a duplicate member. The data,
   animation and UI-preview workspaces do unregister. After the level workspace
   closes, those commands dangle, and reopening silently fails to register them
   again. Unregistering `"editor"` would be wrong, because app-lifetime
   commands and archived settings share that owner.
2. **Not a defect (corrected).** Revision 2 listed catalog actions running in
   the most recently active workspace (`KyusuApp.cpp:376-384`) as a bug. That
   is the intended semantics: an action runs where its keybind would. Operations
   target the level workspace explicitly (4.7), so they don't depend on it.
3. **Hand-written brush data can crash the loader:** an uncaught `std::stoul`
   on non-numeric `brush_meshes` keys (`BrushMeshSerialization.cpp:200`), and
   unchecked loop indices (`BrushValidation.cpp:63`).
4. **Suspected, unproven: parented brushes may cook at their local transform**
   when cooking from file. `CollectCookBrushes` reads `WorldTransform`, and no
   step on the load-to-cook path propagates transforms. A test settles it.
5. **The cook's editor-only strip list is hard-coded**
   (`DocumentCookInput.cpp:329-348`). New editor-only components would grow a
   central list. It becomes a declaration at component registration.
6. **Focus loss cancels tool previews** (`ViewportToolDispatcher.cpp:49-64`).
   This is correct for pointer gestures and is recorded only because drafts
   must not inherit it. Drafts are not pending edits, so they don't.
7. **A navigation query context larger than 65535 nodes failed every query**
   as "search limit reached": Detour's node pool indexes with 16 bits. Fixed
   by clamping at the context's construction.
8. **The editor's world validation ignored docks.** Reachability walked the
   manifest's dock endpoints, which only the world cook fills, so every zone
   reached through a dock was reported unreachable. Validation now checks
   against the topology compiled from the world scene, as the cook does.
9. **Normalized data could fail its own schema**: a member with no default
   came back as null. It now stays out.

---

## 10. Decisions for review

**D1. Branch base.** This plan's branch is cut from `claude/kyusu-shoji`.
`main` is 109 commits behind it and predates the Shoji editor, so anything
touching Kyusu UI (proposal tray, ghost preview) belongs on the Shoji base.
Recommended: keep it on the Shoji base.

**D2. A space is an authored volume brush.** This is the core bet, and M1
tests it first (R4). The alternatives are a named group with no volume, or
seed points with derived extent (section 3).

**D3. Marking an open face.** Options:
- **(a)** A designated open-face material in conventions. This reuses face
  painting and leaves the brush format unchanged. Recommended.
- **(b)** A per-face flag in the brush format. This changes a persisted format
  for every brush.

**D4. Revise roadmap tool 10.** `BrushRole` becomes `Solid | Volume`, and
volume meaning comes from components rather than from enumerated roles.

**D5. Every mutation is a change set through an operation.** The move gizmo
and the Space tool adopt this in M1, because carry must behave the same for a
designer and an agent (5.7). `BrushTool` follows in M2. Other tools adopt it as
their commit paths are next touched, tracked in `docs/deferred.md`.

**D6. Shared walls by the gap rule now; CSG only if R3 fails.**

**D7. Transport order.** Headless host (M0), then proposal files (M1), then
socket, CLI and skill v1 (M2). MCP only later, as a thin client of the socket.

**D8. Where proposal files live.** Recommended: `<project>/.proposals/`. That's
outside the content roots, so neither asset discovery nor the cook sees them.
Kyusu lists them in the proposal tray. Whether they are committed to version
control is a per-project choice; the default would be to ignore them.

---

## 11. Disposition of the review of revision 1

1. **What a space represents.** Accepted, and rev. 1's abstraction was wrong.
   - Identity (the `space` tag on a volume), authoring representation (any
     brush), parametric derivation (`shell`) and inferred relationships
     (analysis) are now four separate things.
   - No hierarchy of spatial types: the model is one tag, one generator, one
     opening entity.
   - ECS ownership is answered in 2.4.
2. **Separating semantics.** Accepted. See section 6.
   - Openings don't create navigability.
   - Docks stay authored.
   - Conditions belong to LevelFlow.
   - Analysis results carry provenance.
   - LevelFlow and T were verified as not built on this base.
   - Pushback: entity-local conditions are LevelFlow's requirement, and this
     plan records it rather than building it.
3. **A robust operation API.** Accepted. Section 4 covers typed results,
   query/mutation kinds, subjects, digest preconditions, change sets,
   diagnostics, determinism, undo, incremental `changes.since`, neighbourhood
   and dependency queries.
4. **Transactions.** Both models were evaluated (section 3), and the draft
   document wins on every listed hazard. Drafts survive focus loss, have
   inspectable diffs and diagnostics, and serve human batches.
5. **Stress-testing parametric geometry.** Section 5 takes each case in turn.
   - Unsupported shapes refuse with diagnostics.
   - Nothing references generated geometry.
   - Disagreement is impossible while a shell exists and reported after detach.
   - Harvest was dropped instead of adding machinery.
6. **Observation earlier.** Revision 2 made its first slice observation-only,
   on existing levels, with floor plans in it. Revision 3 calls this M0.
   Design heuristics are out of scope and would be labelled heuristic.
7. **Vertical slices.** Revision 2 tested the operation and draft architecture
   with plain brushes before the spatial bet, then tested the spatial model
   against ordinary modelling. Revision 3 merges both into M1 (section 12).
8. **Philosophy.**
   - Tools and agents share operations.
   - Nothing is agent-specific.
   - The strip list and role enum are replaced by declarations.
   - No new graph authoring for local properties.
9. **Provisional preferences.**
   - 1, 3, 4, 6: agreed.
   - 2: agreed, and pushed further: no runtime representation is designed until
     a consumer is named.
   - 5: agreed, with less than proposed: there is no capability asset at all;
     scenarios are query parameters over existing navigation concepts.
   - 7: agreed. Part A lives in `editor/common` and knows nothing spatial.
10. **Deliverable.** This document.

---

## 12. Disposition of the review of revision 2

Several points in this review describe revision 1: the prism space, harvest,
and the top-down map in Phase 5. Revision 2 had already replaced each of them.
Where revision 2 already answers a point, this section says so instead of
reopening it. What changed in revision 3 is marked **Changed**. Pushback is
marked **Pushback**.

1. **Separating Space identity from generation.** Already the shape of rev. 2:
   identity is the `space` tag, generation is `shell`, analysis is derived, and
   detach keeps identity.
   - **Changed:** "spatial membership" is split into ownership (the existing
     hierarchy) and containment (measured). Corridors are spaces (2.1). A named
     group with no volume and per-document id sets were evaluated and rejected
     (sections 2.4 and 3).
   - **Pushback:** the review lists membership and adjacency as part of
     identity. Storing either would duplicate a relation that already exists
     (the hierarchy) or one that is measured (adjacency). Identity is only "this
     region is a space, with this name". Listing corridors as a separate kind
     of geometry authorship would add a generator nobody needs.
   - **Pushback:** a `space` component is not a runtime-style abstraction
     forced onto documents. The level editor's ECS *is* its document model,
     and `name` and `brush` already live there (2.4).
2. **Distinct kinds of topology.** Already in rev. 2, section 6.
   - **Changed:** the four kinds of state are mapped to their owners (6.3), and
     reachability returns sure and possible walks with bypass detection (6.4).
     Both come from SINR's decision records (6.7).
   - **Pushback:** the spatial model owns only the architectural part of
     structure. Modelling conditions, persistent facts or transient state
     inside it, even as "metadata for analysis", would be the second logic
     system this review warns against. They enter analysis only as declared
     scenario assumptions.
3. **A proper authoring API.** Already in rev. 2, section 4.
   - **Changed:** results gain `derived`, the generated outputs a change
     touched.
   - **Pushback:** "dependency impacts" stops at direct derived outputs.
     Computing transitive impact on every operation costs time on every call
     for an answer `deps` gives on request.
4. **Transaction semantics.** Already drafts in rev. 2.
   - **Changed:** batch atomicity, proposal files carrying the "why" as
     free-text `rationale`, and each listed failure mode mapped to the
     mechanism that handles it (4.5).
   - **Pushback:** copy-on-write isn't needed until R1 measurements say a full
     clone of the affected zones is too slow. Capturing reasoning beyond a
     rationale string would be machinery with no consumer.
5. **Geometry derivation.**
   - **Changed:** moving a room with a connected corridor is specified as the
     carry rule, with explicit refusal cases (5.7). Shared faces are convex in
     M1, and the rest refuse with a diagnostic.
   - **Pushback:** persistent references through regeneration are refused
     outright, because that is CAD's topological naming problem (section 3).
   - **Pushback:** the scene-instance projection precedent is not reused for
     shells. Projection creates editable entities and folds edits back
     (harvest). Shells are evaluated pieces like modifier output, which Kyusu
     already treats as not editable, and that removes harvest entirely.
6. **Observation earlier.** Already M0 in rev. 2.
   - **Changed:** conventions move into M0, and reachability there already
     reports both walks.
7. **Implementation sequence.**
   - **Changed:** M1 is now the review's nine-step milestone, with spaces from
     the start and only seven mutations.
   - **Changed:** the socket, the wider operation catalogue and skill v1 move
     to M2.
   - **Changed:** proposal files let a headless client hand work to a designer
     without a live connection.
   - Loss Function has one small Sencha level (`test.sscene`: about 10
     entities, 8 brushes, no zones), and it isn't under this repository's
     tests. M0 tests on `templates/fps` and covers Loss Function in an owner
     walkthrough.
8. **Philosophy.**
   - **Changed:** the move gizmo and the Space tool commit through the same
     operations as agents in M1, so a designer and an agent can never get
     different results from the same move (D5).
9. **Preliminary positions.**
   - 1, 3, 4, 6, 7: agreed.
   - 2: the principled request mechanism already exists as profile-selectable
     cook steps (6.6). **Pushback:** its contents wait for a named consumer.
   - 5: analysis already uses the runtime's own navigation types and capability
     matching, so there is nothing to reconcile later.
10. **Deliverable.** This revision. The remaining decisions are D1 to D8 in
    section 10.
