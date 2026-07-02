# Phase E1: World Document and Partition Tree

Status: execution spec (2026-07-02). Implements Phase E1 of
`docs/plans/world-partition-authoring.md` (Sections 6.1 through 6.3 there). Read that,
then `00-execution-overview.md` (especially decisions D3, D4, D5, D7, D8), before
writing code. Phase 1 (`01-manifest-and-identity.md`) must be complete: this phase
consumes `WorldPartitionManifest`, the id helpers, `WorldPartitionIndex`, and
`ValidateWorldPartitionManifest`.

Scope: the `WorldDocument` container, the workspace refactor, `.sworld` file actions,
zone editor states with the Context (loaded, visible, locked) preset, the partition
tree panel, the zone bounds overlay, and the world cook.

Non-goals (later phases; do not build any of it): entity moves between zones (E2),
transitions or portals in the editor (E3), snap-to-context-zone (E4), real-material
dimming of context zones (E4), any runtime streaming (R), multi-editable zones
(rejected for v1.0, overview D3).

Stages W0 through W6, in order, each a separate commit with the suite green.
`scripts/check_editor_layering.sh` is part of every gate.

---

## Standing decisions for every stage

- **Exactly one editable zone: the focus zone (overview D3).** `WorldDocument` stores
  `FocusZone`; "is this zone editable" is `zone == FocusZone()`. There is no stored
  editable flag anywhere, so the invariant cannot drift.
- **Focus change goes through the document-open reset path (overview D5).** Changing
  focus tears down and rebuilds the tool context, manipulation sink, sessions, pivot,
  marquee, and overlay, clears the selection, and clears the undo stack, via the same
  code `DocumentFileActions::ResetEditorState` runs on Open today (refactor that
  function so both callers share it; do not duplicate it).
- **Reference-holding rule.** Long-lived objects (panels, `DocumentFileActions`,
  `PieDriver`) never hold `EditorDocument&` or `EditorScene&` members; they hold
  `EditorWorkspace&` (or `WorldDocument&`) and resolve the focus document at each use.
  Exempt, because D5 destroys and rebuilds them on every focus change: `ToolContext`,
  `BrushManipulationSink`, and anything they own. The W1 grep audit enforces this.
- **View state is not undoable and not shared.** Zone open/visible/focus states and
  overlay toggles are view state (the grid-frame precedent): never on the
  `CommandStack`, never in the `.sworld`, persisted per user in a
  `<world>.sworld.user.json` sidecar (W2).

---

## W0. `WorldDocument`

New files: `editor/kyusu/src/document/WorldDocument.h` / `.cpp`. No wiring into the
workspace yet; this stage is the type plus its headless tests.

```cpp
#pragma once

#include "EditorDocument.h"

#include <zone/WorldPartitionManifest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Per-zone editor view state. Editability is NOT here: the focus zone is the one
// editable zone (00-overview D3), derived, never stored.
struct ZoneViewState
{
    bool VisibleInEditor = true;
};

// The container above EditorDocument: the authored partition manifest plus the set
// of open zone documents. Exactly one of two modes for its whole lifetime:
//   Legacy: no manifest; one anonymous zone document (a bare .level.json). Every
//           existing single-level behavior routes through this mode unchanged.
//   World:  manifest-backed (.sworld); zones open and close individually.
class WorldDocument
{
public:
    explicit WorldDocument(LoggingProvider& logging);

    // Mode and files.
    [[nodiscard]] bool IsWorld() const;
    bool LoadWorld(std::string_view path);          // parses .sworld, loads start zone as focus
    bool SaveWorld();                                // world file + every dirty zone document + user sidecar
    bool SaveWorldAs(std::string_view path);
    void NewWorld(std::string_view name);            // one minted region and zone, focus on it
    // Legacy passthroughs (New/Load/Save/SaveAs on the single zone document).

    [[nodiscard]] WorldPartitionManifest& Manifest();          // World mode only (asserts)
    [[nodiscard]] const WorldPartitionIndex& Index() const;    // rebuilt on manifest edits

    // Zone lifecycle. LoadZone deserializes the zone's scene into a fresh
    // EditorDocument (Context state). UnloadZone refuses (returns false, logs)
    // when that zone document is dirty; saving first is the caller's job.
    bool LoadZone(ZoneId zone);
    bool UnloadZone(ZoneId zone);
    [[nodiscard]] bool IsZoneOpen(ZoneId zone) const;
    bool SetZoneVisible(ZoneId zone, bool visible);

    // Focus. SetFocusZone loads the zone if needed, then fires OnFocusChanged
    // (after the switch; the workspace uses it to run the D5 reset).
    bool SetFocusZone(ZoneId zone);
    [[nodiscard]] ZoneId FocusZone() const;
    [[nodiscard]] EditorDocument& FocusDocument();
    [[nodiscard]] const EditorDocument& FocusDocument() const;

    // Deterministic iteration for rendering and the tree: manifest order.
    // fn(ZoneId, EditorDocument&, const ZoneViewState&), open zones only.
    template <typename Fn> void VisitOpenZones(Fn&& fn);

    [[nodiscard]] bool IsDirty() const;   // any zone document dirty, or the manifest edited

    // Id minting (00-overview D4): random nonzero 64-bit, re-rolled on collision
    // with any id already in the manifest. Editor-side only by design.
    [[nodiscard]] ZoneId MintZoneId();
    [[nodiscard]] RegionId MintRegionId();
    [[nodiscard]] TransitionId MintTransitionId();

    // Manifest edits go through verbs so the index rebuild and dirty flag cannot
    // be forgotten. W4's panel calls these; nothing writes Manifest() fields raw.
    ZoneId AddZone(RegionId region, std::string name);        // mints id, empty SceneRef until first save
    RegionId AddRegion(std::string name);
    bool RenameZone(ZoneId zone, std::string name);
    bool RenameRegion(RegionId region, std::string name);

    std::function<void()> OnFocusChanged;

private:
    // Each open zone document gets a unique RegistryId {NextRegistryIndex++, 1},
    // starting at 2, never reused within a session (the ZoneRuntime discipline),
    // so a stale SelectableRef can never alias a later-opened zone.
    // EditorDocument gains SetRegistryIdentity(RegistryId, ZoneId), callable only
    // while the document is empty (asserted), for exactly this hand-off.
};
```

Notes pinned:

- `SetAssetEnvironment` is forwarded to every zone document at load (the same
  `RuntimeAssets` the workspace already binds).
- `AddZone` leaves `SceneRef` empty until the world is saved, at which point
  `SaveWorld` writes `levels/<sanitized-zone-name>.level.json` for any zone lacking
  one and fills the ref. Sanitization: lowercase, spaces to underscores, ASCII only;
  collision appends `_2`, `_3`.
- Zone name uniqueness is not enforced (ids are identity); the tree may show
  duplicates and validation stays silent about them in E1.
- Manifest edits mark the world dirty; `Index()` rebuilds lazily on next access
  after an edit.

Gate W0: new tests in `test/editor/WorldDocumentTests.cpp` (headless, silent
`LoggingProvider`, no UI):

- `LegacyModeWrapsSingleDocument` (construct, no manifest, focus document exists,
  `IsWorld()` false)
- `NewWorldHasOneRegionOneZoneFocused`
- `MintedIdsAreNonzeroAndUniqueAcrossManifest`
- `LoadZoneAssignsUniqueMonotonicRegistryIds` (open two zones, close one, open the
  third: ids 2, 3, 4, no reuse)
- `SetFocusLoadsAndFiresObserverOnce`
- `UnloadRefusesDirtyZone`
- `WorldSaveLoadRoundTripsManifestAndZoneScenes` (two zones with one entity each,
  through a temp dir)
- `SaveWritesSceneFilesForNewZones` (empty `SceneRef` filled, file exists)
- `VisitOpenZonesFollowsManifestOrder`

---

## W1. Workspace surgery

The mechanical stage. No new behavior; the editor must be indistinguishable before
and after.

1. `EditorWorkspace.Document` (inline member, `EditorWorkspace.h:94`) becomes
   `WorldDocument World;`. Add `EditorDocument& ActiveDocument()` forwarding to
   `World.FocusDocument()`.
2. Every current use of the workspace document resolves through `ActiveDocument()`
   (or takes the document as a per-call parameter, which many tools already do).
   Do the replacement mechanically: `grep -rn "Workspace->Document\|Workspace_.Document\|workspace.Document" editor/`
   and fix every hit.
3. Constructor changes for the long-lived holders, per the reference-holding rule:
   `SceneHierarchyPanel` (currently `EditorScene& Scene; EditorDocument& Document;`),
   `DocumentFileActions` (`EditorDocument& Document;`), `PieDriver`, `InspectorPanel`,
   and `MeshEditPanel` if they hold document or scene members: each now takes
   `EditorWorkspace&` and resolves at use time. `EditorServices` wiring updates to
   match.
4. Extract the body of `DocumentFileActions::ResetEditorState` into a workspace
   method (`EditorWorkspace::ResetInteractionState()`); `DocumentFileActions` calls
   it; `World.OnFocusChanged` is bound to it in `EditorWorkspace::Init`. In legacy
   mode focus never changes, so the observer never fires and behavior is identical.

Gate W1: full suite green with zero test edits. Manual smoke: open, edit, save,
undo, cook, PIE a legacy level; all identical. Grep audit:

```
grep -rn "EditorDocument& *[A-Za-z_]*;\|EditorScene& *[A-Za-z_]*;" editor/kyusu/src --include=*.h
```

returns hits only inside `WorldDocument`, `ToolContext`, `BrushManipulationSink`,
and types owned by those two (list the survivors in the commit message).

---

## W2. World file actions and the user sidecar

1. `DocumentFileActions` learns `.sworld`: the Open dialog filter accepts both
   extensions and routes by extension (`LoadWorld` vs the legacy path). `Save` on a
   world calls `SaveWorld()`. New menu entry "New World" (calls `NewWorld`; existing
   "New" keeps making a legacy level). Window title becomes
   `<world name> : <focus zone name> [*]` in world mode; legacy format unchanged.
2. The user sidecar `<world>.sworld.user.json`: written by `SaveWorld` and on world
   close, read by `LoadWorld`. Contents, snake_case: `focus_zone` (hex id), and
   `zones` as an array of `{ "id", "open", "visible" }`. Malformed or missing
   sidecar is silently ignored (defaults: start zone focused, nothing else open).
   Recommend adding `*.sworld.user.json` to the template project's ignore rules
   (one line in `template/`; do it, it is content hygiene).
3. Validation wiring, editor half: on world load, save, and any manifest edit, run
   `ValidateWorldPartitionManifest` plus one editor-side check the pure layer
   cannot do: `partition.zone.scene_unresolved` (Error) when a nonempty `SceneRef`
   resolves to no file under the project content roots. Store the merged records on
   `WorldDocument` for W4's panel to display; log Errors through the existing
   logging provider as they are produced.

Gate W2: `test/editor/WorldDocumentTests.cpp` additions:
`UserSidecarRoundTripsFocusAndZoneStates`, `MissingSidecarYieldsDefaults`,
`SceneUnresolvedRecordFiresForMissingFile`. Manual: create a world, add nothing,
save, reopen; focus and states persist.

---

## W3. Zone view states in viewport and picking

1. Rendering (`EditorRenderFeature` and the renderers it owns): instead of the
   single workspace registry, iterate `World.VisitOpenZones`, skipping
   `!VisibleInEditor`. The focus zone renders exactly as today (all paths including
   `MeshForwardPass`). Context zones render dimmed and reduced:
   - brush solids through the existing tintable solid-preview path, tint multiplied
     by the theme constant `EditorUiStyle::ContextZoneDim` (new, default
     `{0.45f, 0.45f, 0.50f, 1.0f}`),
   - wireframe and component visuals with per-segment colors multiplied by the same
     constant,
   - no `MeshForwardPass` (real-material context rendering is E4), no selection
     highlight, no hover glow.
2. Picking: every `PickingService` call site passes the focus zone's scene only.
   Context zones are unpickable by construction, which implements "greyed out,
   cannot select, but visible". (Snap-to-context is E4; do not add an
   include-locked mode now.)
3. Guard: `SelectionContext` gains a debug assert that every ref added carries the
   focus document's `RegistryId`.

Gate W3: manual fixture (a world with two zones authored via W2): focus zone edits
normally; the context zone is visible, dimmed, and completely inert to clicks,
marquee, and Select All; hiding it removes it from the viewport. Suite green.
No automated viewport test (consistent with the existing render features); the
`SelectionContext` assert is the regression net.

---

## W4. Partition tree panel and scoped outliner

1. New panel: `editor/kyusu/src/ui/WorldPartitionPanel.{h,cpp}`, an `IEditorPanel`,
   `DockSlot::Left`, registered in `EditorServices::BuildUi`, hidden (not created)
   in legacy mode. Content, top to bottom:
   - Tree: region nodes (collapsible) containing zone rows in manifest order. A
     zone row shows: name; state badge (`F` focus, `C` context, `H` hidden,
     nothing for header-only) rendered as a small colored tag; a dirty dot when
     that zone document is dirty; `EditorUi::TextDim` for header-only rows.
   - Row interactions: double-click focuses (loading if needed); an eye toggle
     flips `VisibleInEditor` (open zones only); right-click menu: Focus, Load As
     Context, Unload (disabled while dirty, tooltip says save first), Rename
     (inline text edit); region right-click: New Zone, Rename; panel-header menu:
     New Region.
   - Validation list: the stored records from W2, one row each
     (severity icon, rule id, message). Clicking a row focuses the offending
     zone's region in the tree and, for zone-kind records, selects the row.
     No fix-it actions in E1.
2. All mutations route through the `WorldDocument` verbs (`AddZone`, `AddRegion`,
   `Rename*`, `SetFocusZone`, `LoadZone`, `UnloadZone`, `SetZoneVisible`). The
   panel holds `EditorWorkspace&` only and contains no state beyond ImGui
   transients (the inline-rename buffer). Manifest verbs are not undoable in E1
   (they are document structure, like file operations; recorded trigger to revisit:
   a designer loses work to a mis-click Rename).
3. `SceneHierarchyPanel`: already holds `EditorWorkspace&` after W1; its draw now
   reads the focus zone's scene and titles itself with the focus zone name in
   world mode. No other behavior change (still flat, still per-entity eye/lock).

Gate W4: manual, on the two-zone fixture: focus switching from the tree works and
resets interaction state (D5); creating a region and zone from the tree and saving
produces the expected `.sworld` and scene file; breaking the manifest by hand
(dangling transition endpoint added to the JSON) shows the validation row on
reopen and clicking it navigates. Suite green.

---

## W5. Zone bounds overlay and derived bounds

1. Derived bounds: on each zone document save, compute the union of
   `TryGetWorldBounds` over its entities and write it to that zone's header unless
   `BoundsOverridden`; a zone with no boundable entities keeps its previous bounds.
   Pure helper `ComputeZoneBounds(const EditorScene&)` in the document layer,
   unit-tested.
2. Overlay: new `ZoneBoundsRenderer` in `editor/kyusu/src/render/`, a sibling of
   `ComponentVisualRenderer`, drawing through `EditorWideLinePipeline`: one AABB
   wire box per manifest zone (loaded or not). Colors from the theme: focus zone
   in the selection accent, open zones in the standard line color, header-only
   zones in the dim constant. Zone name labels through
   `EditorOverlayState.Labels` at the box top center.
3. Toggle: `bool ShowZoneBounds` (default on in world mode) in a new
   `WorldViewSettings` value member on `EditorWorkspace` (view state, the
   `GridSettings` pattern), surfaced as a toolbar toggle next to the existing view
   toggles. Persisted in the user sidecar.

Gate W5: `ComputeZoneBoundsUnionsEntities` and
`ComputeZoneBoundsRespectsOverrideFlag` in `test/editor/`; manual: three boxes in
three states visible, labels on, toggle works and persists.

---

## W6. World cook

1. New `editor/kyusu/src/document/WorldCook.{h,cpp}`:
   `CookWorld(WorldDocument&, assetsRoot, cellSize, logging, RuntimeAssets*)`.
   Refuses (error result naming the zones) when any zone document is dirty or any
   `SceneRef` is unresolved: the world cook cooks saved files only, no live-snapshot
   path (the live cook stays a legacy-mode feature; recorded trigger to revisit:
   designer friction).
2. For each manifest zone in order: run the existing file-based `CookDocument` on
   its scene. Reuse its outputs verbatim; this stage adds no cook mechanics. Collect
   per zone: cooked scene path, collision sidecar path, and the content hash the
   cooked cache keyed that cook by (expose it on `CookDocument`'s result struct if
   it is not already there).
3. Write the cooked manifest to `.cooked/worlds/<world>.sworld.json` via
   `WriteWorldPartitionManifest`: the authored records plus `cooked_scene`,
   `cooked_collision`, and `content_hash` per zone. Unchanged zones re-cook as
   cheaply as the cooked cache already makes them; the world cook adds no second
   cache.
4. `PieDriver`: in world mode the Cook button runs `CookWorld`; Play stays
   single-zone on the focus zone's cooked scene (`+map` with its cooked path).
   Play-from-world (`+world`) is Phase R's runtime work; do not fake it here.

Gate W6: test in `test/level_cook/WorldCookTests.cpp`: a temp-dir world with two
one-brush zones cooks to a cooked manifest whose two zones carry nonzero hashes and
existing cooked scene paths; re-cooking without edits rewrites byte-identical zone
artifacts (id stability); editing one zone changes only that zone's hash. Manual:
Cook then Play from the editor on the fixture world plays the focus zone.

---

## Definition of done (whole phase)

The overview Section 5 checklist per stage, plus:

- The design doc's Phase E1 gate: open a three-zone world; edit one zone with a
  second greyed and unselectable and a third header-only; save; reload; states and
  validation persist.
- Legacy levels: every pre-existing workflow (new, open, edit, undo, save, cook,
  PIE) byte-for-byte unaffected in behavior; no legacy test edited.
- Grep audits from W1 still pass at phase end.
- No manifest field is written anywhere except through `WorldDocument` verbs and
  `SaveWorld`.
- The editor builds with no new warnings; `scripts/check_editor_layering.sh` green;
  nothing under `engine/` changed in this phase except additions from Phase 1 (if
  a needed engine hook is missing, stop; that is a Phase 1 gap to fix there, not
  an editor workaround).
