# Open Work

Known gaps in the render layer, carried forward from the executed renderer
plans (phase-3 lighting, hardening, and the CPU-profile / portability / Vulkan
audit). Each entry states what is missing, why it was not done, and what
unblocks it. Measurement artifacts for the executed work remain under
`docs/plans/evidence/`.

## Not implemented

| Item | State |
|---|---|
| Transparency limits | Blend works: per-view back-to-front, one draw per item, depth write off. What does not exist: order-independent transparency (sorting is per draw, so interpenetrating blended surfaces resolve by bounds-centre distance), and blended surfaces still cast full-silhouette shadows -- the same caster-material gap as Mask |
| Alpha-tested shadow casters | The forward and debug passes discard, but shadows do not: `ShadowCasterItem` carries no material handle and the shadow vertex layout carries no UVs, so a masked surface casts an unmasked silhouette. Structural, and scheduled with the shadow work rather than with materials |
| Post-processing pass | No post phase. Exposure and the tonemap shoulder run inside the forward fragment shader |
| Directional lights and cascaded shadows | `GpuLightType::Directional` exists in the enum and the fragment loop skips it. Lands with the outdoor/sun need. The rule that baked AO must never contain sunlight is already recorded against that work |
| Skybox | No cubemap. The background is the procedural gradient drawn from the ambient hemisphere (`SkyGradientPass`) |
| GPU skinning | The rest-pose path is live end to end: `SkinnedMeshComponent` persists and cooks, extraction draws the rest geometry through the forward pass (`skinned_rest` golden scene is the gate), the influence stream is GPU-resident with vertex+storage usage so either skinning branch binds it as-is, and the palette math is pure and tested (`anim/SkinningPalette.h` -- identity at bind pose). What does not exist: a pose source (the animation runtime) and the posed draw itself, which is the recorded vertex-shader-vs-pre-skin fork (pipeline Decision N). Skinned meshes ground via projected object shadows and render in the editor viewport; they stay out of the light-map caster path deliberately (the participation seam's second flag has no skinned reader yet) |
| Particles | None |
| Compute pipelines | `VulkanPipelineCache` builds graphics pipelines only |

## Deferred with a stated trigger

These are deliberately not built ahead of a metric. The triggers are in
[constraints.md](constraints.md#escalation-triggers).

- Per-object light lists.
- Tiled or clustered light culling.
- Coarse spatial binning of shadow casters. The counters that would evaluate it
  are in place; no checked-in scene reaches the caster counts that would show it.
- Moving lights and shadow slots out of the frame UBO into a storage buffer.

## Owed engineering

### Upload staging ring

Every buffer and image upload allocates staging memory, submits, and waits on
its fence, on the graphics queue. Nothing has demonstrated it as a bottleneck,
and profiling streaming hitches comes before the rewrite. It needs a streaming
benchmark first: a zone load/unload loop with the upload path scoped.

(The fence wait is bounded and error-checked as of 2026-07-24, so a lost device
reports upload failure instead of hanging; only the batching question remains.)

### Fill-heavy bench scene

The per-fragment light cost question is unanswered because no fill-heavy scene
is authored. Without it, nothing can say whether per-object light lists are
warranted. Owner decision 2026-07-24: the low-tier GPU budget stays deferred
until representative game content exists to measure, so the bench and the
budget land together; the audit's 12.5 ms p99 iGPU placeholder is not adopted.

### Shadow recording versus caster count

The measurement needs scenes whose casters actually draw, meaning placed mesh
entities through the asset system rather than cloned cell meshes.

### Vulkan audit classes not run

The memory and synchronization hazard classes ran. These did not:

- instance and device feature cross-checks by SPIR-V reflection
- descriptor pool sizing verification
- queue-family divergence (separate graphics and present families)

### Cross-hardware coverage

Owed but needs hardware: AMD RADV, separate graphics/present queue families,
low-VRAM devices, and two-image swapchains across drivers. A real Windows
hardware run is also owed; correctness and capability are proven from the MinGW
plus Wine path, native performance is not. Owner decision 2026-07-24: parked
until the hardware exists; this section is the tracking record.

Descriptor-capacity edge tests (bindless slots 1023 / 1024 / 1025) and
device-lost fault injection are owed in the same bucket.

## Smaller items

- **No pre-device hook for render features.** `Contribute` was removed: it
  could never fire, because `GraphicsServices` creates the device during
  `Engine::Initialize`, before any hook that could build a feature. A game that
  needs a device extension or feature bit needs a hook at engine configuration
  time, where `EngineConfig` is still mutable and the policy has not been built.
- **Headless testability.** `VulkanFrameScratch`, `ShadowDepthPass`,
  `Renderer::AddFeature`, and `StaticMeshCache` all need a live device to
  construct, so their contracts cannot be tested headlessly. One case was fixed
  by extracting `FrameScratchRing`; the others rest on code review and live
  runs. Extracting the policy from the Vulkan handling is the pattern when one
  of them next needs a test.

## Resolved 2026-07-24

Former open questions, decided by the owner:

- **Pipeline cache disk persistence: deleted.** `LoadFromDisk` / `SaveToDisk`
  had no call sites, prewarming already moved compilation to load, and every
  desktop driver keeps its own on-disk shader cache. At the current pipeline
  count the engine-level blob bought nothing; reintroduce the API if a material
  graph ever multiplies pipeline counts.
- **Validation defaults by build type.** `EngineGraphicsConfig::EnableValidation`
  defaults on in debug builds and off in optimized builds (`NDEBUG`), so a
  release or profiling run never silently pays the layer cost. Config still
  overrides in either direction.
- **Low-tier GPU budget: deferred** until representative content exists (see
  the fill bench entry above).
- **Cross-hardware coverage: parked** until hardware exists (see above).
