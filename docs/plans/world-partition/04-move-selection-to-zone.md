# Phase E2: Move Selection To Zone

Status: execution spec (2026-07-03). Implements Phase E2 of
`docs/plans/world-partition-authoring.md` (Section 6.4 as amended; read it, then
`00-execution-overview.md` D3, D5, and D12, before writing code).

Prerequisite: Phase E1 complete (it is). Phase R is a parallel lane; neither blocks
the other.

Scope: the cross-zone move command, its two UI entry points, the D12 unload pin, and
the bounds-containment validation rule.

Non-goals (do not build any of it): bulk ownership tools (Adopt In Bounds, Reassign
By Containment, Split/Merge: deferred, design doc Section 11), Select Entities Owned
By Zone and Show Ownership Tint (dropped by owner decision, recorded in the amended
Section 6.4 with revisit triggers: a cross-zone audit-selection request; designer
confusion between multiple context zones), transitions and portals (E3), runtime
entity migration (Track C item 5), any change under `engine/`.

Stages M1 through M3, in order, each a separate commit with the suite green.
`scripts/check_editor_layering.sh` is part of every gate.

---

## Standing decisions for every stage

- **A move is content-only.** It touches two zone documents and their brush mesh
  sidecars; it never writes a manifest field. The phase-end grep audit enforces this.
- **The target zone must already be open.** Loading it is the designer's explicit
  prior step (Load As Context in the tree); the command factory refuses rather than
  loading implicitly (overview Section 6 pin, unchanged).
- **Selection is focus-zone-only** (the `SelectionContext` registry guard from E1
  W3). Moved entities land in a non-focus document, so the move clears the selection;
  it never tries to select the moved copies.
- **`BrushId`s are per-document.** Restoring into the target uses a fresh mesh id;
  undoing back into the source re-seats the original id (the delete-undo precedent in
  `EditorDocument::RestoreEntity`).

---

## M1. `MoveEntitiesToZoneCommand` and the unload pin

### New files: `editor/kyusu/src/document/commands/MoveEntitiesToZoneCommand.h` / `.cpp`

```cpp
#pragma once

#include "document/EntitySnapshot.h"

#include "commands/ICommand.h"

#include <ecs/EntityId.h>
#include <zone/ZoneId.h>

#include <memory>
#include <span>
#include <vector>

class EditorDocument;
class SelectionService;
class WorldDocument;

// Moves entities between two zone documents by capture/destroy/restore: the
// snapshot path already carries every registered component, the brush mesh
// sidecar entry, and the view flags, so a move is a delete in one document and
// a restore in the other (design doc Section 6.4). Restore into the target
// mints fresh BrushIds (ids are per-document); undo re-seats the originals in
// the source. Moving one placement of an instanced brush un-instances the
// moved copy: mesh sharing is per-document state and does not survive the
// crossing.
class MoveEntitiesToZoneCommand : public ICommand
{
public:
    MoveEntitiesToZoneCommand(std::span<const EntityId> entities,
                              EditorDocument& source, EditorDocument& target);

    void Execute() override;   // capture once; destroy in source; restore in target (fresh mesh ids)
    void Undo() override;      // destroy in target; restore in source (original mesh ids re-seated)

private:
    EditorDocument& Source;
    EditorDocument& Target;
    std::vector<EntitySnapshot> Snapshots;
    // Live ids in whichever document currently holds the entities (target after
    // Execute, source after Undo). Fresh generational handles each time; nothing
    // outside the command may cache them.
    std::vector<EntityId> CurrentIds;
    bool Captured = false;
};

// The user-facing assembly: SelectCommand(clear) first, then the move, as one
// CompositeCommand (the MakeDeleteEntitiesCommand pattern). Returns nullptr and
// logs when the move is refused:
//   - the workspace is not in world mode,
//   - target names no manifest zone or is not open (load it as context first),
//   - target is the focus zone's own id... the entities are already there,
//   - the entity list is empty after dropping non-entity refs.
[[nodiscard]] std::unique_ptr<ICommand> MakeMoveEntitiesToZoneCommand(
    std::span<const EntityId> entities, WorldDocument& world, ZoneId target,
    SelectionService& selection);
```

Pinned semantics, exhaustive:

- `Execute`: on first run, `Source.CaptureEntity` per entity (snapshots reused across
  redo, the `DuplicateEntitiesCommand` precedent). Then destroy each in `Source`, then
  `Target.RestoreEntity(snapshot, /*freshMesh*/ true)`. Both documents `MarkDirty`.
- `Undo`: destroy each moved entity in `Target`, then
  `Source.RestoreEntity(snapshot, /*freshMesh*/ false)`, which re-seats the original
  `BrushId` (free again because Execute's destroy released it). Both documents
  `MarkDirty`.
- Order within the composite: the `SelectCommand` clear runs first on Execute and
  therefore last on Undo, restoring the pre-move selection (its own snapshot
  behavior, unchanged).
- An entity carrying E3's portal component moves like any other; the stale linkage it
  produces is validation's job (`partition.portal.wrong_zone`, specced in `05-`),
  not a special case here.

### The D12 unload pin (same stage, it protects this command)

`WorldDocument` gains:

```cpp
// Fires after a zone document is destroyed by UnloadZone. The workspace clears
// the undo stack on it: commands may hold references into any open zone
// document (the cross-zone move holds two), and an unload would dangle them.
// Narrower than the D5 focus reset: focus did not change, so the tool context,
// sink, and selection stay.
std::function<void(ZoneId)> OnZoneUnloaded;
```

`EditorWorkspace::Init` binds it: `World.OnZoneUnloaded = [this](ZoneId) { if
(Commands != nullptr) Commands->Clear(); };`. `CloseWorldToLegacy` and world reload
paths already clear the stack through the D5 reset; this observer covers the one gap
(explicit Unload of a context zone).

### Gate M1

New tests in `test/editor/MoveEntitiesToZoneTests.cpp` (headless, the
`WorldDocumentTests.cpp` fixture pattern):

- `MoveRestoresEntityInTargetRegistry` (component data intact, entity gone from
  source, present in target)
- `MoveCarriesBrushMeshSidecarEntry` (mesh geometry equal in target's store; source
  store entry released)
- `MoveUnsharesInstancedMesh` (two instances in source; moving one leaves the other
  alive on the original mesh; the moved copy has its own)
- `UndoReseatsOriginalBrushIdInSource`
- `RedoMovesAgainWithFreshTargetId`
- `MoveMarksBothDocumentsDirty`
- `FactoryRefusesUnloadedTarget`
- `FactoryRefusesFocusTarget`
- `FactoryRefusesEmptySelection`
- `UnloadZoneFiresObserver` (WorldDocument level)
- `UnloadClearsUndoStack` (workspace level: execute a move, unload the target's
  sibling... any open non-focus zone; `CanUndo()` is false after)

---

## M2. UI entry points

1. `SceneHierarchyPanel`: the entity row context menu (today: Delete) gains
   **"Move To Zone >"**, a submenu listing every open zone except the focus zone, in
   manifest order, by name. Empty (no other open zone): the item is disabled with
   tooltip "load a target zone as context first". Selecting a target routes the
   CURRENT entity selection (entity-kind refs only) through
   `MakeMoveEntitiesToZoneCommand` on the workspace command stack.
2. `WorldPartitionPanel`: the zone row context menu gains **"Move Selection Here"**,
   enabled when the zone is open, is not the focus zone, and the selection contains
   at least one entity-kind ref. Same factory, same stack.
3. Both entry points report through the status/log line already available to panels
   ("moved N entities to <zone name>"); no new UI surface.

### Gate M2

Suite green with zero test edits. Manual, on a two-zone world fixture: select two
brushes in the focus zone, Move To Zone via each entry point; entities appear dimmed
in the context zone; undo returns them selected-cleared; redo moves again; saving
both zones and reopening the world persists the move.

---

## M3. Bounds-containment validation and the phase gate

### What changes

`WorldDocument::RunValidation` (the editor-side half, beside `scene_unresolved`)
gains one rule:

| RuleId | Severity | Fires when |
| --- | --- | --- |
| `partition.zone.entity_outside_bounds` | Warning | An open zone has at least one entity whose `TryGetWorldBounds` result is not fully contained by the zone header's `Bounds`. One record per zone; `SourceId` is the zone id; the message names the entity count. |

Pinned details:

- Open (loaded) zones only; header-only zones have no content to check.
- Entities without world bounds (no brush mesh, no boundable component) are skipped.
- Containment is `Bounds.Contains` of both corners of the entity box (add a small
  epsilon only if the first fixture proves float noise fires it; note the decision in
  the commit if taken).
- No `BoundsOverridden` exemption, deliberately resolving the design doc's "unless
  `BoundsOverridden`" clause the other way around: zones with DERIVED bounds self-heal
  on save (E1 W5 recomputes the union), so the rule is transient noise there at
  worst; designer-set bounds are exactly where a straying entity stays wrong until a
  human acts, so the warning must persist there. The design doc clause is amended by
  this spec.

### Gate M3

- Tests in `test/editor/MoveEntitiesToZoneTests.cpp` (same file; the rule exists for
  the move workflow): `EntityOutsideBoundsFires`,
  `EntityOutsideBoundsSilentWhenContained`,
  `EntityOutsideBoundsSelfHealsOnSaveForDerivedBounds` (save recomputes bounds, rule
  stops firing).
- `test/level_cook/WorldCookTests.cpp` gains `CookReflectsCrossZoneMove`: cook, move
  an entity between zones, save, re-cook; the source zone's content hash changes,
  the target's changes, and the moved entity's geometry appears in the target's
  cooked artifacts only.

---

## Definition of done (whole phase)

The overview Section 5 checklist per stage, plus:

- The design doc Phase E2 gate: move an entity between zones, undo, redo; the cook
  reflects the move; validation updates.
- Grep audits: the W1 reference-member audit re-run
  (`grep -rn "EditorDocument& *[A-Za-z_]*;\|EditorScene& *[A-Za-z_]*;"
  editor/kyusu/src --include=*.h` gains only `MoveEntitiesToZoneCommand`'s two
  members, safe under D12 exactly as the other command members are safe under D5);
  `grep -n "Manifest()" editor/kyusu/src/document/commands/MoveEntitiesToZoneCommand.cpp`
  returns nothing (a move never touches the manifest).
- Legacy mode untouched: every entry point is world-mode-gated; no legacy test
  edited.
