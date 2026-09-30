# Deferred work

Work deliberately left for later. Each entry names what was deferred, where the
code lives, and the concrete trigger that makes it due. When a trigger lands,
do the work and delete the entry.

## Animation decision log depth

- **What:** the runtime plan (`docs/plans/animation-runtime.md`, decision
  history) asks for a global cvar setting the decision log's ring depth.
- **Where:** `AnimDecisionLog` in `engine/include/anim/AnimDecisionLog.h` is a
  component with a fixed 64-record ring (`kAnimDecisionLogCapacity`). A
  runtime depth needs the records out of relocatable component storage, into a
  registry-owned ring addressed from the component.
- **Trigger:** a trace exported with `anim.trace.export` that reports
  overwritten records the investigation needed, or a consumer that wants a log
  on many entities at once and cannot afford 64 records each.

## Default decision log on character entities in dev builds

- **What:** the runtime plan says dev builds keep a short decision log on every
  Character-tier entity. Today a log is added by content, by the editor
  preview, or by `anim.trace <entity>`.
- **Where:** tiers are presets in the animation editor
  (`editor/animation_editor/src/authoring/AnimationRigRecipe.h`), not a runtime
  property, so the runtime has nothing to key the default on.
- **Trigger:** the rig or scene format gaining a declared per-entity diagnostic
  setting, or the decision-log depth work above, which settles how a short ring
  is stored.

## Engine-wide participation LOD tiers

- **What:** the animation runtime plan places its server participation tiers
  beside "the existing participation LOD tiers", which are proposed but not
  built. Animation's server skip is its own rule today: a machine that does not
  present poses skips a rig whose binding proves it drives no gameplay
  (`AnimBoundRig::DrivesGameplay`, `ShouldRunAnimationLogic`).
- **Where:** the proposal is `docs/action-adventure-core-runtime.md` §C and
  `docs/plans/engine-roadmap.md`; the animation rule is in
  `engine/include/anim/AnimRigBinding.h` and `engine/src/anim/AnimRigBinding.cpp`.
- **Trigger:** the first engine participation tier landing. Animation's skip
  then becomes a consumer of that tier rather than its own predicate.

## Animation editor surfaces

- **What:** purpose-built surfaces the animation editor still lacks. Every field
  is editable today through the Document panel's schema form. Missing:
  tag-set scenario inputs and the predicate text form; a behavior policy
  inspector linked to the rules and rows that reach it; slot-map precedence, a
  blendspace layout editor and fact derivations; lifecycle bindings in their
  own inspector; event marks on the shared timeline; the take-A ghost drawn
  where the character stood and a composed-motion view.
- **Where:** `editor/animation_editor/src/ui/`.
- **Trigger:** the animation editor UX polish pass that follows the editor
  consolidation.

## Layer weight fades

- **What:** a layer's weight steps when its weight rule changes; it does not
  fade over time.
- **Where:** layer weights resolve in `engine/src/anim/AnimContentSystem.cpp`
  and are applied by the pose pass.
- **Trigger:** the first rig whose upper layer visibly pops in or out, or the
  UX polish pass.

## Picking up another program's edits before save

- **What:** an open document does not notice its file changing on disk until
  it is saved, when the conflict is refused and settled by keeping one side.
- **Where:** the editors' document sets in `editor/common/src/data/` and
  `editor/animation_editor/src/authoring/`.
- **Trigger:** file watching in the consolidated editor.

## Other editors' documents on the shared document layer

- **What:** Shudei's `MaterialTabSet` and Kyusu's document handling keep their
  own open, save and undo instead of the shared document layer the Data Editor
  and the animation editor use.
- **Where:** `editor/material_editor/src/MaterialTabSet.*`, `editor/level_editor/src/document/`.
- **Trigger:** the editor consolidation ticket, which gives the one application
  one undo journal and one save-all.

## Sanitizers and benchmarks in CI

- **What:** the `asan` and `tsan` presets and the gated benchmarks run only by
  hand; CI runs neither.
- **Where:** `.github/workflows/ci.yml`, `CMakePresets.json`,
  `scripts/bench_animation.sh` (`test/runtime/AnimBench.cpp`,
  `test/editor/AnimationPreviewBench.cpp`) and the other gated benches.
- **Trigger:** a performance or memory-safety regression gate wanted in CI.

## Resident push against file-watcher reload

- **What:** the editors push a committed working version into the resident
  asset; nothing stops a file-watcher reload replacing it with the saved file.
- **Where:** `editor/common/src/data/DataResidentSync.*`.
- **Trigger:** a host that runs source hot reload beside a document set, which
  the consolidated editor will.

## Play-in-editor and unsaved values

- **What:** because working versions are pushed into resident assets, a
  play-in-editor session started from the same process would see unsaved
  values. Whether it should is a product decision.
- **Where:** `editor/common/src/data/DataResidentSync.*`.
- **Trigger:** the consolidated editor wiring play-in-editor to the shared
  document set.

## Unsaved-document prompt polish

- **What:** closing, renaming or deleting an unsaved document asks Save,
  Discard or Cancel one document at a time with plain wording, and window focus
  loss still cancels typed text that has not been committed.
- **Where:** `editor/common/src/ui/DocumentShellActions.*`.
- **Trigger:** the UX polish pass.

## Destroying a document source that still has changes

- **What:** the plan for the shared document layer makes destroying a document
  source with unsaved documents a debug assertion, reachable only through the
  exit prompt. Today "Discard and close" and tests destroy sets with changes,
  so the assertion would need an explicit discard-everything step first.
- **Where:** `editor/common/src/data/DataDocumentSet.cpp`,
  `editor/animation_editor/src/authoring/AnimationClipEventsSet.cpp`,
  `editor/common/src/ui/DocumentShellActions.cpp`.
- **Trigger:** the consolidated editor's single shutdown path, which can
  discard every source's documents before tearing them down.

## Gameplay events on behaviors reached without a request

- **What:** the plan's invariant 12 forbids Gameplay-scope events (clip marks or
  lifecycle bindings) on behaviors reachable without a request. Binding does not
  enforce that half: gameplay events are produced only by the authority, and the
  remediation's requirements include gameplay events surviving a carry, which
  happens between fact-selected cyclic behaviors. Blendspace samples are required
  to share one gameplay track.
- **Where:** `AnimSelectorBinding.cpp` (`Validate`, beside the Reconstruct and
  root-motion pairings) and `docs/plans/animation-runtime.md`, invariant 12.
- **Trigger:** the owner's ruling on whether fact-selected behaviors may carry
  gameplay events.

## Animation performance pass

- **What:** the review's Phase D, left out of the correctness ticket: validating
  each rig's binding once per tick through a bindings epoch rather than once per
  run of equal handles in each system (`AnimRigRunCache` is where the epoch
  lands); an optional-column accessor on `Query` so systems stop reading
  same-chunk components through `TryGet`; rig-sized per-layer state in place of
  eight-layer arrays; selector gate digests over only the slots selectors read;
  per-behavior row ranges; skipping layers that contribute nothing to a pose; a
  release-mode check on archetype rows wider than a chunk; evicting
  `AnimRigBindings` entries for rigs no longer resident (a released rig's key is
  never looked up again, so this is memory, not correctness); World resource
  lookups hashing a type name, which a single-entity World such as the editor
  preview pays per system per tick; and a logic-tick bench for Simple and
  Character entities, which comes first.
- **Where:** `docs/plans/animation-runtime-review.md`, findings P1-P6 and P8,
  and "Remediation plan", phase D.
- **Trigger:** the performance and cleanup ticket that follows, and before any
  scene with hundreds of rigged entities ships.
