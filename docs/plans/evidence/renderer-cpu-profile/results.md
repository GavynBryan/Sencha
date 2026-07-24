# Renderer CPU Profile, Portability, and Vulkan Audit: Results

Run 2026-07-23 on the development laptop (Raptor Lake-P, RTX 4060 Laptop,
Intel RPL-P iGPU, Fedora 43, Mesa 25.x, NVIDIA 1.4.312, validation layer
1.4.341). Executes
[`renderer-cpu-profile-portability-and-vulkan-audit.md`](../../renderer-cpu-profile-portability-and-vulkan-audit.md).

## What the tooling situation forced

The plan opened with a `dnf install` line. `sudo` requires a password here,
so **none of perf, valgrind, heaptrack, clang-tidy, cppcheck, mingw, or
spirv-tools were available**, and the stages that depend on them did not
run: P2 cycle attribution, P3 cache attribution, P4 allocation audit, P5
static analysis, W1 MinGW cross-compile, W2 Wine smoke.

What was available (gdb, clang 21, glslc, wine, python3, three Vulkan
drivers, a display) was used instead, and the substitutions are named in
each section rather than glossed as equivalent.

## The most important result: the sync validation instrument does not work here

The plan's V0 rests on synchronization validation, and the plan's own rule
is that no verdict lands without an instrument. That rule is what caught
this.

An initial 300-frame run with `SENCHA_VALIDATE_SYNC=1` reported zero
hazards, which reads as a strong correctness result. It is not one. A
negative control was run to check the instrument: the shadow atlas
read-back barrier was deleted outright, which leaves the forward pass
sampling the atlas with no barrier and in the wrong layout. Core validation
immediately reported the layout violations
(`VUID-vkCmdDrawIndexed-imageLayout-00344`, ten
`VUID-vkCmdDraw-None-09600`), proving messages reach the debug messenger.
**Sync validation reported nothing, on the same run.**

Three enabling paths were tried against that deliberately broken build:
`VkValidationFeaturesEXT` at instance creation (confirmed by the layer's
own "Validation feature enabled: synchronization" log line), a
`vk_layer_settings.txt` with `validate_sync` plus
`syncval_submit_time_validation`, and the
`VK_LAYER_SETTING_khronos_validation_validate_sync` environment variable.
None produced a `SYNC-HAZARD` line. The Fedora
`vulkan-validation-layers-1.4.341.0-2.fc43` binary does contain the
`SYNC-HAZARD-*` message strings, so the capability is compiled in and
something about this build or its configuration keeps it from reporting.

Consequences, stated plainly:

- **No synchronization claim in this document rests on syncval.** The
  earlier clean runs are evidence that core, GPU-assisted, and
  best-practices checks pass, and nothing more.
- GPU-assisted validation and best-practices validation **do** work here
  (best practices produced a real finding, below), so V0 is not a total
  loss.
- Closing this needs a validation layer built from the Khronos SDK rather
  than the distribution package, on a machine with sudo.

## Findings and fixes

### Fixed: host-visible buffers were not required to be coherent

`VulkanBufferService` allocated host-visible memory with
`HOST_ACCESS_SEQUENTIAL_WRITE | MAPPED` and no `requiredFlags`. The frame
scratch writes through that mapped pointer every frame and submits without
flushing, and `VulkanFrameScratch.h` justified the missing flush by naming
that VMA flag. The justification does not hold: `SEQUENTIAL_WRITE` asks for
write-combine-friendly memory and says nothing about coherency. VMA's
selection for that flag combination requires only `HOST_VISIBLE` and merely
de-prefers `HOST_CACHED`, so a driver exposing a non-coherent host-visible
type could satisfy it, and every per-frame uniform and instance stream
would reach the GPU stale.

Not observed, and not observable here: all three local drivers return
coherent memory. It is the exact shape of a defect that ships working on
the development machine and fails on other hardware. Fixed by requiring
`VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`, which the spec guarantees exists;
the stale comment was corrected. Verified: clean runs on all three drivers
after the change, including the Intel iGPU's different memory model.

Both audit passes reached this independently.

### Fixed: the depth barrier's first synchronization scope was empty

One `VulkanDepthTarget` serves every frame in flight, and
`VulkanFrameService::BeginFrame` waits on the current slot's fence, which
at two frames in flight is the frame two slots back. Consecutive frames can
therefore write the depth image concurrently, and the per-frame barrier in
`Renderer::RecordMainColorPhase` is what was supposed to order them. It
named `VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT` as its source stage with a zero
source access mask. TOP_OF_PIPE names no stage, so the first scope was
empty and the barrier ordered nothing. In steady state the layouts also
match, so it was not even performing a transition.

The shadow atlas barriers in `LightBindings` already carry a real source
scope for the identical problem shape; depth was the inconsistent one.
Fixed to name the depth attachment write stages and access.

**Evidence status: reasoning, not instrument.** The confirming instrument
is syncval, which does not work here. The change is grounded in the spec
text on TOP_OF_PIPE, the shared-image and fence-cadence structure, and the
inconsistency with the working barriers in the same renderer. It is cheap
and cannot be incorrect in the weaker direction. Suite green and all three
drivers clean after it, which shows it broke nothing but does not confirm
the original hazard.

### Fixed: a fresh shadow atlas was sampleable with undefined contents

Best-practices validation flagged `ParkDepthImage` transitioning
`UNDEFINED -> DEPTH_STENCIL_READ_ONLY_OPTIMAL`. Legal, but it parks a newly
created atlas or cube slice in a sampled layout holding whatever the
allocator returned, and frames that render no shadow view sample it. The
dummy shadow images in the same file already clear to the far plane; the
real ones now match. This also removes a source of driver-dependent
rendering, since recycled memory differs between drivers.

### Not reproduced: the shared frame-UBO descriptor range

Both audits flagged that `SetFrameUniformBuffer` is last-writer-wins:
`ShadowDepthPass::Setup` writes `range = sizeof(Mat4)` (64) and
`MeshForwardPass::Setup` writes `range = sizeof(MeshFrameUniforms)` (5712)
into the same descriptor, with feature registration order deciding the
winner (5712 today). The shadow pass then binds that descriptor with a
64-byte allocation's dynamic offset, and
`VUID-vkCmdBindDescriptorSets-pDescriptorSets-01979` requires
`offset + range <= buffer size`.

Attempted repro: `SENCHA_FRAME_SCRATCH_BYTES` at 65536, 16384, and 8192 on
`bench_stress` with validation on. No VUID at any size. The reason appears
structural: the shadow pass runs in the Offscreen phase before the forward
pass, so its allocations sit near the *start* of a slice, not near the end
where the arithmetic bites.

Left unfixed deliberately. The reachable-in-principle claim is unproven,
and the natural fix (reserving max-range headroom in the allocator) is more
invasive than a finding of this confidence justifies. What remains true
regardless, and is worth an owner decision: **one descriptor's range is
decided by the order two features happen to register**, and nothing at
either write site says so. If the two `AddFeature` calls were ever swapped,
every forward frame would bind a 5712-byte uniform block through a 64-byte
range.

### Corrected: a stale claim in the hardening plan

`renderer-hardening.md` listed "cache extraction queries per world" under
Gate 5, and the status line reported Gate 5 as landed. The code never took
that change: `RenderExtractionSystem` still holds one `CachedQuery` behind
a single `LastWorld` sentinel, and its header explains why (0.038 ms for
the whole walk, and a map keyed on world addresses can alias after a
streamed-out zone is freed). The plan was corrected to record the rejection
and its reason. Hypothesis H-E therefore stays live and unmeasured.

## Measurements

### Extraction scales linearly with caster count; recording does not move

Caster count could not be varied through authored brushes: the document
cook merges brush geometry into one mesh per partition cell, so 406
authored brushes cooked to 9 mesh entities. Scenes were instead built by
cloning a cooked mesh entity (`scripts/gen_caster_scale_scene.py`),
following the repository's existing hand-edited-cooked-level practice.

Profile build (Release codegen with symbols), pinned to the performance
cores, `IMMEDIATE` present mode, 400 frames, first 100 dropped, p50 in
milliseconds. Full table in [`caster_scale.csv`](caster_scale.csv).

| scene | Extract/Meshes | Extract/ShadowCasters | Record/ForwardOpaque |
|---|---|---|---|
| 100 casters, 1 shadow light | 0.0076 | 0.0101 | 0.0019 |
| 400 casters, 1 shadow light | 0.0261 | 0.0377 | 0.0024 |
| 1600 casters, 1 shadow light | 0.1057 | 0.1442 | 0.0035 |
| 400 casters, 2 shadow lights | 0.0256 | 0.0375 | 0.0023 |
| 400 casters, 4 shadow lights | 0.0270 | 0.0380 | 0.0026 |

Reading:

- Both extraction scopes are linear in caster count: 16x the casters costs
  13.9x (meshes) and 14.3x (shadow casters). No superlinear term.
- **Shadow caster extraction costs consistently ~1.4x mesh extraction over
  the same entity set.** That ratio is stable across all three counts and
  is the measured shape of hypothesis H-F: the shadow extractor does a
  per-entity `TryGet<WorldTransform>` where the forward extractor
  co-iterates chunk columns. This is now a number rather than a suspicion,
  though attributing the gap specifically to cache behavior needs the
  cachegrind pass that could not run.
- Shadow light count does not move any scope (0.0375 to 0.0380 across 1, 2,
  and 4 lights).
- Total render-side CPU at 1600 casters is roughly 0.26 ms p50, well inside
  the 2 ms placeholder budget.

**H-A is not confirmed and not refuted.** The clones render (extraction
walks them and their cost scales) but produce no draws and no shadow
casters: `draw_calls` stays at 6 and `shadow_casters_tested` at 0 in every
scene. The cloned entities reference cell meshes whose geometry is
authored in world space, so relocating the entity does not relocate the
geometry, and the clones fall outside the view. The scale scenes therefore
exercise the extraction path only, and `Record/ShadowViews` stays at
~0.0001 ms throughout because no shadow work runs. Measuring shadow
*recording* against caster count needs scenes whose casters are actually
drawn, which means authoring placed mesh entities against a unit mesh
through the asset system rather than cloning cell meshes.

## Windows portability

Nothing here compiles for Windows: MinGW is not installed and clang-cl has
no Windows SDK, so W1 and W2 did not run and no statement about a Windows
build being correct is available.

What did land:

- **`scripts/check_render_portability.sh`**, encoding the W0 audit as a
  gate rather than a one-time reading: no POSIX-only headers, no platform
  conditionals, no bare `long`, no direct platform surface calls, no raw
  threads, and diagnostic pragmas covering both compilers, over
  `engine/{src,include}/{render,graphics,profiling}`. It passes clean, and
  a negative control with three seeded violations fails it with exit 1, so
  the pass means something.
- **The VMA translation unit's diagnostic suppression was GCC-only.** The
  file exists to keep upstream warnings out of the build; under MSVC `/W4`
  they would all have returned. The MSVC counterpart was added. This was
  the one concrete Windows code defect found.
- **`sencha_engine` is a SHARED library with no MSVC export story.** MinGW
  auto-exports; MSVC exports nothing without annotations or
  `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS`. Recorded so the first MSVC link
  failure is expected rather than investigated.
- **`.github/workflows/ci.yml`** with a Linux leg (build, test, portability
  check) and a Windows leg (MSVC, Vulkan SDK, SDL3 via vcpkg, build and
  test). Authored and committed, **not pushed**: enabling Actions starts
  billable runs and is the owner's call.

### Cross-driver capability envelope

The plan proposed the Khronos profiles layer to simulate Windows devices.
It is not installed, but this machine has three real Vulkan
implementations, which is stronger evidence than a simulated profile.

| driver | result |
|---|---|
| NVIDIA RTX 4060 Laptop (proprietary, 1.4.312) | clean, 200+ frames |
| Intel RPL-P iGPU (Mesa ANV, 1.4.328) | clean, 200 frames |
| llvmpipe / lavapipe (Mesa CPU, 1.4.328) | clean, 100 frames |

All with validation on. lavapipe matters most: it is an independent
implementation sharing no code with the other two, and it exercises a
different memory model, which is the closest local proxy for "a driver we
have never run on". Every device-capability path the renderer depends on
held on all three, including after the coherency change.

## State

Suite green at 1739 tests at every commit. Ten commits, each one step.

Not done, and why: everything gated on the unavailable tools (cycle and
cache attribution, allocation audit, static analysis, MinGW, Wine), the
shadow-recording scale axis (needs scenes whose casters draw), and the
V1 audit's remaining hazard classes (instance/device feature
cross-checks by SPIR-V reflection, descriptor pool sizing, queue-family
divergence) which were scoped to the two classes that ran.
