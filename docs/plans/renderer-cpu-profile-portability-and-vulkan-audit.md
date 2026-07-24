# Renderer CPU Profile, Windows Portability, and Vulkan Audit

Status: proposed, 2026-07-23. Nothing here is built.

Scope: the render layer only (`engine/src/render`, `engine/src/graphics`,
`engine/src/profiling`, and the SDL WSI glue in `engine/src/platform`).
Out of scope by owner direction: cross-platform work outside the renderer,
a second RHI, macOS, and parallel command recording.

Three parts. Part 1 takes the CPU-side picture of the render path down to
hardware counters (cycles, cache misses, allocations) and applies only the
changes the numbers justify. Part 2 bounds the Windows risk of the renderer
from this Fedora machine, without pretending it can measure Windows
performance from here. Part 3 audits the Vulkan services and frame pipeline
for spec correctness, with a hard rule that no verdict lands without a
mechanical instrument behind it.

This plan continues from `renderer-hardening.md` (Gates 0-2 and 5 landed,
Gate 3 stopped at measurement, Gate 4 landed three of four). It does not
absorb that plan's owed items (upload staging ring, fill bench, pipeline
cache persistence); those stay tracked there.

## The verdict that shapes Part 1

A review pass over the render CPU path (2026-07-23: extraction, queue, both
passes, feature/phase plumbing, service wiring) found no architectural bloat
to remove. The draw path is flat: Renderer walks two phase buckets, each
feature records through its pass, pipeline selection is an array index, and
the only virtual dispatch is two `OnDraw` calls per frame at a documented
game-binary boundary. The risks it found are data movement and algorithmic
scaling, not indirection.

So "reduce architectural bloat" currently resolves to: do not add any. The
deletion list is empty until a measurement fills it.

Reviewed and cleared, no measurement can justify churn here:

- `IRenderFeature` and the phase buckets: two virtual calls per frame at a
  real extension boundary (`Renderer.h:113-141`).
- `RendererServices`: a setup-time wiring bundle; features cache their own
  pointers, nothing touches it per frame.
- The profiling plumbing: `CpuScopeTimer` is a null-sink no-op below
  Counters mode, and the `SENCHA_ENABLE_RENDER_PROFILING` off path has an
  object-file A/B proving it costs nothing. Do not re-audit it.

## Ground rules

1. Numbers come from optimized builds. All renderer-hardening evidence was
   taken on Debug; ratios held there, but cycle and cache numbers from
   Debug builds lie. Part 1 blocks on the profile preset (P0.2).
2. Every number comes from the deterministic bench harness
   (`scripts/bench_render_ab.sh`: scripted camera, foreground,
   `SENCHA_PRESENT_MODE=IMMEDIATE`), percentiles only, warmup excluded.
3. Every structural change lands with its A/B delta filed under
   `docs/plans/evidence/renderer-cpu-profile/`, or it reverts. "The
   measurement said so" is the only justification this plan accepts.
4. The roughly 1 ms chunk-parallel gate stands. Nothing in the render CPU
   path is near it today; the serial path stays serial.
5. Tool correction, on the record: gdb does not profile cache misses; it is
   a debugger. Hardware cache counters are read through perf
   (`perf_event_open`). Deterministic per-line miss attribution comes from
   valgrind's cachegrind. gdb stays in the kit for debugging whatever the
   profilers find, not for producing the numbers.

## Part 1: CPU profile of the render path

### P0: Toolkit and build substrate

**P0.1 Install the tools.** This machine currently has none of the
profilers. Owner action (needs sudo):

```
sudo dnf install perf valgrind clang-tools-extra cppcheck heaptrack \
    hotspot mingw64-gcc-c++ spirv-tools spirv-cross
```

The last two serve Part 3's reflection cross-checks, not Part 1.

`kernel.perf_event_paranoid` is 2, which permits per-process user-space
counting (`:u` suffixed events) without sudo. That is all this plan needs;
no sysctl change unless `perf c2c` is ever wanted, and the render path is
single-threaded so it is not expected to be.

**P0.2 Profile preset.** Add a `profile` configure preset: release
optimization plus `-g -fno-omit-frame-pointer`, `SENCHA_ENABLE_RENDER_PROFILING`
ON, debug UI off. Requirement, not mechanism: codegen must match `release`
except for symbols and frame pointers, so perf attribution maps to shipping
code shape. Validation stays off for bench runs (already config).

**P0.3 Pin the CPU.** This is a Raptor Lake hybrid part. Bench and profile
runs get pinned to P-cores (`taskset`) so run-to-run variance and per-core
PMU differences do not smear the numbers. The pinning goes into
`bench_render_ab.sh` behind a flag so A/B pairs share it.

**P0.4 Close the scope blind spot.** `ShadowResidency::Update` and
`ApplyGrants` run every frame (`DefaultRenderPipeline.cpp:280-282`) outside
any `CpuScope` bracket. Add a scope so the coarse map is complete before
anything is measured against it.

**P0.5 Reconcile the query-cache state.** `renderer-hardening.md` Gate 5
records a per-registry extraction query cache as landed; the header still
shows a single `CachedQuery` keyed on one `LastWorld` sentinel
(`RenderExtractionSystem.h:45-46`). Establish which is true before P2
measures the multi-registry workload, and correct whichever artifact is
stale (the code or the doc).

Exit: tools present, profile preset builds, all five plus one CPU scopes
report, bench pinned and green on the profile preset.

### P1: Workloads that scale on the axes under test

The bench scenes must have knobs on the axes the hypotheses scale with.
Follow the existing generated-scene convention (`RenderBench.Generate`,
`scripts/gen_light_stress_scene.py`).

- **Forward-scale scene**: instance count swept (1k, 5k, 20k, 50k), one
  material family, no shadow lights. Exercises extraction, sort, instance
  stream, `DrawRuns`.
- **Shadow-scale scene**: caster count x shadow-casting light count swept
  (spot and point mixed, so both the per-view and per-cube-face paths run).
  Exercises `ShadowGather`/`ShadowRecord`.
- **Streaming scene**: N active registries (multi-zone), moderate content
  each. Exercises per-registry extraction and the query cache under the
  case the single-registry numbers never covered.
- The existing bench map stays as the fixed regression anchor.

Exit: three parameterized scenes checked in and runnable through
`bench_render_ab.sh` with a knob argument; each run self-identifies its
knob values in the capture metadata.

### P2: Cycle attribution (where the time goes)

`perf record --call-graph dwarf -F 999` over the bench per scene per knob
point, on the profile preset. Reduce to: per `CpuScope` phase, the top
self-time symbols, and how each phase scales against its knob.

Hypotheses this either confirms or kills, in predicted order of movement:

- **H-A**: `ShadowRecord` grows with (shadow views x casters);
  `GatherVisibleCasters` (`ShadowDepthPass.cpp:195-229`) re-walks every
  caster per spot view and per point face, with a `meshes.Get()` per caster
  per view. Predicted to dominate first as scenes scale.
- **H-E**: with N registries, query rebuild cost
  (`RebuildMatchingArchetypes`) appears in `Extraction` at N times per
  frame (pending P0.5; the header's "cheap" claim was measured
  single-registry).
- **H-C**: `MeshFrameUniforms` zero-init plus 5712-byte copy
  (`MeshForwardPass.cpp:296,356`) is measurable but negligible. Measure it
  precisely once, quote the number, close it permanently.

Exit: a table (scene x knob x scope -> ms and top symbols) in the evidence
dir, with each hypothesis marked confirmed or dead.

### P3: Cache attribution (why the time goes there)

Two instruments, cross-checked:

- `perf stat` per bench run: `cycles:u, instructions:u` (IPC),
  `L1-dcache-load-misses:u`, `LLC-load-misses:u`, `branch-misses:u`.
  The macro picture: is the hot phase compute-bound or miss-bound?
- cachegrind on a short deterministic run (`app.exit_after_frames 120`;
  it runs 20-100x slow, so short and deterministic is the whole point),
  then `cg_annotate` on the files P2 implicated. Per-line miss counts that
  sampling cannot give.

Hypotheses:

- **H-B**: misses concentrate in `AssetCache::Resolve`
  (`AssetCache.h:222-231`) pulling fat entries (`GpuStaticMesh` plus a
  `std::string` path key plus refcount) into cache to read two buffer
  handles: per caster per view in shadow gather, per run in `DrawRuns`,
  per caster in shadow extraction.
- **H-F**: shadow caster extraction
  (`ShadowCasterExtractionSystem.cpp:127-134`) shows more misses per entity
  than forward extraction, because it does a per-entity
  `TryGet<WorldTransform>` pointer-chase where the forward path co-iterates
  chunk columns. Two strategies over the same data; one is cache-friendly.
- **H-G**: at high instance counts, the instance-stream gather
  (`MeshForwardPass.cpp:374-380`) and run-merge walk scatter-read 152-byte
  `RenderQueueItem`s through the sort permutation; the item carries roughly
  28 bytes dead after extraction (`WorldBounds`, `CameraDepth`).

Exit: annotated miss profiles filed; each hypothesis confirmed or dead with
per-line evidence.

### P4: Allocation audit

Claim to prove or break: after warmup, a steady-state frame performs zero
heap allocations in the render path. The transient vectors are reused by
design (`RenderQueue` scratch, `ShadowResidency`), but that is an
assumption, and `AddOpaque` push_backs with no reserve while streaming can
spike caster counts past retained capacity.

- heaptrack over a long bench run: flat allocation timeline after warmup,
  or the offender is named.
- An allocation counter compiled into the bench app (global operator
  new/delete tally, cvar-armed after warmup) asserting per-frame delta zero
  across the run, including across a zone stream event in the streaming
  scene.

Exit: either a filed proof of zero steady-state allocations, or the named
offender and its fix (a reserve to measured high water, kept only if the
A/B shows the realloc mattered).

### P5: Static analysis (code quality, not bloat-hunting)

The review already answered the architecture question; this stage is
mechanical defect and pessimization sweeping over
`engine/{src,include}/{render,graphics,profiling}` only.

- Add a curated `.clang-tidy` (there is none today):
  `bugprone-*`, `performance-*`, `clang-analyzer-*`, narrowing
  conversions; explicitly not the style families, no churn checks.
  `compile_commands.json` is already exported everywhere.
- `cppcheck` as the second opinion (different engine, catches different
  things).
- Findings are a candidate list, not a work order: each surviving item
  needs either a correctness argument or a bench delta to land, same bar
  as everything else.

Exit: `.clang-tidy` checked in, both tools clean or waived-with-reason on
the render layer.

### P6: The change list, gated by numbers

Candidate changes, each bound to the measurement that arms it. None land
without their gate firing, and each lands with its A/B delta.

| Candidate | Armed by | Shape of the change |
|---|---|---|
| Shadow extraction moves to chunk co-iteration (same cached-query shape as forward extraction) | H-F | Replace `ForEachComponent` + per-entity `TryGet` with `Query<Read<WorldTransform>, Read<StaticMeshComponent>>` |
| Share per-entity `{worldMatrix, worldBounds}` between forward and shadow extraction | P2 shows the duplicate `ToMat4` + 8-corner transform cost is real | One extracted transform product feeds both queues |
| `AssetCache` hot/cold split | H-B | Hot array of GPU handles indexed by slot; path key and refcount move to a cold sibling array |
| `RenderQueueItem` slimming or SoA split | H-G | Drop dead-after-extraction fields; if still hot, split a narrow draw-identity column and an 80-byte `{World, LightmapScaleBias}` column matching `MeshInstanceData` |
| Reserve queue/caster vectors to measured high water | P4 | `reserve` once per scene shape, no per-frame effect |
| Shadow gather restructuring beyond the above (share culling work across views) | H-A survives the cheaper fixes | Design against the numbers; not specified in advance |
| Per-registry query cache (if P0.5 finds the doc stale) | H-E | Small map keyed by world sentinel, flat build count asserted by a two-registry test |

Explicitly not on the list: any parallelization (gate rule 4), any change
to the feature/service plumbing (cleared list), tile light lists and upload
batching (owned by `renderer-hardening.md`).

Exit criteria for Part 1: every hypothesis has a number and a verdict;
every landed change has its delta in the evidence dir; every dismissed
suspicion has the number that dismissed it, so it is never re-litigated.

## Part 2: Windows portability of the renderer, from Fedora

Honest framing first. From this machine we can establish: the renderer
compiles and links for Windows, boots and renders correctly under Win32
semantics, and makes no device-capability assumption a Windows driver
would break. We cannot establish real Windows performance: driver compile
times, present pacing, fullscreen interactions, scheduler behavior. Those
need one real Windows capture run eventually; the harness already makes
that a ten-minute task on any borrowed machine.

The starting position is strong, and the survey confirms the roadmap's
framing (Track F: "the work is toolchain, filesystem and process details"):

- WSI is SDL3 end to end (`SDL_Vulkan_CreateSurface`,
  `SDL_Vulkan_GetInstanceExtensions`); zero xcb/wayland/win32 code in the
  render layer, zero platform `#ifdef`s in render/graphics/WSI code.
- Timing is `std::chrono::steady_clock` only; no POSIX calls, no `dlopen`,
  no threads, no bare `long`, no file IO in the render layer.
- Shaders are offline SPIR-V embedded at build time; shipping builds carry
  no compiler.
- The build already carries MSVC-aware warning flags, a MinGW static
  runtime stanza, `winmm` linkage, and `__declspec` handling for game
  modules.

### W0: Static portability audit (immediate, no new tools)

A checklist sweep, filed as an evidence table with one verdict per row:

- `#pragma GCC diagnostic` around the VMA include
  (`VulkanMemoryAllocatorImpl.cpp:7-21`): needs the matching
  `#pragma warning(push/disable)` pair for MSVC. The one known concrete
  code defect for Windows in the render layer.
- `sencha_engine` is a SHARED library. MinGW auto-exports; MSVC exports
  nothing without annotations. Decide the story when the CI leg (W4)
  surfaces it: `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` as the bring-up bridge,
  a proper export macro as the real fix if data symbols or the symbol cap
  bite. Flagged now so the first MSVC link failure is expected, not
  investigated.
- Confirm the depth-format and present-mode fallback chains have no
  Linux-driver assumptions baked in (feeds W3's profile runs).
- Re-verify the survey's clean findings on the final tree (no `long`, no
  POSIX, no platform ifdefs) with a scripted grep, so the audit is
  reproducible rather than a one-time claim.

### W1: MinGW cross-compile and link

Deliverable: `sencha_engine.dll` plus `SceneViewer.exe` built on this
machine by `x86_64-w64-mingw32-g++`, via a toolchain file and a
`windows-mingw` preset. Every compile or link error found here is a
portability bug found without owning a Windows box.

The fiddly part is dependencies, known routes in order of preference:

- SDL3: Fedora `mingw64-SDL3` if packaged, else the official SDL3 Windows
  devel archive (ships a MinGW tree).
- Vulkan: `mingw64-vulkan-headers` plus an import library for
  `vulkan-1.dll` (Fedora `mingw64-vulkan-loader` if packaged, else
  `gendef` + `dlltool` against a Windows `vulkan-1.dll`).
- Shader compilation runs on the host: point `Vulkan_GLSLC_EXECUTABLE` at
  the native glslc in the toolchain file (cross target, host tool).

Cook, editor, and hot-reload stay OFF in this preset; this is the runtime
renderer only, per the branch scope.

### W2: Runtime smoke under Wine

Run the W1 `SceneViewer.exe` through the deterministic bench under Wine.
Wine's `vulkan-1.dll` maps to the host ICD, so this exercises the real
Win32-flavored code paths: SDL3's Windows video driver, `LoadLibrary`
module loading (`GameModuleLoader.cpp`), the `timeBeginPeriod` pacer
branch, the Windows log-sink path.

Pass condition is correctness, not speed: the run boots, renders, exits
clean, and the deterministic counters in the capture (draw calls,
triangles, instances, shadow casters per frame) are identical to the
native Linux capture of the same scripted run. Frame times from Wine are
recorded but carry no authority.

### W3: Device capability envelope (the "would a Windows driver break us" test)

Two instruments, both runnable today:

- **Second vendor, real driver**: this laptop's Intel iGPU through Mesa
  ANV, selected by the `DeviceIndex` graphics config that Gate 0.6 already
  built. Full bench, validation on, zero `[Vulkan]` lines. Catches
  NVIDIA-shaped assumptions directly.
- **Simulated Windows devices**: `VK_LAYER_KHRONOS_profiles` (LunarG SDK)
  with profiles built from vulkan.gpuinfo.org entries for representative
  Windows configurations (NVIDIA, AMD, Intel). Specific things to break on
  purpose: run with `VK_KHR_present_id`/`present_wait` masked (the code
  treats them as optional; prove the fallback path actually runs), tighter
  `maxPushConstantsSize` (128 is the floor; `MeshPushConstants` is 80,
  should pass), depth-format table variations, small-BAR memory heap
  shapes, and `minImageCount` variations against the 2-image pacing
  assumption.

Exit: bench green on ANV; validation-clean runs against three
Windows-representative profiles; every capability the renderer requires
listed with where it is checked at runtime.

### W4: Windows CI leg (the MSVC truth)

The repo has a GitHub origin and no CI. A `windows-latest` workflow
(MSVC + Vulkan SDK + SDL3, build all targets, run the non-graphical test
suite; the `ci` preset's own notes say no GPU is needed for that) is the
only way to get real MSVC semantics from this environment, and it is the
gate the roadmap's Track F item 1 already names. Authoring and pushing the
workflow is fully doable from here; the first run will surface the
`sencha_engine` export story (expected, per W0).

Local alternative considered and parked: clang-cl exists on this box but
needs the MSVC STL and Windows SDK headers (via xwin) to do anything;
that adds a Microsoft-redistributable download with licensing terms the
owner should opt into, and CI gives the genuine article anyway. Only worth
revisiting if CI is declined.

### W5: The report

One document in the evidence dir: the W0 audit table, W1 build log
verdicts, W2 counter-identity proof, W3 capability envelope, W4 CI status,
and the explicit residual-unknowns list (real-driver performance, present
pacing, fullscreen behavior). That is the honest answer to "how well would
it run on Windows": everything knowable from here, known; everything not
knowable from here, named.

## Part 3: Vulkan correctness audit

Why this is its own part: Vulkan setup errors are silent until a driver
change. Code that is spec-invalid can run clean for years on one vendor's
driver (wrong semaphore reuse, a missing flush on non-coherent memory, a
feature used but never enabled) and then fail on the next driver, the next
vendor, or the Windows build Part 2 is preparing for. A prose review is
exactly where these details get glossed, by models and by humans. So the
audit's rule: **no verdict without an instrument.** Every audit row ends in
one of three things attached to a specific file and line: a clean run of a
named mechanical checker, a test, or a spec citation (VUID number). "Looks
correct" is not a verdict; a row without an instrument stays open.

The house pattern from `renderer-hardening.md` applies: findings are
re-verified against the tree before adoption, and rejected findings are
recorded with the reason so they are never re-litigated.

### Seed observations (verified while scoping, 2026-07-23)

The audit starts from these facts, not from zero:

- **Fine-grained validation is not wired anywhere.** A repo-wide search
  finds no `VkValidationFeaturesEXT`, no sync-validation, GPU-assisted, or
  best-practices configuration. Today's "validation on" is the layer's
  default core checks only, which do not catch most synchronization
  hazards or out-of-bounds descriptor access. V0 closes this coverage gap
  without code changes.
- **The frame sync scheme has the right shape at the type level**:
  per-frame `ImageAvailable` semaphores, per-swapchain-image
  `RenderFinished` semaphores, and per-image in-flight fence tracking
  (`VulkanFrameService.h:88-109`). This is the scheme that avoids the
  classic present-semaphore reuse hazard. The audit verifies the pairing
  and the recreation reset paths, not the shape.
- **VkResult discipline in the frame path is real**: acquire, present,
  submit, fence, and pool-reset results are checked with explicit
  `DEVICE_LOST` branches throughout `VulkanFrameService.cpp`. The known
  unchecked results live in the upload path (hardening Gate 4.2, still
  outstanding there).
- **Flush discipline exists on the upload paths** (`vmaFlushAllocation` at
  `VulkanBufferService.cpp:251,285`), but the per-frame scratch ring's
  coherency contract is not visible in `VulkanFrameScratch.cpp` at all
  (its allocation lives elsewhere). Whether scratch writes are flushed
  before submit, or guaranteed coherent by allocation flags, is the first
  open row of the audit.
- **Device creation chains Vulkan 1.2 and 1.3 feature structs** plus
  optional present-id/present-wait (`VulkanDeviceService.cpp:67-127`), and
  shaders compile with `--target-env=vulkan1.3`. The enabled-versus-used
  cross-check (V1 class 1) is therefore live, not hypothetical.
- **The present queue is a distinct concept** (`Queues.GetPresentQueue()`)
  but no code path surfaced for a device whose present family differs from
  graphics, and hardening lists that case as owed. Swapchain sharing mode
  and queue family indices are an audit row, not an assumption.

### V0: Raise the mechanical floor (no code changes)

Run the bench scene, the resize/minimize loop, and an editor session under
each of the following, on both GPUs (NVIDIA and ANV via the `DeviceIndex`
config):

- **Synchronization validation** (`khronos_validation.validate_sync`):
  catches the hazard class the default checks miss entirely: missing
  barriers, write-after-write on attachments, image layout races.
- **GPU-assisted validation** (short runs; it is slow): out-of-bounds and
  uninitialized descriptor access.
- **Best-practices validation** with the vendor bits on: advisory output,
  triaged rather than obeyed.

Mechanism: layer settings file or environment (`VK_LAYER_SETTINGS_PATH`),
wired into `bench_render_ab.sh` behind a flag so runs are reproducible.
Whether sync validation later becomes an `EngineGraphicsConfig` field is an
owner call after the first runs (a config field, not a seam).

Gate: zero errors from sync validation and GPU-assisted runs on both
devices. Best-practices findings triaged into the V3 ledger.

### V1: Service-by-service checklist audit

The deep read: all Vulkan services in `GraphicsServices` construction
order, plus the Vulkan usage inside the two passes and the per-image
layout tracking in `Renderer`. Organized by hazard class, each with the
named killer details that reviews gloss:

1. **Instance, device, features.** Every extension used is gated on its
   query; every feature used by code or shaders is enabled in the pNext
   chain. Mechanical check: reflect all embedded SPIR-V (spirv-cross or
   spirv-reflect), list required capabilities and extensions, diff against
   the enabled 1.2/1.3 feature structs and `policy.DeviceFeatures`. API
   version consistency: instance version, device version, `--target-env`.
2. **Queues and WSI.** Either a working path for present-family differing
   from graphics, or an explicit rejection at device selection with a log
   line (both are valid verdicts; silent assumption is not). Swapchain
   `imageSharingMode` and family indices. The acquire and present result
   matrix (`OUT_OF_DATE`/`SUBOPTIMAL` on both ends). Recreation ordering
   against in-flight frames (the `oldSwapchain` retire timing that just
   landed). Zero-extent and minimize. Surface format and colorspace
   selection policy. Pretransform and composite alpha.
3. **Synchronization.** Wait and signal semaphore pairing across acquire,
   submit, present. Per-image fence tracking on image reuse. Submission
   stage masks. The per-swapchain-image layout tracking against actual
   recorded transitions. Depth target transitions. Upload-to-graphics
   visibility (staging copy to first use).
4. **Memory.** The coherency/flush rule on every persistently mapped
   allocation, scratch ring first (see seed observation). Alignment
   sources (`minUniformBufferOffsetAlignment` and friends) against
   `FrameScratchRing` granularity. VMA usage and flags at every allocation
   site. Staging lifetime against its fence.
5. **Pipelines and descriptors.** Descriptor set layouts against SPIR-V
   bindings, per pipeline, as a mechanical reflection diff, not a read.
   Pool sizing against worst case (absorbs hardening's owed
   1,023/1,024/1,025 edges). Dynamic state completeness. Attachment
   formats against the swapchain and depth formats actually chosen at
   runtime.
6. **Lifetime.** Deletion queue delay is at least frames-in-flight for
   every resource class routed through it; anything destroyed outside the
   queue carries a justification; swapchain-dependent resources across
   recreation.
7. **VkResult discipline.** Sweep every `vk*` call site: checked, or on a
   recorded waiver list with the reason. Seed finding: the upload fence
   wait (hardening Gate 4.2).

Verdict discipline: each row records file:line, the instrument, and
pass, fail, or open. The read runs as independent narrow-lens passes (one
hazard class per reviewer) with findings verified against the spec text
before adoption, mirroring how the hardening plan treated its external
audit. This stage is shaped for a multi-agent review workflow; it runs
that way on owner request, otherwise as sequential single-lens passes.

### V2: Negative and stress runs

- The scripted resize/minimize/restore loop under sync validation
  (hardening 4.3 named 500 cycles; automate if the harness allows).
- The 2-image swapchain assumption on ANV versus NVIDIA (different
  `minImageCount` and acquire blocking behavior).
- Descriptor capacity edges (absorbs the hardening owed item).
- Scratch exhaustion and partial-grant paths under sync validation.
- Device-lost fault injection where seams allow; where they do not, the
  gap is recorded, not papered over (absorbs the hardening owed item).

### V3: Adoption ledger

Findings triage into: fix now (correctness), fix with its test, or waived
with reason. Every fix carries a validation gate or a test. Rejected
findings are recorded with reasons. Output:
`docs/plans/evidence/vulkan-audit/` holding the full row table and the
ledger. Fixes land through the same bench A/B discipline as Part 1 where
they touch the frame path.

Exit criteria for Part 3: every audit row closed or explicitly open with
what closing it requires; sync validation and GPU-assisted runs clean on
both local devices; the reflection diffs (features, bindings) automated
enough to rerun after any shader or pipeline change.

## Sequencing

- **P0 first**; it is half a session and everything in Part 1 depends on
  it. P0.1 needs the owner (sudo).
- **P1 -> P2 -> P3/P4**: P2's cycle profile decides where P3 aims
  cachegrind. P4 is independent after P1. P5 any time. P6 rolls as gates
  fire.
- **W0 immediately** (no dependencies, hours). **W4 early**: cheap to
  author, longest feedback loop, biggest unknown (MSVC). **W1 -> W2**
  as one arc. **W3** any time after P0.3 (shares the bench).
- **V0 immediately**: no code changes, and its clean-run gate is a
  precondition worth having before Part 1 changes any frame-path code.
  It also pairs naturally with W3 (same layer machinery, same two-GPU
  runs). **V1** is the big read and interleaves with anything. **V2**
  after V0. Audit fixes (V3) ride the same A/B discipline as P6.
- Parts 1, 2, and 3 do not block each other, with one ordering worth
  honoring: V0's sync-validation baseline before P6 lands frame-path
  changes, so hazards introduced by a change are distinguishable from
  hazards that were always there.

## Open questions for the owner

1. P0.1 needs sudo for the tool install; run the dnf line above, or say
   so and it gets scripted for you to approve.
2. Is enabling GitHub Actions on the repo acceptable (W4)? Track F gates
   Windows on a CI leg regardless; this just starts it early.
3. What is the CPU budget that arms "needs work" in Part 1? Proposal:
   total render-side CPU (extract through record, sum of the six scopes)
   p99 under 2 ms on this laptop at the P1 bench scenes, mirroring the
   hardening plan's shadow budget shape. Placeholder until you set it.
4. If CI is declined: is xwin (Microsoft CRT/SDK download for clang-cl)
   acceptable, or does MSVC truth wait for real Windows hardware?
5. Should V1 run as a multi-agent review (one narrow hazard-class lens per
   reviewer, findings adversarially verified against the spec before
   adoption)? It is the stage that most rewards independent lenses, and it
   is also the most expensive; the default without an answer is sequential
   single-lens passes.
6. After V0's first clean runs: should sync validation become an
   `EngineGraphicsConfig` field (default off, on in bench and CI), or stay
   an environment-driven developer tool? Related to the still-open
   hardening question 5 about the validation default, since both decide
   what a developer gets by default.
