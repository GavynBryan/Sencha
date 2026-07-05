# Phase: Retire Portals; Doors Are World Content

Status: execution spec (2026-07-05), NOT implemented. Owner review before any stage
starts. Reverses `00-execution-overview.md` D9, D15, D19, D20 and retires
`05-transitions-and-portals.md`. Read `00-execution-overview.md` and
`07-global-content.md` first.

## Why

The portal was an editor-only marker whose one job was to derive a zone connection
from where a designer placed a box (D19). That job is redundant: a connection is a
world-owned transition edge, and `ConnectZones` already mints it directly from a
zone-to-zone choice. Worse, the marker had to live in a zone, which forced an
arbitrary owner on a thing that spans two zones (the friction D20 was already
fighting). So the portal buys nothing the transition graph does not already carry,
and it costs an ownership decision. It goes.

Deleting it exposes the real problem it was standing in front of: a **door** (a mesh
with an animation and collision, between two zones) is genuine runtime content that
must be visible and collidable from **both** rooms. An entity lives in exactly one
registry, so "belongs to both zones" is not expressible as zone content: from the far
side of either room the owning zone can stream out and the door vanishes. The only
registry resident whenever either room is resident is `ZoneRuntime::Global()`, the
world-scene registry (07). So a door is **world-scene content bound to a transition**,
not zone content and not a portal.

This is not the portal coming back. The portal *authored a connection* (redundant,
editor-only, zone-owned). The transition binding *attaches real always-resident
content to a connection that was already authored* (not redundant, runtime, world-
owned). Connections are still authored explicitly zone-to-zone; nothing derives them
from geometry anymore.

## Standing decisions

- **P-D1. Portals are removed entirely.** No `PortalComponent`, no portal geometry,
  no portal validation rules, no portal rendering, no portal UI, no cook exclusion for
  them. Reverses D9, D15, D19, D20. Grep audit at phase end: `grep -rin portal editor
  engine test` returns only this doc's reversal record.
- **P-D2. Connections are authored zone-to-zone, never derived.** `ConnectZones`
  (mint forward Doorway edge + reverse unless one-way, revalidate) stays the one flow.
  It loses its portal parameter and its link step. The panel's "Connect To >" submenu
  and D20's undirected one-row-per-pair display are unchanged and become the sole
  authoring surface. `ReconcilePortalConnections` is deleted; the zone-bounds
  recompute it also performed (WorldDocument.cpp 520-531) is extracted to
  `RefreshDerivedZoneBounds()` and still runs from save and `Revalidate`.
- **P-D3. Doors are out of scope here; the future direction is recorded, not built.**
  We have no doors yet. This phase only removes portals. The point of writing the
  direction down is to confirm removing portals does not box the engine in: when a door
  (a mesh between two zones, visible from both rooms) does arrive, it is world-scene
  content, not a portal and not zone content. It is authored in the world scene (07) and
  loaded once into `Global()`, which is resident whenever either room is, so it renders
  and collides from both sides. 07 already provides that substrate. See "Future" below.
  Nothing in this phase builds it.

---

## Part A: Retire portals (buildable now, no dependency on 07)

One stage per commit, full suite green and `scripts/check_editor_layering.sh` green
between stages. Order removes leaf consumers first so the core deletion in A4 lands
against nothing.

### A0. Record the reversal

- This doc committed. `00-execution-overview.md`: strike D9, D15, D19, D20 (leave a
  one-line "reversed by 09-" stub at each so the numbering and history stay legible),
  and add a document-map row for this phase. `05-transitions-and-portals.md`: mark
  superseded by this doc at the top; do not delete it (it is the record of what E3
  shipped).
- No code. Suite green trivially.

### A1. Authoring UX: drop portal creation and linking

- `editor/kyusu/src/ui/WorldPartitionPanel.{h,cpp}`: remove the `[+ Portal]` header
  button and its `FitPortalBoxToFace` / `MakeCreatePortalBrushCommand` path, the
  portal connect bar (Show / Unlink / the "place a portal across a boundary" hints),
  the `PendingConnectPortal_` member, and the "Select Portal" / "Link Selected Portal"
  context-menu items. Keep the zone-row "Connect To >" submenu and the transition rows.
- `editor/kyusu/src/ui/InspectorPanel.{h,cpp}`: remove `DrawPortalSection` and its call
  in `DrawWorldModeSections` (or equivalent).
- `editor/kyusu/src/document/TransitionConnect.{h,cpp}`: `ConnectZones` drops the
  `EntityId portal` and `CommandStack&` parameters and the link block; signature
  becomes `ConnectZones(WorldDocument&, ZoneId from, ZoneId to, bool oneWay)`.
- `test/editor/TransitionConnectTests.cpp`: drop the portal-link assertions; keep the
  edge-minting and reverse-pairing cases.
- Gate: suite green. The portal component still compiles; nothing here needs it gone.

### A2. Validation and reconcile

- `editor/kyusu/src/document/WorldDocument.{h,cpp}`: extract the bounds recompute
  (520-531) into `void RefreshDerivedZoneBounds();`. `Revalidate` becomes
  `RefreshDerivedZoneBounds(); RunValidation();`. Confirm the save path recomputes
  bounds on its own; if it relied on `ReconcilePortalConnections`, call
  `RefreshDerivedZoneBounds()` there too. Delete `ReconcilePortalConnections` and both
  its call sites' portal work.
- `RunValidation`: remove `partition.transition.portal_missing`,
  `portal_duplicate`, `portal_unverified`, `portal_misaligned`,
  `partition.portal.unlinked`, `partition.portal.wrong_zone`,
  `partition.portal.brush_missing`. Keep `partition.transition.unpaired` and every
  non-portal rule.
- `test/editor/TransitionValidationTests.cpp`: delete the portal-rule tests; keep
  `AddTransitionMintsAndReindexes`, `RemoveTransitionDropsRecordAndRevalidates`,
  `SettersRewriteAndRevalidate`.
- Gate: suite green.

### A3. Rendering

- `editor/common/src/EditorTheme.h`: remove `PortalFill` and `PortalWire`.
- `editor/kyusu/src/render/BrushSolidRenderer.{h,cpp}`: remove the `IsPortal` flat-fill
  branch and its comment.
- `editor/kyusu/src/render/WireframeRenderer.{h,cpp}`: remove the `portalColor`
  parameter and the `IsPortal` branch; callers pass one color.
- `editor/kyusu/src/render/BrushFillRenderer.{h,cpp}`: remove `DrawPortalVolumes` and
  the portal lines in the wash pass.
- `editor/kyusu/src/render/EditorRenderFeature.cpp`: remove the `DrawPortalVolumes`
  calls and `dimmedPortalWire`.
- `editor/kyusu/src/render/ZoneBoundsRenderer.cpp`: remove the `PortalWire` segment use.
- `editor/kyusu/shaders/editor_solid.frag.glsl`: drop the portal line from the comment.
- Gate: suite green; editor still renders brushes and wireframe.

### A4. Component, serialization, commands, geometry, cook (the core)

- `editor/kyusu/src/document/EditorScene.{h,cpp}`: remove `PortalComponent`, its
  `TypeSchema`, `IsPortal`, `TryGetPortal`.
- `editor/kyusu/src/document/DocumentSerialization.cpp`: remove the `TransitionId`
  `SceneFieldCodec`, the `ComponentStorageTraits<PortalComponent>`, and the
  `RegisterComponent<PortalComponent>()` line.
- `editor/kyusu/src/document/EditorDocument.cpp`: remove
  `world.RegisterComponent<PortalComponent>()`.
- Delete `editor/kyusu/src/document/commands/LinkPortalCommand.{h,cpp}` and
  `MakeCreatePortalBrushCommand` in `commands/CreateEntityCommand.h`.
- Delete `editor/kyusu/src/document/PortalGeometry.{h,cpp}`.
- `editor/kyusu/src/document/BrushCookInput.cpp` and `DocumentCook.cpp`: remove the
  `IsPortal` skip and the portal-component strip (nothing to exclude now).
- Delete `test/editor/PortalTests.cpp`; drop `CookStripsPortalBrushes` from
  `test/level_cook/WorldCookTests.cpp`; update `test/CMakeLists.txt`.
- Update CMake source lists for the deleted `.cpp` files.
- Gate: suite green; `scripts/check_editor_layering.sh` green.

### A5. Engine-side comments and the `Unverified` severity

- `engine/include/zone/WorldPartitionManifest.h`: the `Doorway` enum comment drops
  "expects a portal entity in From's content" (an opening realized by world-scene
  content now); the "No portal reference" comment on `TransitionRecord` states the
  binding direction plainly.
- `engine/include/zone/ContentRiskRecord.h`: remove `ContentRiskSeverity::Unverified`.
  Its only consumer was `portal_unverified`; with that gone it is a dead value, and
  D6's coordination note said to defer to Track C's vocabulary if it lands. (Owner
  confirm: keep it only if a non-portal consumer is imminent.)
- Gate: `grep -rin portal editor engine test` returns only A0's reversal record; suite
  green.

### Part A definition of done

- The grep audit above is clean. Suite green, layering script green.
- A three-zone world still authors a two-way Doorway from the "Connect To >" submenu,
  the pair shows as one undirected row (D20), one-way still mints a single edge, Remove
  removes one direction, and validation rows still navigate.
- No dead field, no commented-out code, no em dashes in any touched file or commit.

---

## Future: doors (not now, just checking the road stays open)

We have no doors, and this phase builds none. This note exists only to confirm the
removal does not paint the engine into a corner later.

A door is a mesh between two zones that must be visible and collidable from both rooms.
An entity lives in exactly one registry, so it cannot belong to both zones; from the far
side of either room the owning zone can stream out and the door would vanish. The
registry that is resident whenever either room is resident is `ZoneRuntime::Global()`,
the world-scene registry. So a door, when it comes, is world-scene content: a mesh
authored in the world scene (07), loaded once into `Global()`, referencing the
transition it realizes. That is the world-owned home the portal ownership question kept
pointing at, and 07 already builds the substrate for it (the focusable world scene and
the synchronous `Global()` load). Nothing about deleting portals blocks that path.

Two things to check when doors are actually built (not now):

- **Collision across registries.** The pawn already lives in `Global()` and moves
  through streamed zone geometry, so a cross-registry path exists; confirm it
  generalizes to a `Global()`-resident door collider before trusting door collision.
- **Always-resident budget.** A `Global()` door is resident for the world's lifetime.
  Fine at low door counts; a bounded per-transition boundary registry (07's recorded
  next mechanism) is the answer if it ever costs memory or the physics step.

## Non-goals

- Any door work at all: no component, no world-scene binding, no runtime code. Recorded
  direction only.
- Reintroducing any geometry-derived connection authoring (that is the portal; it is
  gone, P-D2).
