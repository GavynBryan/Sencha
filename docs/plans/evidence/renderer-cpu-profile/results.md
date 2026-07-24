# Renderer CPU Profile, Portability, and Vulkan Audit: Results

Run 2026-07-23/24 on the development laptop (Raptor Lake-P, 12 P-cores +
4 E-cores; RTX 4060 Laptop; Intel RPL-P iGPU; llvmpipe; Fedora 43, Mesa
25.3, NVIDIA 580.159, validation layer 1.4.341). What the renderer looks like
after these changes is documented in [`docs/renderer/`](../../../renderer/readme.md);
what remains undone is tracked in
[`docs/renderer/open-work.md`](../../../renderer/open-work.md).

The full toolchain was installed partway through (perf, valgrind, heaptrack,
clang-tidy, cppcheck, mingw, spirv-tools), so every stage ran. An earlier
revision of this file reported several stages blocked on tooling; that no
longer holds and the blocked framing was removed.

## Headline

- **Four defects fixed**, three of them the "works here, breaks elsewhere"
  class: host-visible memory not required coherent, an empty-scope depth
  barrier, an undefined-contents shadow atlas, and a Windows-build AVX
  stack-alignment crash.
- **The renderer has no CPU bloat and no CPU problem at current scale.**
  Engine code is 0.55% of user cycles in a normal scene. The draw path is
  flat. The steady-state frame allocates essentially nothing (0.65% of
  cycles in allocation functions even in a pathological scene).
- **The renderer builds and runs on Windows.** A MinGW cross-build renders
  the bench scene under Wine with per-frame counters identical to Linux,
  after the AVX crash was fixed.
- **One instrument is dead here**: synchronization validation never
  reports on this machine's layer build (proven by negative control).

## Defects found and fixed

### Host-visible buffers were not required to be coherent (portability)

`VulkanBufferService` allocated host-visible memory with
`HOST_ACCESS_SEQUENTIAL_WRITE | MAPPED` and no `requiredFlags`. The frame
scratch writes through that mapped pointer every frame and submits without
flushing, and the header justified the missing flush by naming that VMA
flag. The justification does not hold: `SEQUENTIAL_WRITE` asks for
write-combine-friendly memory and says nothing about coherency. VMA's
selection for it requires only `HOST_VISIBLE` and merely de-prefers
`HOST_CACHED`, so a driver exposing a non-coherent host-visible type could
satisfy it, and every per-frame uniform and instance stream would reach the
GPU stale. All three local drivers return coherent memory, so it never
showed. Fixed by requiring `VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`, which the
spec guarantees exists. Both audit passes found this independently.

### The depth barrier's first synchronization scope was empty (correctness)

One `VulkanDepthTarget` serves every frame in flight, and `BeginFrame`
waits on the frame two slots back, so consecutive frames can write depth
concurrently. The per-frame barrier meant to order them named
`TOP_OF_PIPE` with a zero access mask: `TOP_OF_PIPE` names no stage, so the
first scope was empty and the barrier ordered nothing. The shadow atlas
barriers already carry the real scope for the identical shape; depth was
the inconsistent one, now fixed to name the depth-write stages and access.

Evidence status: reasoning, not instrument. The confirming instrument is
syncval, which is dead here (below). The fix is grounded in the spec text
on `TOP_OF_PIPE`, the shared-image and fence-cadence structure, and the
inconsistency with the working barriers in the same file. Suite green and
all drivers clean after it.

### A fresh shadow atlas was sampleable with undefined contents (portability)

Best-practices validation flagged `ParkDepthImage` parking a newly created
atlas in a sampled layout with whatever the allocator returned. Frames that
render no shadow view sample it, so undefined memory reached the shader, and
undefined memory differs between drivers. The dummy shadow images already
cleared to the far plane; the real ones now match.

### The Windows build crashed on the first frame from AVX stack misalignment (portability)

The MinGW cross-build initialized the full renderer under Wine (instance
with `VK_KHR_win32_surface`, swapchain, frame service) and then faulted on
a worker thread. The faulting instruction was `vmovdqa %ymm0, 0x40(%rsp)`:
a 32-byte-aligned 256-bit AVX store to the stack. GCC auto-vectorizes to
256-bit AVX at `-O2`/`-O3` and spills YMM registers with such stores, but
the Windows x64 ABI guarantees only 16-byte stack alignment and GCC's
realignment for the wider case does not hold on this target.

Root cause confirmed by reproduction: `-mstackrealign` (16-byte) did not
help; `-mprefer-vector-width=128` (no 256-bit spills) made it run to
completion. Fixed in the toolchain file. MSVC handles AVX alignment
correctly and needs no equivalent, so the shipping MSVC target is expected
unaffected. The CI Windows leg will confirm.

## The dead instrument: synchronization validation does not report here

V0 rests on sync validation, and the plan's rule is no verdict without an
instrument. That rule caught this.

A 300-frame run with `SENCHA_VALIDATE_SYNC=1` reported zero hazards, which
reads as strong proof. It is not one. Negative control: the shadow atlas
read-back barrier was deleted, leaving the forward pass sampling the atlas
with no barrier and the wrong layout. Core validation immediately reported
the layout violations (`VUID-vkCmdDrawIndexed-imageLayout-00344`, ten
`VUID-vkCmdDraw-None-09600`). **Sync validation reported nothing on the
same run.** Three enabling paths were tried (`VkValidationFeaturesEXT`, a
`vk_layer_settings.txt`, the `VK_LAYER_SETTING_*` env var); none produced a
`SYNC-HAZARD` line, though the layer binary contains those strings.

Consequence: no synchronization claim here rests on syncval. GPU-assisted
and best-practices validation do work (best-practices found the atlas bug).
Closing this needs a Khronos-SDK-built layer rather than the distro package.

## CPU profile

### The engine is a small fraction of frame CPU; the driver dominates

`perf record` on the profile build (Release codegen with symbols), pinned
to the P-cores, `IMMEDIATE` present:

- **Normal scene (`bench_stress`, 8 draws): `libsencha_engine.so` is 0.55%
  of user cycles.** The frame is the NVIDIA driver (30%), libc
  threading/syscall (28%), vdso (25%), and Wayland (14%). There is no
  engine-side CPU cost to chase at representative complexity.
- **Pathological scene (1600 casters): engine rises to 30.9%.** Only a
  deliberately extreme entity count makes engine code the majority, and
  even then the absolute cost is ~0.26 ms p50 across all render scopes,
  well inside the 2 ms placeholder budget.

### Allocation: the steady-state frame is allocation-free

Allocation functions (`_int_malloc`, `malloc`, `free`, `operator new`, and
friends) total **0.65% of cycles** in the 1600-caster scene over 400
frames, i.e. startup and streaming amortized, not per-frame churn. The
`RenderQueue` and `ShadowResidency` clear-not-free vector reuse works as
designed. P4's question (zero steady-state render allocations) is answered
yes. heaptrack's injection did not follow into the render process, so this
is read from the cycle profile instead.

### Cache attribution: misses are in the engine, and where the review predicted

At 1600 casters, **61.9% of all process L1 data-cache misses are in
`libsencha_engine.so`**. Ranked by share of engine L1 misses:

| symbol | L1-miss share | hypothesis |
|---|---|---|
| `ShadowCasterDiff::Apply` sort | 21.0% | sorts fat records, not indices |
| `MeshForwardPass::BindInstanceStream` | 11.3% | H-G: scatter-gather over 152-byte items |
| `RenderQueue::SortOpaque` | 8.4% | (already index-sorts; residual) |
| `ShadowCasterExtractionSystem::Extract` | 6.2% | H-F: per-entity `TryGet` pointer-chase |
| `ShadowCasterDiff::Apply` (body) | 3.3% | diff walk |

Cross-checked against the earlier scope timing: shadow caster extraction
costs a stable **~1.4x** mesh extraction over the same entity set (0.038 vs
0.026 at 400 casters, 0.144 vs 0.106 at 1600), the measured shape of H-F.
Both extraction scopes are linear in caster count (16x casters costs
13.9x/14.3x), no superlinear term.

cachegrind was attempted for per-line attribution but crashes against the
NVIDIA driver under emulation; perf event sampling gave the symbol-level
picture instead.

### H-A (shadow recording scales with views x casters) is not measured

The cloned caster entities reference cell meshes whose geometry is authored
in world space, so relocating an entity does not relocate its geometry and
the clones fall outside the view: `draw_calls` stays at 6-7 and
`shadow_casters_tested` at 0 in every scale scene. The scenes exercise the
extraction path only; `Record/ShadowViews` stays near zero because no
shadow work runs. Measuring shadow recording against caster count needs
scenes whose casters are actually drawn, which means authoring placed mesh
entities against a unit mesh through the asset system rather than cloning
cell meshes. This is the one measurement the plan wanted that these scenes
do not deliver.

## No CPU changes were made, on purpose

The plan gates every structural change on the budget being exceeded. Total
render-side CPU is ~0.26 ms p50 at 1600 casters against a 2 ms budget, and
0.55% of cycles at normal complexity. None of the P6 candidates are armed.
The cache findings are real and now carry numbers, but optimizing a
sub-millisecond path that no shipping scene reaches would be speculation of
exactly the kind the Carmack ethos rejects.

The findings are recorded ranked, with fixes identified, so the list is
armed the moment a real scene crosses the budget. The top candidate is
`ShadowCasterDiff::Apply` sorting fat `ShadowCasterRecord`s (an `Aabb3d`
plus handles, ~80 bytes) by a small key, where `RenderQueue::SortOpaque`
already shows the index-sort pattern to copy; it only runs when on-change
shadow residency is active. Second is H-F, the shadow extractor's
per-entity `TryGet`, whose fix is to co-iterate chunk columns like the
forward extractor. Neither is worth the correctness risk until the budget
says so.

## No architectural bloat

The pre-execution review found the draw path already flat (Renderer walks
two phase buckets, features record through concrete passes, pipeline
selection is an array index, two virtual calls per frame at a real
game-binary boundary). Execution confirmed it: the cycle profile shows no
indirection overhead, no per-draw virtual dispatch, no hidden hops. There
is nothing to delete. "Reduce bloat if desirable" resolved to "it is not."

## Static analysis

- **clang-tidy** (bugprone + performance, scoped to the render layer,
  committed as `.clang-tidy`): clean of actionable findings. The only
  non-noise hits were two `implicit-widening-of-multiplication-result` on
  the same line, a compile-time `1024 * 1024` constant that fits in `int`,
  which is not a real overflow.
- **cppcheck**: its only errors were template false positives
  (`arrayIndexOutOfBounds` inside `Mat.h`, from instantiating `Mat<3,3>`
  paths while analyzing Mat4 code).

The render layer has no static-analysis defects at these tools' sensitivity.

## Windows portability

### The build

`cmake/toolchain-mingw64.cmake` cross-compiles the runtime with MinGW-w64.
`sencha_engine.dll` (65 MB), `SceneViewer/app.exe`, and `game.dll` all
build with zero errors. The only compile failures in the whole tree are in
editor code (`Project.cpp`: `std::filesystem::path` is `wchar_t`-based on
Windows), which is outside the renderer branch's scope. Render-layer
warnings under MinGW were two trivial ones (a missing field initializer, an
enum/non-enum conditional).

### The run (W2 counter identity)

Under Wine, the fixed build renders `bench_stress` for 250 frames and exits
clean. Deterministic counters match the Linux build of the same scripted
scene exactly:

| counter | Linux | Windows |
|---|---|---|
| draw_calls | 7 | 7 |
| instances_dropped | 0 | 0 |
| passes_skipped | 0 | 0 |
| shadow_caster_draws | 0 | 0 |
| point_shadow_faces | 0 | 0 |

Captures: [`linux_capture.json`](linux_capture.json),
[`windows_wine_capture.json`](windows_wine_capture.json). Frame times from
Wine carry no authority; the counter identity is the point, and it holds.

What this cannot establish: real Windows performance, MSVC semantics,
native present pacing, fullscreen behavior. Those need real Windows
hardware; the bench harness makes that a ten-minute run when one is
available.

### Static portability gate and CI

- `scripts/check_render_portability.sh` encodes the audit as a gate (no
  POSIX-only headers, no platform conditionals, no bare `long`, no direct
  platform surface calls, no raw threads, diagnostic pragmas covering both
  compilers). Passes clean; a seeded negative control fails it.
- The VMA translation unit's diagnostic suppression was GCC-only and would
  have leaked warnings under MSVC `/W4`; the MSVC counterpart was added.
- `sencha_engine` is a SHARED library with no MSVC export story; MinGW
  auto-exports, MSVC will not. Recorded so the first MSVC link failure in
  CI is expected, not investigated.
- `.github/workflows/ci.yml` has a Linux leg and a Windows MSVC leg,
  authored and committed but not pushed (enabling Actions is billable and
  the owner's call).

### Cross-driver capability envelope

Three real Vulkan implementations, all clean with validation on, all after
the coherency change:

| driver | result |
|---|---|
| NVIDIA RTX 4060 Laptop (proprietary) | clean, 200+ frames |
| Intel RPL-P iGPU (Mesa ANV) | clean, 200 frames |
| llvmpipe / lavapipe (Mesa CPU) | clean, 100 frames |

lavapipe matters most: an independent implementation sharing no code with
the others and a different memory model, the closest local proxy for a
driver never run on. Every device-capability path held on all three.

## Not done

- **H-A (shadow recording vs caster count)**: needs scenes whose casters
  draw (placed mesh entities via the asset system, not cloned cell meshes).
- **V1 remaining hazard classes**: instance/device feature cross-checks by
  SPIR-V reflection, descriptor pool sizing, queue-family divergence. The
  memory and synchronization classes ran; these did not.
- **Real Windows hardware run**: correctness and capability are proven from
  here; native performance is not.

## State

Suite green at 1739 tests at every commit. Working tree clean. Every change
is one commit.
