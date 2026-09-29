# Sencha Animation Runtime and Authoring Environment

Status: implementation plan, 2026-09-22. The runtime it builds is fixed by
[animation-runtime.md](animation-runtime.md), which is the plan of record for
architecture; this document stages the runtime and the Animation Editor
workspace together. Each runtime milestone ships with the authoring and preview
capabilities needed to exercise it interactively. The editor's current state is
described in [editor/animation_editor/README.md](../../editor/animation_editor/README.md).

## 1. Deliverable

The editor's organizing concept is a rig under a reproducible scenario. Authors
can follow the complete decision chain without losing the rig preview, clock,
or scenario:

facts + requests → winning rule → behavior → resolved slot row → content →
event → authored binding

The runtime architecture is unchanged: flat selection with named memory,
constrained flows, component-based tiers, request-only animation replication,
and authored verbs as the sole animation-to-gameplay channel.

### Hosting decision

`animation_editor` is a thin application host over separate
`animation_authoring` and editor rendering/UI libraries. It opens a project and
rig directly and integrates with Kettle and asset "Open in Animation Editor"
actions. This is preferable to expanding `IDataSubtypeEditor` into a
multi-document application framework: that hook updates only the active
document's editor, while animation requires a persistent session spanning many
assets. Data Editor remains useful for generic editing and entry points into the
specialized workspace.

| Repository evidence | Reuse decision |
| --- | --- |
| `DataDocument` owns working data, revisions, validation, dirty tracking, and begin/preview/commit/cancel transactions. | Extracted into editor-common (done); Data Editor behavior and tests preserved. |
| `IDataSubtypeEditor` / `DataEditorServices` are active-document oriented. | The subtype editor contract, document tabs, journal, save report and resident sync moved to editor-common (done); both editors use them, and animation keeps its own session lifetime. |
| `UiPreviewSession` separates simulation/model ownership from panels and owns the single diagnostic drain. | Follow the pattern with a concrete `AnimationPreviewSession`; do not generalize Shoji's session into a universal preview interface. |
| Shoji's `VocabularyCatalog` loads module declarations into a metadata World without executable implementations. | Moved into editor-common (done); used for verb, binding, and vocabulary inspection. |
| Kyusu's `EditorRenderFeature` depends on level documents, brushes, manipulators, zone views. | Do not import it. The orbit/projection math shared by the material and animation previews is in editor-common (`OrbitCamera`); Kyusu's fly/ortho camera is a different mechanism and stays. |
| `MaterialPreviewRenderFeature` demonstrates an offscreen target using runtime passes. | Reuse the approach and `RenderTargetStore`, `ImGuiTargetPresenter`, `MeshForwardPass`, GPU skinning. |
| `EditorUiFeature`, `IEditorPanel`, `CommandStack`, `CompositeCommand`. | Reuse directly; multi-document transaction ownership is `DocumentSourceSet` in editor-common (done). |
| `SelectionService` addresses entities and mesh elements. | Animation selection is asset identity plus stable authored item key; entity selection is not overloaded. |
| `SourceReloadRoots` watches, cooks, and publishes through owner-thread reload. | Reuse for saved assets; add skeletal reload operations and dependency invalidation. |
| Generic asset/tag fields are largely text; skeleton joint names may be empty or duplicated. | Add typed catalog pickers and durable skeleton joint references; bone masks cannot depend on display names. |
| Process launching was POSIX-only. | Shared launcher supports Windows (done); direct command-line startup works. |

No generic editor-framework rewrite is included. Shared extractions must have
their existing consumer and the Animation Editor wired in the same stage.

## 2. The authoring workspace

### Default screen

Opening a rig restores its last workspace layout and preview scenario:

```text
Project / rig tabs     Mesh · Rig · Scenario     Play Pause Restart Step · Speed
─────────────────────────────────────────────────────────────────────────────
Rig & asset outline   │  3D rig viewport                  │ Selected item
                      │  orbit / pan / frame / overlays   │ inspector
Layers                │                                   │
Behaviors             ├───────────────────────────────────┤───────────────────
Selectors             │  Authoring workbench              │ Preview inputs
Slot maps / overlays  │  Rules | Slots | Flow | Layers     │ Facts | Requests
Content / bindings    │  Blends | Events | Root           │ Scenario controls
─────────────────────────────────────────────────────────────────────────────
Timeline: content, sections, requests, behavior changes, events, corrections
─────────────────────────────────────────────────────────────────────────────
Decision history / Problems / Event admissions / Changes
```

The bottom diagnostic drawer starts collapsed to a summary strip; the timeline
remains visible. Panels are dockable; Reset Animation Layout restores this
arrangement.

Three selections remain distinct: the **preview subject** (rig, mesh, skeleton,
scenario, session), the **authoring selection** (a rule, behavior, row,
section, layer, bone, event, or binding), and the **inspection time** (live, or
a point in captured history). Changing authoring selection never silently
replaces the preview subject or moves the clock. An explicit Audition content
action temporarily previews selected content.

### Cross-asset navigation

Every authored item carries a stable location: asset path, item key, and field
path. Runtime source maps refer back to these locations.

- Selecting a rule highlights its predicate results and exposes its behavior as
  a link; following it opens the policy inspector and the effective slot rows.
- Slot rows show origin asset/overlay, precedence, predicate outcome, resolved
  content, and whether that content is currently pinned.
- Opening content changes the workbench and timeline while preserving the rule
  / behavior context in a breadcrumb.
- Selecting an event opens its binding key and producer inputs; following the
  binding opens the `authored.bindings` record and target verb contract.
- Back/Forward, Used by, and Return to active winner navigate both directions.
- A diagnostic opens the exact authored item and field, including items inside
  delegated selectors or inherited overlays.
- Shared assets show their other users before editing. Create override in…
  writes an overlay contribution linked through the rig; it does not modify the
  base asset invisibly.
- Workspace-local reference updates are grouped undoable commands; references
  outside the loaded dependency closure are reported, not silently rewritten.

### Preview setup and transport

The viewport provides real materials, grid/floor, orbit/pan/zoom, frame
subject/selected bone, skeleton overlay, mask visualization, root path, and an
optional comparison ghost. Mesh selection checks skeleton/rig compatibility
before activation; incompatible skeletons produce a located error and no
implicit retargeting occurs.

Transport: play, pause, restart scenario, previous/next fixed tick; a display
frame grid distinct from simulation ticks; speed, loop range, timeline zoom,
numeric tick/seconds/normalized entry; jump to event, section boundary, request
action, winner change, or diagnostic; looping a test range by restoring its
scenario state. Speed changes how quickly fixed ticks are scheduled, never the
fixed timestep.

| Mode | Meaning |
| --- | --- |
| Simulate | Run the normal animation pipeline in an isolated preview World with scenario inputs. |
| Audition content | Sample a clip/blendspace/section at arbitrary content time using scratch pose output. Does not advance requests, latches, event cursors, or the suspended simulation. |
| Inspect history | Inspect a recorded tick. Reconstruction happens in a disposable replay session; the live session is unchanged. |

Returning to Live restores the suspended session. Continue from here creates a
new scenario branch through deterministic reset/replay; it never treats an
arbitrary sampled pose as valid runtime state.

### Facts, requests, and scenarios

**Facts.** Typed controls for bool, float, int, tag and counted tagset inputs;
search, pin, reset to scenario defaults, provider/locality metadata. Derived
facts are read-only and expand into derivation, inputs, history window and
warm-up status. Changes show evaluated rule results immediately; while paused, a
disposable next-tick evaluation previews the result and stepping commits time.
No silently registered misspelled gameplay tags: project vocabulary and
declared preview fixtures determine valid names.

**Requests.** An issue form derived from the request schema: source, layer
mask, lifetime, start tick, typed parameters. A live table with identity, age,
primary-record status, claims, terminal status, anchor. Cancel offers the
specified reasons; Supersede uses the runtime insertion semantics and reports
capacity failure. Anchors are read-only in normal simulation; a separate
snapshot-injection tool controls them for reconstruction tests. Preview source
entities have stable scenario identities and actual local generational entities.

**Scenarios.** Versioned editor-only sidecars: preview assets, fixed tick rate,
initial inputs, named participants, deterministic seed, and a sequence of timed
fact/request/correction actions. Ships idle, acceleration/sprint,
airborne/landing, reload, interruption, cancellation, late join, and correction
examples. Recording, naming, restarting, and running against another compatible
rig are supported. Scenario controls and camera never dirty shipping assets;
saving a scenario is explicit. Scenario files also drive automated preview
tests.

### Authoring surfaces

| Surface | Interaction |
| --- | --- |
| Selectors | Ordered rule table: priority, enter/stay, result, hold, cooldown, live outcome. Delegations expand as a source tree; a flattened execution view is available. "False operand", "lower priority", "cooldown", "blocked by latch" are distinct. |
| Predicates | Typed condition rows with nested All/Any/Not, fact/request pickers, only the closed ops. Optional text editing parses into the same representation. No rule-to-rule links or transition canvas. |
| Fact/request schemas | Typed slot/parameter tables, provider binding status, derivation editor, dependency display, temporal horizon, locality diagnostics. |
| Behaviors | Policy inspector (kind, latch, interruption, late join, sync group, root motion, event thresholds, blend) linked to every selecting rule and resolving row. |
| Slot maps | Behavior → predicate → content table; effective overlay stack and row origin; newly matching row vs content pinned at entry. Reordering is undoable and shows precedence. |
| Flows | Horizontal section strip: durations, progress, loop badge/count, forward branch annotations, a distinct cancel lane. Backward connections cannot be created. |
| Layers | Ordered list: selector, mask, mode, authored/current weight, winner, content, local time. Solo/mute are labeled preview overrides and never change authored weights. |
| Bone masks | Searchable skeleton tree plus viewport picking; include/exclude bones or descendants, invert, coverage preview. Masks remain bone sets. |
| Blendspaces | Sample layout, clip pickers, coordinate/fact bindings, draggable preview coordinates, weights, phase display, validation. |
| Blends | Inertialization/crossfade/snap, phase carry, pairwise overrides; A/B replay from identical state with pose ghosts and residuals. |
| Events | Timeline markers: add, drag, snap, multi-select, duplicate, delete, numeric time. Inspector picks an authored binding key and renders typed producer inputs against its compiled destinations. Cosmetic/Gameplay distinguished by label and shape as well as color; minWeight, effective weight, crossing tick, fired/skipped/suppressed, admission. |
| Root curves | Translation/yaw plots and 3D path; extracted displacement vs composed motion vs achieved collision-resolved movement. |

Flow-section and behavior lifecycle bindings are edited in their owning
inspectors with the same binding picker, scope, input validation, and
diagnostics.

### Decision history and validation

The debugger is available from the first selector milestone. A selected tick
shows active selector, winner, candidates and failing operands, fact snapshot,
requests/anchors, layer weights, resolved/pinned content, flow progress, blend
decision, and event admissions; selecting a record highlights the source and
timeline mark. Editor recordings retain inputs, checkpoints, and asset
generations for exact local replay. Imported runtime traces report what they
captured; unavailable pose history is reported, never invented.

Validation uses the same schema/compiler/cook validators as runtime assets,
with structured diagnostic codes and authored locations, over the complete
working dependency closure including unsaved documents. Problems appear beside
fields, rows, sections, and markers; safe fixes are explicit undoable commands;
unknown names and unsupported fields are preserved; compilation validity,
reference resolution, and executable verb availability are separate statuses;
invalid working edits remain editable while the preview keeps its last valid
generation and says so.

## 3. Ownership, APIs, reload, and compatibility

### Editor structure

- `AnimationWorkspace`: open documents, rig dependency index, authored
  selection, navigation history, dirty state, workspace command history.
- `AnimationPreviewSession`: isolated World(s), fixed clock, scenario runner,
  checkpoints, animation pipeline, pending verb drain, diagnostic/history
  capture.
- `AnimationPreviewRenderFeature`: offscreen rendering of extracted preview
  data.
- `AnimationAssetLocation`: stable cross-asset item/field identity used by
  selection, commands, diagnostics, and source maps.

Panels read these owners and issue commands; they do not compile assets,
advance simulation, or drain diagnostics. GUI-independent authoring/session
code lives in `animation_authoring`; rendering and panels link separately; no
editor code enters shipping runtime targets.

The workspace owns one content command stack across its document closure; a
command that creates a behavior, adds a slot row, and links a clip is one undo
step. Navigation and preview inputs use separate session history. Drags use
begin/preview/commit/cancel; Escape, focus loss, document closure, workspace
replacement, and shutdown terminate transactions; undo during a drag cancels
the pending edit first.

### Preview execution boundary

Preview uses the production compiler, selector, flow, timing, pose,
root-motion, and authored dispatch kernels, and runs them through a schedule
built by `RegisterAnimationSystems`, so its order is the game's. The rule
verdicts it shows come from the select system through an
`AnimSelectionExplanation` resource naming the previewed entity. The editor substitutes only the
gameplay input environment: scenario fact providers write preview-owned inputs;
scenario actions use the normal request APIs; metadata declarations load
without starting the game; each preview World sets its authority role; the
preview dispatcher has no application-shell or real-game implementations.

A binding with no implementation returns Unavailable. Authors can attach a
preview recorder to a declared contract; it receives a normal invocation and
returns a normal admission, visibly labeled as a recorder result. Deterministic
preview fixtures may implement gameplay responses through the same authored
API. No recorder result is presented as proof that the real game performed the
operation.

### Rendering and skeletal identity

Preview rendering uses the existing sampling, palette, compute-skinning, and
forward paths over extracted frame data, with a unique `RenderEntityKey::Scope`
per session (including A/B and authority/client previews).

Stable imported joint keys are added for persisted masks. The skeleton cooked
format carries them; v1 files still read, and an explicit import mapping is
required where legacy names cannot identify joints unambiguously. A mask is
never remapped silently by display name or current joint index.

### Unsaved iteration and hot reload

Unsaved edits compile from a workspace overlay of the original documents
through the same data compilation and cook kernels; there is no alternate
evaluator. Recompile only affected dependency closures keyed by document
revisions; publish complete valid generations at an owner-thread boundary;
discard stale asynchronous results; preserve camera, scenario, selection, and
compatible runtime state; remap rules, rows, sections, markers, and bones by
stable identity; otherwise reset the affected state with an `Anchored` decision
and an explanation. External changes to clean documents reload normally; dirty
conflicts offer compare/reload/keep. Save reports per-file success and reload
status; a multi-file save is not presented as atomic.

Saved source reload uses `SourceReloadRoots` and the existing asset publication
boundary, with the missing clip/skeleton/skinned dependency reload support added.
Authoritative multiplayer timing changes still require session reload/rejoin;
compatible cosmetic reload remains supported.

### Runtime decisions retained

- One request anchor, with cook-enforced shared flow timing across claiming
  layers and one authority writer.
- Terminal request metadata retained inside the eight-record set while a
  reconstructible cancel/finish tail remains.
- Gameplay-owned narrow request prediction journal; no animation/fact-history
  rewind.
- Small/large facts, optional history/flow/pose storage, selector-free Prop rigs.
- Actual footprint measurements replace the complete-Simple-tier "under 100
  bytes" claim.
- Bounded derivation history, positive request-path proofs, pinned-content
  reconstruction checks, authoritative root/event equivalence.
- Only request records replicate; binding/verb/tag runtime IDs never become
  portable asset identities.
- Worker event production uses bounded records and deterministic owner-thread
  dispatch.

## 4. Paired implementation stages

Each stage has a runtime track and an authoring track. A stage is incomplete
until its interactive gate passes.

**Stage 0 — Workspace and real 3D preview foundation.** Characterize clip
playback, sampling, skinning, document transactions, preview rendering;
establish shared document/vocabulary/camera extractions. Editor: thin host,
persistent rig session, docking, typed mesh/clip pickers, preview leases,
offscreen skinning, transport, content audition, launch actions. Gates: open a
skinned asset, play/pause/step/scrub, change mesh, orbit without a game;
auditioning leaves suspended playback unchanged; resize/close/reopen/device
rebuild/asset replacement keep resource lifetime correct; Data Editor and Shoji
tests still pass.

**Stage 1 — Assets, facts, requests, and scenarios.** Versioned animation data
schemas; World-local compilation; components and pooled storage; request
lifecycle APIs; bound/derived facts; stable source identities; structured
validation; skeletal joint identity and clip event/root metadata formats.
Editor: rig setup, dependency outline, schema editors, fact controls, request
console, scenario record/save/replay, initial Problems and Changes. Gates:
changing Speed/Grounded/tags shows the preview snapshot and derived history;
issue/cancel/supersede with schema-derived parameters; capacity rejection,
primary-record choice, terminal retention visible; a saved scenario reproduces
the same tick sequence; preview inputs never alter shipping documents.

**Stage 2 — Prop/Simple selectors, behaviors, and slot resolution.** Closed
bytecode, delegation/source maps, stay/hold/cooldown, latches and delayed
feedback, behavior policies, request-keyed Prop path, slot overlays, content
pinning, timer-aware selector wakeups. Editor: rule table, typed predicate
builder, behavior inspector, effective slot map, linked navigation,
current/recorded decision debugger. Gates: Speed changes the winning rule and
resolved clip without a game; why a rule lost is explainable without source;
enter/stay, cooldown, latch blocking show distinct reasons; rule → behavior →
slot → clip navigation preserves the session; a Prop fixture runs without
facts, selector state, or Character pose storage.

**Stage 3 — Event tracks and authored verb dispatch.** `.sanim` event tracks;
`VerbBindingSet` dependencies/refresh; four typed inputs; fixed-size pending
records; deterministic owner-thread drain; authority/provenance; crossing,
skipping, weight suppression, admission logs. Editor: event timeline, binding
search/create/navigation, typed input mapping, contract inspection, lifecycle
bindings, preview recorder, admission timeline. Gates: place a marker, choose a
binding, supply inputs, play through it, see the normal admission; one input
feeding several arguments validates against every destination; scope/weight
demonstrate authority and suppression; scrubbing never dispatches into the
suspended session; binding reload/removal/reordering updates without stale
pointers or raw VerbId authoring; a marker drag is one undo step and cancel
restores it exactly.

**Stage 4 — Flows, layers, and bone masks.** Forward-only flows, section
pinning, loop/cancel, lifecycle events, anchors, terminal tails, layer weights,
masks, override/additive composition. Editor: section strip with branches and
cancel lane, layer stack, skeleton tree/picking, subtree mask operations,
coverage. Gates: Reload issued, sought, cancelled, with cancel section and
lifecycle admissions; backward branches cannot be authored; upper-body mask
selected visually while locomotion continues; solo/mute preview-only; all six
architecture examples editable and runnable; multi-layer flows with
incompatible single-anchor timing fail validation.

**Stage 5 — Blendspaces, blend policies, and correction visualization.**
Phase-locked blendspaces, inertialization, crossfade residue, phase carry,
pairwise overrides, post-movement pose evaluation, render extraction migration.
Editor: blendspace layout, coordinate controls, A/B blend recorder, ghosts,
residuals, override counts, history pose inspection. Gates: change
inertialization duration and replay; A/B uses identical inputs, clock, seed,
start pose; stacked changes, phase carry, per-layer isolation visible and
tested; overrides warn at the specified count; serial and parallel pose paths
are equivalent.

**Stage 6 — Replication, reconstruction, and prediction laboratory.** Bounded
request codec; stable source/tag wire translation; fact snapshot interpolation;
authority clock alignment; narrow gameplay request journal/reconciliation;
authoritative content compatibility identity. Editor: late-join snapshot panel,
paired authority/client sessions, latency/loss, correction injection, anchor
inspection, before/after pose comparison. Gates: late joiner reconstructs
sections and skipped marks; Count/While loops, fact branches, terminal tails
reconstruct; correction without history rewind; clients never originate
Gameplay events; laboratory scenarios pass headless; role and synthetic-input
status always visible.

**Stage 7 — Root curves and movement preview.** Root extraction/pose stripping;
root motion through `MotionComposition`; displacement/yaw integration; shared
live/replay kernels. Editor: curves, root path, floor/wall fixtures,
requested-vs-achieved path, collision markers. Gates: a request-driven move
collides against a preview wall; sampling never moves a capsule; stripped pose
does not double-apply; Timing and Full agree on displacement and Gameplay
timing; prediction/replay converges through cancellation and correction.

**Stage 8 — Compatibility cutover and qualification.** Import legacy
`AnimationClipPlayer` into a one-layer minimal setup preserving time, rate,
reverse/paused, loop/clamp; remove competing advancement/extraction paths;
budgets, risk counters, trace export. Editor: migration report, presets, batch
scenario runner, trace import, full cross-asset save/conflict handling,
documentation. Gates: existing playback/render fixtures preserved; Simple enemy
and layered third-person rigs authorable without hand-editing `.sdata`; the
third-person exercise covers locomotion, upper-body reload, mask, cancellation,
lifecycle binding, clip event, blend override, late join, correction; shared
selector + overlay edit, validation fix, save, reload, multi-asset undo without
losing context; no growing resources or stranded transactions over a long
session.

## 5. Verification, completion, and deferrals

Three levels of evidence: headless model/session tests; rendering and
interaction tests (goldens, masks, root paths, resize/DPI, drags, focus,
cancellation, undo/redo); recorded workflow acceptance with the shipping editor.
Dependency fitness checks prove animation authoring/session code needs no
ImGui/Vulkan initialization, shipping animation has no editor dependency, and
the editor does not depend on Kyusu's world/brush workspace. Measure compile
cost, seek/replay time, steady-state allocation, Prop/Simple/Character
footprints, and request bandwidth. Final verification is the canonical
configure/build/serial-test workflow, `git diff --check`, relevant sanitizers,
serial/parallel equivalence, module ABI/layout checks, and
renderer/physics/editor isolation tests.

Explicitly deferred: arbitrary FSM/transition graphs and backward flow control;
general ability/effect rollback beyond request lifecycle prediction; independent
per-layer anchors on one request; retargeting, IK, motion matching, morph
tracks, general node/property animation; clip compression and alternate
skinning backends; a general remote-debugging transport or embedding the
workspace into every editor host; coordinated authoritative content replacement
during an active multiplayer session.
