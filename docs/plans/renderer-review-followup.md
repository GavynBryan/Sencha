# Renderer Review Follow-Up: Fixes, Capabilities, and Evidence

Status: planned. Execution requires a live Vulkan device and the profiling
tooling (`profile` preset, `render.profile.mode`, capture/trace scripts), so
this plan runs on a local machine, not a headless environment.

## Context

An architecture review adjudicated three opposed renderer assessments
(teardown / preservation / consumer) against tree `c47bbf1`. Verdict: a strong
Vulkan substrate and strong CPU policy engines under an unfinished
orchestration layer. This plan executes the review's "fix before adding major
capabilities" list plus three scope decisions:

- **RenderPacket: delete it.** It is a declared sim→render boundary with no
  producer (`PacketWrite.Queue`) and no consumer (`PacketRead`). Constraint on
  the deletion: non-Euclidean **portals are a concrete planned multi-view
  consumer** — the packet must not be replaced with another single-camera
  implicit frame model. The eventual frame/composition representation must
  support multiple views, render targets, and recursive/offscreen views;
  render-thread/N-1 buffering is a separate future decision. This plan only
  deletes dead state and records that constraint.
- **AlphaMode + LayerMask: implement them.** Alpha-mask discard as mesh
  pipeline variants; LayerMask culling in extraction against a new camera-side
  mask. Blend keeps warning and rendering opaque until a transparent pass
  exists.
- **Evidence: full.** Including the owed streaming/upload benchmark
  (`docs/renderer/open-work.md`, "Upload staging ring") and caster scaling
  with visible geometry.

Confirmed defects being fixed (all code-verified):

1. `framesInFlight` is unbounded above (config validates ≥ 1 only,
   `GraphicsConfig.cpp:141`) while the editor's
   `ViewportTargetCache::kMaxSlots = 4` silently folds higher frame indices to
   slot 0 (`ViewportTargetCache.cpp:36`) — frames 0 and 4 can concurrently
   render into and sample one viewport image.
2. No in-flight retirement of logical identity: bindless slots return to the
   free list immediately (`VulkanDescriptorCache.cpp:310-323`); probe slots
   are freed on streaming unload with no device idle (`ZoneProbeResidency`
   dtor fires in `FramePhase::ZoneResidency`, before the same frame's Render;
   `RuntimeWorld.cpp:369`). Physical lifetime is correctly deferred
   (`VulkanDeletionQueueService`, framesInFlight+1 ring); logical identity is
   not. `LightBindings.h:43-51` documents the unimplemented parking invariant.
3. Frame-UBO descriptor range is last-writer-wins across exactly two
   `SetFrameUniformBuffer` call sites (`MeshForwardPass.cpp:116` = 5712 B,
   `ShadowDepthPass.cpp:108` = 64 B); feature setup order is load-bearing and
   enforced only by comments.
4. `MaterialAlphaMode::Mask` and `StaticMeshComponent::LayerMask` are authored,
   serialized, and silently inert (Mask has no diagnostic; LayerMask has no
   read site while its header comment claims extraction consumes it).
5. `RenderPacket` is dead duplicated state.

**Explicitly deferred** (measurement-first, per the review): the upload-path
rewrite (this plan builds its benchmark, not the rewrite), per-view shadow
caster gathering changes, pipeline-cache indexing, sort-key section width,
and any render-graph / GPU-driven / multi-view design work.

**Discipline** (CLAUDE.md): canonical preset workflow per workstream commit
(`cmake --preset dev`, build, `ctest --preset dev` serially,
`git diff --check`); regression tests observed failing before their fix; no
performance claims without measurement. Branch
`claude/renderer-architecture-review-lxiz7w`, one commit per workstream.

**Execution order: A → B → E → F.1/F.2 (baseline) → C → F.3–F.5 → D → final
verification.** F.2's baseline must precede C (C touches the extraction hot
path). D lands last so the phase rename does not churn C's diff.

---

## Workstream A — framesInFlight upper bound

**Invariant:** every per-frame-in-flight slot array sizes against one
engine-owned bound; config can never exceed it. Owner:
`GraphicsServices::ResolveFramesInFlight` (the single config→backend funnel,
`GraphicsServices.cpp:30-33`).

- Add `static constexpr uint32_t kMaxFramesInFlight = 4;` to
  `engine/include/graphics/vulkan/VulkanFrameService.h` (class scope,
  mirroring the `VulkanDescriptorCache::kBindlessImageCapacity` precedent).
  Not in `GraphicsConfig.h` — core/config must not own a backend limit. Value
  4 preserves the editor's existing bound.
- `ResolveFramesInFlight` becomes
  `std::clamp(config.Graphics.FramesInFlight, 1u, VulkanFrameService::kMaxFramesInFlight)`
  (subsumes the `== 0` guard); move it `private` → `public` static in
  `GraphicsServices.h:86` (the test boundary earns the visibility change).
  Warn on clamp from the public `GraphicsServices` ctor body (it has
  `LoggingProvider&`), naming both values.
- `editor/common/src/render/ViewportTargetCache.h:63`:
  `kMaxSlots = VulkanFrameService::kMaxFramesInFlight` (the include edge
  already exists via `Renderer.h`). `BeginFrame` (`.cpp:36`): replace the
  silent `: 0` fold with `assert(frameInFlightIndex < kMaxSlots)` and direct
  assignment — unreachable after the clamp; aliasing is a contract violation,
  not a mode.
- Comment the `FramesInFlight` field (`GraphicsConfig.h:11`): values above the
  bound clamp with a warning.

**Tests (device-free):** new `test/core/FramesInFlightResolveTests.cpp`
(guard the TU with `#ifdef SENCHA_ENABLE_VULKAN`): table over
`{0,1,2,3,4,5,9,UINT32_MAX}` → `{1,1,2,3,4,4,4,4}`. Must fail before the fix
(5+ currently passes through).

**Docs:** `docs/renderer/vulkan-backend.md` frame-service section notes the
bound.

---

## Workstream B — logical-identity retirement (bindless + probe slots)

**Invariant:** a logical slot index is reassigned only after every frame that
could reference its old meaning has provably retired. The proof already
exists once: the fence wait in `VulkanFrameService::BeginFrame`
(`.cpp:112-135`), sole caller of `VulkanDeletionQueueService::AdvanceFrame`.
Owner of retirement cadence: **`VulkanDeletionQueueService`**.

**Mechanism** — one mechanism, two consumers, zero new per-frame calls.
(Ticking from extraction was considered and rejected: `BeginFrame` returns
`SurfaceUnavailable` *before* the fence wait while minimized, so extraction
frames can pass with zero proven retirements — an extraction-driven ring
would unpark early.)

1. `VulkanDeletionQueueService` gains a monotonic `uint64_t RetiredFrames`
   (incremented in `AdvanceFrame()`), exposed as `GetRetiredFrameCount()` and
   `GetRetireDelayFrames()` (= `Buckets.size()` = framesInFlight+1 — the same
   margin physical destruction uses). A slot parked at count R is reusable at
   `R + delay`. No queue entries hold cache pointers, so the shutdown-dangle
   hazard (DeletionQueue destroyed after the caches) never arises.
   `ResetAfterSwapchainRecreate` needs no change: the counter stalls with
   `Submitted` cleared and parked slots simply wait longer — the same
   conservative behavior deferred destroys already have; state this in the
   counter's comment.
2. New pure policy type `ParkedSlotQueue`
   (`engine/include/graphics/ParkedSlotQueue.h`, following the
   `FrameScratchRing` precedent — pure arithmetic, device-free tests):
   `Park(slot, readyAt)`, `TakeReady(now, out)`. Earned: two real consumers
   plus a test boundary.
3. `VulkanDescriptorCache`: ctor gains `const VulkanDeletionQueueService&`
   (construction order supports it — DeletionQueue at
   `GraphicsServices.cpp:56` precedes Descriptors at `:62`, the only
   construction site; reverse destruction keeps the pointer valid).
   `UnregisterSampledImage`: keep the immediate `BindlessLookup` erase (dedup
   can never hit a freed pair) but park the slot instead of pushing
   `BindlessFreeSlots`. `RegisterSampledImage`: drain `TakeReady` into
   `BindlessFreeSlots` before consulting it; the capacity error fires only
   after the drain. `UpdateSampledImage` untouched — TextureCache
   reload-in-place identity preserved. In-flight frames keep sampling old
   descriptor contents; the old image outlives on the same deadline via the
   deletion queue. Update the `.h:106-109` comment. Callers
   (`TextureCache::OnFree`, `ViewportTargetCache::DestroySlot`) unchanged.
4. `ProbeVolumeSet`: `Setup` gains `const VulkanDeletionQueueService*`
   (passed from `DefaultRenderPipeline::AddMeshRenderFeature` as
   `&graphics.DeletionQueue`; the set already includes Vulkan headers).
   `ReleaseVolumes`: keep `ResetProbeVolume` (in-flight frames sample the
   **dummy** — old resource or dummy, never a new unrelated one) and the
   already-deferred `Images->Destroy`; park the slot with `SlotUsed` staying
   true. `AddZoneVolumes`: drain `TakeReady` (clearing `SlotUsed`) before the
   slot scan — reuse is the only moment the invariant matters, so lazy
   reaping at the reuse point is the smallest owner; **no per-frame hook, no
   device idle on the streaming path**. All-8-parked churn hits the existing
   "all slots resident, volume denied" warning — transient, self-healing;
   document. Update `LightBindings.h:43-51`: the parking invariant is now
   implemented by `ProbeVolumeSet`, not owed by callers.

**Tests:** device-free `test/core/ParkedSlotQueueTests.cpp` (not-ready before
deadline / ready at deadline / shared deadlines / interleaved / stalled
counter / delay-0) and `test/core/DeletionQueueRetirementTests.cpp`
(`VulkanDeletionQueueService` constructs without a device; the counter
increments once per advance; delay == framesInFlight+1). Live device (report
honestly — the aliasing defect has no validation-layer signature; the
regression surface is the policy tests plus structural deadline arithmetic):
SceneViewer probe-zone load → unload → reload with validation on; kyusu
viewport resize/close cycles (bloom bindless slots); F.3's streaming loop
exercises exactly this path.

**Docs:** `vulkan-backend.md` bindless lifecycle; `baked-lighting.md` probe
parking and the transient denial.

---

## Workstream E — frame-UBO descriptor range ownership

**Invariant:** the frame set's dynamic-UBO binding always covers the largest
declared range regardless of setup order, and every grantable scratch offset
is legal against that range. Owner: `VulkanDescriptorCache` (arbitration) +
`VulkanFrameScratch` (offset validity).

**Verified subtlety (state in the commit):** the scratch buffer is sized
exactly `framesInFlight × alignedBytesPerFrame`
(`VulkanFrameScratch.cpp:48-49`: `info.Size = Ring.GetTotalBytes()`). With a
bound range of 5712, a 64-byte shadow grant near the tail of the **last**
slice would violate `dynamicOffset + range ≤ bufferSize`. Nothing structural
prevents this today — it holds only because per-frame high water sits far
below the 1 MiB slice.

1. Replace `SetFrameUniformBuffer(buffer, range)` with
   `RequireFrameUniformRange(BufferHandle, VkDeviceSize minRange)` (safe
   rename: exactly two call sites — `MeshForwardPass.cpp:116`,
   `ShadowDepthPass.cpp:108`). The cache tracks
   `{BoundFrameUboBuffer, BoundFrameUboRange}` and writes the descriptor only
   when the buffer changes or the monotonic max grows. Setup order stops
   mattering.
2. `VulkanDescriptorCache::kMaxFrameUniformRange = 8192` — codifies the
   recorded budget (`constraints.md`: "Frame UBO ≤ 8 KiB… currently 5712").
   `RequireFrameUniformRange` rejects above it; the passes add
   `static_assert(sizeof(MeshFrameUniforms) <= …)` so the failure is
   compile-time. The constant lives at the graphics layer (render types
   cannot be named from graphics/).
3. Tail-pad the scratch buffer:
   `info.Size = Ring.GetTotalBytes() + AlignUp(kMaxFrameUniformRange, UniformAlignment)`.
   Ring arithmetic untouched (the pad is outside every slice, never granted);
   `FrameScratchRing` and its tests unchanged. Rejected: `VK_WHOLE_SIZE`
   (the whole ring exceeds `maxUniformBufferRange`'s guaranteed minimum of
   16384 — illegal); per-pass frame sets (duplicates set 0, extra binds).
4. Remove the order comment at `EditorRenderFeature.cpp:105-107`. **Keep**
   `DefaultRenderPipeline.cpp:129-132` — it documents the still-load-bearing
   lighting-set-layout Setup ordering (set 2 must exist before the forward
   pipeline layout builds), not the frame UBO. Rewrite `vulkan-backend.md`
   "The frame UBO range trap" (~:266): the trap is removed; document the
   arbitration, the constant, and the tail-pad invariant. Update the comments
   in `VulkanDescriptorCache.h:91-97` and `VulkanFrameScratch.h:38-41`.

**Tests:** the descriptor write needs a device; the three-line arbitration
stays in the cache (extracting it would be an unearned seam). Verification is
live SceneViewer + kyusu with validation (the two hosts exercise both Setup
orders). Report the absence of a device-free test honestly; the static_assert
plus the tail pad carry the structural half.

---

## Workstream F.1 + F.2 — upload instrumentation and baseline (before C)

**F.1 Upload counters (capture schema 4 → 5).** Surface decision:
`RenderStats` + capture fields, not a new `CpuScope`, not trace phases —
`CpuScopeTimings`/`RenderStats` are reset by the mode latch at the top of the
extract phase, but uploads commit in `DrainAsyncTasks` (phase 3), so scope
accumulation there would be wiped before the frame-end publish; trace phase
spans conflate upload waits with all other async commits and cannot count
submits/bytes.

- `VulkanUploadContextService` gains monotonic totals: `UploadSubmitCount`
  (u64), `UploadWaitSeconds` (double, begin-of-submit → fence-wait return),
  `UploadStagingBytes` (u64, fed via `AddStagingBytes` from
  `VulkanBufferService::Upload` / `VulkanImageService::Upload` — both already
  hold the upload context).
- Publish per-frame deltas in `Renderer::DrawFrameScheduled`'s existing stats
  block (`Renderer.cpp:200-208`, `Services.Upload` is in reach) → new
  `RenderStats` fields `UploadSubmits`, `UploadStagingBytes`, `UploadWaitMs`.
- Bump `RenderCapture::kSchemaVersion` 4 → 5; add `upload_submits_count` /
  `upload_staging_bytes` / `upload_wait_ms` to the fixed-field list and CSV
  writer per `extending.md:249`; update `instrumentation.md`; extend
  `scripts/capture_stats.py`.
- Tests: update the schema assertions in `test/core/RenderProfilingTests.cpp`
  / `RenderInstrumentationBundleTests.cpp`; a device-free test that
  `SerializeJson` emits the new keys.

**F.2 Baseline (MUST precede C).** Build the `profile` preset; generate the
bench scenes (`SENCHA_RENDER_BENCH_ROOT=<assets-root>` + `RenderBench.Generate`
via `level_cook_tests`); run
`scripts/bench_render_ab.sh <SceneViewer> <content> out/baseline-pre-C 5 3000 levels/bench_hub`
and the same for `levels/bench_stress`; reduce with `bench_trace_stats.py` /
`capture_stats.py --warmup 400`; archive under `docs/plans/evidence/` (F.5
README). The gate scripts fail on scratch-alloc-failures/instances-dropped —
a dirty baseline invalidates the comparison.

---

## Workstream C — alpha-mask pipelines + LayerMask

### C.1 Alpha-mask

**Invariant:** a material authored `alpha_mode: mask` renders with
per-fragment discard below its cutoff, selected by the same single function
everywhere. Owner: `SelectOpaquePipeline`.

- `RenderQueue.h`: `OpaquePipelineId` grows to 8 values, **bit 2 = masked**
  (bit 0 = double-sided, bit 1 = unlit preserved). `SelectOpaquePipeline`
  adds +4 on `AlphaMode == Mask`; Blend still selects opaque (the load warn
  at `MaterialAssetLoader.cpp:156-161` is kept).
- `RenderQueue.cpp:6-18`: sort key becomes
  `[7b pass][3b pipeline][14b material][20b mesh][4b section][16b depth]`
  (pass to bit 57 — `ShaderPassId` has one value, so 7 bits is free headroom;
  pipeline mask `0x7` at bit 54). Run merging compares real fields, so
  correctness is key-independent — update the key tests anyway.
- `MeshForwardPass.h`: `OpaquePipelines` → `std::array<VkPipeline, 8>`;
  `MeshPushConstants::Pad2` (offset 76) → `float AlphaCutoff = 0.5f`;
  static_asserts updated (`sizeof == 80` unchanged, add the offsetof).
- `MeshForwardPass.cpp`: the build loop iterates 8 variants — spec constants
  `{id 0 = unlit (bit 1), id 1 = masked (bit 2)}`, cull from bit 0; extend
  `kPipelineNames`. `DrawRuns` sets `push.AlphaCutoff = material->AlphaCutoff`
  (safe per-run: run identity includes Material). Prewarm compiles 8 variants
  instead of 4 at load — note in the commit; invisible (paid at load).
- **Debug/overdraw collapse: keep the 2-wide arrays** — the existing
  `pipelineIndex &= 1u` (`.cpp:441`) drops the mask bit unchanged; debug views
  visualize lighting terms and the overdraw view honestly measures full
  masked geometry; expanding would add 8 pipelines for no diagnostic gain.
  Document.
- Shaders (auto-rebuilt by `SenchaShaders.cmake`):
  `mesh_forward.frag.glsl` — `layout(constant_id = 1) const bool
  MATERIAL_ALPHA_MASK = false;` and, immediately after `SampleBaseColor()`
  (before the unlit branch, so both paths mask):
  `if (MATERIAL_ALPHA_MASK && baseColor.a < pushData.AlphaCutoff) discard;`.
  Spec-constant gating keeps the discard (and its early-Z cost) out of
  non-masked variants. `mesh_material.glsli:23` and
  `mesh_forward.vert.glsl:32`: `Pad2` → `AlphaCutoff` for block consistency.
- Editor: set `item.Pipeline = SelectOpaquePipeline(*material)` at the
  placed-mesh site (`SceneRenderQueueBuilder.cpp` ~:285-291), the zone-brush
  site (~:385-394), and `MaterialPreviewRenderFeature.cpp:189-195` (the
  preview must show masking); the plain-brush site (~:231-239) only if its
  material cache is in reach, otherwise a one-line comment.
- **Scope boundary (document, don't implement):** the shadow pass stays
  alpha-ignorant — masked casters cast opaque shadows. Record in
  `features-and-passes.md` and as an `open-work.md` row (alpha-tested shadow
  pass).

### C.2 LayerMask

**Invariant:** extraction emits an instance for a camera iff
`(renderer.LayerMask & camera.LayerMask) != 0`, alongside
Visible/ExcludedEntity. Owner: a named camera-visibility predicate in the
extraction layer.

- `CameraComponent.h`: add `uint32_t LayerMask = 0xFFFFFFFFu` plus a defaulted
  schema field (additive; old scenes load all-layers; no format bump).
  `Camera.h` `CameraRenderData`: mirror the field;
  `CameraRenderDataSystem::Build` copies it.
- `RenderExtractionSystem.h`: a small named constexpr predicate
  `CameraSeesInstance(const StaticMeshComponent&, EntityId, const CameraRenderData&)`
  = Visible ∧ not-excluded ∧ mask-intersects; `emitChunk` (`.cpp:143-147`)
  replaces its two inline checks with it — same checks, same order, one extra
  AND+branch, all before the cache lookups. This is the earned test seam
  (the full `Extract` path needs a live device; the sibling
  `AppendShadowCasterSections` free-function pattern established it). The
  header claim at `StaticMeshComponent.h:22-24` becomes true.
- `StaticMeshComponent.h:106`: add `.Default(defaults.LayerMask)` for sibling
  parity. Note: cooked output may omit the default value going forward →
  content-hash churn on the next cook (acceptable).
- **Shadow casters are NOT filtered by LayerMask** — mirrors the
  ExcludedEntity principle (casters occlude light views, not the observer's);
  a hidden-by-layer object still casts. No change in
  `ShadowCasterExtractionSystem.cpp`; document. The editor
  `SceneRenderQueueBuilder` is untouched (it has its own visibility model);
  note it.

### C tests

- `RenderQueueTests.cpp`: the 7/3 key split; a `SelectOpaquePipeline` table
  over Shading × DoubleSided × AlphaMode (Blend → opaque); masked-variant
  ordering. `RenderQueueRunTests.cpp`: items differing only in `Pipeline`
  never merge.
- New `test/runtime/RenderExtractionFilterTests.cpp`: table-driven
  `CameraSeesInstance` — disjoint/overlapping/zero masks, visible, excluded;
  must fail before the fix.
- `CameraRenderDataTests.cpp`: mask propagation and the default-when-unauthored
  case; a camera `layer_mask` absent-default case in the serializer tests
  (follow the `orthographic_height` sibling pattern).

### C measurement (hot path)

Re-run F.2's two `bench_render_ab.sh` invocations identically into
`out/post-C`; compare with `bench_trace_stats.py --compare` plus
`capture_stats.py` deltas of `Extraction_cpu_ms` and draw counters. Expected:
no measurable delta (one predictable branch; existing content is
all-layers/no-mask). Separately author one masked-material scene variant and
record the masked-pipeline cost as a feature-cost note, not a regression
gate. Record under `docs/plans/evidence/`. Claim only what the numbers show.

**Docs:** `features-and-passes.md`, `shaders.md` (push-constant field),
`open-work.md` (alpha-tested shadows row).

---

## Workstream F.3–F.5 — streaming benchmark, caster scaling, audits

**F.3 Streaming/upload benchmark** (pays the `open-work.md` "Upload staging
ring" debt):

- Scene: extend `test/level_cook/RenderBenchGen.cpp` (same env gate) with
  `levels/bench_stream/zone_00…zone_07` — an 8-zone ring, each zone with a
  room shell, placed textured meshes, a probe volume, and lights: every load
  pays real image and buffer uploads; every unload exercises B's probe
  parking.
- Traversal: extend `example/SceneViewer/SceneViewerGame.cpp` with a scripted
  zone loop via cvars (`sceneviewer.stream.scripted`, `.zone_count`,
  `.frames_per_zone`), generalizing the existing single-`kPlayZone`
  `AsyncZoneLoader::BeginLoad` path (~:360-450, including `AttachZoneProbes`):
  every K frames (deterministic, never wall clock), begin-load zone
  `(i+1) mod N`, release `(i−2) mod N`.
- Runner: new `scripts/bench_stream_upload.sh` modeled on
  `bench_render_ab.sh` (profile preset, CPU pinning, IMMEDIATE present,
  capture mode, fallback-asset gate). Reduce with the extended
  `capture_stats.py`; correlate `upload_wait_ms` against frame-time p99
  hitches.
- Evidence: `docs/plans/evidence/streaming-upload/` README + results
  (scenario, exact commands, distributions, hitch correlation). Update
  `open-work.md` "Upload staging ring" with the measurement outcome — the
  batching-rewrite decision stays out of scope; the numbers to make it now
  exist.

**F.4 Caster scaling with visible geometry** (recorded gap: cloned cell
meshes never relocate; `draw_calls` stuck at 6-7 —
`docs/plans/evidence/renderer-cpu-profile/results.md`): extend
`RenderBenchGen.cpp` with `levels/bench_casters_{100,400,1600,6400}` — N
placed `StaticMeshComponent` entities on unit meshes in a lit room with
shadow-casting spot/point lights (reuse `AuthorRoomShell` and the light
helpers). Run via `bench_render_ab.sh`; **verify the instrument first**
(`shadow_casters_tested`/`draw_calls` must actually scale with N) before any
scaling claim; `scope_scale_stats.py` over `ShadowGather_cpu_ms` /
`ShadowRecord_cpu_ms` vs N → `docs/plans/evidence/shadow-caster-scaling/`;
update the stale open-work.md entry.

**F.5 Cheap audits** → `docs/plans/evidence/renderer-review-2026-08/README.md`:

1. framesInFlight config census (all checked-in configs; record that all are
   ≤ 4).
2. Schema-field-vs-read-site sweep across every `MakeField` (LayerMask was
   the dead field found; prove it was the only one or record more — findings
   only, fixes out of scope).
3. Sync-validation negative control per `constraints.md` ("The dead
   instrument"): temporarily delete one barrier locally, run with
   `validate_synchronization: true`, record whether `SYNC-HAZARD` appears,
   **revert — never commit the deletion**. If the instrument works now,
   update the "dead instrument" section; if still dead, re-affirm it with the
   new date.

---

## Workstream D — delete RenderPacket, record the multi-view constraint

Pure structural deletion; records the portals constraint, designs nothing.

- Delete `engine/include/runtime/RenderPacket.h`.
- `FrameDriver.h`: drop the include (:6), `PacketWrite`/`PacketRead` from
  `PhaseContext` (:48-49), the `Packets` member (:91); **rename
  `FramePhase::ExtractRenderPacket` → `ExtractRender`** (mechanism naming).
  Trace-label safety verified: `bench_trace_stats.py` matches only
  `"Frame N"` events; `scope_scale_stats.py` reads capture keys; no other
  `ToString(FramePhase)` consumer exists outside docs.
- `FrameDriver.cpp`: the `ToString` case (:17); remove the packet
  Reset/writes (:121-123) and both `Packets.Flip()` calls (:82, :137) —
  observable no-ops (ctx binds at the top of `StepOnce`; Reset preceded
  extract).
- `GameContexts.h`: `RenderExtractContext` loses the two packet refs;
  `Presentation` stays.
- `EngineFramePhases.cpp:721-745`: drop the packet fields;
  `.Presentation = ctx.Runtime->GetCurrentFrame().Presentation` — verified
  identical value and the established sibling pattern (`rf.Presentation` at
  :542/:552/:571). The packet's `FrameIndex` has no reader.
- `DefaultRenderPipeline.cpp`: remove the dead `ctx.PacketWrite.*` writes
  (:195, :211, :220, :290-292; the early returns keep returning). Reword the
  `DefaultRenderPipeline.h:30` comment.
- Tests: delete the flip test (`test/runtime/FrameLoopScenarioTests.cpp:530-539`
  plus its include); rewire the `test/core/EngineScheduleTests.cpp:202-207`
  scaffolding.
- Docs (fix stale content while touching): `core-systems-map.md:124,424-431`;
  `renderer/architecture.md:89`; `renderer/frame.md` (**already stale** —
  says eleven phases, numbers 8/9; the actual enum has 13 with 10/11 —
  correct the whole table); `renderer/readme.md:45`; `instrumentation.md:31`;
  `plans/networking.md:146-149` (also-stale phase list).
- **Portals constraint record** in `open-work.md` ("Frame composition
  representation"): RenderPacket was deleted as dead single-camera state, not
  as a verdict against a packetized handoff; the eventual representation must
  carry multiple views / render targets / recursive offscreen views (portals
  are the planned consumer); N-1 render-thread buffering is a separate
  decision to be made with it.

---

## Verification

1. Per-workstream: focused
   `ctest --test-dir build -R '<pattern>' --output-on-failure`; each new
   regression test observed failing before its fix.
2. After each workstream commit and at the end: `cmake --preset dev &&
   cmake --build --preset dev --parallel && ctest --preset dev` (serial —
   never parallel) and `git diff --check`.
3. Live-device runs with validation for B/E/C: SceneViewer
   (`+map levels/bench_hub`; probe-zone load→unload→reload; the F.3 streaming
   loop) **and** kyusu (viewport open/resize/close for the bloom bindless
   slots; material preview with a masked material). Zero validation errors
   expected — noting per `constraints.md` that syncval is a dead instrument
   unless F.5.3 re-proved it.
4. C's A/B measurement (F.2 baseline vs post-C) recorded; no unexplained
   `Extraction_cpu_ms` or frame-time regression.
5. F artifacts present under `docs/plans/evidence/` (streaming-upload,
   shadow-caster-scaling, renderer-review audits) with
   scenario/method/results; open-work.md updated; F.5.3's temporary barrier
   deletion confirmed absent from the diff.
6. Commits: one per workstream (A, B, E, F.1+F.2, C, F.3–F.5, D) on
   `claude/renderer-architecture-review-lxiz7w`; each message states the
   invariant and owner. The handoff reports anything not run honestly.

### Critical files

- `engine/src/graphics/vulkan/VulkanDescriptorCache.cpp` / `.h` — B (parked
  bindless slots), E (range arbitration)
- `engine/include/graphics/vulkan/VulkanDeletionQueueService.h` — B
  (retired-frame counter, the single retirement owner)
- `engine/src/render/ProbeVolumeSet.cpp` / `.h`,
  `engine/include/render/LightBindings.h` — B (probe slot parking)
- `engine/src/render/MeshForwardPass.cpp` / `.h`,
  `engine/shaders/mesh_forward.frag.glsl`, `mesh_material.glsli` — C
  (8 variants, AlphaCutoff)
- `engine/src/render/RenderExtractionSystem.cpp` / `.h`,
  `engine/include/render/RenderQueue.h` / `.cpp` — C (predicate, pipeline
  selection, sort key)
- `engine/include/runtime/FrameDriver.h` / `.cpp`,
  `engine/src/app/EngineFramePhases.cpp` — D (packet deletion, phase rename)
- `engine/src/graphics/vulkan/GraphicsServices.cpp`,
  `engine/include/graphics/vulkan/VulkanFrameService.h`,
  `editor/common/src/render/ViewportTargetCache.*` — A
- `engine/src/graphics/vulkan/VulkanUploadContextService.*`,
  `engine/include/profiling/RenderStats.h`, `RenderCapture.*`,
  `scripts/capture_stats.py` — F.1
- `test/level_cook/RenderBenchGen.cpp`,
  `example/SceneViewer/SceneViewerGame.cpp`,
  `scripts/bench_stream_upload.sh` (new) — F.3/F.4
