# World Partition Authoring and Runtime Plan

Status: proposed design (2026-07-02). Candidate execution spec for Track C item 3
(`WorldPartitionRuntime`) and the partition authoring additions to Track D in
`docs/plans/engine-roadmap.md`. The roadmap owns versions and gates; Section 13 lists the
version moves this document asks the roadmap to ratify. Where this document assigns a
version, read it as a proposal until the roadmap records it.

Audience: whoever implements any item here (human or agent), and reviewers. This document
consolidates the partition model evaluation (zones, regions, portals, streaming,
loaded-but-inactive editing, multi-space worlds) into one plan grounded in the seams that
exist in the tree today.

---

## 0. Verdicts up front

The proposal under evaluation: a typed directed graph of zones as runtime truth, with
regions as organizational groups and reality/spacetime overlays, explicitly rejecting
recursive runtime zones. The verdicts, argued in the sections named:

Accepted:

- The graph is the runtime truth. Zones are nodes, transitions are directed edges,
  regions are metadata groups, and every entity has exactly one owning zone (Section 2).
- Rejecting recursive runtime zones. The existing substrate already decides this:
  a zone is one `Registry` with one participation state, and nothing in
  `ZoneRuntime` or `FrameRegistryView` can express nested ownership without wrecking
  the span model (Section 2.1).
- Separating the logical transition edge from the editable portal geometry (Section 5).
- Always-loaded headers, with a hard size rule: headers are O(zones + edges), never
  O(entities) (Section 3.3).
- The four orthogonal editor states (Loaded, VisibleInEditor, EditableInEditor,
  runtime-preview flags) with loaded-but-inactive as a first-class state (Section 6.3).

Renamed:

- Reality / Spacetime becomes **Space**. "Reality" names the gameplay intent (parallel
  worlds); the mechanism is a coordinate space inside which zone bounds are comparable
  and outside which no implicit spatial relationship exists. Directive 1 applies
  (Section 8).
- Aperture merges into **Portal**. One word for the editable geometry; "portal" is the
  established mechanical term (portal culling) and "aperture" adds a synonym, not a
  concept.

Deferred, each with a trigger recorded in Section 11:

- `RenderContext`: no per-zone environment exists in the renderer today (no skybox, no
  post). A dead id field is a dead seam.
- `PortalHeader` in the manifest: portal geometry lives in zone content until
  see-through rendering (v2.0 visibility work) needs header-level portal data.
- Subzone / Sector: the level cook's grid cells already subdivide zone content
  mechanically; no authoring concept is earned yet.
- The warp transform on transitions: identity for every v1.0 transition; the field
  lands with spaces.
- Split Zone / Merge Zones, Adopt Entities In Bounds, Reassign By Containment: bulk
  conveniences over Move Selection To Zone; build the primitive first.
- The node-link graph panel and the demand inspector UI: v2.0, per the roadmap's
  records-first-UI-second pattern.

Rejected:

- Storing both `Outgoing` and `Incoming` transition lists in zone headers. Store each
  edge once in the manifest's transition list; derive per-zone adjacency indices at
  load. Dual lists are a consistency liability with no read-side win (Section 3.2).
- The nine-badge zone state list as stored state. Editor zone state is the four
  orthogonal flags plus cook status; badges are derived display (Section 6.3).
- State Overlay as a new vocabulary item. It is Track C stage 5's `ZoneStateRecord`,
  already planned; this document adds nothing to it except consuming it.

---

## 1. Grounding: the substrate this plan builds on

Verified against the tree, not against docs' claims.

Runtime (exists):

- `ZoneRuntime` (`engine/include/zone/ZoneRuntime.h`): one global `Registry` plus a flat
  list of loaded zones, each exactly one `Registry` plus one `ZoneParticipation`.
  Create/destroy, reserve-and-attach for async builds, participation get/set,
  `BuildFrameView()` compiling participation into `FrameRegistryView` spans.
- `AsyncZoneLoader` (`engine/include/zone/AsyncZoneLoader.h`): detached registry build on
  the async lane, attach and finalize at the owner-thread drain point, optional
  preload-gated attach, dormant attach without a temporal discontinuity.
- `ZoneParticipation`: four booleans (Visible, Physics, Logic, Audio) compiled to spans.
  Participation tiers (Dormant, PhysicsShell, LogicHot) are Track C item 4.
- `AsyncCommitBudgetMs` on `EngineRuntimeConfig` (`core/config/RuntimeConfig.h`), consumed
  at `FramePhase::DrainAsyncTasks`. Note: CLAUDE.md describes a cvar console; the tree's
  actual pattern is config fields on `EngineRuntimeConfig`. The codebase wins; new
  tunables in this plan follow the config-field pattern until a console lands.
- `StrongId<Tag, Underlying>` (`core/identity/StrongId.h`): the persisted id vocabulary
  (`AssetId`, `GameplayTagId`, `ComponentTypeId`). Runtime `ZoneId` is currently a bare
  `uint32_t` wrapper outside this family; Section 3.1 migrates it.
- The level cook (`editor/kyusu/src/document/DocumentCook.cpp` over
  `engine/include/assets/cook/`): clusters brushes into world-grid cells, one relocatable
  cell-local `.smesh` plus `StaticMeshComponent` per cell, collision sidecar, cooked
  scene, content-hashed cache. The runtime loads one cooked scene into one zone
  (`ZoneId{1}`) today.

Runtime (planned, not built; this plan consumes or implements them):

- `WorldPartitionRuntime`: Track C item 3, "stance only" per the roadmap's Section 3
  table. This document is its execution spec.
- `ZoneBudgetRecord` / `TraversalBudgetRecord` / `StreamingTelemetryRecord` /
  `ContentRiskRecord` and the traversal-hitch harness: Track C items 1 and 2, which
  precede item 3 in the Track C spec's rollout.
- Stateful detach and `ZoneStateRecord`: Track C item 5.
- Transition model (runtime timing semantics): Track C item 6.
- Visibility, portals, vista proxies: Track C item 7, v2.0.

Editor (exists):

- One `EditorWorkspace` owning one `EditorDocument` owning one `Registry` plus an
  `EditorScene`. Flat outliner (`SceneHierarchyPanel`), no grouping, no multi-document.
- Per-entity sparse Hidden/Locked sets on `EditorScene`; picking and rendering already
  skip hidden entities, picking skips locked ones. `SelectableRef` already carries
  `RegistryId`, so multi-registry selection is representable without changing the ref.
- `CommandStack` plus `CompositeCommand` for multi-part undo steps;
  `CaptureEntity`/`RestoreEntity` snapshots for undoable entity lifecycle.
- `BrushOps` pure verbs including `CarveFaceRect` and `RectFaceFrame`; `FaceCarveTool`
  as the canonical face-pick, live-preview, commit tool pattern. `PierceFaceRect` does
  not exist.
- Shared `EditorLinePipeline`/`EditorWideLinePipeline`/`EditorFillPipeline` for overlay
  rendering; `EditorOverlayState.Labels` for world-anchored text. The `EditorLineBatch`
  consolidation (hardening W5) is still a plan.
- Out-of-process PIE: cook to disk (or live cook), spawn `app --game <module> +map <name>`.

Editor (absent, owned by this plan): any zone or partition concept, a document container
above `EditorDocument`, outliner grouping, transition or portal entities, partition
metadata in the cook, real-material dimming, the validation panel.

---

## 2. The model

One sentence: **the world is a typed directed graph of zones; everything else is
metadata over that graph.**

| Concept | Runtime meaning | Kyusu meaning | Status |
| --- | --- | --- | --- |
| World | Owns the partition manifest and global records | Top-level document (`.sworld`) | v1.0 |
| Zone | The residency and ownership atom: one `Registry`, one participation state, streamed as a unit | A room or area node; owns its entities; one authored scene file | v1.0 (exists at runtime) |
| Region | A named group of zones: budget scope, cook scope, validation scope | Organizational cluster in the tree ("Chozo Ruins" as data, never as a type) | v1.0 |
| Transition | A directed edge between two zones: topology, traversal flags, streaming hints | Authored edge; usually created from a doorway opening | v1.0 |
| Portal | Editable geometry realizing a transition in its source zone | A framed opening with gizmos; owned by the source zone's content | v1.0 minimal (entity in zone content), v2.0 in headers |
| Space | A coordinate space and causal island: bounds comparable inside, no implicit relationship across | A group that can be soloed or ghosted; cross-space edges drawn distinctly | v2.0, designed now (Section 8) |
| ZoneStateRecord | Persisted divergence from authored zone content | Save/validation view | Track C item 5, unchanged |

Invariants of the model:

1. Every entity has exactly one owning zone. At runtime this is structural (an entity
   lives in exactly one zone's `Registry`); the editor mirrors that structure with one
   registry per open zone, so ownership is where an entity lives, not an annotation
   that can drift.
2. Zone headers for the whole world stay resident; zone contents load and unload
   independently.
3. Regions and spaces are metadata over zones. Neither has a registry, ticks, or owns
   entities. There is no runtime object for either in v1.0; they are id fields on
   headers plus grouping logic in tools and policy.
4. Transitions are directed. A two-way door is two edges; the authoring tool creates
   the pair, and validation checks the pairing (Section 9).
5. All spatial reasoning (bounds, distance, adjacency-by-proximity) is scoped to one
   space. v1.0 has exactly one implicit space; the invariant is enforced by keeping
   bounds queries internal to `WorldPartitionRuntime` rather than exposing a global
   "world position of zone X" API (Section 8).

### 2.1 Why recursive runtime zones are rejected

The proposal's rejection is correct, and the existing substrate already decides it.
A zone is one `Registry` plus one `ZoneParticipation` compiled into flat
`FrameRegistryView` spans each frame. Every question recursive zones raise (does a
parent tick, does a parent have a registry, can a child be visible while the parent is
not, who owns an entity, what does undo target) is a question the span model cannot
answer without either inventing a parent registry that owns nothing or making
participation hierarchical, and hierarchical participation destroys the one property
that makes the frame view cheap: a flat span per phase.

The authoring hierarchy (World, Region: Chozo Ruins, Zone: Hub Room) is real and stays,
but only the Zone level is a runtime concept. A "child area" that needs independent
loading is a sibling zone in the same region connected by transitions. Grouping needs
above the zone are regions; subdivision needs below the zone are the cook's grid cells,
which already exist and already keep culling granular without any authoring concept.

---

## 3. Identity and data model

### 3.1 Ids

All partition ids join the `StrongId` family, persisted, minted by the editor at
creation time as random nonzero 64-bit values (the `AssetId` precedent: stable across
renames, hex text form in JSON):

```cpp
using ZoneId       = StrongId<struct ZoneIdTag,       uint64_t>;
using RegionId     = StrongId<struct RegionIdTag,     uint64_t>;
using TransitionId = StrongId<struct TransitionIdTag, uint64_t>;
using SpaceId      = StrongId<struct SpaceIdTag,      uint64_t>;   // v2.0, reserved
```

This migrates the existing `zone/ZoneId.h` (a bare `uint32_t` struct outside the
StrongId vocabulary) to `StrongId<ZoneIdTag, uint64_t>`. The migration is mechanical:
`ZoneRuntime` uses the id only as a key, and test literals (`ZoneId{7}`) keep compiling.
The payoff is that the id in the manifest, the id the runtime streams by, and the id the
editor selects by are the same value with the same serialization, and CLAUDE.md's
strong-id rule holds. Unlike gameplay tag ids (registration-order, never serialized),
partition ids are persistent by construction and safe to serialize.

Names ("Hub Room", "Chozo Ruins", "Dark World") are display data on headers, never
identity and never types.

### 3.2 The manifest

Authored form: a `.sworld` JSON document, the top-level kyusu document, referencing
per-zone authored scene files. This answers the Track C spec's open question ("a new
`.sworld` asset, beside scenes, or a higher-level JSON that references scene assets"):
a higher-level document that references scene files, so zone scenes stay independently
diffable, mergeable, and streamable.

Cooked form: the same records under `.cooked/`, produced by the world cook (Section 7),
consumed by `WorldPartitionRuntime`. Text first; it flips to binary with Track F's
binary cooked scenes.

```cpp
struct WorldPartitionManifest
{
    std::vector<RegionRecord>     Regions;
    std::vector<ZoneHeader>       Zones;
    std::vector<TransitionRecord> Transitions;
    // v2.0, with spaces: std::vector<SpaceRecord> Spaces;
    // v2.0, with see-through portals: std::vector<PortalRecord> Portals;
};

struct RegionRecord
{
    RegionId    Id;
    std::string Name;
    // v1.0 budget scope: optional per-region overrides of streaming config values.
};

struct ZoneHeader
{
    ZoneId      Id;
    std::string Name;
    RegionId    Region;              // exactly one
    AssetPath   SceneRef;            // authored: levels/<zone>.level.json; cooked: cooked scene ref
    Aabb        Bounds;              // in space coordinates; derived from content, manual override flag
    bool        BoundsOverridden = false;
    ZoneCookStatus CookStatus;       // cooked content hash + stale flag, cook output only
    // Budget hints arrive as ZoneBudgetRecord references (Track C item 1), not inline fields.
    // v2.0: SpaceId Space;
};

struct TransitionRecord
{
    TransitionId Id;
    ZoneId       From;
    ZoneId       To;
    TransitionTopology Topology;     // Seam, Doorway, Teleport (v1.0 set; grows by data)
    TransitionFlags    Flags;        // OneWay is the only v1.0 flag
    StreamingHint      Hint;         // preload priority for To when From is the focus zone
    std::optional<EntityRef> Portal; // portal entity in From's content, if any
    // v2.0, with spaces: Transform Warp; identity is implied absent.
};
```

Deliberate omissions, per the verdicts:

- No `Outgoing`/`Incoming` lists on `ZoneHeader`. Edges live once in `Transitions`;
  `WorldPartitionRuntime` and the editor derive adjacency indices at load. One source
  of truth, nothing to cross-validate.
- No `RenderContext`, no `RealityId`, no `PortalHeader` (Section 11 triggers).
- No inline budget numbers. Budgets are Track C item 1 records keyed by `ZoneId`;
  duplicating them into headers makes two sources of truth.

### 3.3 Headers versus content: the size rule

Headers hold what every always-resident consumer (partition tree, validation, streaming
policy, target pickers, graph panel later) needs about a zone it has not loaded:
identity, name, grouping, scene ref, bounds, cook status, and the edges touching it.
Everything per-entity (components, brush meshes, portal geometry detail, entity names)
lives in zone content.

The enforcement rule: **the manifest is O(zones + transitions), never O(entities)**.
Any proposed header field that scales with entity count is content. The one narrow
exception is the `Portal` entity ref on a transition: a single stable id into content,
carried so validation and the tree can name the portal without loading the zone. When
see-through portal rendering needs shape and frame data header-side (v2.0), a
`PortalRecord` is added then, not before.

Stable entity references across the loaded/unloaded boundary use the serialized entity
identity that the scene serializer and the coming `ZoneStateRecord` work already require;
this plan adopts whatever identity Track C item 5 lands rather than minting a parallel
scheme.

---

## 4. Runtime: `WorldPartitionRuntime`

The metadata and policy layer over `ZoneRuntime`, exactly as the roadmap frames it. It
owns the cooked manifest and decides desired residency; it never deserializes scenes and
never owns registries.

Shape:

```cpp
class WorldPartitionRuntime
{
public:
    // Cooked manifest in; adjacency indices built here.
    bool LoadManifest(/* cooked manifest source */);

    // The one policy input. Focus is a position in the (single, v1.0) space,
    // or a zone id when position is not meaningful (menus, scripted warps).
    void SetFocus(Vec3 position);
    void SetFocus(ZoneId zone);

    // Pins: script/transition-driven residency demands beyond the policy
    // (boss arenas, cutscene targets). Data, not subclasses.
    void PinZone(ZoneId zone, ZoneParticipation minimum);
    void UnpinZone(ZoneId zone);

    // Called once per frame before FramePhase::DrainAsyncTasks. Diffs desired
    // residency against actual, issues AsyncZoneLoader::BeginLoad (dormant),
    // ZoneRuntime::SetParticipation, and unload requests.
    void Update(AsyncZoneLoader& loader, ZoneRuntime& zones);

    // Why is this zone resident: the demand-source record per zone,
    // the data the editor's demand inspector consumes later.
    std::span<const ZoneDemandRecord> DemandRecords() const;
};
```

The v1.0 policy is one concrete function, not a strategy seam
(`IZonePopulationStrategy` stays deferred per the roadmap's recorded decision): the
desired set is the focus zone at full participation, its graph neighbors within a
configured hop count (weighted by `StreamingHint`) attached dormant, plus pinned zones;
everything else unloads after a configured linger. Tunables are `EngineRuntimeConfig`
fields beside `AsyncCommitBudgetMs`: neighbor hop count, linger seconds, and a resident
zone cap.

Contract details that matter:

- Loads are issued dormant (`ZoneParticipation{}`), so attach commits under the drain
  budget with no temporal discontinuity; participation flips when the focus enters the
  zone or a transition demands it. This is exactly the recipe already documented in
  `AsyncZoneLoader.h`.
- Unload is `DestroyZone` until Track C item 5 lands, then `DetachZone` with state
  capture. `WorldPartitionRuntime` is the first caller of stateful detach.
- `ZoneDemandRecord` (zone id, desired participation, list of demand sources: focus,
  neighbor-of, pinned, lingering) is emitted every update. Records first, UI second:
  this is the data contract the kyusu demand inspector and the content risk dashboard
  read; no editor code is required for it to be useful in logs and tests.
- The build callback wiring (which cooked scene file, collision sidecar, preload
  manifest to load for a given `ZoneId`) resolves through the manifest's `SceneRef`,
  replacing the hardcoded single-zone `+map` path in the template game with a
  world-manifest path. `+map` keeps working for single-zone documents.

Gate (unchanged from Track C stage 3, sharpened): game code calls `SetFocus` as the
player moves through a three-zone cooked world; zones load dormant ahead of traversal,
flip participation on entry, and unload behind, with zero missed fixed ticks under the
traversal-hitch harness, and no game code names a `ZoneId` except through transitions.

---

## 5. Transitions and portals

Two records, one optional link, exactly as proposed:

- **Transition**: the logical directed edge (Section 3.2). It exists without geometry:
  a contiguous seam, an elevator, a scripted teleport. It carries topology, flags, and
  streaming hints. The runtime timing semantics of crossing one (history reset, input
  policy, camera policy) are Track C item 6's transition model; this plan supplies the
  graph those semantics attach to and does not duplicate that design.
- **Portal**: an entity in the source zone's content (transform, rect shape, normal,
  linked `TransitionId`). It is the editable realization: gizmo-manipulable frame,
  validation anchor, and later the see-through render surface. Not every transition has
  one; every doorway-created one does.

Authoring flow (v1.0), built on the `FaceCarveTool` pattern:

1. Designer blocks out a room, picks a wall face.
2. A new pure `BrushOps` verb, `PierceFaceRect` (planned in the roadmap's tool suite as
   batch 1, item 2), cuts the through-opening: `CarveFaceRect` on the picked face and
   the projected opposite face, `DeleteFace` on both inset rects, four bridging interior
   quads inheriting the host `FaceMaterial`.
3. The transition tool offers "create transition from opening" on the resulting
   opening: target picked from zone headers (loaded or not; headers make unloaded
   targets pickable) or "new zone in region R".
4. One `CompositeCommand` mints: the transition record (and its reverse unless OneWay),
   the portal entity sized to the opening's `RectFaceFrame` world rect, and, for a new
   target, the zone header plus empty scene file.
5. Validation runs incrementally (Section 9); the target zone can be loaded as context
   (Section 6.3).

The portal entity's frame math reuses `RectFaceFrame`; no new geometry vocabulary. Edge
styling in views derives from `Topology` plus `Flags`; the proposal's long styling list
(seam, doorway, see-through, one-way, warp, elevator, cross-space) is presentation over
those two fields plus, later, the space comparison, not stored per-edge styling.

---

## 6. Kyusu: partition authoring

### 6.1 The world document

A `WorldDocument` above `EditorDocument`: it owns the authored manifest plus a set of
open zone documents, each an `EditorDocument` (one registry per zone, preserving the
structural-ownership invariant inside the editor). `EditorWorkspace` moves from owning
one `EditorDocument` to owning one `WorldDocument`; a single-zone world degenerates to
today's behavior, and opening a bare `.level.json` wraps it in an implicit world so
existing content and tests keep working.

Zone scene files stay separate on disk (Section 3.2), so two designers editing two
zones touch two files plus, only for graph edits, the world file.

### 6.2 Partition tree

The tree panel is the manifest rendered: World, then regions, then zones (spaces add a
level in v2.0). Grouping data comes from headers, so header-only zones appear without
any content loaded. Per-zone rows show derived badges: editor state (Section 6.3) and
cook status (fresh/stale from the content hash). Selection of a zone row scopes the
existing flat entity outliner to that zone's registry, which retires the
single-registry assumption in `SceneHierarchyPanel` without redesigning it.

The node-link graph panel stays v2.0 (the roadmap's partition map view). For the v1.0
zone counts (single digits), the tree plus transition rows under each zone carry the
same information; the graph panel earns itself when worlds outgrow the tree.

### 6.3 Editor zone states: loaded but inactive

Four orthogonal facts per zone, stored on the world document (view state, like the
grid: not undoable, not serialized into content; persisted per-user in the workspace
ini):

- **Loaded**: the zone's content is deserialized into an editor registry.
- **VisibleInEditor**: its entities render in viewports.
- **EditableInEditor**: picking, selection, and commands may target it.
- **RuntimePreview**: the four `ZoneParticipation` booleans, used only by PIE and the
  streaming preview; irrelevant to editing.

Presets over those flags: Editing (loaded, visible, editable), **Context** (loaded,
visible, locked: the requested greyed-out state), Hidden (loaded, not visible), Header
Only (not loaded). The nine-badge list from the proposal is display derived from these
plus cook status, not stored state.

Implementation rides the existing mechanisms:

- Locked/hidden enforcement: zone-granular checks in the picking service and selection
  context (the per-entity Hidden/Locked sparse sets stay for per-entity control inside
  editable zones). `SelectableRef` already carries `RegistryId`, so cross-zone
  selection state needs no new ref shape; commands assert their targets are in
  editable zones.
- Greyed rendering, two steps: v1.0 draws context zones through the existing line and
  solid-preview paths with a dim tint (both already have per-vertex or per-material
  tint) and skips selection highlights; a per-draw tint uniform on `MeshForwardPass`
  follows as a small render change so real-material context zones dim too. Context
  zones remain valid snap and measurement targets (`PickSurface` gains an
  include-locked-for-snap mode) so portal alignment against a locked neighbor works.
- Zone bounds overlays: header AABBs drawn through the shared line pipelines by a new
  renderer sibling (the W5 `EditorLineBatch` consolidation can absorb it whenever it
  lands); zone name labels via `EditorOverlayState.Labels`. Ownership tint is a
  per-zone hue on wireframes, cheap and off by default.

### 6.4 Entity ownership tools

Because ownership is structural, the tool set reduces to moves between registries:

- **Move Selection To Zone** (v1.0): a `CompositeCommand` capturing each entity via the
  existing `CaptureEntity` snapshot path, destroying it in the source registry, and
  restoring it into the target registry. Brush mesh sidecar entries move with their
  entity. Undo restores the original registry. Cross-zone entity id stability follows
  the serialized-identity scheme from Track C item 5 (Section 3.3).
- **Select Entities Owned By Zone** (v1.0): trivial; a zone's entities are its
  registry's contents.
- **Show Ownership Tint** (v1.0): Section 6.3.
- Adopt Entities In Bounds, Reassign By Containment, Split Zone, Merge Zones: deferred
  bulk conveniences over the move primitive (Section 11). Validation's
  bounds-containment warning (Section 9) covers the audit need meanwhile.

"Validate Entity Ownership" as a rule disappears: an entity cannot have zero or two
owners any more than it can be in two registries. What remains checkable is whether an
entity sits inside its owning zone's bounds, and that is a warning, not an error,
because overhangs and shared set dressing are legitimate.

### 6.5 Cook and PIE

The cook becomes two nested steps: the existing per-zone level cook (unchanged
mechanics: cells, collision sidecar, cooked scene) runs per zone document, and a world
cook assembles the cooked manifest with per-zone content hashes and cook status. The
content-hash discipline already in `CookedCacheIndex` gives incremental behavior for
free: unchanged zones do not re-cook.

PIE gains play-from-zone: `app --game <module> +world <name> +zone <hex id>` (falling
back to `+map` for single-zone documents), where the template game's map handler feeds
`WorldPartitionRuntime::SetFocus` instead of hand-loading `ZoneId{1}`. This is also the
roadmap's play-from-here item wearing its partition shape.

### 6.6 Streaming preview and the demand inspector

v1.0 ships the record, not the panel: `ZoneDemandRecord` from Section 4 is serialized
with the streaming telemetry (Track C item 1's JSON writer), so "why is this zone
resident" is answerable from a log today and from a panel in v2.0. The v2.0 panel
renders those records live (editor-camera and player-start preview, preload radius
overlays, predicted traversal), aligned with the roadmap's records-first rule. The
editor never becomes the runtime: preview evaluates the same policy code on the same
manifest, fed editor camera or player-start positions.

---

## 7. Phasing

Two lanes over one contract. The manifest (Phase 1) is the shared truth; after it, the
runtime lane and the editor lane proceed in parallel and neither blocks the other.
Track C items 1 and 2 (records, traversal harness) precede the runtime lane per the
Track C spec's own rollout and are not restated here.

**Phase 1: manifest and identity (v1.0).**
`StrongId` migration of `ZoneId`; `RegionId`/`TransitionId`; manifest records, JSON
round-trip, adjacency index build; validation as pure functions emitting
`ContentRiskRecord`s. No editor, no runtime policy.
Gate: a hand-authored three-zone `.sworld` round-trips, and every Section 9 v1.0 rule
fires on a deliberately broken fixture.

**Phase R: runtime consumption (v1.0).** `WorldPartitionRuntime` per Section 4; world
cook output consumed; template game path switched; first caller of stateful detach when
Track C item 5 lands (until then, unload is destroy).
Gate: Section 4's gate, under the traversal-hitch harness.

**Phase E1: world document and partition tree (v1.0).** `WorldDocument`, zone documents,
tree panel, zone bounds overlay, editor zone states with the Context preset (dim via
existing tintable paths), scoped outliner, validation surfaced in a minimal panel over
the same records.
Gate: open a three-zone world; edit one zone with a second greyed and unselectable and
a third header-only; save; reload; states and validation persist.

**Phase E2: ownership editing (v1.0).** Move Selection To Zone with undo across
registries, select-owned, ownership tint, bounds-containment warnings.
Gate: move an entity between zones, undo, redo; cook reflects the move; validation
updates.

**Phase E3: transitions and portals (v1.0).** `PierceFaceRect` verb; transition tool
(create from opening, target picker over headers, reverse-pair creation, new-zone
target); portal entity with frame gizmo; transition validation.
Gate: the Section 10 slice workflow end to end.

**Phase E4: multi-zone editing polish (v1.0 stretch, v2.0 otherwise).**
`MeshForwardPass` tint for real-material dimming, snap-to-locked, region load/edit
presets.

**Phase 2-era (v2.0).** Graph panel; demand inspector UI over `ZoneDemandRecord`s;
see-through portals and `PortalRecord` headers with Track C item 7; participation
preview tied to tiers; Split/Merge and bulk ownership; spaces (Section 8) with warp
frame gizmos, inverse validation, solo/ghost, walk-through preview.

The proposal's own phase list maps onto this with two corrections: runtime integration
(its Phase 6) must not trail four editor phases, because Track C's dependency chain
(partition metadata blocks participation tiers blocks stateful detach blocks the save
system) runs through it, so it becomes the parallel Phase R; and its Phase 5 (streaming
preview UI) moves behind the records it visualizes.

### The v1.0 / v2.0 cut, and why the roadmap moves

The roadmap's v1.0 gate is "three interconnected zones authored in kyusu" with
hitch-free streaming and save/restore. That gate is unreachable while kyusu has no zone
concept, so a minimal partition authoring loop (tree, context zones, ownership move,
transitions) is not scope creep into v2.0 editor territory; it is the v1.0 gate's own
prerequisite. What stays v2.0 is everything the three-zone slice does not need: the
node-link map view (already v2.0 in the roadmap), see-through portal rendering and
vista proxies (already v2.0), the demand inspector panel, bulk ownership tools, and all
of spaces.

---

## 8. Spaces and non-Euclidean worlds (design now, build v2.0)

The mechanism under "realities/spacetimes": a **space** is a coordinate space and
default-relationship island. Inside one space, zone bounds share a frame, distance is
meaningful, and policy may infer (preload by proximity, draw in the distance). Across
spaces, nothing is implicit:

- No implicit distant rendering. Zones in another space do not exist to the renderer
  except through an explicit portal.
- Cross-space traversal requires a transition; cross-space visibility requires that
  transition to carry a portal; cross-space causality requires explicit event data
  (deferred until a real consumer exists).
- A transition crossing spaces (or non-Euclidean within one space) carries a warp:
  `Warp = TargetFrame * Inverse(SourceFrame)`, authored as the two frames (source frame
  on the portal entity, target frame on the transition), never as a raw matrix.
  Designers manipulate paired frames with gizmos; inverse-pair validation checks that a
  reverse edge's warp is the inverse within tolerance.

How kyusu shows "different spacetime" without implying distance: **there is no
transform relating two spaces, so the editor never composes them into one coordinate
system implicitly.** Multi-space display is per-space viewport focus (solo), or a
second space ghosted at an explicit, user-chosen, clearly-labeled preview offset that
is view state and never data. The partition tree gets a space level; cross-space edges
draw in a distinct style in tree and graph; portal-based preview (render the target
space only through the portal frame) is the eventual honest visualization and arrives
with see-through portal rendering.

What v1.0 must do so none of this is precluded, at zero carried cost:

- All cross-zone references are ids, never positions (already true in Section 3).
- Bounds comparisons stay internal to `WorldPartitionRuntime` and validation; no public
  API answers spatial questions across arbitrary zones (Section 2, invariant 5).
- Transitions are directed records that can grow a `Warp` field; JSON makes the format
  addition trivial, and identity-warp is the implied default forever.
- `SpaceId` is reserved in the id vocabulary but not emitted: a manifest without a
  `Spaces` array is a one-space world, which keeps the field out of v1.0 files entirely
  (no dead data) while fixing the upgrade path.

---

## 9. Validation

All rules are pure functions over the manifest plus loaded content, emitting
`ContentRiskRecord`s (Track C item 1) with a source id to jump to. The panel is a thin
view over records, per the records-first rule.

v1.0 set, structural:

1. Ids are nonzero and unique per record type across the manifest.
2. Every zone names exactly one existing region.
3. Every transition's `From` and `To` name existing zones and differ.
4. A transition's `Portal` ref, when present, resolves to a portal entity in `From`'s
   content whose linked transition is this one (checked when `From` is loaded; recorded
   as unverifiable-until-loaded otherwise, which is itself a visible state, not silence).
5. Portal frame normal agrees with transition direction (points out of `From` through
   the opening).
6. A non-OneWay doorway or seam has a reverse edge (derived pairing: matching swapped
   endpoints and portal linkage; an explicit pairing field is deferred until warps make
   derivation ambiguous).
7. Zone `SceneRef` resolves; cooked hash matches (stale cook warning).
8. Zone bounds contain owned entities, warning-level, unless `BoundsOverridden`.
9. Overlapping zone bounds within one space: warning (legitimate for vertical layering,
   suspicious otherwise).
10. Every zone is reachable from the world's designated start zone through the graph:
    warning (deliberately disconnected content exists, but silence hides broken links).
11. Exactly one player start across the world's zones (when the game module defines
    one): error at cook.
12. References from any record to unloaded zones or entities use stable ids, never
    indices or names: enforced by construction in the record types, asserted in review.

v2.0 additions, landing with their features: inverse-warp consistency within tolerance;
cross-space edges require a portal for visibility claims; see-through portals require
target loadability; unloadable-zone-holds-uncapturable-state (needs `ZoneStateRecord`);
persistent-simulation zones declare budgets (needs participation tiers); seam
transitions require compatible environment settings (needs whatever per-zone
environment mechanism earns `RenderContext`'s trigger).

Dropped from the proposal's list: "every entity has exactly one owning zone" (structural,
Section 6.4), "every zone belongs to exactly one reality" and "every zone has a render
context" (deferred with their concepts).

---

## 10. Minimal vertical slice

Two slices, one per lane, together proving the direction without portal rendering,
migration tooling, or any space work.

Editor slice (Phases 1, E1, E2, E3): a world of one region and three zones. Hub Room
editable; Hallway A loaded as Context (greyed, unselectable, snappable); Boss Arena
header-only. Cut a doorway with `PierceFaceRect`; create a transition from the opening
targeting Hallway A (reverse edge minted); move one entity from Hub Room to Hallway A
ownership and undo/redo it; break the portal link by hand-editing and watch validation
name it and jump to it.

Runtime slice (Phase R): cook that world; in the template game, walk Hub Room toward
the doorway. Hallway A attaches dormant ahead of arrival, flips participation on entry,
Hub Room unloads behind after linger; the traversal-hitch harness reports zero missed
fixed ticks; `ZoneDemandRecord`s in the telemetry log answer "why is Hallway A
resident" at every step.

The proposal's five-zone slice with two regions adds a second region and two more
header-only zones; they prove nothing the third header-only zone does not, so they are
content, not slice scope.

---

## 11. Recorded deferrals and triggers

- **`RenderContext` / per-zone environment id.** Trigger: the first per-zone
  environment mechanism in the renderer (skybox or post settings varying by zone), or
  see-through portals crossing an environment boundary. Until then it would be a dead
  field.
- **`PortalRecord` in the manifest.** Trigger: see-through portal rendering or
  cross-space visibility needs portal shape header-side (Track C item 7, v2.0).
- **Subzone / Sector authoring concept.** Trigger: a zone whose cook cells and
  per-entity flags demonstrably cannot express a needed interior subdivision (e.g.
  designer-authored visibility sectors). The cook's grid cells stay an internal
  mechanism until then.
- **`Warp` on transitions, `SpaceId`, and all space tooling.** Trigger: the first
  authored content that needs a non-identity warp or a second space; v2.0 at the
  earliest. Section 8 records what v1.0 keeps clean so the trigger is cheap to honor.
- **Split Zone / Merge Zones / Adopt In Bounds / Reassign By Containment.** Trigger:
  a real world reorganization that Move Selection To Zone makes painful at bulk scale.
- **Graph (node-link) panel.** Trigger already recorded in the roadmap (v2.0 partition
  map view); also concretely: the first world where the tree stops answering adjacency
  questions.
- **Demand inspector panel.** Trigger: Phase R's `ZoneDemandRecord`s exist and a
  designer asks the question interactively; v2.0 alongside the risk dashboard panel.
- **`IZonePopulationStrategy`.** Unchanged from the roadmap: not until a second
  population policy exists. `WorldPartitionRuntime` ships with one concrete policy.
- **Cross-space causal correspondence data.** Trigger: the first gameplay feature where
  an event in one space must affect another; needs the scripting runtime and Track C
  item 6 first.

---

## 12. The nine questions, answered

1. **Is World, Reality, Region, Zone, Subzone-metadata the right model?** The spine is
   right; two edits. Reality becomes Space (mechanism name, Section 8) and is deferred
   to v2.0 with a clean upgrade path. Subzone/Sector is dropped from the authoring
   vocabulary; the cook's cells already are that mechanism, internally (Section 2.1).
2. **Is rejecting recursive runtime zones right?** Yes, and the existing span model
   already decides it (Section 2.1). Zone is the only runtime unit; region and space
   are metadata; independent children are sibling zones with transitions.
3. **Should loaded-inactive editing precede runtime streaming integration?** Neither
   precedes the other: they are parallel lanes over the Phase 1 manifest (Section 7).
   Serializing them would push the Track C dependency chain (metadata, tiers, detach,
   save system) behind editor UX work it does not need.
4. **Separate logical edge and aperture geometry records?** Yes: Transition record and
   Portal entity, optional link, one word for the geometry (Section 5).
5. **v1.0 versus v2.0?** v1.0: manifest and ids, `WorldPartitionRuntime` with one
   policy, world cook, partition tree, Context (loaded-inactive) zones, Move Selection
   To Zone, transition-from-opening with `PierceFaceRect`, structural validation,
   play-from-zone. v2.0: graph panel, see-through portals, demand inspector UI, bulk
   ownership tools, participation-tier preview, spaces and warps. Rationale and the
   roadmap moves this requires: Section 7 and Section 13.
6. **Missing validation rules?** Reachability from start, bounds overlap within a
   space, stale cook hash, portal-transition mutual linkage with an explicit
   unverifiable-until-loaded state, single player start, id uniqueness (Section 9).
   Also two of the proposal's rules dissolve structurally rather than being checked.
7. **Headers versus content?** The size rule: manifest is O(zones + transitions), never
   O(entities); headers carry identity, grouping, refs, bounds, cook status, edges;
   the sole content-pointing exception is the portal entity ref (Section 3.3).
8. **How does kyusu show different-space zones without implying distance?** By never
   composing spaces into one coordinate system implicitly: per-space solo, explicit
   labeled preview offsets as view state, distinct cross-space edge styling, and
   eventually portal-framed preview (Section 8).
9. **Smallest slice?** Section 10: one region, three zones (editable, context,
   header-only), one pierced doorway with a paired transition, one ownership move, one
   validation break; plus the runtime twin proving dormant preload and hitch-free
   traversal over the same cooked manifest.

---

## 13. Proposed roadmap amendments

For the roadmap owner to ratify; this document does not edit versions on its own
authority.

1. Track C item 3 gains this document as its execution spec (Section 12 table row).
2. Track D adds a v1.0 item: partition authoring loop in kyusu (world document,
   partition tree, editor zone states with Context, Move Selection To Zone,
   transition-from-opening). Justification: the v1.0 gate's "three interconnected zones
   authored in kyusu" clause has no path without it (Section 7).
3. `PierceFaceRect` (already batch 1, item 2 of the tool suite) is noted as a Phase E3
   dependency of this plan.
4. The v2.0 partition map view item absorbs the graph panel and demand inspector panel
   as its concrete contents.
5. When `WorldPartitionRuntime` lands (Phase R gate), delete its row from the roadmap's
   Section 3 vocabulary-versus-code table, per that table's own rule.
