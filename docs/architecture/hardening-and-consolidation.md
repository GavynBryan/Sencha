# Hardening & Consolidation Plan

Status: standing plan (living document). Companion to
`docs/plans/sencha-level-editor/09-module-abi-hardening.md` (the deep dive for
the module-ABI zone).

## Why this exists

Sencha's internal architecture is disciplined — the mesh-edit dependency ctest,
`BrushValidateAndRepair` before every commit, `TryGet`/`optional` defaults,
module-stable `ComponentTypeId`s. The goal here is not a rewrite. It is to close
the specific seams where that discipline lapses, so the codebase fails *less
often* and, when it does, fails *loudly and locally* instead of silently.

## Guiding principle

"Infallible" is not literally reachable, and chasing it produces gold-plating.
The reachable target is:

> **Illegal states unrepresentable; mistakes caught mechanically; the fragile
> surface small and tested; failures loud and local.**

Every item below is justified by that bar, not by perfectionism.

## The three fragility zones (diagnostic)

Observed empirically: the bugs hit while building the select-tool spike and the
camera-visual feature clustered in exactly three places. That is the priority
map — closing these eliminates the *families* of failure actually seen.

1. **The unguarded native-module ABI.** A virtual added to `IComponentSerializer`
   without an ABI bump shipped a host/module vtable skew → segfault inside the
   module, far from the cause, no diagnostic.
2. **GUI-welded math with no headless tests.** Backwards gizmo drag,
   relative-vs-absolute snapping, and "move only the last selection" all lived in
   code unit tests cannot reach because it is bound to `ImVec2`/`EditorViewport`/
   Vulkan.
3. **Event plumbing that silently drops data.** The viewport dispatcher
   decomposed input events into `(point, delta)` and dropped modifiers, so
   Shift/Ctrl selection misbehaved.

## Workstreams

Ordered by leverage. Each notes the existing system to consolidate with, so we
reuse rather than reinvent.

### W1 — Make the module ABI legible, then narrow it  (zone 1)

Consolidate with what already exists: module-stable `ComponentTypeId`, the
registration identity tuple (anti-aliasing), `-fvisibility=hidden`, the
`SENCHA_GAME_ABI_VERSION` handshake, and the cook pipeline's content-addressing
(reuse its hash primitive — do not add a parallel one). The vtable exposure
(modules instantiating `ComponentSerializer<T>`) is the lone inconsistency in an
otherwise careful cross-module posture.

Full tiers in `09-module-abi-hardening.md`. Summary:
- **Tier 0 (now):** validate via an `extern "C"` token before any virtual call;
  widen it to a build-identity record (ABI version + compiler + C++ stdlib +
  build type + pointer size + sanitizers).
- **Tier 1 (now):** derive the compatibility token as a build-time *fingerprint*
  of the ABI headers so skew is detected mechanically, not by remembering to bump
  an integer. Bias toward over-refusing (a false positive is a harmless rebuild).
- **Tier 2 (when investing):** modules register components via POD descriptors +
  C-ABI callbacks; the engine owns all serializer vtables. Then changing
  `IComponentSerializer` can never break a shipped module. Tool-only hints
  (`EditorVisual`) ride the descriptor as data, not a virtual.

### W2 — Decouple viewport/picking/gizmo math from the GUI; unit-test it  (zone 2)

Highest single leverage. `ViewportProjection`, `PickingService`, and the
translate-manipulator drag math are welded to `ImVec2` + `EditorViewport`, so the
exact code where three bugs occurred has zero headless coverage.

- Make the math take plain types (a camera/matrix + `Vec2`/rect), not `ImVec2`/
  `EditorViewport`. Keep the GUI types at the call sites only.
- Add headless tests for: closest-point-on-axis (sign), absolute grid snap,
  `PickInRect` gather per element mode, world↔pixel round-trip. These bugs become
  red tests instead of drive-test surprises.

### W3 — Route whole input events; never decompose-and-drop  (zone 3)

The modifier bug existed because the dispatcher unpacked events and lost a field.
Pass the event (or a complete pointer-event struct) through `ITool`/`IManipulator`
so no field can be silently lost. Make dropping input data structurally
impossible rather than relying on threading each field by hand.

### W4 — Guarantee interaction termination; unify the live-edit transaction

Preview mutates live scene state and relies on a terminal commit-or-revert.
- Ensure `OnCancel` (revert) fires on *any* interruption — focus loss, tool
  switch, escape, exception — so an interrupted drag can never strand
  uncommitted live geometry.
- Consolidate the begin → preview → commit/cancel pattern (the manipulation sink,
  inspector field edits, and manipulators each reimplement it) into one reusable
  "live edit transaction."

### W5 — Consolidate duplicated subsystems

- **Editor line rendering → one overlay batch.** `WireframeRenderer`,
  `SelectionRenderer`, and `ComponentVisualRenderer` each carry an identical
  `LineVertex` + line pipeline + setup/draw/teardown. Collapse to one
  `EditorLineBatch` that everyone contributes to (repeat the win
  `ViewportProjection` already delivered by collapsing three `BuildRay` copies).
- **Component visuals → the real asset pipeline.** Replace the runtime-glTF +
  `SENCHA_EDITOR_ASSET_DIR` shortcut in `ComponentVisualRenderer` with
  `StaticMeshCache::Acquire("asset://…")` (cooked `.smesh`, hot-reloadable via
  `AssetHotReloader`). Deletes code and removes a second, divergent mesh-load
  path in the editor.
- **Merge the redundant edit backends.** `ManipulationSink` vs `IMeshEditTarget`
  both resolve a mesh and mint mesh-edit commands; `BrushManipulationSink` vs
  `BrushEditTarget` are two classes holding `Scene&`/`Document&` doing nearly the
  same thing. Collapse into one brush edit backend before they drift.

### W6 — Grow the fitness-function suite

The `meshedit_dependency_directions` ctest is the seed of the right instinct.
Add siblings so invariants are enforced mechanically, not by review:
- the W1 ABI fingerprint check,
- a check that tool-only concerns never appear on module-ABI headers,
- golden `sizeof`/`offsetof` tables for boundary types,
- existing layering rules generalized.

## Footgun register (lower priority, real)

- **`Vec3d` is float-backed** (`Vec3d = Vec<3>` defaults to `float`; the double
  alias is `Vec3dd`). A name that contradicts its semantics is a correctness
  hazard. High ripple to rename; track it, fix opportunistically, and never let
  the lie deepen.
- **`ToolContext` is trending toward a god-object** (9 references, +1 per feature
  added this spike). Watch before every tool is coupled to every service; group
  or split if it keeps growing.

## Sequencing

1. W1 Tier 0+1 — module ABI legible + automatic (closes zone 1; matches the
   "transparent about what's failing" requirement).
2. W2 — decouple + test the gizmo/picking math (closes zone 2; retroactively
   guards the bugs just fixed).
3. W3 — route whole events (closes zone 3).
4. W4 — guaranteed termination + live-edit transaction.
5. W5 — pure consolidations (line batch, asset-pipeline reuse, merge backends).
6. W6 — fitness functions, added alongside each workstream that creates an
   invariant worth guarding.

W2 may be reordered ahead of W1 if the select-tool spike (P3+) continues first,
since it directly de-risks that work.

## "Done enough"

Not "no bugs ever." The bar is met when: the module boundary refuses skew with a
clear message (never a crash); the gizmo/picking/snap math has headless tests;
input events cannot lose fields in transit; interactions always terminate; and
the duplicated renderers/backends are single-sourced. At that point the three
observed failure families are structurally closed and the remaining surface is
small enough to reason about.
