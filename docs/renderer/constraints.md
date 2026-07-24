# Constraints, Limits, and Traps

## Hard limits

Compile-time constants. Changing one is a multi-file change; see the sync rules
in [shaders.md](shaders.md#keeping-cpu-and-gpu-structs-in-sync).

| Limit | Value | Constant | Also declared in |
|---|---|---|---|
| Forward lights per frame | 64 | `kMaxForwardLights` | `mesh_frame.glsli` `MAX_LIGHTS` |
| Spot shadow slots | 8 | `kMaxSpotShadows` | `MAX_SPOT_SHADOWS` |
| Point shadow slots | 4 | `kMaxPointShadows` | `MAX_POINT_SHADOWS` |
| Point cube faces | 6 | `kPointShadowFaceCount` | implicit |
| Active probe volumes | 8 | `kMaxActiveProbeVolumes` | `MAX_PROBE_VOLUMES` |
| Probe SH channels | 3 | `kProbeVolumeChannelCount` | `PROBE_VOLUME_CHANNELS` |
| Bindless sampled images | 1024 | `VulkanDescriptorCache::kBindlessImageCapacity` | `BindlessTextures[1024]` |
| Mesh sections | 32 | `kMaxMeshSections` | `StaticMeshComponent::SectionMask` width |
| Spot atlas extent | 2048 | `kSpotShadowAtlasExtent` | |
| Spot tile tiers | 256 / 512 / 1024 | `ShadowResolutionTier` | |
| Spot guard band | 8 texels | `kSpotShadowGuardTexels` | |
| Spot filter reach | 7 texels | `kSpotShadowFilterReachTexels`, asserted `< guard` | |
| Point face extent | 512 | `kPointShadowFaceExtent` | |
| Shadow softness clamp | 0.5 to 4.0 texels | `kSpotShadowSoftnessMin/MaxTexels` | clamped again in GLSL |
| Frame UBO size | 5712 bytes | `sizeof(MeshFrameUniforms)`, asserted | |
| Push constant size | 80 bytes | `sizeof(MeshPushConstants)`, asserted | |
| Fixed shadow memory | 20 MiB (8 atlas + 12 cubes) | derived | |

Runtime budgets, all cvars: `render.shadow.max_spot` (8),
`render.shadow.max_point` (4), `render.shadow.max_views_per_frame` (12),
`render.shadow.min_invalidated_views_per_frame` (1),
`EngineGraphicsConfig::FrameScratchBytesPerFrame` (1 MiB per slice),
`EngineGraphicsConfig::FramesInFlight` (2).

## Device floor

The renderer refuses to run on a device missing any of these. See
[vulkan-backend.md](vulkan-backend.md#device-selection-floor) for the full list
and the reason each is required.

- Vulkan 1.3 with `synchronization2`, `dynamicRendering`,
  `shaderDemoteToHelperInvocation`
- The Vulkan 1.2 descriptor-indexing set including
  `descriptorBindingSampledImageUpdateAfterBind` and
  `shaderSampledImageArrayNonUniformIndexing`
- `samplerAnisotropy`, `textureCompressionBC`, `imageCubeArray`
- `VK_KHR_swapchain`

`VK_KHR_present_id` and `VK_KHR_present_wait` are optional; without them the
frame service falls back to acquire-based pacing.

A no-Vulkan or headless build is not supported. The render layer includes Vulkan
headers unconditionally.

## Invariants

These are the rules a change to the render layer must not break.

1. **The backend never reads live ECS state.** Extraction copies values into
   transient render-domain structures before any command is recorded.
2. **One pipeline family, selected by data.** New visual behavior enters through
   material data, cvars, components, or specialization constants, not through a
   parallel pipeline or a new code path per scenario.
3. **Features cache service pointers in `Setup`.** No service lookup in the hot
   draw path.
4. **Every Vulkan object freed at runtime routes through the deletion queue.**
   Never call `vkDestroyX` or `vmaDestroyX` directly on a resource that a frame
   in flight could still reference.
5. **`GraphicsServices` member order is the teardown order.** Do not reorder
   members, and do not introduce a shutdown sequencer to compensate.
6. **The shadow feature registers before the mesh feature.** Set 2's layout must
   exist before the forward pipeline layout is built, and the offscreen phase
   must record before the main color phase reads its targets.
7. **Slot records describe what was rendered, not what was requested.** Never
   publish a shadow record for content that was not drawn with it.
8. **Scheduling and selection are deterministic.** No unordered container
   iteration, no address-derived ordering, no time-seeded randomness anywhere in
   extraction, selection, or arbitration. Ties break on `RenderEntityKey`.
9. **Counters accumulate at run granularity, unconditionally.** Only the copy
   into `RenderStats` is gated on the instrumentation bundle.
10. **A frame that dropped work must say so.** Any path that abandons draws sets
    `Skipped` and counts the dropped instances.
11. **The render layer reaches the OS only through SDL, VMA, and the Vulkan
    loader.** Guarded by `scripts/check_render_portability.sh`.

## Traps

Things that look fine and are not.

### `TOP_OF_PIPE` as a first synchronization scope

`TOP_OF_PIPE` names no stage, so a barrier using it as the source scope has an
empty first scope and orders nothing. This shipped once: the per-frame depth
barrier meant to order consecutive frames' depth writes named `TOP_OF_PIPE` with
a zero access mask, and one depth image serves every frame in flight while a
frame only waits on the fence two slots back. Name the real stages and accesses.

### The single frame descriptor set

There is one set 0 and both passes write its buffer range in `Setup`. The last
writer wins. See
[vulkan-backend.md](vulkan-backend.md#the-frame-ubo-range-trap).

### Host-visible does not mean coherent

`VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE` asks for write-combine
friendly memory and says nothing about coherency. The frame scratch writes
through its mapped pointer and submits without flushing, so
`VulkanBufferService` requires `VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`
explicitly. Removing that requirement works on every local driver and corrupts
every per-frame uniform on one that exposes a non-coherent host-visible type.

### Undefined image contents are not "whatever, it gets cleared"

A freshly created depth target that is sampled before any view has rendered into
it reads differently on different drivers. `LightBindings::ParkDepthImage`
clears the atlas and cube pool to the far plane and parks them in the sampled
layout at creation, precisely so a frame with zero shadow views still samples
defined memory.

### Partial scratch grants are prefixes, not gaps

`AllocateVertexElements` can return fewer elements than asked for. Both passes
treat the grant as a prefix of the draw order and clip runs to it. Treating it
as a sparse set would draw instances against the wrong transforms.

### `Aabb3d::Extent()` is the half extent

Not the full size. This has bitten bake and bounds code before.

### `GetGpuImage` is not stable across hot reload

`TextureCache::ReloadInPlace` swaps the entry's image. Anything holding a view
or descriptor built from it must re-check each frame.

### Sort key bits are truncated

The material and mesh fields in the opaque sort key are masked to 14 and 20 bits.
Run merging therefore compares the actual item fields, never the key. Do not
"optimize" run detection to compare keys.

## Determinism

The render path must produce identical CPU-side output for identical input:

- Light selection sorts by score with a `RenderEntityKey` tie-break.
- Shadow scheduling orders by frame stamp, pool, then slot index. All three
  comparisons are total.
- Atlas allocation scans nodes in Morton order, so identical request sequences
  produce identical placements.
- Caster bounds are quantized to 1/16 world unit before diffing, so
  float noise in transform propagation cannot masquerade as movement.
- The caster diff is a sorted merge, not a hash-set difference.

Nothing in the render layer spawns threads. Extraction, arbitration, recording,
and submission all run on the frame thread. Probe residency mutations happen at
the async drain point, on the same thread, earlier in the same frame.

## Portability

The renderer builds and runs on Windows: a MinGW cross-build
(`cmake/toolchain-mingw64.cmake`) renders the bench scene under Wine with
per-frame counters identical to Linux. CI has a Windows leg.

Known portability rules, each guarded by `scripts/check_render_portability.sh`:

- No POSIX-only headers or APIs in the render layer.
- No assumptions about `long` width or struct packing.
- Nothing may reach the OS except through SDL, VMA, and the Vulkan loader.

One Windows-specific defect has been fixed and is worth remembering: an AVX
stack-alignment crash in the MinGW build. Over-aligned locals in code reached
through a callback whose ABI does not guarantee 32-byte stack alignment will
fault.

### The dead instrument

**Vulkan synchronization validation does not report on the development
machine's layer build.** A negative control (a deliberately deleted barrier)
produced core-validation layout errors and zero `SYNC-HAZARD` lines across three
enabling paths. Any claim that a change is "syncval clean" must first re-prove
the instrument with that same negative control. Treat a clean syncval run here
as no evidence at all.

## Performance budgets

Reference target: 1920x1080, GTX 1060 / RX 580 class, 60 fps, total GPU frame
under 12 ms. These are the numbers the counters are read against.

| Item | Budget |
|---|---|
| MainColor GPU, representative room, lights + shadows + probes | <= 6.0 ms |
| Shadow phase GPU, steady state (caches warm) | <= 0.3 ms |
| Shadow phase GPU, worst invalidation frame (view clamp active) | <= 2.5 ms |
| One 512 spot view | <= 0.15 ms |
| One point cube update (6 faces, per-face cull) | <= 0.8 ms |
| Probe sampling added fragment cost | <= 0.3 ms full screen (measured 0.04 to 0.06 on a 4060) |
| Shadow memory | 20 MiB fixed |
| Probe memory per zone, default density | <= 2 MiB (typical room around 50 KiB) |
| Frame UBO | <= 8 KiB (16 KiB hard line). Currently 5712 bytes |
| Frame scratch slice | 2 MiB with shadows; high water under 75 percent in benchmarks |
| CPU extraction (meshes + lights + casters) at 5k queue items | <= 1.2 ms |
| Caster table diff, 3k casters, on-change slots resident | <= 0.10 ms |
| `ShadowResidency` plus probe residency CPU | <= 0.15 ms |
| Instrumentation, mode Off versus compiled out | statistically indistinguishable |
| Instrumentation, Counters mode CPU | <= 0.05 ms |
| Instrumentation, Gpu mode CPU plus GPU | <= 0.10 ms |
| Capture ring memory (Capture mode only, lazily allocated) | <= 16 MiB default |
| Visible lights after cull | <= 24 typical, 64 hard cap |

### Measured state

Two evidence passes, both on an RTX 4060 laptop (not the reference tier), with
readings scaled by a conservative 3x throughput factor where a reference-tier
number is quoted.

**GPU budgets (2026-07-21, `docs/plans/evidence/render-3c-review/`).** Every row
of the table above passes and no escalation trigger fires. Representative
content measures 0.45 ms MainColor median with 7 visible lights against a 6.0 ms
budget and an 8 ms escalation trigger. The deliberate 64-light stress scene
reaches roughly 9 ms scaled only at p95 with every light visible, which the
earlier point-light-cost evidence already ruled a content-design wall: the cap
is a correctness boundary, not a target workload. Steady-state shadow phase cost
with all slots cached is 0.0007 ms. Frame scratch high water was 7.1 KiB against
a 2 MiB slice.

Two rows are not meaningfully exercised by the checked-in benches, which are
brush-only and geometrically trivial (4 draws, around 100 triangles): CPU
extraction at 5k queue items, and the per-view shadow render rows. Re-run those
when real game content exists.

**CPU profile (2026-07-24, `docs/plans/evidence/renderer-cpu-profile/`).** Engine
code is 0.55 percent of user cycles in a normal scene, the draw path is flat,
and the steady-state frame allocates essentially nothing (0.65 percent of cycles
in allocation functions even in a pathological scene). No CPU change was made,
because nothing was armed against the budget.

### Escalation triggers

Do not build these ahead of the metric.

- **Per-object light lists** when representative content shows MainColor over
  8 ms at 1080p with average per-fragment light iterations above 16. The
  implementation would be a CPU range-versus-bounds cull writing a small
  per-draw light index list.
- **Tiled or clustered culling** only after per-object lists exist and visible
  lights regularly exceed 64, or lists average more than 8 lights per object.
- **Lights and shadow slots in a storage buffer** if the frame UBO grows past
  its ceiling. The scratch already carries `STORAGE` usage.
- **A deferred architecture**: no trigger inside the target game space. Out of
  scope by decision.
- **Coarse spatial binning of shadow casters** if shadow record time exceeds
  2 ms after batching. The counters to evaluate this are in place; no
  checked-in scene reaches the caster counts that would show it.

## Contingencies recorded against known risks

| Risk | Recorded response |
|---|---|
| `imageCubeArray` unavailable on a target device | fall back to six atlas tiles per point light behind the same `ShadowResidency` interface, with face selection in the shader. Not built |
| D16 precision on long spot ranges | near-plane scaling covers the target ranges; per-light `ShadowBiasScale` is the manual escape |
| Caster-diff cost on caster-heavy frames | bounded by the on-change gate; escalations are a `Changed<>` prefilter, then a static/dynamic caster split |
| Probe dilation leaking at thin interior walls | cell-size guidance, volume splitting, and the dilated-probe overlay. Occlusion-aware interpolation is the recorded escalation, with its cost written down |
| Scratch overflow on worst-case invalidation frames | sized by config, measured by a high-water counter, degraded by skipping views (which re-queue), never by corruption |
