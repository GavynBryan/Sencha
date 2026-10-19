# Data-driven triggers and Kyusu authoring

Status: implementation in progress. The complete authoring workflow is not
delivered. The sensor foundation is merged in #176; generic movement remains in #177.
The continuation implements compact Trigger components, inline program ownership,
shared typed Verb/Query argument binding, authoritative execution and scene
serialization. Trigger brush cooking, inspector and authored bumper demonstration
remain required subsequent work.

## Audit against HEAD

The original audit began at `b2bbae51760b912be1ac19143f3b6a4c62719b92`.
The continuation starts from merged sensor foundation
`0f86be71031125a5a9697d218766f1818e5f321a`. `editor/ui/.cooked/index.json` was already modified and
is outside this implementation.

| Existing mechanism | Verified contract and integration consequence |
| --- | --- |
| `VerbBindingCompiler`, `VerbBindingSet`, `VerbDispatcher` | Typed constants, persistent entity references and positional dynamic inputs compile once. Sets refresh on asset, catalog and tag revisions. Trigger commands must use these mechanisms. |
| `VerbRelaySystem` | Bounded, detached fixed-logic batches validate entity/logic-partition membership and stamp producer, instigator and tick. A relay invokes one asset binding; it does not own inline programs, conditions or cooldown. |
| Authored Queries and Events | Queries use revision-bearing handles and validate read-only answers. Events buffer occurrences, enforce budgets and quarantine runaway roots. Candidate evaluation shares the literal compiler for query constants but supports only one contextual entity argument. |
| `Collider.IsTrigger` | Becomes a Jolt sensor and the Trigger collision layer. The initial physics facade exposed no gameplay overlap lifecycle. Collider has no complete authored-scene schema for volume-derived sensors. |
| `CharacterMover` | Retains `CharacterVirtual` without an inner body or broadphase character presence. Rigid-body contact callbacks alone cannot cover it. The new implementation uses its cached shape against Jolt's sensor layer. |
| Brush volume role | `EditorScene::RoleOf` derives Volume from `BrushVolume`; evaluated pieces feed existing tools and retained drawing. `CollectCookBrushes` admits only Solid pieces, preserving unrelated semantic volumes. |
| Inspector and creation | `BrushModifierEditorAdapter` provides undoable ordered rows and actions through the property tree. `EditorWorkspace` installs component adapters and creation recipes; the normal Brush tool owns placement transactions. |
| Frame phases | Fixed logic precedes buffered Event drain and physics. Body simulation precedes character movement. Sensor observations now publish after characters. Gameplay must consume them at the next authoritative fixed-logic phase before motion composition. |
| Scene cook | `DocumentCookSnapshot`, passthrough scene collection and `CookedSceneAssembly` lead to `.smap` component value trees with schema fingerprints. Runtime imports those through existing component serializers. `ZoneCollisionLoader` creates solid collision-cell bodies and cannot substitute for authored sensor entities. |

## Current review units

1. [Sensor lifecycle, PR #176](https://github.com/GavynBryan/Sencha/pull/176):
   `SensorOverlaps`, cached Jolt shape queries, physics-phase
   publication, residency/destruction cleanup and sensor transform updates.
   Tests: `SensorOverlapTests.cpp`, `SensorOverlapAllocationTests.cpp`.
   Contract and reproducible benchmark: `docs/physics/sensor-overlaps.md`.
2. [Generic movement, PR #177](https://github.com/GavynBryan/Sencha/pull/177):
   `movement.apply_velocity`, ordered Add/Replace through
   composition, authority checks and dilated substep consumption. This unit
   is independent of trigger execution and can be reviewed before it.
   Tests: `MovementVelocityTests.cpp`. The movement and authored API docs
   describe its semantics, prediction limit and ABI consequences.
3. [Build prerequisite, PR #174](https://github.com/GavynBryan/Sencha/pull/174):
   the starting `GameplayTagContainerWireTests.cpp` lacked
   the codec include, used a mutable getter through a const World and omitted
   an aggregate initializer. Its compilation repair preserves all assertions.
4. [Runtime fixture prerequisite, PR #175](https://github.com/GavynBryan/Sencha/pull/175):
   runtime prefab tests could load stale FPS cooked content before its template
   fixture ran. Reuse that existing cook fixture before runtime tests.

PRs #174, #175 and #176 are merged. #177 remains independent of the trigger
runtime. Continuation work lives in the persistent isolated checkout
`build-trigger-source` on `codex/trigger-program-runtime`; the original dirty
workspace is preserved. Previous checks against other source revisions are not
substitutes for testing this checkout.

The user approved the shared archive value operation on 2026-10-19 after its SDK
ABI impact was identified. JSON archives preserve arbitrary value subtrees;
legacy binary archives use their existing JSON and length-prefixed string
facilities. Existing scalar fields and `.smap` value-tree encoding are unchanged.
The new Trigger component uses its own `TRGR` chunk and version-1 inline program.
Module fingerprint inputs now explicitly include archive headers.

## Remaining required work

- Publish and review the verified runtime unit. The full dev build and serial
  CTest passed (6,148 enabled entries, zero failures); inline program save/load,
  actual `.smap` package import and module boundary checks are covered.
- Cook validation and dependency preloading for scene-inline programs.
- Brush-derived oriented sensor geometry, supported-shape diagnostics and
  `.smap` identity/reference preservation, excluded from solid/nav/render cooks.
- Brush-first creation and safe conversion using existing transactions,
  persistent identity, geometry manipulation, duplication and undo/redo.
- Orange translucent faces, bright edges and repeating TRIGGER lettering in
  `VolumeRenderer`, with normal/hide/X-Ray and practical pick filtering.
- Activation/condition/ordered-command inspector adapters with schema-driven
  controls, contextual entity sources, unknown-data preservation and undo/redo.
- Generic directional metadata, trigger-relative sources and undoable viewport
  direction editing synchronized with inspector values.
- Authored Loss Function bumper, save/cook/load/play/edit reproduction,
  automated end-to-end authoring coverage and representative performance data.
  The existing project is at `/home/gavyn/projects/loss-function-sencha`,
  including `assets/levels/test.sscene`; its integration is not yet modified.

Continuous nonlinear sensor trajectories remain a recorded limitation in
`docs/deferred.md`; supported linear crossings have physics regression tests.
