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
| Alpha-tested shadow casters | The forward and debug passes discard, but shadows do not: `ShadowCasterItem` carries the material handle now, so what remains is UVs in the shadow vertex layout and a discard in `shadow_depth.frag`, which is an empty main today. A masked surface still casts an unmasked silhouette until both land |
| Post-processing pass | No post phase. Exposure and the tonemap shoulder run inside the forward fragment shader |
| Directional lights and cascaded shadows | `GpuLightType::Directional` exists in the enum and the fragment loop skips it. Lands with the outdoor/sun need. The rule that baked AO must never contain sunlight is already recorded against that work |
| Skybox | No cubemap. The background is the procedural gradient drawn from the ambient hemisphere (`SkyGradientPass`) |
| GPU skinning | The posed draw is live: pipeline Decision N resolved to **compute pre-skin** (owner, 2026-08-23), so `SkinnedPoseRenderFeature` dispatches `skin_pose.comp` per skinned instance in the Offscreen phase and every geometry pass consumes the posed buffer exactly as static vertices. A pose source exists: `AnimationClipPlayerComponent` names a clip and a time, `AnimationClipPlaybackSystem` advances that time on fixed ticks, and the render extract samples the clip into the palette (`skinned_pose` golden scene is the gate; a paused player at a fixed time is what makes it deterministic under the tick accumulator). What does not exist: blending, a state graph, and pose history for presentation interpolation -- the animation runtime proper, which replaces this component rather than extending it. Also absent: playback in the editor viewport (kyusu draws rest pose; an authored paused pose does not preview yet). Skinned meshes render in the editor viewport at rest (identity palette makes that identical to posing) and cast no shadows at all (projected grounding shadows were removed by owner ruling 2026-08-23 -- see `shadows.md`; the light-map caster path deliberately has no skinned reader yet). Recorded limits: an extreme pose can exceed the rest bounds culling and (eventually) shadows use, and posed buffers cost one `sizeof(StaticMeshVertex)`-per-vertex allocation per instance per frame in flight |
| Particles | None |


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

- **The editor render tree has no isolation fence.** `cmake/CheckRenderIsolation.cmake`
  covers `engine/{include,src}/render` only. `editor/kyusu/src/render` names 81
  distinct Vulkan symbols across 12 files, and `EditorBloomPass` is a complete
  offscreen post chain -- backend code living in an editor `render/` directory.
  Nothing enforces where that line sits. The engine-side rules are the template
  when someone draws it.
- **The editor's queue building is a second traversal.** `SceneRenderQueueBuilder`
  walks `EditorScene` entities, cooked brush meshes, and a lightmap-preview
  snapshot rather than the ECS chunks the runtime walks. The per-item kernels
  ARE shared -- `EmitMeshSections`, `SelectForwardLights`, the caster gathers and
  `AppendShadowCasterRecord` -- so classification cannot drift; what stays
  separate is traversal over genuinely different sources, plus one documented
  behavioural difference (the editor's light gather does not frustum-cull,
  because every viewport samples one atlas). Examined 2026-08-25 and
  deliberately not converged further: a shared record layer over different
  traversals adds indirection without removing a divergence risk.
- **Viewport rendering still mutates UI-owned state.** `RenderViewportOffscreen`
  saves `EditorViewport::RegionMin`/`RegionMax`, overwrites them with a
  target-local rect so the grid and backdrop can derive viewport and scissor,
  and restores them after. No exit path currently escapes the window, and the
  camera no longer comes from that rect, so this is a latent trap rather than a
  defect: a future early return, or anything that reads the rect during
  recording, gets target-local coordinates where it expects screen ones. The fix
  is a draw context carrying the target-local rect explicitly.
- **No streaming content carries probe volumes.** Probe slots now retire on the
  frame clock before they recycle, but nothing checked in unloads a zone that
  owns an `IrradianceVolume` mid-run: `probe_spike` is a single map, and the
  `traversal3` world's zones have no probes cooked. The gate was proven live by
  forcing a release from a temporary probe (held one frame, reclaimed the next,
  no validation errors), and the slot state machine is covered headlessly. What
  is not covered is the real path -- unload, reclaim, and the zone streaming in
  behind it re-acquiring the slot. A probe volume authored into a streamed zone
  would close it, and would also exercise the transient 2x slot demand a zone
  reload creates.
- **Scratch reservations are not built.** Per-consumer accounting landed
  (`ScratchTag`), so an exhausted slice now names who filled it. Reservations or
  protected minimums would change which allocations succeed, and there is no
  observed exhaustion to size them against -- a nonzero per-tag failure count in
  a real scene is the trigger.
- **Feature status is a bool.** `Setup` returning true after a pass failed to
  build is deliberate degradation (the frame still presents), and the failing
  passes now log what will be missing. A structured Ready/Degraded/Failed status
  is what a host would need to *surface* that state in UI; nothing wants to
  today, and it would change the fingerprinted feature contract, so it waits for
  a consumer.
- **Complexity outliers left standing.** Measured 2026-08-24 across the render
  and graphics trees (median function 10 lines, 6% above cyclomatic 10, so the
  tree is healthy and the pain is local). Five worst cases were fixed; these
  were recorded instead: `MeshForwardPass.cpp` at 834 lines and
  `LightBindings.cpp` at 633 (both one cohesive mechanism, so a split needs a
  seam rather than a line count); `EditorRenderFeature.cpp` at 663 with a
  15-parameter constructor (its `ReleaseSceneResources` hook is gone, but the
  breadth remains); `VulkanFrameService::BeginFrame` / `EndFrame` at
  cyclomatic 26 and 23, which is `VkResult` triage fan-out rather than
  algorithm; `FrameComposition::Resolve` at 27, a multi-phase topological sort
  in one function; and `Renderer`'s own 15-parameter constructor.
- **`graphics/vulkan/` headers stay installed and unfingerprinted.** The
  neutral contract means a module implementing `IRenderFeature` no longer needs
  them, but `install(DIRECTORY include/)` still ships them and the ABI
  fingerprint does not cover them, so a host-style module composing asset
  stacks from raw services (the template does) can still skew silently. An SDK
  surface question, not a render one.
- **No pre-device hook for render features.** `Contribute` was removed: it
  could never fire, because `GraphicsServices` creates the device during
  `Engine::Initialize`, before any hook that could build a feature. A game that
  needs a device extension or feature bit needs a hook at engine configuration
  time, where `EngineConfig` is still mutable and the policy has not been built.
- **Headless testability.** `GpuFrameScratch`, `ShadowDepthPass`,
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
