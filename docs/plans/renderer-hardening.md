# Renderer Hardening

Status: Gates 0, 1, 2, and 5 landed. Gate 3 stopped at its measurement step by
design. Gate 4 landed three of four items; the upload staging ring is the one
outstanding piece. Derived from an external renderer audit (2026-07-23),
re-verified against the tree before adoption. Findings that did not survive
verification are listed below and carry no work items.

Evidence for every claim below is under
[`evidence/renderer-hardening/`](evidence/renderer-hardening/). All numbers
were taken on a Debug build unless stated; the ratios are the point, not the
absolute values.

## Outcomes

- **Gate 0 (done).** SceneViewer mounts the cooked index, so the bench stopped
  measuring fallback materials. The committed cube mesh is current-format and
  a test now fails on any stale one. Captures carry device, driver, build,
  validation state, map, and per-frame resolution. Scratch use, allocation
  failures, skipped passes, dropped instances, and shadow caster counts are
  live counters; CPU scopes attribute extraction, light selection, shadow
  gather, and both record paths. The bench refuses runs with fallback assets,
  dropped work, or too few recorded frames. Validation costs 4.8x on CPU
  render recording, so runs default to it off.
- **Gate 1 (done).** Meshes over 32 sections are rejected at cook, serialize,
  load, and upload. The frame scratch grants partial allocations and both
  passes render the prefix that fits and count the rest, so the 13,035
  instance boundary no longer blanks a frame. The scratch budget is real
  configuration, which is what makes the path testable.
- **Gate 2 (done).** Shadow views gather their own visible sets, sort them
  into instanced runs, and upload only what they draw; point faces reject
  out-of-range casters with one sphere test for all six. The caster record
  table is built only when an on-change slot consumes it.
- **Gate 3 (measured, then stopped).** Light selection costs 0.068 ms p50
  discrete / 0.089 ms integrated against 6.7 ms / 13.4 ms frames, so the
  selection path was left alone. The per-fragment loop question is unanswered
  and needs the fill bench that does not exist yet.
- **Gate 4 (three of four).** Pipelines compile at load: first-frame CPU
  recording fell from 6.78 ms to 0.16 ms. Swapchain recreation hands the old
  chain over as `oldSwapchain` and the duplicate device idle is gone. Feature
  setup failure is expressible and acted on.
- **Gate 5 (done).** `Renderer::DrawFrame` deleted.

Scope: the Vulkan renderer's correctness cliffs, measurement pipeline, shadow
CPU cost, per-pixel light cost, and startup/resize/upload hitches. The forward
renderer's architecture is kept: flat service ownership in `GraphicsServices`,
the `IRenderFeature` phase model, one pipeline selected by data.

## Audit corrections (adopted findings vs. rejected ones)

Verified and adopted (file references checked against the tree):

- The 1 MiB per-frame scratch slice is a hard scene-complexity cliff. The
  forward pass allocates all instance data as one indivisible block
  ([`MeshForwardPass.cpp:341`](../../engine/src/render/MeshForwardPass.cpp))
  and returns without drawing when it fails. The shadow pass allocates one
  matrix per caster for the whole caster set before any per-view culling
  ([`ShadowDepthPass.cpp:109`](../../engine/src/render/ShadowDepthPass.cpp))
  and runs first, so heavy shadow scenes blank the main pass. The measured
  boundary (13,035 instances render, 13,036 blank the frame) reproduces from
  the struct sizes: (1,048,576 - 5,712) / 80.
- Shadow command generation emits one indexed draw per surviving caster per
  view, six views per point light. Measured: ~10k draws and ~9.5 ms of CPU
  recording for one point light at 10k casters.
- `1u << sectionIndex` is undefined behavior past 31 sections, and
  `ValidateMeshGeometry` never caps the section count
  ([`MeshValidation.cpp`](../../engine/src/render/static_mesh/MeshValidation.cpp)).
- SceneViewer never calls `RegisterCookedAssets`, unlike the template
  ([`SceneViewerGame.cpp:195`](../../example/SceneViewer/SceneViewerGame.cpp)
  vs [`TemplateGame.cpp:418`](../../template/src/TemplateGame.cpp)), so the
  bench harness (which drives SceneViewer) rendered fallback materials.
  Most checked-in `.smesh` artifacts are format v4 against a v5-only loader.
- The light-cap warning fires when exactly `kMaxForwardLights` are selected
  with zero dropped
  ([`DefaultRenderPipeline.cpp:265`](../../engine/src/app/DefaultRenderPipeline.cpp)).
- The forward fragment shader loops over every selected light per fragment;
  measured 16 to 64 lights: 3.65x GPU cost on the 4060, 4.2 to 7.5 ms on the
  Intel iGPU at 720p.
- First visible frame pays 18-38 ms for synchronous lazy pipeline creation;
  `VulkanPipelineCache::LoadFromDisk`/`SaveToDisk` exist with no call sites.
- Uploads submit and fence-wait per buffer on the graphics queue; swapchain
  recreation destroys before creating, without `oldSwapchain`, with two
  device-idle waits; `Renderer::AddFeatureImpl` registers a feature whose
  `Setup` cannot report failure.
- `ShadowCasterDiff::Apply` sorts records every frame even when
  `Residency.HasOnChangeSlots()` is false.
- `RenderExtractionSystem` caches one query behind a single `World*`
  sentinel; the default pipeline runs one extractor across all active
  registries, so with more than one active registry the cached query is
  rebuilt every frame for every registry.
- `Renderer::DrawFrame` has no callers (`EngineFramePhases` uses
  `DrawFrameScheduled`).

Rejected, with the reason on the record:

- "The full build is not green; GenerateCubeDemoAssets and core_tests fail to
  link; narrowing errors in WorldPartitionRuntimeTests.cpp:511." False. The
  default build compiles all 342 targets and ctest passes 1726/1726.
  `currentX` in that test is a `double` feeding a `Vec3d`; there is no
  narrowing. No work items derive from the audit's build-health section.
- "Establish explicit high/medium/low light budgets." Rejected as shaped:
  that is the deleted fidelity-tier system returning under a new name.
  Light cost is bounded by data (cvars, per-scene budgets) on one pipeline.
- "Remove no-call-site IRenderFeature::Contribute." Rejected:
  [`Renderer.h:125`](../../engine/include/graphics/vulkan/Renderer.h)
  documents it as the game-driven bootstrap-policy extension point. That is
  a game binary boundary: planned infrastructure, not a dead seam. Removal
  is an owner decision, not a cleanup item.
- Chunking the scratch workaround separately inside each consumer. Both
  consumers already perform the same all-or-nothing ritual around
  `VulkanFrameScratch`; the contract is the problem, so the fix lands in the
  arena's API once and both passes move onto it (Gate 1).

## Invariants held throughout

- No render graph, no RHI layer, no GPU-driven visibility system, no
  asynchronous job framework for rendering. Every gate below is concrete
  fixes to named mechanisms.
- No fidelity tiers or parallel pipelines. Budgets and toggles are cvars and
  scene data on the single forward path.
- Two concurrency lanes only; no new threads for uploads or pipeline
  creation. Prewarming is synchronous work placed at a loading boundary.
- Suite green at every commit checkpoint. No half-wired mechanisms left
  behind a checkpoint: each new counter has a live producer, each new API a
  consumer.
- Determinism: nothing in this plan may make serial and parallel paths
  diverge (all touched passes record on one thread today; keep it that way).

## Evidence workflow

All measurements land under `docs/plans/evidence/renderer-hardening/`.

- Build and suite: `cmake --build build -j` then `ctest --test-dir build -j 8`.
- Deterministic A/B: `scripts/bench_render_ab.sh`, run in the foreground with
  `SENCHA_PRESENT_MODE=IMMEDIATE` (background runs throttle and skew
  percentiles).
- Live GPU validation: SceneViewer / SenchaTest `+map` runs; a clean run has
  no `[Vulkan]` validation lines in the log.
- Percentiles only (p50/p95/p99), never averages alone. Warmup frames
  excluded.

Gate order is strict for 0, 1, 2 (measurement before fixes, cliffs before
throughput). Gates 3 and 4 may interleave after Gate 1. Gate 5 items ride
along wherever they touch code already open.

## Gate 0: Make measurements trustworthy

Nothing downstream is credible until the bench renders what it claims to
render and a failed frame cannot masquerade as a fast frame.

**0.1 SceneViewer cooked registration.** Add
`RegisterCookedAssets(std::string(kAuthoredRoot), runtimeAssets.Registry);`
after the two scans in `SceneViewerGame.cpp`, matching the template's mount
order (authored scan, cooked scan, cooked index).
Verify: run the bench scene; zero unresolved-asset or fallback-material log
lines.
Commit: `Register cooked assets in the SceneViewer mount`

**0.2 Recook stale v4 meshes; pin the format version.** Regenerate
`template/assets/meshes/dev/cube.smesh` via `GenerateCubeDemoAssets` and
recook the checked-in `.cooked/levels/*` artifacts through the editor cook.
Do not delete cooked levels: the live GPU validation workflow hand-edits
them. Add a test that walks every checked-in `.smesh` and asserts
`header.Version == kSmeshFormatVersion`.
Verify: the new test passes; no `unsupported version` lines when loading any
checked-in level.
Commit: `Recook the stale v4 meshes and pin the smesh version in a test`

**0.3 Bench fails loud on fallback.** `bench_render_ab.sh` exits nonzero when
the run log contains asset-resolution failures or (after 0.5 lands) a nonzero
scratch-failure or skipped-pass counter in the capture.
Verify: seed a bad material reference locally, confirm the script fails, then
revert.
Commit: `Fail the render bench on unresolved assets`

**0.4 Shipping-representative captures.** Validation is already config data
(`EngineGraphicsConfig::EnableValidation` from the `graphics` config
section); no code change needed to turn it off. Extend the capture JSON
metadata (in `profiling/RenderCapture`) with: GPU name, device ID, driver
version, resolution, present mode, validation state, build SHA, scene name,
and the frame's asset counts. Record a validation-on/off A/B pair for the
bench scene as the first evidence artifact.
Verify: two capture JSONs in the evidence dir, self-identifying, reproducible.
Commit: `Record device, driver, and validation state in render captures`

**0.5 Counters and CPU scopes.** Add to `RenderStats` (each with its live
producer, per the header's contract): `ScratchUsedBytes` (per frame, distinct
from the lifetime high water), `ScratchAllocFailures`, `PassesSkipped`,
`InstancesDropped`, `ShadowCastersTested`, `ShadowCastersVisible`,
`ShadowInstanceRuns`. Add CPU scopes for extraction, light selection, shadow
gather/diff, and pass recording so Gate 2/3 decisions are measured, not
guessed. Fix the light-cap warning to fire only when
`LightsDroppedAtCap > 0` (the counter already exists).
Verify: stats panel shows the new counters live; a capture of the 10k-caster
repro shows `PassesSkipped > 0` (the cliff is now visible instead of silent).
Commit: `Add scratch, skip, and shadow counters with CPU scopes`

**0.6 Device override.** Optional `DeviceIndex` in the graphics config,
honored by `VulkanPhysicalDeviceService` selection, so NVIDIA vs Intel A/B on
one machine does not depend on driver environment tricks. A config field, not
a seam.
Verify: forcing each index on this laptop selects the expected adapter (log
line), renders the bench clean on both.
Commit: `Select the Vulkan device by config index when one is given`

Exit criteria: zero asset warnings on bench scenes; captures self-identifying;
validation on/off A/B recorded; an allocation failure is visible in stats and
fails the bench.

## Gate 1: Remove the correctness cliffs

**1.1 Section cap.** `ValidateMeshGeometry` rejects meshes with more than 32
sections, with a `static_assert` tying the cap to the width of
`StaticMeshComponent::SectionMask`. Confirm the cook path routes through the
same validation; if it does not, add the call at import so bad content fails
at cook time, not at runtime.
Verify: new tests at 31 (works), 32 (works), 33 (rejected with a precise
error). Both extraction shift sites are now unreachable past bit 31.
Commit: `Reject meshes with more sections than the section mask holds`

**1.2 Partial grants in the frame scratch.** The arena gains
`AllocateUpTo(maxSize, alignment, granularity)`: it returns the largest
granularity-multiple slice that fits, instead of all-or-nothing. Existing
`Allocate` keeps its contract for the small fixed uploads (frame uniforms,
per-view matrices).
Verify: unit tests on the arena: exact-fit, partial grant, zero grant,
granularity rounding, per-frame slice isolation.
Commit: `Let frame scratch grant partial allocations`

**1.3 Forward pass streams instances in bounded runs.**
`BindInstanceStream` becomes a loop: request the remaining instances via
`AllocateUpTo`, fill and draw that run, repeat. The existing per-material
draw iteration wraps naturally around run boundaries (a run boundary is just
an extra vertex-buffer rebind). If the arena is fully exhausted mid-frame,
the pass draws what it has, counts the remainder in `InstancesDropped`, and
never blanks the frame.
Verify: the 13,035/13,036 boundary renders identically on both sides
(draw-call and triangle counters equal, capture diff). A test with
`FrameScratchBytesPerFrame` shrunk to a few KB still renders every instance
across many runs. A 50k-instance generated scene reports zero
`PassesSkipped` and zero `InstancesDropped` at the default 1 MiB.
Commit: `Stream forward instances in bounded runs`

**1.4 Shadow pass streams casters in bounded runs.** Same treatment for
`ShadowDepthPass::BindInstanceStream`. In this gate the upload is still the
whole caster set (per-view upload is Gate 2); the cliff just stops being a
cliff, and the 10k-caster scene stops blanking the main pass.
Verify: the 10k-caster repro renders the main scene (nonzero draw calls and
triangles) with shadows present; a 20k-caster generated scene reports zero
skipped passes.
Commit: `Stream shadow casters in bounded runs`

Exit criteria: scratch exhaustion degrades (bounded, counted) instead of
deleting passes; boundary tests and shrunk-scratch tests green; the
generated 50k-instance and 20k-caster scenes render with zero skipped
passes and zero allocation failures at default scratch size.

## Gate 2: Fix shadow CPU scaling

Measured problem: ~10k draws and 9.5 ms p50 CPU recording for one point
light. Target shape: draws scale with mesh/state runs times views, not with
caster count.

**2.1 Range cull before view work.** A caster outside the light's range
sphere cannot cast into any face; cull once per light before the six
per-face frustum loops. Exact and cheap; no new spatial structure.
Commit: `Cull shadow casters against the light range before view tests`

**2.2 Per-view visible lists as instanced runs.** For each view: collect
surviving casters, sort by mesh (then pipeline/front-face state), upload
only those matrices (via the Gate 1 run streaming), and emit one instanced
draw per run. Populate `ShadowCastersTested`, `ShadowCastersVisible`,
`ShadowInstanceRuns` from this path.
Verify: the synthetic one-mesh/one-material 10k-caster workload records one
or two runs per face; `ShadowCasterDraws` collapses from ~10k to runs x
views. Existing shadow correctness tests stay green; on-change and cached
tile behavior verified when casters move, unload, and change material.
Commit: `Batch shadow casters into instanced runs per view`

**2.3 Diff records only when consumed.** Build and sort
`ShadowCasterSet.Records` only when `Residency.HasOnChangeSlots()`; on the
false-to-true transition rebuild `Previous` from the current set so the
first diff after a gap does not emit spurious events.
Verify: a test that toggles on-change residency across frames and asserts no
spurious add/remove events; extraction CPU scope shows the gather cost drop
in the no-listener case.
Commit: `Skip caster diff records when no on-change slots exist`

**2.4 Coarse spatial bins: only if still needed.** If, after 2.1 to 2.3, the
shadow record p99 on this laptop still exceeds 2 ms at the 10k-caster
workload, add coarse binning (zones are the first candidate bin). Do not
build it speculatively; the CPU scopes from Gate 0 make this a measured
decision.

Exit criteria on this laptop: main pass present at 10k casters plus one
point light; shadow record p99 below 2 ms; draw count scales with runs x
views; no stale shadow tiles when casters move, unload, or change material.

## Gate 3: Bound per-pixel light cost

Measured problem: the per-fragment loop over all selected lights costs 7.5 ms
GPU on Intel at 720p with 64 lights, before production geometry.

**3.1 Read the selection scope first.** `SelectForwardLights` scores and
sorts every candidate every frame; its CPU scope (Gate 0) decides whether
that path gets touched at all. No selection changes without a number.

**3.2 Checked-in fill bench.** A committed fill-heavy scene (high screen
coverage, 16 and 64 light candidates with realistic influence radii) under
`template/assets/levels/`, wired into `bench_render_ab.sh`, following the
existing generated-scene convention (`RenderBench.Generate`).
Commit: `Add a fill-heavy light bench scene`

**3.3 Per-tile light lists, if the bench demands them.** If production-shaped
content needs more than roughly 16 to 32 overlapping lights, build
CPU-generated screen-tile light lists with a fixed per-tile cap, feeding the
existing forward shader. Tile size and cap are cvars
(`renderer.light.tile_size`, `renderer.light.max_per_tile`). One pipeline,
one shader family; the tile list is data the shader indexes. GPU-side list
generation only if profiling later proves the CPU build is the bottleneck.
Verify: bounded per-tile counts at 64 and 256 candidates; orbit captures
show no selection popping (add score hysteresis only if popping is
observed); Intel 720p fill bench p99 within the budget below.
Commit: `Feed the forward shader from per-tile light lists`

Exit criteria: the fill bench is checked in and tracked; per-pixel light
work is bounded by tile cap, not by global light count. Provisional GPU p99
budget: 12.5 ms on the Intel iGPU at 720p for the fill bench (owner sets the
real number; this placeholder is the audit's and needs sign-off).

## Gate 4: Remove hitches, harden transitions

**4.1 Pipeline prewarm.** `MeshForwardPass` and `ShadowDepthPass` expose a
prewarm entry that runs their `EnsurePipelines` paths once the swapchain
formats are known, called at the scene-load boundary instead of inside the
first `Draw`. Then measure cold start: if the driver-cache disk persistence
(`VulkanPipelineCache::LoadFromDisk`/`SaveToDisk`) still buys a measurable
win, wire it at engine startup/shutdown; if not, delete both methods. Either
way the no-call-site state ends here.
Verify: first visible frame's CPU record p99 below 2 ms in capture (currently
18 to 38 ms); cold-start measurement recorded in evidence; grep shows the
persistence API either wired or gone.
Commits: `Prewarm the forward and shadow pipelines at scene load`, then one
of `Persist the driver pipeline cache across runs` / `Delete the unwired
pipeline cache persistence`

**4.2 Upload batching.** Replace per-upload allocate/submit/wait-forever in
`VulkanBufferService` (and the image path) with: a persistent staging ring,
one submission per drain, every `vkResetFences`/`vkQueueSubmit`/
`vkWaitForFences` result checked, finite timeout with device-lost handling.
Stay on the graphics queue; a dedicated transfer queue is a later, measured
step.
Verify: streaming a zone load has no unbounded waits; failure injection on
submit/wait paths fails closed with a logged error, not a hang.
Commit: `Batch buffer uploads through a persistent staging ring`

**4.3 Swapchain recreation.** Create the replacement with `oldSwapchain`
set, retire the old one only after successful creation, and drop the second
device-idle (frame reset already waits). Creation failure keeps the old
swapchain presentable.
Verify: existing lifecycle suite green; a scripted resize/minimize/restore
loop (500 cycles if automation holds; otherwise record the manual pass and
the automation gap as owed) with zero validation errors.
Commit: `Recreate the swapchain before retiring the old one`

**4.4 Feature setup failure is explicit.** `IRenderFeature::Setup` returns
`bool`; `Renderer::AddFeatureImpl` tears down and drops a feature whose
setup fails, returning null as the contract already implies.
Verify: a test registering a feature with failing setup observes null and no
phase-bucket entry.
Commit: `Make render feature setup failure explicit`

Exit criteria: first-frame hitch gone from captures; upload and swapchain
failure paths checked and recoverable; no silent feature registration after
failed setup.

## Gate 5: Contracts and dead code

- Delete `Renderer::DrawFrame` (no callers).
  Commit: `Delete the unused Renderer::DrawFrame wrapper`
- Key `RenderExtractionSystem`'s cached query per registry (small map keyed
  by the world sentinel) so multi-registry frames stop rebuilding queries
  every frame. This restores the "cached Query objects" rule in the
  multi-zone case the current single-sentinel cache defeats.
  Verify: a two-registry extraction test asserts the query build count stays
  flat across frames.
  Commit: `Cache extraction queries per world`
- `IRenderFeature::Contribute` stays: documented game-bootstrap seam,
  classified as planned infrastructure. If the owner decides the capability
  is no longer intended, that is a separate decision, not this plan.
- Pipeline-cache persistence is resolved (wired or deleted) by Gate 4.1.

## Owed and out of scope

- Cross-hardware coverage beyond this laptop (AMD RADV, separate
  graphics/present queue families, low-VRAM devices, 2-image swapchains
  across drivers) is owed but needs hardware; tracked, not gated.
- Descriptor-capacity edge tests (sampled-image slots 1,023/1,024/1,025) and
  device-lost fault injection: fold into Gate 4 verification where the seams
  allow, otherwise record as owed.
- The audit's full "production benchmark suite" is a menu, not a gate. The
  checked-in benches from Gates 0 to 3 (textured geometry, fill lights,
  caster stress) are the tracked set; grow it when content exists to demand
  it.

## Outstanding

**Gate 4.2, the upload staging ring, is not built.** Every buffer and image
upload still allocates staging memory, submits, and waits on its fence with
no timeout, on the graphics queue. Nothing here demonstrated it as a
bottleneck (the audit called it a grounded risk, not a measured regression),
and the plan's own order puts profiling streaming hitches before the rewrite.
It needs a streaming benchmark first: a zone load/unload loop with the upload
path scoped. The unchecked `vkWaitForFences` is the part worth fixing
regardless of the batching question, since an infinite wait on a lost device
hangs the process rather than reporting.

**Gate 3.2 and 3.3 need content and a number.** The fill-heavy bench scene is
not authored, so nothing here can say whether per-tile light lists are
warranted. Blocked on the low-tier budget question below.

**Gate 2.4 was not needed and not built.** Coarse spatial binning was
conditional on the shadow record time still exceeding 2 ms after batching. No
checked-in scene reaches the caster counts that would show it, so the
condition could not be evaluated either way; the counters to evaluate it are
in place.

**The pipeline cache's disk persistence is still unwired.** `LoadFromDisk` and
`SaveToDisk` have no call sites. Prewarming moved compilation to load, so what
persistence would buy is shorter loads. Wiring it means choosing where the
cache file lives, and the engine has no user- or cache-directory convention to
follow; inventing one is an owner decision, not a mechanical cleanup. The
capability is real, so it was not deleted either. See the question below.

**Testability gaps found while working.** `VulkanFrameScratch`,
`ShadowDepthPass`, `Renderer::AddFeature`, and `StaticMeshCache` all need a
live device to construct, so their contracts cannot be tested headlessly. One
was worth fixing here: the scratch's offset arithmetic moved into
`FrameScratchRing`, which owns no memory and is fully tested. The others were
left alone rather than restructured on the way past. The shadow record
gating and the feature-setup contract therefore rest on code review and live
runs, not tests.

## Open questions for the owner

1. The Intel 720p GPU p99 budget (Gate 3 exit): 12.5 ms is the audit's
   placeholder. What is the real low-tier target for the two shipping games?
2. Is `IRenderFeature::Contribute` still an intended game-bootstrap
   capability? Neither the template nor the examples call it yet; this plan
   keeps it as planned infrastructure per the unused-code rules, but the
   classification deserves an explicit yes.
3. Is AMD (RADV) hardware available anywhere for the owed cross-hardware
   column, or should that stay parked until it is?
4. Where should a driver pipeline cache file live? That is the only thing
   blocking the unwired persistence API from being wired (or, if the answer
   is that the driver's own on-disk cache is enough on every target, from
   being deleted).
5. Should `EngineGraphicsConfig::EnableValidation` still default to true? It
   costs 4.8x on CPU render recording and is on in every build unless a run
   opts out, which is how the audit came to measure a non-shipping
   configuration. Flipping the default is a one-line change but it changes
   what a developer gets by default, so it is not made here.
