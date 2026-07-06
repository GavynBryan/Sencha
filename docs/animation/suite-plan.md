# Sencha Animation Suite Plan

Status: ready for review. Executor: staged, each stage independently landable with the suite green.

This document is the execution spec for the v1.0 animation work called out by the engine roadmap: skinned rendering, the fixed-tick animation runtime, montage integration for AbilityKit, and the `kabuki` animation editor. The current asset substrate already imports skeletons, clips, and skinned meshes, but there is no runtime that samples clips, blends poses, evaluates graphs, or renders skinned meshes.

## Decisions on the record

1. **Scope is the full vertical suite.** Runtime skinning, sampling, graphs, montages, and the editor ship in sequenced stages.
2. **Launch path is Kettle plus Kyusu handoff.** `kabuki` is a sibling editor executable registered from Kettle, and Kyusu gains an edit action that spawns it with `--project` and `--asset`.
3. **Product name is `kabuki`.** The name is limited to the executable and window title. Runtime and editor types use mechanical names such as `AnimationGraph...`.
4. **Graph UI uses hand-rolled ImGui canvas widgets.** The shared canvas and timeline widgets live in `editor_common` and become reusable substrate for later sequencer work.

## Non-goals and revival triggers

- `IPoseModifier` is not built until a second concrete pose modifier such as look-at or foot IK exists. The seam is between finalized local pose and palette build.
- Additive blend layers wait for reactive hit or stagger overlays and enter through a graph format-version bump.
- Sub-state-machines wait for a real authored graph that becomes unmanageable when flat.
- Cross-state sync groups wait for a real cross-state or cross-layer synchronization need. V1 only shares normalized phase within one blend space.
- Clip compression and uniform-rate sampled storage wait for measured decode or memory pressure on real content.
- Sequencer and cinematics are v2.0 work, with the timeline widget reused.
- Chunk-parallel pose evaluation waits until profiling shows the serial pose system exceeds the roughly 1 ms rule.
- Multi-mesh-one-pose waits for armor-set style content and should enter as a pose-source entity reference on `SkinnedMeshComponent`.

## Runtime architecture

The runtime is one concrete fixed-tick pipeline. Behavior variation enters as data through `AnimationGraph` and `AnimationMontage` cooked assets. Evaluation uses flat arrays and closed-enum switches, not node objects or one-implementation interfaces. Per-entity evaluation is independent so serial and parallel lanes can produce byte-identical pose products.

### Pose data model

Add `engine/include/anim/SkeletonPose.h` and `engine/include/anim/AnimationPoseStore.h` plus matching sources under `engine/src/anim/`.

- `JointTransform` stores translation, rotation, and scale.
- `AnimationPoseHandle` is a generational slot handle.
- `AnimationPoseStore` owns local pose spans and skinning palette spans for allocated slots.
- Model-space transforms are transient scratch. The persistent palette is object-space and is built by multiplying model-space joints with inverse bind matrices.
- Slot lifecycle follows the `StaticMeshComponentAssets` precedent through component traits and a registry resource that provides animation graph, skeleton, and pose-store services.

### Clip sampling

Add `AnimationClipSampler` as pure free functions:

- `WriteBindPose` fills a local pose from skeleton bind data.
- `WrapClipTime` handles looping and clamping.
- `SampleClip` writes tracked channels into a pre-filled pose and leaves untracked channels unchanged.

Sampling rules are fixed in the header: step selects the key where `Times[i] <= t < Times[i + 1]`; linear tracks lerp translation and scale; rotations use nlerp with a hemisphere fix and normalization.

### Blending

Add `AnimationBlend` as pure free functions over spans:

- Binary blend.
- N-way weighted blend in cooked sample order.
- Masked override blend using resolved joint weights.
- 1D blend spaces with sorted samples and clamped bracket selection.
- 2D blend spaces with cook-time triangulation, cooked-order triangle walk, barycentric weights, and deterministic hull clamp.

Blend-space states advance one normalized phase and evaluate each sample at `phase * sample duration`.

### Animation graph asset and evaluator

Add authored `*.animgraph.json`, cooked `.sanimgraph`, FourCC `SGRF`, and the next free `AssetType`. Follow the new asset-type recipe from `docs/core-systems-map.md`: registry extension, cache, staged loader, owner-thread commit, `AssetSystem` accessors, cook importer, and cooked cache index bump.

The cooked graph contains a string table, parameters, layers, states, transitions, conditions, notifies, blend-space tables, masks, and clip paths. Loader commit acquires the skeleton and clips, validates skeleton consistency and mask lengths, and compiles gameplay tag queries by name.

Evaluation semantics:

- Transition conditions are all-of and transitions are tested in cooked order, first match wins.
- Trigger bits are consumed after transition evaluation each fixed tick.
- State and montage notifies fire on playhead crossing within `(t0, t1]`, including loop wrap.
- `TagWindowBegin` and `TagWindowEnd` grant and release counted gameplay tags.
- `Event` notifies push `AnimationNotifyEvent` into a per-registry event buffer.
- All simulation-visible work advances in fixed tick. Pose sampling runs in PostFixed, and zero-tick frames reuse the persistent palette.

### Montages and root motion

Add authored `*.montage.json`, cooked `.smontage`, FourCC `SMTG`, and the next free `AssetType`. A montage is one clip plus named sections, notifies, a target layer, optional mask, blend-in, blend-out, play rate, and optional root motion.

Runtime keeps one active montage slot per graph layer. The state machine continues advancing underneath the montage so blend-out lands on current graph output. Combo branching is modeled as a requested section jump honored at section end.

The montage sink boundary remains isolated: AbilityKit defines an interface in terms of entity id, montage path, and section hash; the animation side owns an `AnimationMontageRequest` event buffer; the composition root adapts one to the other.

Root motion is cooked by bumping `.sanim` to v2 and optionally adding a root motion track. The graph system writes `AnimationRootMotion`, and `RootMotionApplySystem` converts the delta into character-controller movement before physics.

### ECS components and systems

Add these trivially copyable components:

- `AnimationGraphComponent` with graph and pose handles.
- `AnimationGraphParameters` with float, bool, and trigger storage.
- `AnimationGraphRuntimeState` with per-layer state and montage slots.
- `AnimationRootMotion` with delta translation, delta yaw, and active flag.
- `SkinnedMeshComponent` mirroring `StaticMeshComponent` schema shape.

Add `SkinnedMeshComponent` and `AnimationGraphComponent` to `EngineSceneComponents` so reflection and Kyusu inspector pickup are automatic.

Systems are registered through `EngineSchedule`:

- `AnimationGraphSystem` in FixedLogic after movement.
- `RootMotionApplySystem` in FixedLogic after the graph system and before physics.
- `AnimationPoseSystem` in PostFixed.
- `SkinnedMeshExtractionSystem` in ExtractRender after regular render extraction.
- `SkinnedMeshForwardPass` in the render feature after static opaque draw.

### GPU skinning

Use vertex-shader matrix-palette skinning. The cooked influence stream is a separate vertex buffer binding, and palettes are copied into a per-frame storage buffer. Push constants carry palette base and joint count. `RenderPacket` gains a skinning palette arena, and `RenderQueue` gains skinned items that merge only when mesh, section, and material match.

Compute pre-skinning stays deferred until multiple geometry-consuming passes make per-pass skinning measurably expensive.

## Editor architecture

`kabuki` is a new executable under `editor/kabuki/` over `editor_common`, following the `chakin` application and service pattern. It registers animation runtime systems so preview uses the same code path as the game.

### Application and launch

- Add `AnimationEditorApp`, `AnimationEditorServices`, and `main.cpp`.
- Mirror `chakin` CMake and install behavior.
- Add a Kettle launch action for `kabuki`.
- Lift `ResolveEditorBinary` into `editor/common/src/project/` so Kettle and Kyusu share it.
- Add a Kyusu edit action for animation graph assets that spawns `kabuki --project ... --asset ...`.

### Document model

Mirror `MaterialEditSession` with `AnimationGraphEditSession` and `MontageEditSession`. Each open tab owns one session and one `CommandStack`. Every edit is an undoable command, including state, transition, notify, blend-space, parameter, and montage-section edits. Node layout remains editor-only JSON and is stripped by cook.

### Panels

`kabuki` registers these panels:

- Asset browser, grouped by skeleton.
- Graph canvas.
- Preview viewport.
- Inspector.
- Timeline.
- Parameter dashboard.
- Validation panel.

### Canvas and timeline widgets

Shared hand-rolled ImGui widgets live under `editor_common/ui/canvas/`. `CanvasView` handles pan, zoom, transforms, node drawing, edge drawing, arrowheads, and hit tests. `TimelineTrack` handles playhead, range selection, snapping, notify markers, and section blocks.

The graph canvas shows states, transitions, default-state markers, any-state rail, selection, drag-create transitions, delete, box select, and live evaluation overlay from the preview runtime state.

### Preview and parameter dashboard

The preview follows the `MaterialPreviewRenderFeature` pattern and renders a preview entity through the real skinned runtime path. It supports mesh selection, ground grid, root-motion trail, in-place versus traveling root-motion display, turntable, scrub, step, rate, and play controls.

The parameter dashboard exposes graph floats, bools, triggers, a 2D stick widget for two float parameters, simulated gameplay tags, and a montage test bench that writes through the same request buffer used by gameplay.

### Validation and reload

Validation is a pure implementation shared by editor and cook. It reports skeleton mismatches, unreachable states, empty transition conditions, duplicate blend-space coordinates, section range errors, notify range errors, and missing mask joints.

Graph and montage loaders gain `CommitReload`, Kyusu watches the new authored extensions, and `kabuki` continuously applies the working session to the resident preview state without requiring save.

## Phased rollout

1. **Pose math and sampler.** Add pose structs, store, clip sampler, blend functions, composition, palette build, and pure math tests.
2. **Skinned rendering.** Add component, traits, manifest, queue, packet, extraction, pass, shader, and a temporary fixed-time sampling driver.
3. **Animation graph asset and evaluator with 1D blend space.** Add asset type, cook, loader, cache, animation components, graph system, pose system, and notifies. Delete the temporary stage-2 driver. Add the permanent determinism hash test.
4. **Root motion, montages, and montage sink.** Add `.sanim` v2 root track cook, root motion system, montage asset, request buffer, and composition-root sink adapter.
5. **2D blend spaces and layered masking.** Add cooked triangulation, barycentric evaluation, hull clamp, multi-layer graphs, and masks.
6. **kabuki foundation.** Add executable, Kettle wiring, sessions, tabs, commands, browser, canvas, graph editing, inspector, tag-query builder, and shared validation.
7. **Preview and parameter dashboard.** Add preview render feature, preview entity on real systems, live overlay, parameter controls, stick widget, and tag simulation.
8. **Timeline and montages in editor.** Add timeline widget family, notify editing, montage documents, section branching, windows, blend handles, and montage test bench.
9. **Loop closure.** Add graph and montage hot reload, Kyusu watcher extensions, Kyusu edit handoff, and template exemplar content.

## Guardrails

- Match named precedents before inventing new shapes.
- If existing substrate does not fit, extend that substrate rather than adding a parallel workaround.
- Do not add one-implementation interfaces, future enum values, or half-wired strategy seams.
- Keep graph evaluation as flat data with one evaluator switch.
- Treat determinism as a gate. Any unordered-container iteration in evaluation is a defect.
- Write comments for why, not what, and avoid em dashes in code, comments, commits, and docs.
- Keep editor code out of the runtime and cook code behind `SENCHA_ENABLE_COOK`.
- Prefer reuse and deletion over new parallel code.
- When stage 3 lands, remove stale roadmap Section 3 vocabulary rows converted to code and mark shipped items with dates.

## Verification gates

- Every stage runs `cmake --build build` and `ctest --test-dir build`.
- Stage 2 and later visually confirm the skinned character through the cooked path and attach a screenshot to the PR.
- Stage 3 and later keep the serial versus `worker_count == 4` pose digest test as permanent ctest coverage.
- Stage 6 and later launch Kettle, launch `kabuki`, author the reference graph, cook, and run the template game. Headless tests cover sessions, commands, validation, and canvas hit-test math.
- Stage 9 demonstrates the full `kabuki` save to Kyusu PIE hot-reload loop, with editor layering and isolation fitness tests green.
