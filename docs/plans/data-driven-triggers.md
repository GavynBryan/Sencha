# Data-driven triggers and Kyusu authoring

Status: implementation in progress. The complete authoring workflow is not
delivered. The sensor foundation is merged in #176; generic movement remains in #177.
[Runtime PR #178](https://github.com/GavynBryan/Sencha/pull/178) implements
compact Trigger components, inline program ownership,
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

- Review runtime PR #178. Its full dev build and serial CTest passed
  (6,148 enabled entries, zero failures); inline program save/load, actual
  `.smap` package import and module boundary checks are covered. Linux CI stops
  during configuration on a missing LLVM-16 `libclangBasic.a`, before compiling.
- Complete the cook unit on `codex/trigger-volume-cook`. Its prerequisite now validates
  programs through the shared compiler before document collection, including
  eligibility and required data preloads through AssetSystem. Unknown disabled
  rows are retained. Geometry and program validation have focused tests; sensor
  generation and full save/cook/load coverage are still required.
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

## Collider integration boundary under review

The next source change needs explicit SDK/format approval under CLAUDE.md.
No collider layout or serializer change has been made yet.

A volume's mesh can be offset or rotated relative to its entity. Changing the
entity transform during cook would change trigger-relative directions, parent
relationships and child transforms. A separately editable collider or hidden
child entity would create another geometric authority. The general correction
is a shape pose relative to the body's origin:

- Add identity-default `BodyTransform ShapePose` to `Collider` and `BodyDesc`.
  Its translation and rotation place the primitive or cached shape in body space.
- Realize it through Jolt's existing rotated/translated shape decorator. Body
  transform, driven attachment frames and teleport APIs retain their origin
  semantics; the backend's center-of-mass transforms still drive queries.
- Give inline Collider primitives a scene schema and serializer. Do not persist
  process-local mesh cache handles. Unsupported mesh-backed authoring must report
  an error until its existing cooked asset ownership path can represent it.
- Derive dimensions and shape pose from evaluated brush geometry at cook time.
  Preserve the entity's identity, transform and authored axes. Reject non-boxes,
  multiple pieces and affine shear rather than approximating them.
- Preserve runtime-bearing brush entities and required hierarchy ancestors in
  passthrough projection; strip only editor geometry and metadata. Existing
  solid render/collision/navigation collection remains restricted to Solid.
- Test offset/rotation, parent movement, body-origin versus COM semantics,
  sensor endpoint occupancy, teleport and scene/package round trips.

`ExtractBrushBox` is the exact six-quad recognizer for this consumer. It verifies
all corners, face adjacency and closure, and handles local geometry edits,
rotation, scale and reflection. It has nine focused tests. Until the collider
change lands, it is a tested prerequisite, not completed sensor cooking.

The extension changes SDK layouts and adds a previously unavailable collider
scene contract. Modules must rebuild; archive and existing scene scalar
encodings need no migration. Collider configuration is still supplied before
initial binding. Live resident collider edits remain the generic reconciliation
work recorded in `docs/deferred.md`; a fresh play/cook binding honors the new
configuration without a trigger-specific recreation path.

Cook-prerequisite verification: the dev configure and full build passed with
warnings as errors. Serial CTest reported zero failures across 6,165 enabled
entries in 331.78 seconds (17 opt-in generators skipped; one additional test
disabled). The focused CTest selection passed all 62 entries, including required
fixtures, with its benchmark skipped. This does not establish the absent
brush-to-sensor, inspector, or authored game demonstration paths.

The remaining cook integration must also ensure disabled unknown invocation
asset strings do not create executable dependencies: authoring must retain the
rows, while runtime dependency publication must reflect executable commands.
The generic `.smap` reference collector currently scans all nested asset strings.
