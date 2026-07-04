# Phase E3: Transitions and Portals

Status: execution spec (2026-07-03). Implements Phase E3 of
`docs/plans/world-partition-authoring.md` (Sections 5 and 9 rules 4 through 6, both
as amended) under the owner override recorded as `00-execution-overview.md` D9:

**A portal is a marker volume brush.** The designer cuts the opening with the
existing mesh tools, fits a thin box brush into it, and flags that brush as a portal.
There is no opening-cut verb in this phase; `PierceFaceRect` is dropped from E3 and
survives only as an unrelated roadmap tool-suite item. The portal component stores
only the linked `TransitionId` (D1 unchanged: the manifest never references
entities); shape and normal derive from the brush geometry.

Prerequisites: Phase E1 complete (it is); Phase E2's D12 unload pin in place (a
transition popup can load zones as context, and linked-portal commands sit on the
undo stack across zone documents).

Scope: the portal component and its rendering, cook exclusion, transition CRUD verbs
and validation rules, and the partition panel's transition UI.

Non-goals (do not build any of it): see-through portal rendering and `PortalRecord`
manifest headers (v2.0, Track C item 7), any runtime portal consumption (D13; the
cook strips portals), transition timing semantics (Track C item 6), warps and spaces
(v2.0, design doc Section 8), any `BrushOps` change, any portal-specific gizmo (a
portal is a brush; the existing manipulators are its gizmo).

Stages T1 through T5, in order, each a separate commit with the suite green.
`scripts/check_editor_layering.sh` is part of every gate.

---

## Standing decisions for every stage

- **The portal component is editor-side and trivially copyable.** It lives beside
  `BakedBrushComponent` (the marker-beside-`BrushComponent` precedent) and never
  appears under `engine/` (D13; the phase-end grep audit enforces it).
- **Transition records are manifest edits through `WorldDocument` verbs**,
  non-undoable like `AddZone` (D11). The portal link (the component's field) is an
  ordinary undoable component edit. An undone link can orphan a transition record;
  validation reports it (`partition.portal_missing` fires again) and removal stays an
  explicit panel action.
- **Derived geometry, never stored.** The portal's normal axis is the axis of
  minimum world-bounds extent of its brush, computed by a pure helper. No frame, no
  rect, no normal field anywhere.
- **Reverse pairing is derived** (matching swapped endpoints, validation rule
  `partition.transition.unpaired` from Phase 1). Creating a non-OneWay transition
  mints the reverse edge with its own id; nothing stores a pairing field.

---

## T1. Portal component, serialization, rendering

### What changes

1. `editor/kyusu/src/document/EditorScene.h`, beside `BakedBrushComponent`:

```cpp
// Marks a brush entity as a portal: the marker volume realizing a transition
// (a thin box fitted into the opening; the areaportal shape). Editor-only, like
// BrushComponent: the cook strips it and bakes no geometry for it. Transition
// is the manifest edge this portal realizes; invalid means placed but not yet
// linked, which validation reports rather than forbids.
struct PortalComponent
{
    TransitionId Transition;
};
static_assert(std::is_trivially_copyable_v<PortalComponent>);

template <>
struct TypeSchema<PortalComponent>
{
    static constexpr std::string_view Name = "portal";

    static auto Fields()
    {
        return std::tuple{
            MakeField("transition", &PortalComponent::Transition),
        };
    }
};
```

2. `editor/kyusu/src/document/DocumentSerialization.cpp`: a
   `SceneFieldCodec<TransitionId>` specialization mirroring the `BrushId` codec but
   through the 16-hex text forms (`TransitionIdToString` /
   `TransitionIdFromString`; malformed text fails the field load), and
   `PortalComponent` registration in `RegisterDocumentSerializers()` following the
   `BakedBrushComponent` block. Registration makes the component appear in the
   inspector's generic Add Component menu with no further work (verify; if the menu
   curates its list, add the entry there).

3. `EditorScene` gains `[[nodiscard]] bool IsPortal(EntityId entity) const;` (a
   `TryGet` presence check, the `TryGetBakedBrush` shape). This is the one predicate
   every other stage keys on.

4. Rendering: `editor/common/src/EditorTheme.h` gains

```cpp
// Portal marker brushes: read as volumes, not walls. Translucent cyan fill so
// the opening stays visible through them; the wire stays full-strength.
inline constexpr Vec4 PortalFill{ 0.25f, 0.75f, 0.95f, 0.30f };
inline constexpr Vec4 PortalWire{ 0.25f, 0.75f, 0.95f, 1.0f };
```

   `BrushSolidRenderer` tints portal brushes with `PortalFill` in place of the
   per-material tint; `WireframeRenderer` draws their edges in `PortalWire` instead
   of the body color. Context zones modulate both by `ContextZoneDim`, like
   everything else. Picking is untouched: a portal picks, selects, and manipulates as
   the brush it is.

### Gate T1

New tests in `test/editor/PortalTests.cpp` (headless):

- `PortalComponentRoundTripsThroughSceneJson` (save/load a scene with a linked and
  an unlinked portal; ids intact)
- `PortalSurvivesCaptureRestore` (the E2 move path carries it)
- `TransitionIdCodecRejectsMalformed` (bad hex fails the component load, entity
  still restores its other components)
- `IsPortalReflectsComponentPresence`

Grep audit: `grep -rn "PortalComponent" engine/` empty.

---

## T2. Cook exclusion

### What changes

`CollectCookBrushes` (`editor/kyusu/src/document/BrushCookInput.cpp`) skips entities
where `scene.IsPortal(entity)`. That one predicate placement carries the exclusion
to every baked surface, because all three consumers collect through it: the file and
live cooks (`DocumentCook.cpp`: render cells AND the collision blobs, which are baked
from the same collected cells) and the editor's real-material Solid path
(`SceneRenderQueueBuilder`). The editor's non-baked paths (`ForEachVisibleBrush`:
solid preview, wireframe, picking) are deliberately untouched; portals must render
and pick in the editor.

The cooked passthrough scene must not carry the portal entity either: verify the
brush-entity drop in `DocumentCookKernel` (it destroys brush entities before
`SaveSceneJson`) covers portal brushes (they carry `BrushComponent`, so it does), and
pin that with the test below rather than code.

### Gate T2

Test in `test/level_cook/WorldCookTests.cpp`: `CookStripsPortalBrushes` (a zone with
one wall brush and one linked portal brush cooks to artifacts whose cell geometry
and collision sidecar reflect only the wall, and whose cooked scene contains no
portal entity and no `"portal"` key). Suite green.

---

## T3. Transition verbs, the `Unverified` severity, validation rules

### What changes

1. `WorldDocument` gains the transition verbs, exactly the `AddZone` pattern
   (mint editor-side, `MarkManifestEdited`, `RunValidation`, non-undoable):

```cpp
// Mints the edge (and nothing else; reverse pairing is the caller's explicit
// second call). Returns the new id.
TransitionId AddTransition(ZoneId from, ZoneId to, TransitionTopology topology,
                           bool oneWay, int32_t preloadPriority);
bool RemoveTransition(TransitionId transition);
bool SetTransitionTopology(TransitionId transition, TransitionTopology topology);
bool SetTransitionOneWay(TransitionId transition, bool oneWay);
bool SetTransitionPreloadPriority(TransitionId transition, int32_t priority);
```

2. `engine/include/zone/ContentRiskRecord.h`: `ContentRiskSeverity` gains a third
   value, `Unverified`, for rules whose inputs are not loaded (D1's
   "unverifiable until loaded is a visible state, not a silence"; Warning would
   misreport and silence is forbidden). D6-style coordination note in the header:
   if Track C item 1's streaming records land first with their own severity
   vocabulary, use it and delete this note. The panel renders `Unverified` with a
   dim question-mark icon (`EditorUi::TextDim`).

3. New pure helper `editor/kyusu/src/document/PortalGeometry.h` / `.cpp`:

```cpp
// The portal's derived facing: the world axis (0/1/2) of minimum extent of its
// brush bounds. A thin box fitted into a wall faces through that wall; nothing
// is stored on the component (00-overview D9).
[[nodiscard]] int DominantPortalAxis(const Aabb3d& worldBounds);
```

4. `WorldDocument::RunValidation` appends the E3 rules after `scene_unresolved` and
   E2's `entity_outside_bounds`, in this table order, ascending source id within a
   rule. Portal scanning covers OPEN zones only; rules whose From zone is not open
   emit `Unverified` instead of a verdict.

| RuleId | Severity | Kind / SourceId | Fires when |
| --- | --- | --- | --- |
| `partition.transition.portal_missing` | Warning | Transition | A Doorway transition whose From zone is open has zero linked portals in it. |
| `partition.transition.portal_duplicate` | Error | Transition | More than one portal (across all open zones) names the same transition. |
| `partition.transition.portal_unverified` | Unverified | Transition | A Doorway transition whose From zone is not open. |
| `partition.transition.portal_misaligned` | Warning | Transition | Doorway, From open, exactly one linked portal, and `DominantPortalAxis` of the portal's bounds is not the dominant axis of `To.Bounds.Center() - From.Bounds.Center()`; skipped when the centers coincide within epsilon. The honest check on a symmetric thin box; direction sign is not checkable without a stored normal, which D9 forbids. |
| `partition.portal.unlinked` | Warning | Zone | An open zone contains a portal whose `Transition` is invalid or names no manifest record. One record per zone; message counts them. |
| `partition.portal.wrong_zone` | Error | Transition | A portal's linked transition's `From` is not the zone the portal lives in (the E2 move-a-portal case). |
| `partition.portal.brush_missing` | Error | Zone | An entity carries `PortalComponent` but has no brush mesh (the inspector let it onto a non-brush entity). One record per zone. |

Teleport and Seam transitions are exempt from all portal rules (a doorway is the
only topology that promises an aperture).

### Gate T3

New tests in `test/editor/TransitionValidationTests.cpp`:

- `AddTransitionMintsAndReindexes` (index shows the new edge; dirty set;
  validation ran)
- `RemoveTransitionDropsRecordAndRevalidates`
- `SettersRewriteAndRevalidate`
- One test per rule, named after it: `PortalMissingFires`, `PortalDuplicateFires`,
  `PortalUnverifiedWhenFromUnloaded`, `PortalMisalignedFires`,
  `PortalUnlinkedFires`, `PortalWrongZoneFires`, `PortalBrushMissingFires`
- `TeleportSkipsPortalRules`
- `DominantAxisIsMinimumExtent` (the pure helper, three orientations)

Grep audit: `grep -rn "Unverified" engine/include/zone/ContentRiskRecord.h` shows
the value plus its coordination note and nothing else engine-side consumes it yet.

---

## T4. Partition panel transition UI and linking flows

> **Amended 2026-07-04 (owner UX revision, post-implementation).** The popup-driven
> flow below shipped and was then reworked for discoverability; the shipped shape is:
> the "Add Transition To >" popup is DELETED. Transitions create instantly with
> defaults (two-way Doorway, priority 0) through one shared flow
> (`document/TransitionConnect.h`: `ConnectZones`, which also auto-links a selected
> portal undoably and revalidates); the zone row and the hierarchy panel's portal
> entry both expose it as a flat "Connect To >" submenu (zones plus New Zone In
> <region>). The World panel header gains [+ Region] [+ Zone] [+ Portal] buttons;
> + Portal spawns a thin portal brush fitted over the selected face
> (`FitPortalBoxToFace`) as one undo step, selected. Selecting a single portal shows
> a connect bar between the tree and validation: unlinked portals get a one-click
> Connect with the target pre-guessed from the portal's facing
> (`GuessPortalTargetZone`); linked portals get Show and undoable Unlink. The
> transition ROW UI and its context menu (item 1 and 2 below) are unchanged and
> remain the editing surface after instant creation.

### What changes (all in `editor/kyusu/src/ui/WorldPartitionPanel.{h,cpp}`)

1. **Transition rows.** Each zone row grows child rows for its outgoing transitions
   (via `WorldDocument::Index().Outgoing`, already id-ordered): `-> <To zone name>`
   plus a topology badge (`D`/`S`/`T`) and a one-way arrow glyph when set. Rows for
   transitions with `portal_missing`/`unlinked` records show the validation
   severity icon inline.
2. **Transition row context menu:** Topology submenu (the three values), One-Way
   toggle, Preload Priority (inline int edit), Remove (with confirm; removes ONLY
   this edge, never its reverse), **Link Selected Portal** (enabled when the From
   zone is the focus zone and the selection is exactly one portal entity; an
   undoable `RawComponentEditCommand` writing the `Transition` field), **Select
   Portal** (enabled when linked and From is the focus zone).
3. **Zone row context menu** gains **"Add Transition To >"**: a popup with a target
   picker over ALL zone headers (open or not, manifest order) plus "New Zone In
   <region>..." (reusing the `AddZone` flow), topology (default Doorway), one-way
   checkbox, priority field, and a "link selected portal" checkbox pre-checked when
   the current selection is one portal brush in this zone. Confirm calls
   `AddTransition` (and the reverse `AddTransition` with swapped endpoints, same
   topology and priority, unless one-way), then the link as the one undoable step.
4. **`SceneHierarchyPanel`** entity context menu gains **"Create Transition From
   Portal"** on portal entities, opening the same popup with the link box checked.
5. `NavigateToRecord` handles Transition-kind records: opens the From zone's region,
   highlights the transition row.

### Gate T4

Suite green (verbs and rules carry the automated weight from T3). Manual, on the
three-zone fixture: create a Doorway pair Hub/Hallway from a selected portal brush;
the reverse edge appears with `portal_missing` until a portal is placed and linked
on the Hallway side; one-way creation mints a single edge; Remove removes one
direction; validation rows navigate.

---

## T5. Slice gate

The design doc Section 10 editor slice, restated under D9 and run end to end as the
phase's manual gate:

1. Three-zone world (Hub editable, Hallway context, Arena header-only).
2. Cut a doorway opening in Hub's wall with the existing carve and face tools.
3. Fit a thin box brush into the opening; add the portal component in the inspector;
   the brush renders translucent cyan.
4. Create a Doorway transition to Hallway from the portal (reverse edge minted and
   reporting `portal_missing`; Arena-targeted transitions report
   `portal_unverified` while Arena stays header-only).
5. Move an entity from Hub to Hallway and undo/redo it (E2).
6. Hand-edit Hub's scene JSON to break the portal link; reopen; validation names it
   and the record row navigates to the transition.
7. Cook: no portal geometry, collision, or entity in any artifact
   (`CookStripsPortalBrushes` automates this half).

## Definition of done (whole phase)

The overview Section 5 checklist per stage, plus:

- The T5 slice reproducible end to end.
- Grep audits: `grep -rn "PierceFaceRect" docs/plans/world-partition editor engine`
  returns only the D9 override record; `grep -rn "PortalComponent" engine/` empty;
  the W1 reference-member audit unchanged.
- No manifest field written outside `WorldDocument` verbs and `SaveWorld`
  (the E1 phase audit, re-run: the panel's transition UI goes through the T3 verbs
  only).
- Legacy mode untouched; no legacy test edited.
