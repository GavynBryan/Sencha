# Renderer Phase 3: Lighting, Shading, Shadows, Baked Irradiance, and Renderer Profiling

Status: PROPOSED 2026-07-10. Plan only; no implementation has landed.

This document is the lighting portion of the "render ladder plan" that
`docs/assets/pipeline.md` repeatedly defers to ("Decision L ships the data, not
the lighting", pipeline.md:643). It supersedes the ordering of
`docs/plans/engine-roadmap.md` Track B item 2 (engine-roadmap.md:324-328), which
scheduled directional cascaded shadows first: Phase 3 ships spot and point
shadows and no directional lights. It also pulls Track B item 8 ("GI (v2.0
baked)", engine-roadmap.md:345-346) forward as a first zone-scoped baked
irradiance solution. Both roadmap rows should be updated when this plan is
accepted. Where this document and the code disagree, the code as cited was
inspected on 2026-07-10 at commit 962a3aa.

Scope summary of decisions made here:

- Shading: keep the Decision L material data schema; evaluate it with a
  stylized model (wrapped diffuse, normalized Blinn-Phong specular, hemispheric
  or probe ambient, emission), not a metallic-roughness BRDF.
- Spot shadows: one 2D depth atlas (D16, quadtree tiers 256/512/1024),
  hardware-compare PCF with a 3x3 tent filter.
- Point shadows: a small depth cube-map array (D16, 512 per face, budgeted at 4
  lights), same filter adapted to directions.
- Baked lighting: zone-scoped irradiance probe volumes storing L1 spherical
  harmonics, baked in the editor, streamed with zones, sampled per fragment.
- Profiling: GPU timestamp scopes, Vulkan debug labels, renderer counters, an
  ImGui stats panel, and a JSON/CSV frame-capture exporter.

---

## 1. Current renderer findings

### 1.1 Frame and pass structure

- The renderer is a phase-bucketed feature list. `RenderPhase { Offscreen,
  MainColor }` with the comment "Reserved for: Shadow, Opaque, Transparent, UI,
  Post..." (`engine/include/graphics/vulkan/Renderer.h:57-65`). Features
  implement `IRenderFeature` (`Renderer.h:108-134`) with `Contribute()` (folds
  device features into `VulkanBootstrapPolicy` before device creation),
  `Setup()`, `OnDraw()`, `Teardown()`.
- `Renderer::DrawFrameScheduled` records `RecordOffscreenPhase` then
  `RecordMainColorPhase` into one command buffer on the main thread
  (`engine/src/graphics/vulkan/Renderer.cpp:119-168`). Offscreen features own
  their passes and targets (`Renderer.cpp:193-209`); MainColor opens one
  dynamic-rendering pass on the swapchain image with a depth attachment
  (`Renderer.cpp:211-296`). Everything is Vulkan 1.3 dynamic rendering; there
  are no VkRenderPass objects (`VulkanPipelineCache.h:22-23`).
- The frame loop is single-threaded through the phases in
  `engine/include/runtime/FrameDriver.h:26-39`: `DrainAsyncTasks` (3) runs
  before `Simulate` (5), `ExtractRenderPacket` (7), and `Render` (8). Phase
  callbacks are registered in `engine/src/app/EngineFramePhases.cpp:22-256`;
  extraction runs at `EngineFramePhases.cpp:188-201` and
  `renderer.DrawFrameScheduled()` at `:204`.
- Two frames in flight (`VulkanFrameService.h:64`), per-frame transient data
  through `VulkanFrameScratch`, a persistently mapped ring with 1 MiB per frame
  slice (`VulkanFrameScratch.h:41-58`).

### 1.2 Draw path

- `DefaultRenderPipeline` owns `RenderQueue`, `RenderLightSet`,
  `CameraRenderData`, and the two extraction systems by value
  (`engine/include/app/DefaultRenderPipeline.h:39-52`). `ExtractRender` loops
  `ctx.ActiveRegistries` (one `Registry` per visible zone plus the global
  registry), builds the camera, extracts meshes and lights per registry, and
  sorts the queue (`engine/src/app/DefaultRenderPipeline.cpp:44-120`).
- `RenderExtractionSystem::Extract` walks `WorldTransform` +
  `StaticMeshComponent` chunks, computes world bounds (8-corner transform),
  frustum-culls inline, and emits one `RenderQueueItem` per enabled section
  (`engine/src/render/RenderExtractionSystem.cpp:22-105`). Items are copies,
  never pointers into chunks.
- Sort key layout: `[8b pass][16b material][20b mesh][4b section][16b depth]`
  (`engine/src/render/RenderQueue.cpp:6-21`). `SortOpaque` produces a stable
  order array plus instanced runs of consecutive identical mesh+section+material
  (`RenderQueue.h:36-40`, `RenderQueue.cpp:36+`).
- `MeshForwardPass::Draw` uploads one `MeshFrameUniforms` block per view into
  the scratch (dynamic-offset UBO, set 0 binding 0), writes the per-instance
  world-matrix stream (binding 1, instance rate), and records one
  `vkCmdDrawIndexed` per run with push constants `{BaseColor,
  BaseColorTextureIndex}` (`engine/src/render/MeshForwardPass.cpp:101-246`).
  There is exactly one graphics pipeline for all opaque meshes
  (`MeshForwardPass.cpp:55-99`): back-face cull, CCW front face, LESS_OR_EQUAL
  depth, no blending.
- Descriptors: two global sets owned by `VulkanDescriptorCache`. Set 0 is a
  dynamic-offset UBO; set 1 is a 1024-entry update-after-bind bindless
  combined-image-sampler array (`VulkanDescriptorCache.h:14-57`). Pipeline
  layouts are cached by push-constant signature and always use exactly these
  two set layouts (`VulkanDescriptorCache.h:38-40, 85-89`). Descriptor-indexing
  features are enabled unconditionally
  (`engine/src/graphics/vulkan/VulkanDeviceService.cpp:74`).

### 1.3 Lighting today

- `PointLightComponent { Color, Intensity, Range, Enabled }` with schema-driven
  serialization and editor UI (`engine/include/render/PointLightComponent.h:25-50`,
  chunk `'PLGT'`).
- `LightExtractionSystem` gathers every enabled point light in every active
  registry into `RenderLightSet` with no culling, no sorting, in
  chunk-iteration order; lights beyond the cap are dropped first-come
  (`engine/src/render/LightExtractionSystem.cpp:3-32`,
  `RenderLight.h:60-76`).
- `GpuLight` is a tagged 64-byte std140 record with `DirectionCone` and
  `GpuLightType::Spot/Directional` already reserved, and `ShadowIndex` reserved
  for "a future shadow atlas" (`engine/include/render/RenderLight.h:22-45`).
  `kMaxForwardLights = 64` with the stated rationale that the forward pass
  loops every light per fragment (`RenderLight.h:42-45`).
- The fragment shader computes hemispheric ambient (sky/ground blend on
  N.y) plus, per light, Lambert N.L with windowed inverse-square attenuation:
  `window = saturate(1 - (d/range)^4)^2 / d^2`
  (`engine/shaders/mesh_forward.frag.glsl:49-76`). There is no specular, no
  normal mapping, no emission, no shadow term. The light loop branches on
  `TypeShadow.x` with the comment "switch grows for spot/directional"
  (`mesh_forward.frag.glsl:60`).
- Ambient defaults live in `RenderLightSet` (`RenderLight.h:52-53`); the
  `render.ambient.*` cvars that override them are registered only in the
  editor (`editor/kyusu/src/app/EditorServices.cpp:331-336`, applied at
  `editor/kyusu/src/render/EditorRenderFeature.cpp:161-169`). The runtime
  pipeline never registers or reads them, so the comment at `RenderLight.h:51`
  overstates the current wiring.

### 1.4 Material and texture data

- `Material` is the full Decision L glTF metallic-roughness data model: four
  bindless texture slots (base color, normal, ORM, emissive), factors, alpha
  mode; "The current forward shader consumes BaseColor only; the remaining
  slots ride the data until the PBR pass lands"
  (`engine/include/render/Material.h:33-65`).
- `.smat` is JSON with strict unknown-key rejection and `kSmatVersion = 1`
  (`engine/include/assets/material/MaterialFormat.h`,
  `engine/src/assets/material/MaterialLoader.cpp`; keys: `base_color_factor`,
  `base_color_texture`, `normal_texture`, `normal_scale`, `orm_texture`,
  `roughness_factor`, `metallic_factor`, `emissive_factor`,
  `emissive_texture`, `alpha_mode`, `alpha_cutoff`, `version`).
- Texture color space is already correct end to end:
  - Swapchain prefers `B8G8R8A8_SRGB`
    (`engine/src/graphics/vulkan/VulkanSwapchainService.cpp:207-208`), so the
    hardware encodes linear shader output to sRGB on write.
  - Cooked base color and emissive are BC7_SRGB; normals are BC5 (two channel,
    linear, renormalized per mip); ORM is linear
    (`engine/include/assets/cook/TextureCook.h:19-22`,
    `engine/src/assets/cook/TextureCook.cpp:193-197, 350`,
    `engine/include/render/TextureData.h:23-44`).
  - Material load resolves slots with the right colorspace: base color and
    emissive sRGB, normal and ORM linear
    (`engine/src/assets/material/MaterialAssetLoader.cpp:151-157`).
  - Mip generation linearizes sRGB before averaging
    (`TextureCook.cpp:62, 119`).
  So lighting math already happens in linear space and is encoded once at the
  end. This foundation is sound; Phase 3 builds on it unchanged.
- `StaticMeshVertex` already carries a `Vec4 Tangent` (glTF convention,
  bitangent = cross(N, T.xyz) * T.w), generated by MikkTSpace at cook when the
  source lacks it (Decision M)
  (`engine/include/render/static_mesh/StaticMeshVertex.h:9-19`). The forward
  pipeline strides over it but exposes no attribute yet
  (`MeshForwardPass.cpp:77-81`). Normal mapping is therefore a shader and
  pipeline-desc change, not an asset change.

### 1.5 Depth and projection conventions (audit result)

- `MakeVulkanPerspective` (`engine/src/render/Camera.cpp:9-22`) claims
  "reversed-Z: maps near->1, far->0" and `CameraRenderData`'s header repeats
  it (`engine/include/render/Camera.h:19`). Working the matrix through
  disproves the comments: with `result[2][2] = far/(near-far)` and
  `result[2][3] = far*near/(near-far)`, a view-space point at `z = -near`
  lands at NDC depth 0 and `z = -far` at depth 1. The pipeline agrees with the
  matrix, not the comment: depth clear is 1.0
  (`engine/src/graphics/vulkan/Renderer.cpp:260`) and the compare op is
  LESS_OR_EQUAL (`MeshForwardPass.cpp:91`). The renderer is standard [0,1]
  Z. The comments are stale and must be fixed; see Section 2.
- The vertex shader transforms normals with `mat3(world)`
  (`engine/shaders/mesh_forward.vert.glsl:33`). `Transform3d` carries a full
  `Vec<3> Scale` (`engine/include/math/geometry/3d/Transform3d.h:37`), so
  non-uniformly scaled meshes light incorrectly today.
- The vertex and fragment shaders declare different sizes for the same
  `MeshFrame` UBO binding (`mesh_forward.vert.glsl:13-17` vs
  `mesh_forward.frag.glsl:18-29`). Legal in Vulkan, but fragile once shader
  families multiply; shared `.glsli` includes fix it (glslc `-I` and depfiles
  are already supported, `cmake/SenchaShaders.cmake:14, 96`).

### 1.6 Zones, streaming, and where per-zone render data would live

- Registries are split per zone; an entity belongs to a zone by living in that
  zone's `Registry` (`engine/include/zone/ZoneRuntime.h:68-73`,
  `world/registry/Registry.h:68-76`). Per-zone data placed in
  `Registry.Resources` dies with the zone on `DestroyZone`
  (`engine/src/zone/ZoneRuntime.cpp:78-93`).
- Zone loads build a detached registry off-thread and commit at
  `FramePhase::DrainAsyncTasks` (`engine/src/zone/AsyncZoneLoader.cpp:31-105`);
  the per-zone load recipe seam is `ZoneLoadRecipe { Build, Finalize, Preload }`
  (`engine/include/zone/WorldPartitionRuntime.h:18-25`).
- The cooked world manifest (`.sworld` JSON) carries a per-zone cooked trio
  `CookedSceneRef / CookedCollisionRef / CookedContentHash`
  (`engine/include/zone/WorldPartitionManifest.h:60-62`); collision streams as
  pre-baked binary blobs referenced from a JSON sidecar
  (`editor/kyusu/src/document/DocumentCook.cpp:261-276`,
  `engine/src/zone/ZoneCollisionLoader.cpp:51-101`). This is the precedent a
  baked-probe payload follows. Chunked binary infrastructure exists
  (`core/serialization/BinaryFormat.h:19-99`, used by `.smesh` v3 and the
  `'SCNE'` scene format).
- Zone bounds are AABBs (`WorldPartitionManifest.h:56`); "which zone contains
  point X" exists only as runtime-internal policy (`ZoneDemand.h:100`) by
  deliberate invariant (`docs/plans/world-partition-authoring.md:167-169`).
- Per-zone render environment is an explicitly recorded deferral whose trigger
  is "the first per-zone environment mechanism in the renderer"
  (`world-partition-authoring.md:705-708`). Probe volumes are that trigger.

### 1.7 Editor

- Kyusu renders viewports offscreen through the same `MeshForwardPass`
  (`editor/kyusu/src/render/EditorRenderFeature.h:122`, draw at
  `EditorRenderFeature.cpp:347`); it gathers lights itself in
  `SceneRenderQueueBuilder::BuildLights`
  (`editor/kyusu/src/render/SceneRenderQueueBuilder.cpp:234-256`).
- Component inspection is fully schema-driven; no component type is named in
  inspector code (`editor/kyusu/src/ui/InspectorPanel.cpp:437-490`). Adding a
  component to `EngineSceneComponents`
  (`engine/include/world/ComponentManifest.h:33-39`) yields inspector UI,
  add-component menu, and JSON+binary scene serialization with no editor edits.
- The one shared overlay pipeline is `EditorLinePipeline`
  (`editor/kyusu/src/render/EditorLinePipeline.h`); per-component gizmos render
  through `ComponentVisualRenderer`, but `EditorVisual` supports only static
  meshes (`engine/include/core/metadata/EditorVisual.h:18-34`); no light
  visualization exists. Lights are currently selectable only in the hierarchy
  panel; viewport picking handles brush geometry only
  (`editor/kyusu/src/viewport/Picking.cpp:181-276`).
- View toggles live in `WorldViewSettings`
  (`editor/kyusu/src/viewport/WorldViewSettings.h:12-35`) with toolbar buttons
  (`EditorToolbar.cpp:252-255`). There is no long-running-operation or progress
  UI anywhere; level cook is synchronous (`DocumentCook.h:60-79`).
- Shudei's material panel is hand-written per field, with whole-value
  `EditMaterialCommand` undo
  (`editor/shudei/src/MaterialInspectorPanel.cpp:160-242`).

### 1.8 Profiling and diagnostics today

- CPU frame timing is mature: `TimingFrameSample` ring
  (`engine/include/time/TimingHistory.h:7-76`), assembled by `TimingSampler`
  (`engine/src/graphics/vulkan/TimingSampler.cpp:31-63`), displayed by the
  ImGui `TimingPanel` behind the runtime debug overlay
  (`engine/include/debug/IDebugPanel.h`, `ImGuiDebugOverlay`).
- There are zero GPU timestamps (no `VkQueryPool` anywhere), zero Vulkan debug
  labels or object names (only the validation messenger,
  `engine/src/graphics/vulkan/VulkanInstanceService.cpp:56-79`), and no
  structured stats export. A chrome-trace exporter exists but nothing wires it
  (`engine/include/runtime/FrameTrace.h:55-84`).
- `MeshForwardPass` already keeps `DrawStats { QueueItems, DrawCalls }`
  (`MeshForwardPass.h:72-77`); that is the seed of the counter architecture.
- Cvar and console infrastructure is complete: designated-initializer
  `RegisterCVar` (`engine/src/app/EngineConsoleBuiltins.cpp:69-83` is the
  pattern), console commands via `RegisterCommand`
  (`engine/src/core/console/ConsoleService.cpp:227-237`), JSON writing via
  `JsonStringify` (`engine/include/core/json/JsonStringify.h:7`).

### 1.9 Concurrency, tests, skinned meshes

- The bake lane exists: `AsyncTaskQueue::Submit(work, commit)` with commits at
  `DrainAsyncTasks` and a zero-worker deterministic mode
  (`engine/include/jobs/AsyncTaskQueue.h:95-186`); `JobSystem::ParallelFor`
  with the `worker_count == 0` serial reference path
  (`engine/include/jobs/JobSystem.h:11-40`).
- Tests are GoogleTest, auto-globbed per directory
  (`test/CMakeLists.txt:32-34, 95-99, 265-269`); render tests are pure CPU
  (`test/runtime/LightExtractionTests.cpp`,
  `test/engine_features/RenderQueueTests.cpp`); there is no GPU/headless
  device harness. The serial-vs-parallel determinism pattern is
  `test/runtime/ZoneParallelTests.cpp:169-202`.
- Nothing renders skinned meshes yet (caches only,
  `engine/include/render/skinned_mesh/SkinnedMeshCache.h:22-25`); shadow
  casters in Phase 3 are static meshes only.

---

## 2. Architectural problems and prerequisites discovered during inspection

Ordered by how early they must be resolved.

1. **Stale depth-convention comments (correctness trap).**
   `engine/src/render/Camera.cpp:18, 34` and `engine/include/render/Camera.h:19`
   document reversed-Z; the matrices, the 1.0 depth clear
   (`Renderer.cpp:260`), and LESS_OR_EQUAL compare (`MeshForwardPass.cpp:91`)
   are standard [0,1] Z. Anyone building shadow projection or bias math from
   the comments will invert every bias sign. Decision: stay standard-Z (room
   scale far planes do not need reversed-Z precision; switching would churn
   every pipeline, clear, and compare for no visible gain) and fix the three
   comments in Stage 0. Shadow code uses the same standard-Z convention.

2. **Wrong normal transform under non-uniform scale.**
   `mat3(world) * inNormal` (`mesh_forward.vert.glsl:33`) with
   `Transform3d::Scale` being per-axis (`Transform3d.h:37`). Fix in the Stage 1
   vertex shader with the cofactor (adjugate) matrix built from three cross
   products of the world-matrix columns; no CPU plumbing, correct for
   non-uniform scale, and shared by the shadow vertex shader (normals are not
   needed there, but the lit path is the one that matters).

3. **`VulkanImageService` is deliberately 2D-single-layer-only.**
   "Cubemap, volumetric, and non-default-view images are out of scope until a
   feature actually needs them" (`VulkanImageService.h:26-27`). The trigger has
   arrived. It needs: array layers, cube-compatible creation, 3D images,
   per-layer/per-face views for rendering, depth-format usage, and a path that
   leaves images in layouts other than SHADER_READ_ONLY. This is a mechanical
   widening of `ImageCreateInfo` plus view helpers, done once in Stage 3.

4. **`VulkanSamplerCache` cannot express comparison samplers.**
   `SamplerDesc` (`VulkanSamplerCache.h:27-39`) lacks compare enable/op and
   border color. Hardware PCF needs `compareEnable = VK_TRUE`, op
   LESS_OR_EQUAL, and a white border (outside atlas tile = unshadowed). Small
   struct extension in Stage 3; the hash/equality already key on the whole
   struct.

5. **Pipeline layouts only know the two global sets.**
   `VulkanDescriptorCache::GetPipelineLayout` builds layouts from FrameSet +
   BindlessSet only (`VulkanDescriptorCache.h:38-40, 85-89`). Shadow maps and
   probe volumes cannot ride the bindless sampler2D array (different GLSL
   sampler types: `sampler2DShadow`, `samplerCubeArrayShadow`, `sampler3D`).
   Decision: introduce one additional descriptor set (set 2) owned by the new
   lighting module, containing the shadow atlas, the point cube array, and the
   probe volume textures; extend `GetPipelineLayout` to accept optional extra
   set layouts in the cache key. One set, fixed bindings, rewritten only
   between frames (double-buffered per frame in flight), no update-after-bind
   needed.

6. **Graphics pipelines require a fragment shader.**
   `CreateGraphicsPipeline` errors on a null FS module
   (`engine/src/graphics/vulkan/VulkanPipelineCache.cpp:139-145`). Shadow depth
   passes use a trivial empty fragment shader (`void main() {}`) rather than
   changing the cache contract. Depth bias state already exists in the desc
   (`VulkanPipelineCache.h:100-102`).

7. **Light gather is unbounded, unsorted, and order-unstable at the cap.**
   Lights are packed in chunk order per registry and dropped first-come past
   64 (`RenderLight.h:60-66`, `LightExtractionSystem.cpp:17-31`). With
   multiple zones, which light gets dropped depends on zone attach order.
   Stage 2 adds camera-frustum sphere culling, importance sorting, and a
   stable tie-break so the packed set is deterministic for identical world
   state.

8. **Casters are not extractable today.**
   The only mesh gather is camera-frustum-culled inline
   (`RenderExtractionSystem.cpp:69`), but casters outside the camera frustum
   still cast into it. Shadows need a second, camera-independent caster gather
   (Stage 3) plus a `CastShadows` field on `StaticMeshComponent`.

9. **`render.ambient.*` cvars are editor-only.**
   Registered in `EditorServices.cpp:331-336`, absent from the runtime, while
   `RenderLight.h:51` implies otherwise. Stage 0 moves registration into the
   engine (`EngineConsoleBuiltins.cpp`) so runtime and editor share one
   definition; the editor keeps only its per-frame poll.

10. **Frame UBO growth budget.**
    `MeshFrameUniforms` is ~4.2 KiB today (`MeshForwardPass.cpp:18-31`).
    Phase 3 grows it (spot cone params, shadow slot matrices, probe volume
    headers) to ~6.5 KiB, still under the 16 KiB guaranteed dynamic-UBO range
    the current design leans on (`RenderLight.h:42-44`). No storage-buffer
    migration is needed in this phase; note it as the escape hatch if light or
    shadow caps ever rise.

11. **Query-cache thrash across registries (measure, then fix).**
    `RenderExtractionSystem`/`LightExtractionSystem` cache one `Query` keyed by
    a single `World*` sentinel (`RenderExtractionSystem.h:22-24`), but
    `DefaultRenderPipeline::ExtractRender` calls them once per active registry
    (`DefaultRenderPipeline.cpp:86-102`), busting the cache every call when
    more than one zone is resident. Flagged for the Stage 7 review with a
    counter; the fix (a small per-registry cache map) is mechanical if the
    numbers justify it.

12. **`.smat` unknown-key strictness vs new fields.**
    Unknown keys are errors by design (pipeline.md:661-663). Adding fields
    requires a version bump to `kSmatVersion = 2` with the loader accepting
    1 and 2 (v1 files get defaults), and the writer emitting 2. Old binaries
    reading v2 files fail loudly, which is the intended failure mode.

13. **Editor has no async-operation or progress UI.**
    The probe bake is the first long-running editor operation. It rides
    `engine.Tasks()` (the existing cross-frame lane) with progress polled in
    `EditorServices::ProcessFrame` (`EditorServices.cpp:651-701`); no new
    concurrency mechanism is introduced.

None of these require pushing back on the request itself; no CLAUDE.md
invariant is violated by the feature set. The one deliberate architecture
addition is the third descriptor set (item 5), justified as a real renderer
boundary: multiple passes (forward, future transparent) consume the same
lighting resources.

---

## 3. Recommended Sencha material and shading model

### 3.1 Decision

Keep the Decision L material data exactly as authored (base color, normal,
ORM, emissive, factors) and give it a stylized, non-PBR evaluation named
**StandardLit**. Do not adopt a metallic-roughness BRDF; do not add a parallel
material schema. The data stays glTF-shaped (import-friendly, already cooked,
already refcounted); the shading is Sencha's.

This resolves the apparent conflict between "design a StandardLit model rather
than PBR" and the codebase's PBR-shaped `.smat`: Decision L explicitly shipped
"the data, not the lighting" (pipeline.md:643). The schema is an authoring
vocabulary; StandardLit is its renderer interpretation.

### 3.2 The lighting equation

Per fragment, in linear space:

```
N        = normalize(TBN * reconstructZ(sampleBC5(normalMap)))   (or vertex N)
ambient  = probeIrradiance(N)  if a probe volume covers the point
           else hemiAmbient(N)                    (existing sky/ground blend)
diffuse  = sum over lights: wrap(N, L) * atten * shadow * lightColor
specular = sum over lights: normBlinnPhong(N, H, exponent) * specIntensity
           * specTint * atten * shadow * lightColor
emission = emissiveFactor.rgb * emissiveStrength * emissiveTex
color    = baseColor * (ambient + diffuse) + specular + emission
out      = shoulder(color * exposure)
```

Component decisions:

- **Diffuse: wrapped Lambert.** `wrap(N, L) = saturate((dot(N,L) + w) / (1 + w))`
  with `w` a renderer-level cvar (`render.style.diffuse_wrap`, default 0.25,
  0 = pure Lambert). This is the softened-terminator look the current stylized
  response gestures at, made explicit and tunable in exactly one place. It is
  a style control, not a material control: per-material wrap is rejected to
  avoid toggle sprawl.
- **Specular: normalized Blinn-Phong.** `((n + 8) / 8) * pow(dot(N,H), n)`,
  half-vector from the existing `ViewPositionTime` camera position
  (`MeshForwardPass.cpp:106`). Normalization keeps highlight energy roughly
  constant as width changes, so one artist slider behaves predictably.
  Rejected alternatives: GGX (wrong era of highlight, more ALU), plain
  unnormalized Phong (exponent changes brightness).
- **Roughness maps to exponent perceptually.** `n = exp2(mix(11.0, 1.0,
  roughness))`, i.e. roughness 0 -> n = 2048, roughness 1 -> n = 2. Roughness
  is artist vocabulary over a stylized lobe width, with no physical claim.
  Source: `RoughnessFactor` times the ORM green channel, both already in the
  data (`Material.h:59-61`, `TextureData.h:41`).
- **Specular intensity and tint.** New scalar `SpecularIntensity` (0..1,
  default 0.5) added to the schema (Section 3.4). Specular tint reuses the
  existing metallic data stylistically: `specTint = mix(white, baseColor.rgb,
  metallic)`. Metallic thus keeps a meaning (tinted highlights for metals)
  without a full metallic workflow. No separate specular color field in v1.
- **Emission.** `EmissiveFactor.rgb * EmissiveStrength * emissiveTexture`,
  strength a new scalar defaulting to 1 (the Vec4 factor's w is currently
  unused and defaults to 0, so overloading it would silently zero emission on
  existing materials; a separate field avoids that trap). Emission is added
  after diffuse/specular so lights do not modulate it.
- **Attenuation.** Keep the existing windowed inverse-square exactly as is
  (`mesh_forward.frag.glsl:68-72`); it is correct, local, and already tuned.
  Spot lights multiply the same attenuation by the cone falloff (Section 5 of
  the spot section below).
- **Exposure and shoulder.** `render.exposure` (default 1.0) and a soft
  shoulder `c / (1 + luminance(c))` gated by `render.tonemap` (default on)
  applied at the end of the fragment shader. This is explicitly the interim
  stand-in for the reserved Post phase (engine-roadmap.md:336-338): the
  forward pass renders straight to the sRGB swapchain, so there is no HDR
  intermediate to tonemap in post yet. When the Post phase lands, this block
  moves there verbatim. Without it, emission and tight speculars hard-clip.

### 3.3 Renderer-level style controls (the complete list)

All are engine-registered cvars (Stage 0/1), archived, polled per frame by the
pipelines exactly as the editor already polls ambient
(`EditorRenderFeature.cpp:161-169`):

| Cvar | Default | Meaning |
|---|---|---|
| `render.ambient.sky_r/g/b`, `render.ambient.ground_r/g/b` | current defaults | Hemispheric fallback ambient (moved from editor-only registration) |
| `render.style.diffuse_wrap` | 0.25 | Wrapped-diffuse width |
| `render.style.min_ambient` | 0.0 | Floor added to ambient (legibility guard in unbaked rooms) |
| `render.exposure` | 1.0 | Pre-shoulder scale |
| `render.tonemap` | true | Soft shoulder on/off |
| `render.shadow.darkness` | 1.0 | Global shadow attenuation scale (1 = fully dark shadows) |
| `render.shadow.softness` | 1.0 | Global multiplier on per-light softness |

Light-response ramp textures are rejected for this phase: a global ramp adds a
sampler binding and an authoring pipeline for one stylistic degree of freedom
that wrap + shoulder already cover. Recorded as deferred (Section 15).

### 3.4 Material schema changes (`.smat` v2)

`MaterialDescription` (`MaterialFormat.h:18-35`), `Material` (`Material.h`),
loader, writer, and shudei gain:

| JSON key | Type | Default | Runtime field |
|---|---|---|---|
| `specular_factor` | float 0..1 | 0.5 | `Material::SpecularIntensity` |
| `emissive_strength` | float >= 0 | 1.0 | `Material::EmissiveStrength` |
| `shading` | `"standard_lit"` \| `"unlit"` | `standard_lit` | `Material::Shading` (enum `MaterialShading`) |
| `double_sided` | bool | false | `Material::DoubleSided` |
| `receive_shadows` | bool | true | `Material::ReceiveShadows` |
| `cast_shadows` | bool | true | `Material::CastShadows` |

`kSmatVersion` bumps to 2; the loader accepts 1 and 2 (v1 loads with
defaults), the writer always writes 2. Unknown keys remain errors.

### 3.5 Shader families and pipeline variants

Two fragment families, one shared vertex shader, all built from shared
`.glsli` includes (`frame_uniforms.glsli`, `lighting.glsli`,
`shadow_sampling.glsli`, `probe_sampling.glsli`) so the vert/frag UBO
declaration mismatch (finding 1.5) disappears:

- **StandardLit** (`mesh_forward.frag.glsl`, extended): full model above.
- **Unlit** (`mesh_unlit.frag.glsl`, new): `baseColor * texture + emission`,
  no lights, no shadows received, still depth-tested and depth-written, still
  a shadow caster if its material says so. The emissive-unlit use case (glowing
  panels, screens, skies-on-geometry) is Unlit with an emissive texture and
  strength; it is not a third family.

Pipeline count is deliberately tiny. Properties that genuinely require
separate `VkPipeline`s: fragment shader family (2) x cull mode (back,
none for double-sided) = 4 opaque pipelines, plus the shadow-depth pipelines
(Section 4/5) and debug-only pipelines (Section 9). Everything else (textures,
factors, specular, emission, shadow receive) is push-constant or UBO data in
the one StandardLit pipeline. There is no per-feature shader permutation and
no variant matrix: a material with no normal map samples the flat-normal
default the neutral-slot system already guarantees (`Material.h:42-45`).

Pipeline selection joins the sort key by carving 2 bits from the material
field: `[8b pass][2b pipeline][14b material][20b mesh][4b section][16b depth]`
(`RenderQueue.cpp:6-21`), so runs never straddle pipelines and switches are
counted and minimized by sorting. 16384 material slots remain, far above
current content.

Push constants grow from 20 bytes to 64 (still under the 128-byte guaranteed
minimum): base color (16), emissive rgb + strength (16), params (specular
intensity, roughness factor, normal scale, flags: receiveShadows) (16),
texture indices (base, normal, orm, emissive) (16). `MeshPushConstants`
static asserts extend accordingly (`MeshForwardPass.cpp:15-16`).

### 3.6 Color-space and convention audit disposition

From Section 1.4/1.5: linear lighting, sRGB sampling, sRGB framebuffer
encoding, BC5 normal handling (always reconstruct Z from XY; works identically
for uncompressed linear RGBA normals), and attenuation are already correct.
The three defects to fix are the stale reversed-Z comments (Stage 0), the
non-uniform-scale normal transform (Stage 1, cofactor matrix), and tangent
consumption (Stage 1, attribute location 7 plus TBN in the vertex shader,
MikkTSpace convention already documented at `StaticMeshVertex.h:15-17`).
Exposure/tonemap do not exist today; Section 3.2 adds the minimal version.

---

## 4. Recommended shadow technique for spot lights

**Decision: conventional depth shadow maps in one shared 2D atlas, sampled
with hardware-comparison PCF through a 3x3 tent filter.**

Concretely:

- **Storage.** One `2048x2048` D16_UNORM depth image (8 MiB), usage
  DEPTH_STENCIL_ATTACHMENT | SAMPLED, owned by the renderer (never by
  components). Tiles allocated at 256/512/1024 by a 3-level quadtree
  (Section 6). D16 is sufficient for room-scale spot ranges with the near
  plane pushed out (below); the format is probed at startup with D32_SFLOAT
  fallback, mirroring `VulkanDepthTarget::ChooseDepthFormat`
  (`VulkanDepthTarget.h:46`).
- **Projection.** Standard [0,1] Z (matching the audited main-pass
  convention), perspective FOV = 2 x outer cone angle, near plane =
  `max(0.05, 0.02 * Range)` and far = `Range`. The near plane is the first
  acne lever: pushing it out spreads depth precision over the lit volume.
- **Rendering.** Depth-only pipeline: shared `shadow_depth.vert.glsl`
  (position from the same per-instance matrix stream the forward pass uses)
  plus an empty fragment shader (pipeline-cache contract, finding 2.6).
  Pipeline-level slope-scaled + constant depth bias
  (`GraphicsPipelineDesc::DepthBias*`, `VulkanPipelineCache.h:100-102`).
  Casters keep back-face culling (front faces render), same as the main pass:
  kyusu brush geometry cooks to closed solids, but thin authored props exist,
  and front-face rendering avoids their peter-panning; double-sided materials
  render both faces in the shadow pass too.
- **Sampling.** `sampler2DShadow` with `compareEnable`, LESS_OR_EQUAL, white
  border color (border addressing outside the tile = unshadowed; the border
  also guards atlas-neighbor bleed). Filter: 9 hardware-PCF taps in a 3x3
  tent, tap spacing = `ShadowSoftness` texels (per-light, default 1.5) times
  `render.shadow.softness`. Each hardware tap is itself a 2x2 comparison, so
  the effective kernel is ~6x6: soft-edged, stable (no per-pixel rotation
  noise), and it deliberately reads as slightly blurry, which hides 256/512
  texel footprints exactly as the brief wants. A 5-tap variant is selected
  automatically for tiles at the 256 tier.
- **Acne and peter-panning controls** (each independently debuggable,
  Section 9):
  1. Slope-scaled bias (pipeline, cvar `render.shadow.bias_slope`, default 2.0).
  2. Constant bias (pipeline, cvar `render.shadow.bias_const`, default 4 =
     DepthBiasConstant units, format-relative).
  3. Receiver normal-offset: shift the sampled world position along the
     geometric normal by `texelWorldSize * ShadowBiasScale` before projecting
     into light space. Texel world size is computed per light from tile
     resolution and cone angle and uploaded in the shadow slot record. This is
     the primary defense; it scales with resolution so low tiers do not acne.
  4. Near-plane selection as above.
  5. Front-face rendering as above (back-face rendering is the rejected
     alternative: it trades acne for light leaks through walls thinner than
     the bias, and brush levels have many door-frame-thickness walls).
  6. Per-light `ShadowBiasScale` multiplier for the rare problem light.
  Raising resolution is explicitly not the acne strategy; the defaults are
  tuned at 512 and verified at 256.

**Rejected for spots.** Variance and exponential-variance maps: two extra
color targets, a separable blur pass per light, light bleeding between
overlapping occluders, and mip/filter machinery, all to buy softness the tent
filter already provides at our light counts. Poisson-disc with per-pixel
rotation: shimmering noise under camera motion, which reads as modern and
wrong for the target look; the tent is stable. Per-light dedicated textures
(no atlas): more bindings, more barriers, no packing flexibility.

---

## 5. Recommended shadow technique for point lights

**Decision: depth cube-map array with hardware-comparison sampling, small
fixed budget, per-face depth reconstruction.**

- **Storage.** One `VkImage` cube array: 512x512 D16, 6 faces x
  `kMaxShadowedPointLights = 4` cubes = 24 layers (12 MiB). Created
  cube-array-compatible; `imageCubeArray` is a core 1.0 device feature enabled
  through the existing `IRenderFeature::Contribute` bootstrap seam
  (`Renderer.h:116-120`), which is exactly what that seam exists for. Per-face
  2D views for rendering; one cube-array view for sampling.
- **Rendering.** Six per-face passes with 90-degree perspective (near =
  `max(0.05, 0.02 * Range)`, far = Range), the same depth-only pipeline and
  bias stack as spots. Each face frustum-culls the caster set independently, so
  a light against a wall renders roughly half its faces empty. Six faces is
  the honest cost of an omni shadow; it is budgeted, not hidden: the editor
  UI displays it (Section 10) and update policies amortize it (Section 6).
- **Sampling.** `samplerCubeArrayShadow`. The comparison reference is
  reconstructed from the fragment-to-light vector: take the major axis
  distance `z = max(|v.x|, |v.y|, |v.z|)` and project through the face
  near/far to [0,1] depth. Filter: 5 taps (center + 4 offsets perpendicular
  to the sample direction, spaced by softness), each a hardware 2x2
  comparison. Face-edge seams are a non-issue with hardware cube filtering of
  comparison samplers on desktop hardware.
- **Radial-distance representation (store `length(v)/far` in a color target
  and compare manually): rejected.** It simplifies bias reasoning (uniform
  world-space bias) but costs an extra R16 color attachment per face, loses
  hardware-comparison filtering, and its one advantage is already covered by
  normal-offset biasing. Kept in Section 15 as the fallback if per-face depth
  reconstruction proves fiddly on some driver.
- **Dual-paraboloid and octahedral projections: rejected.** Both halve or
  quarter the pass count but warp straight edges of low-poly brush geometry
  unless casters are tessellated, and both introduce seam filtering work. At a
  budget of 4 point shadows the cube array is simpler and visually safer; the
  exotic projections would only pay off at light counts this phase explicitly
  does not target.

Point and spot shadows share: the depth-only shader pair, the caster set, the
bias stack, the residency arbiter, the budget cvars, and the debug views. They
differ only in storage object and the projection/sampling math.

---

## 6. Shadow allocation, caching, and invalidation architecture

### 6.1 Ownership split

Components describe intent; the renderer owns every GPU resource and all
scheduling state. Nothing on a component references an atlas slot, an image,
or a frame.

Authoring state (ECS, serialized, schema-driven UI for free):

```
PointLightComponent (extended)      SpotLightComponent (new, Section on ECS below)
  Color, Intensity, Range, Enabled    Color, Intensity, Range, Enabled
  CastShadows        = false          Direction from WorldTransform forward axis
  ShadowResolution   = Medium         InnerAngleDegrees = 25, OuterAngleDegrees = 35
  ShadowUpdate       = OnChange       CastShadows, ShadowResolution, ShadowUpdate,
  ShadowSoftness     = 1.5            ShadowSoftness, ShadowBiasScale (same fields)
  ShadowBiasScale    = 1.0
```

`ShadowResolution` is `ShadowResolutionTier { Low = 256, Medium = 512,
High = 1024 }` (points clamp High to 512 per face; a 1024 cube is 4x the
memory and never worth it at this art scale). `ShadowUpdate` is
`ShadowUpdatePolicy { EveryFrame, OnChange, Static }`. Shadow enablement
defaults off: a light is cheap until someone opts into shadows.

Renderer state (new `engine/{include,src}/render/shadow/`):

- `ShadowAtlas`: the 2D depth image + 3-level quadtree slot allocator
  (1024/512/256). Pure allocation logic separated from Vulkan so it unit-tests
  headlessly.
- `ShadowCubePool`: the cube array + a 4-slot allocator.
- `ShadowResidency`: the arbiter. Input: this frame's packed lights (with
  entity ids and zone ids), camera, budgets. Output: per-light `ShadowIndex`
  (into the shadow-slot UBO array) and a list of `ShadowView`s to render this
  frame. Owns per-slot cache state (light-state hash, valid flag, last-used
  frame, score history). CPU-only, deterministic, unit-testable.
- `ShadowView`: one render job = { kind (spot / point face), target (atlas
  rect or cube layer+face), view-projection matrix, caster cull volume }.
- `ShadowCasterSet` + `ShadowCasterExtractionSystem`: camera-independent
  gather of `{ Mesh, SectionIndex, WorldMatrix, WorldBounds }` for every
  visible-zone entity whose `StaticMeshComponent.CastShadows` and material
  `CastShadows` are both true, plus the frame's moved-caster bounds list (see
  6.4). Runs inside `DefaultRenderPipeline::ExtractRender` beside the existing
  extractors (`DefaultRenderPipeline.cpp:86-102`).
- `ShadowDepthPass`: records the depth-only draws for one `ShadowView`
  (per-view frustum cull of the caster set, mesh-sorted instanced runs,
  same instancing mechanism as `MeshForwardPass::BindInstanceStream`,
  `MeshForwardPass.cpp:121-140`). Factored like `MeshForwardPass` so kyusu
  viewports reuse it.
- `ShadowRenderFeature (IRenderFeature)`: runs in a new `RenderPhase::Shadow`
  bucket ordered before `Offscreen` (`Renderer.h:57-65` reserves the name;
  before-Offscreen ordering means editor viewports can sample shadows too).
  Owns barriers: per-tile DEPTH_ATTACHMENT rendering, then one
  atlas/cube-array transition to DEPTH_READ_ONLY + SHADER_READ before
  MainColor samples it (sync2 helpers, `VulkanBarriers.h:21-39`).
- `LightBindings`: the set-2 descriptor set (Section 2 item 5): binding 0 =
  atlas `sampler2DShadow`, binding 1 = cube array `samplerCubeArrayShadow`,
  binding 2 = probe volume `sampler3D` array (Section 8), double-buffered per
  frame in flight.

GPU-side per-frame data appended to `MeshFrameUniforms`:

```
GpuShadowSlot   Spots[kMaxShadowedSpotLights = 8]   // 96 B each:
    Mat4 WorldToShadow; Vec4 AtlasScaleBias; Vec4 Params(texelWorld, softness, biasScale, pad)
GpuPointShadow  Points[kMaxShadowedPointLights = 4] // 16 B each:
    Vec4 Params(near, far, softness, cubeLayer)
```

`GpuLight.ShadowIndex` (already reserved, `RenderLight.h:35`) indexes these
arrays; UINT32_MAX stays "no shadow".

### 6.2 Budgets and prioritization

Budgets are cvars: `render.shadow.max_spot = 8`, `render.shadow.max_point = 4`,
`render.shadow.atlas_size = 2048`. A shadow-enabled component is a request,
never a guarantee.

Each frame `ShadowResidency` scores every shadow-requesting light that
survived light culling:

```
score = Intensity * saturate(Range / distance(camera, light))^2
        * (light volume intersects camera frustum ? 1 : 0.25)
```

plus hysteresis: a light currently holding a slot gets a 1.25x multiplier, and
a slot cannot be stolen until its holder has been outscored for 30 consecutive
frames. Ties break on `EntityId` then `ZoneId`, so allocation is deterministic
and does not flicker when two lights hover at equal importance. Over-budget
lights render unshadowed (their `ShadowIndex` stays UINT32_MAX); a counter and
an editor warning surface it (Sections 9, 10). Tier downgrade under pressure
(High request served at Medium when the atlas is full) is applied before
outright denial.

### 6.3 Update policies

Per-light `ShadowUpdate`:

- `EveryFrame`: re-render whenever resident (flashlights, moving elevators'
  lights).
- `OnChange` (default): re-render when the light's state hash changes or a
  moved caster intersects its volume (6.4).
- `Static`: render once on slot acquisition; afterwards only explicit
  invalidation (editor edit, console command `render.shadow.invalidate`)
  re-renders. For editor-authored fixed scene lighting.

Newly acquired slots always render on acquisition. Per-frame shadow view
count is additionally clamped by `render.shadow.max_views_per_frame`
(default 12) with overflow deferred to following frames, oldest-invalid
first: a worst-case zone load amortizes over a few frames instead of spiking
one.

### 6.4 Invalidation without an observer system

Two detection mechanisms, both local to extraction, no global coupling:

1. **Light change**: `ShadowResidency` hashes the packed light state it
   already receives (position, rotation-derived direction, range, cone
   angles, tier) and compares against the slot's stored hash. Renderer-side
   state diffing; the ECS never calls into the renderer.
2. **Caster movement**: `ShadowCasterExtractionSystem` runs a second cached
   query with `Changed<WorldTransform>` + `StaticMeshComponent` and collects
   the world bounds of moved casters (both old bounds are unknown, so the new
   bounds are taken conservatively; chunk-conservative change detection means
   false positives, which only cost a redundant re-render). `ShadowResidency`
   intersects the moved-bounds list against each resident `OnChange` shadow
   volume (sphere for points, cone-bounding sphere for spots) and invalidates
   on overlap.

Zone attach/detach invalidates trivially: a light whose zone unloads
disappears from extraction, its slot ages out and frees; newly attached zones'
casters arrive as new bounds. Per-zone shadow residency needs no extra
mechanism because extraction is already per-active-registry.

### 6.5 Caching effect

Steady state for an indoor scene of `Static`/`OnChange` lights is zero shadow
draws: the pass renders only invalidated views. `ShadowCacheHits/Misses`
counters (Section 9) make the cache observable, and the atlas debug view shows
slot ages.

---

## 7. Baked-lighting recommendation

**Decision: zone-scoped irradiance probe volumes, baked in the editor against
cooked collision geometry, streamed with zones, sampled per fragment. No
surface lightmaps in this phase.**

Comparison against the alternatives, grounded in this codebase:

1. **Traditional surface lightmaps: rejected.** They require a second UV set
   (the cooked vertex just moved to tangents, Decision M; another vertex
   stream bump plus an unwrapper/packer is a large tool investment), per-texel
   density management over brush geometry that recooks per edit
   (`DocumentCook` re-cells geometry per cook,
   `editor/kyusu/src/document/DocumentCook.cpp:238-258`, so packing would
   churn), streaming of per-zone lightmap pages, seam handling, and long
   bakes. They also do nothing for dynamic objects, which still need probes.
   Cost/benefit is wrong for stable environmental light and mood.
2. **Baked per-vertex irradiance: rejected as the primary mechanism.** Fits
   the target era and is cheap to sample, but couples bake output to vertex
   density (brush-cooked cells are coarse), invalidates on every geometry
   cook, needs a vertex-format bump, and still leaves dynamic objects
   unsolved. Recorded as a possible later addition for hero static meshes.
3. **Irradiance probes (chosen), zone-scoped volumes (chosen).** One mechanism
   covers static and dynamic receivers per directive 3 (behavior from data,
   one pipeline); storage is decoupled from geometry so a brush edit
   invalidates the bake logically, not structurally; zone scoping gives
   streaming, eviction, and leak containment for free via the existing
   registry lifecycle (Section 1.6).
4. **Hybrid direct-dynamic / indirect-baked (chosen by construction).** Direct
   lighting stays fully dynamic (points, spots, their shadows); probes replace
   only the hemispheric ambient term. This is the smallest bake that changes
   how rooms feel.

What the first bake computes (deliberately modest): for each probe, N = 128
fixed-pattern cosine-stratified rays; each ray is traced against the zone's
cooked collision (the Jolt query path already loaded per zone,
`ZoneCollisionLoader.cpp:51-101`); a miss contributes sky/ground hemispheric
color by ray direction; a hit contributes the direct lighting at the hit point
(Lambert from the zone's static shadow-casting lights, occlusion tested with
one shadow ray each) times a constant bounce albedo (`render.bake.albedo`
cvar, default 0.35). The result is projected into L1 spherical harmonics. That
yields sky occlusion, one indirect bounce, and colored room mood. It is not
modern GI and does not try to be.

Determinism: ray directions come from a precomputed table seeded by probe
index (no `Date`/`random` at bake time), the bake parallelizes per probe row
via `JobSystem::ParallelFor`, and the `worker_count == 0` path is the
reference; a test asserts serial and parallel bakes are bit-identical
(pattern: `test/runtime/ZoneParallelTests.cpp:169-202`).

Storage and streaming follow the collision precedent exactly (Section 1.6):

- New chunked binary `.sprobe` (FourCC `'SPRB'`, `kProbeFormatVersion = 1`,
  `BinaryHeader` + `ChunkHeader` per `core/serialization/BinaryFormat.h`),
  one file per zone, containing per-volume grids of L1 SH RGB (12 fp16
  coefficients = 24 bytes per probe) plus a validity bitset.
- `ZoneHeader` gains `CookedProbeRef` + `CookedProbeContentHash` beside the
  existing cooked trio (`WorldPartitionManifest.h:60-62`), written only when
  nonempty (the manifest reader tolerates unknown keys,
  `WorldPartitionManifest.h:112-114`).
- Runtime load rides `ZoneLoadRecipe`: bytes read in `Build` (off-thread),
  GPU volume textures created and registered in `Finalize` (main thread at
  `DrainAsyncTasks`), residency dropped on zone destroy.

Runtime sampling: per fragment. The fragment finds its volume by testing the
world position against the resident volume headers in the frame UBO (at most
`kMaxResidentProbeVolumes = 8` active; smallest containing volume wins, ties
by priority then volume id), then samples three RGBA16F 3D textures (one per
color channel, each texel holding that channel's four L1 SH coefficients)
with hardware trilinear filtering and evaluates irradiance for N. Fragments covered by no volume fall back to
the existing hemispheric ambient, so unbaked zones look exactly as they do
today. Per-fragment selection is chosen over per-instance volume indices
because instanced runs merge across zones (`RenderQueue` merges by
mesh/material only, Section 1.2) and because room-sized cooked cells need
intra-draw ambient variation; 8 AABB tests per fragment is trivial ALU.

Leak control, in order of effect: volumes are authored per room (component
below), so interpolation never spans a wall between rooms; probes whose bake
rays classify them inside geometry (majority of short rays hit backfaces) are
marked invalid and take dilated values from valid neighbors, and validity
weights the trilinear blend; volume membership is zone-restricted by
construction (a volume lives in its zone's registry, and resident volume
headers carry the zone id). Per-probe occlusion-weighted interpolation is
deferred (Section 15).

Dynamic objects sample the same volumes per fragment; nothing special-cases
them. Normal-dependent response comes from L1 SH evaluation.

---

## 8. Probe-volume and 3D-grid recommendation

**Decision: reuse the existing lattice types; add one small value type; do not
build a generalized spatial-field framework this phase.**

Inspection result the request did not anticipate: Sencha already has
`Grid3d<T>` (`engine/include/math/spatial/Grid3d.h`), a flat dense 3D array
with index math, alongside `Grid2d`, `GridPlane`, and `QuadTree`. What is
missing is only the world mapping.

- Add `math/spatial/GridTransform3d`: origin, per-axis cell size, integer
  dimensions; world-to-cell, cell-to-world, cell AABB, `Contains`, and
  trilinear weights for a world point. Pure value type, unit-tested, no
  storage, no streaming, no GPU knowledge.
- `IrradianceProbeGrid` composes `GridTransform3d` + `Grid3d<ProbeSh>` +
  `Grid3d<uint8 validity>` and is the bake output / `.sprobe` payload / GPU
  upload source.
- The authored component is `IrradianceVolumeComponent { Extents (half-size),
  CellSize = 1.0, Priority = 0, Enabled }`, positioned by the entity's
  `WorldTransform` (axis-aligned; rotation is ignored and the editor gizmo
  says so), living in the zone it lights. Registered in
  `EngineSceneComponents` (`ComponentManifest.h:33-39`) for free UI and
  serialization, chunk `'IRVL'`.

The generalized chunked/streamed/GPU-resident spatial-field primitive the
request floats (water, voxels, occupancy) is explicitly deferred under
directive 4: today it would have exactly one consumer, and probe volumes,
water, and voxel systems will want radically different storage (dense fp16
grids vs chunked SDF vs simulation ping-pong buffers). `Grid3d` +
`GridTransform3d` are the real shared substrate; anything more is speculative
abstraction until a second concrete user exists. When one arrives, the
extraction of shared chunked storage is a mechanical refactor over these value
types.

---

## 9. Profiling and debugging architecture

Built in Stage 0 so every later stage lands with numbers attached.

### 9.1 GPU timing

- New `graphics/vulkan/GpuTimestampPool`: one `VkQueryPool`
  (TIMESTAMP, ~64 queries) per frame in flight; `Begin/End(scopeId)` writes
  `vkCmdWriteTimestamp2`; results resolved with availability-checked
  `vkGetQueryPoolResults` when that frame's fence has already been waited
  (two-frames-later readback, no stalls), scaled by
  `VkPhysicalDeviceLimits::timestampPeriod`.
- Scopes: one per `RenderPhase` bucket plus one per feature, plus
  shadow-view batches (`Shadow/SpotViews`, `Shadow/PointFaces`) and the
  forward pass. Scope table is fixed at feature setup; no per-frame strings.
- Timings append to `TimingFrameSample` (new fields: `GpuSeconds` total and a
  small fixed array of named scope spans) via `TimingSampler::PushRenderFrame`
  (`TimingSampler.cpp:31-50`), so the existing `TimingPanel` plumbing carries
  them.

### 9.2 Debug labels and object names

- New `graphics/vulkan/VulkanDebugLabels`: free functions
  (`BeginLabel/EndLabel/InsertLabel/NameObject`) in the style of
  `VulkanBarriers` (`VulkanBarriers.h:10-13`), loaded from
  `VK_EXT_debug_utils` when validation is enabled and no-ops otherwise.
  Renderer phases, features, shadow views, and all Phase 3 images/pipelines
  get names. This is what makes RenderDoc captures of the new passes legible.

### 9.3 Counters

- New `render/RenderStats.h`: one plain aggregate reset each frame and filled
  by the systems that own each number (extraction fills visibility numbers,
  passes fill draw numbers, residency fills shadow numbers), extending the
  existing `MeshForwardPass::DrawStats` pattern (`MeshForwardPass.h:72-77`):
  visible/culled objects, draw calls, instanced draws, submitted triangles
  (index counts summed at extraction), lights visible / culled / dropped at
  cap, per-draw considered lights (min/avg/max: with the global light loop
  this equals visible lights, and the counter exists precisely to expose
  that), shadow-casting lights, shadow views rendered, shadow caster draws,
  atlas tiles used per tier, shadow cache hits/misses, shadow memory,
  pipeline switches, material switches (push-constant updates), descriptor
  updates, probe volumes resident, probe memory, and the extraction query
  rebuild count (finding 2.11).
- `DefaultRenderPipeline` owns the frame's `RenderStats`, hands sub-structs to
  features at wiring time, and pushes the finished frame into a stats ring
  beside `TimingHistory`.

### 9.4 Capture export

- New `render/RenderCapture`: a ring of `{TimingFrameSample, RenderStats}`
  records; console commands `render.capture.start [n]` /
  `render.capture.stop` / `render.capture.write <path>` (registered via
  `ConsoleService::RegisterCommand`, pattern `ConsoleService.cpp:227-237`)
  serialize to JSON (schema-versioned envelope: build info, cvar snapshot,
  scene name, then per-frame records) or CSV (one row per frame, one column
  per counter) using `JsonStringify` (`JsonStringify.h:7`). Buffered structs,
  serialized only at write time, per the documented `JsonValue` hot-path
  caveat (`JsonValue.h:19-22`).
- This file is the interface for AI-assisted analysis: stable keys, explicit
  units (`_ms`, `_bytes`, `_count`), machine-diffable. No heuristics live in
  the renderer.

### 9.5 Debug views

One `render.debug.view` cvar (enum) uploaded as a uint in `MeshFrameUniforms`
and branched uniformly at the end of the StandardLit fragment shader (uniform
control flow, zero cost when 0): world normals, tangent-space normal-map
sample, geometric vs mapped normal delta, diffuse only, specular only,
emission only, roughness/exponent, light complexity (heat ramp on per-fragment
light iterations), shadow term only, shadow bias visualization (raw compare
without filter, for tuning bias independently of softness), probe ambient
only, baked-vs-dynamic split, and probe volume selection id. Atlas contents,
shadow-caster bounds, probe placement/validity/weights, and overdraw are
overlay/pass-level views rather than shader branches: an atlas blit quad, line
batch boxes (editor), and an additive-blend count pipeline respectively; the
runtime exposes them through `RenderStatsPanel`, the editor through
`WorldViewSettings` toggles (Section 10).

### 9.6 Runtime panel

- New `debug/RenderStatsPanel (IDebugPanel)` registered exactly like
  `TimingPanel` (`example/CubeDemo/CubeDemoGame.cpp:327-338`): live counters,
  GPU scope bars, shadow atlas occupancy strip, capture start/stop buttons,
  and the debug-view selector.

---

## 10. Editor workflow

All component-field UI (spot/point light fields, shadow settings, probe volume
fields) arrives free through the schema-driven inspector once the components
are in `EngineSceneComponents` (Section 1.7). The work below is the part that
is not free.

- **Creation.** Hierarchy "New Entity" + Add Component works today; add
  "Light > Point / Spot" and "Lighting > Irradiance Volume" entries to the
  create menu that compose `CreateEntityCommand` + `RawComponentAddCommand`
  (`document/commands/`, undoable by composition).
- **Gizmos.** New `LightVisualRenderer` beside `ComponentVisualRenderer` in
  `EditorRenderFeature` (`EditorRenderFeature.cpp:338-355`): point range
  spheres (three axis circles), spot cones (outer solid, inner dashed, range
  cap), probe volume boxes with cell grid on the focused volume, all through
  `EditorLinePipeline`. `EditorVisual` stays mesh-only; parametric gizmos read
  typed component fields directly (the generic seam cannot express
  field-driven shapes, per inspection).
- **Cone editing.** A new `IManipulator` registered at the single manipulator
  site (`ManipulatorSession.cpp:32-35`): drag the cone rim to set
  OuterAngle, inner ring for InnerAngle, tip-to-cap axis for Range, writing
  through `RawComponentEditCommand` so undo works like any inspector drag.
- **Viewport picking.** Lights and probe volumes get pickable billboard quads
  (constant screen size) added to the `PickingService` candidate loop
  (`Picking.cpp:181-195`); until then hierarchy selection already works.
- **Shadow preview.** `EditorRenderFeature` drives `ShadowDepthPass` +
  `ShadowResidency` for the focus viewport's camera (context zones render
  unshadowed), reusing the engine classes the same way it reuses
  `MeshForwardPass`. `SceneRenderQueueBuilder::BuildLights` gains `AddSpot`
  and the shadow fields (`SceneRenderQueueBuilder.cpp:234-256`).
- **View toggles.** `WorldViewSettings` gains `ShowLightGizmos = true`,
  `ShowProbeVolumes`, `ShowProbeCells`, `ShowShadowAtlas`, `DebugViewMode`
  (per-viewport override of `render.debug.view`); toolbar buttons follow the
  `ShowZoneBounds` pattern (`EditorToolbar.cpp:252-255`).
- **Lighting panel.** New `IEditorPanel` registered with the other panels
  (`EditorServices.cpp:468-551`):
  - Shadow budget readout: requested vs granted shadowed lights per type,
    atlas occupancy by tier, and a persistent warning row when requests exceed
    budget, naming the denied lights (click to select).
  - Selected-light cost line: tier, memory, views per update ("Point, 512:
    6 faces per update" vs "Spot, 512: 1 view per update"), so point shadows
    read as visibly more expensive than spots at authoring time.
  - Probe section: per-volume probe counts and memory, invalid-probe count
    (click to frame them; invalid probes also tint red in the cell overlay),
    bake staleness (content hash of geometry + static lights vs the hash
    stored in the `.sprobe`), Bake Zone / Bake World buttons, progress bar,
    cancel.
- **Bake execution.** Kyusu-side `LightingBake` orchestrator: snapshot the
  zone's static lights + collision, submit per-volume bake work through
  `engine.Tasks()` (`AsyncTaskQueue`), report progress via an atomic counter
  polled in `EditorServices::ProcessFrame` (`EditorServices.cpp:651-701`),
  write `.sprobe` + update the zone header on commit, and mark the world
  document dirty. Bake math itself lives engine-side (`ProbeBakeMath`) so it
  is testable without the editor; the editor owns orchestration, IO, and UI.
  The cooked-manifest path (`WorldCook.cpp:69-85`) invokes the same bake when
  `CookedProbeContentHash` is stale, so PIE and cooked runs stay fresh.
- **Shudei.** Hand-written rows for the v2 material fields in
  `MaterialInspectorPanel::OnDraw` beside the Surface section
  (`MaterialInspectorPanel.cpp:196-203`): specular intensity slider, emissive
  strength, shading combo, double-sided / receive / cast checkboxes. Undo is
  free via the existing whole-value `EditMaterialCommand`. The preview
  viewport picks up StandardLit automatically because it renders through
  `MeshForwardPass` (`editor/shudei/src/MaterialPreviewRenderFeature.h`).

---

## 11. Ordered implementation phases

Stages land independently green; each is a mergeable unit. Estimated sizes are
relative (S/M/L).

### Stage 0: Profiling foundation and convention fixes (M)

- **Goal.** Every subsequent stage is measurable; documented conventions match
  the code.
- **Dependencies.** None.
- **Systems affected.** `Renderer` (scope hooks around phase buckets),
  `TimingSampler`, `TimingHistory`, `DefaultRenderPipeline` (stats ownership),
  `EngineConsoleBuiltins` (cvar moves), `MeshForwardPass` (stats extension),
  kyusu `EditorServices` (drop duplicate ambient cvar registration).
- **New data structures.** `RenderStats`, capture record + ring
  (`RenderCapture`), GPU scope table.
- **GPU resources.** Timestamp query pools (per frame in flight).
- **Shader changes.** None.
- **ECS changes.** None.
- **Editor changes.** None beyond cvar registration cleanup.
- **New files.** `graphics/vulkan/GpuTimestampPool.{h,cpp}`,
  `graphics/vulkan/VulkanDebugLabels.{h,cpp}`, `render/RenderStats.h`,
  `render/RenderCapture.{h,cpp}`, `debug/RenderStatsPanel.{h,cpp}`.
- **Also.** Fix the reversed-Z comments (`Camera.cpp:18,34`, `Camera.h:19`);
  register `render.ambient.*` in the engine; wire `render.capture.*` console
  commands; name existing images/pipelines.
- **Validation.** Capture 300 frames of SceneViewer and the template game;
  confirm JSON/CSV parse and GPU totals roughly match CPU-side
  `RecordSeconds`; ctest green.
- **Completion criteria.** `RenderStatsPanel` shows live counters + GPU scope
  times in CubeDemo; a committed capture file demonstrates the schema;
  validation layer shows named objects in a RenderDoc capture.

### Stage 1: StandardLit shading, normal mapping, emission, Unlit (L)

- **Goal.** The full Section 3 material model renders; visual identity knobs
  exist; debug views for shading channels work.
- **Dependencies.** Stage 0 (debug-view cvar, stats).
- **Systems affected.** `MeshForwardPass` (pipeline variants, push constants,
  UBO growth for style params + debug view), `RenderQueue` (sort-key pipeline
  bits), material loader/writer/asset-loader, shudei panel.
- **New data structures.** `MaterialShading` enum; extended `Material` /
  `MaterialDescription` / `MeshPushConstants`.
- **GPU resources.** None new (pipelines only).
- **Shader changes.** Split shared `.glsli` includes; vertex shader gains
  tangent attribute (location 7), TBN, cofactor normal matrix; StandardLit
  fragment implements wrap diffuse, normalized Blinn-Phong, emission,
  exposure/shoulder, debug views; new `mesh_unlit.frag.glsl`; CMake blocks
  per new shader (`engine/CMakeLists.txt:55-80` pattern).
- **ECS changes.** None.
- **Editor changes.** Shudei v2 field rows; kyusu picks everything up through
  `MeshForwardPass` automatically.
- **Validation.** A test scene with normal-mapped, emissive, rough/smooth,
  double-sided, and unlit materials; debug views inspected; `.smat` v1 files
  load unchanged (loader tests); non-uniformly scaled mesh lights correctly
  (before/after screenshots).
- **Completion criteria.** All six v2 fields round-trip through shudei;
  StandardLit and Unlit draw in one frame with 4 or fewer pipeline switches
  reported by stats; suite green including new MaterialLoader v2 tests.

### Stage 2: Spot lights (M)

- **Goal.** `SpotLightComponent` end to end, plus deterministic light
  culling/prioritization for all lights.
- **Dependencies.** Stage 1 (shader include structure).
- **Systems affected.** `RenderLight.h` (GpuLight grows to 80 bytes: new
  `Vec4 Params` row carrying cosInner + inv cone delta; static asserts and
  `MAX_LIGHTS` unchanged), `LightExtractionSystem` (spot query, frustum
  sphere cull, importance sort, stable ids, cone clamping: outer <= 89.5
  degrees, inner <= outer - 0.5, range > 0.01, one-shot warnings),
  `MeshForwardPass` (UBO asserts), forward shader (spot case in the existing
  type switch: cone falloff = `smoothstep(cosOuter, cosInner, dot(L, spotDir))`
  times the shared attenuation), `SceneRenderQueueBuilder::BuildLights`,
  `DefaultRenderPipeline` (cap warning covers both types).
- **New data structures.** `SpotLightComponent` (+ `TypeSchema`, chunk
  `'SLGT'`), `RenderLightSet::AddSpot`, light record gains source
  `EntityId`/`ZoneId` (CPU side only) for stable sort and later shadow keying.
- **GPU resources.** None.
- **Shader changes.** Spot branch; direction derived on CPU from
  `WorldTransform` forward axis.
- **ECS changes.** New component in `ComponentManifest.h:33-39`.
- **Editor changes.** `LightVisualRenderer` (point spheres + spot cones),
  create-menu entries, billboard picking, cone manipulator.
- **Validation.** Cone edge softness visually verified; light-cull counters
  show off-frustum lights culled; over-cap scene drops lowest-scored lights
  deterministically across runs (test with two registries attached in both
  orders).
- **Completion criteria.** Spot lights author, save, load, render in game and
  editor; extraction tests cover packing, clamping, culling, sort stability;
  suite green.

### Stage 3: Shadow substrate + spot shadows, always-update (L)

- **Goal.** First shadows on screen: spot lights shadow correctly with the
  full bias stack, updated every frame, fixed slot assignment (budget but no
  caching/policies yet).
- **Dependencies.** Stage 2.
- **Systems affected.** `VulkanImageService` (array layers, cube-compatible,
  3D, per-layer views, depth usage; `ImageCreateInfo` widened),
  `VulkanSamplerCache` (compare op + border color in `SamplerDesc`),
  `VulkanDescriptorCache` (`GetPipelineLayout` accepts extra set layouts),
  `Renderer` (`RenderPhase::Shadow` bucket ordered first),
  `StaticMeshComponent` (+`CastShadows = true`, schema field),
  `RenderExtractionSystem` untouched; new caster extraction added to
  `DefaultRenderPipeline::ExtractRender`; `MeshForwardPass` (bind set 2,
  shadow sampling in StandardLit).
- **New data structures.** `ShadowCasterSet`,
  `ShadowCasterExtractionSystem`, `ShadowView`, `GpuShadowSlot` array in
  `MeshFrameUniforms`, `LightBindings`.
- **GPU resources.** 2048x2048 D16 atlas image (fixed 512 grid in this stage),
  comparison sampler, set-2 descriptor sets (x frames in flight).
- **Shader changes.** `shadow_depth.vert.glsl` + empty fragment;
  `shadow_sampling.glsli` (normal offset, projection, 3x3 tent, border
  behavior); StandardLit multiplies the shadow term into diffuse and
  specular; shadow debug views (term, bias raw, atlas blit).
- **ECS changes.** `CastShadows` fields on `StaticMeshComponent`,
  `SpotLightComponent`.
- **Editor changes.** Focus-viewport shadow preview wiring in
  `EditorRenderFeature`.
- **Validation.** Bias tuning scene (grazing walls, thin door frames,
  double-sided sheets) at 256/512/1024 with acne and peter-panning checked
  via the bias debug view; barrier correctness under validation layer;
  GPU scope shows shadow pass cost.
- **Completion criteria.** N spot lights (up to 8) cast filtered shadows in
  game and editor; caster gather excludes `CastShadows = false`; empty-scene
  shadow pass costs < 0.05 ms GPU; suite green with atlas-math and
  caster-extraction tests.

### Stage 4: Shadow residency: budgets, tiers, policies, caching (M)

- **Goal.** Shadow cost becomes a managed budget: quadtree tiers, scoring,
  hysteresis, update policies, moved-caster invalidation, cache counters.
- **Dependencies.** Stage 3.
- **Systems affected.** New `ShadowAtlas` quadtree allocator replaces the
  fixed grid; new `ShadowResidency` arbiter; `ShadowCasterExtractionSystem`
  gains the `Changed<WorldTransform>` moved-bounds gather;
  `ShadowRenderFeature` renders only invalidated views under the per-frame
  view clamp.
- **New data structures.** Slot cache records (state hash, valid, age,
  score), `ShadowResolutionTier`, `ShadowUpdatePolicy` (schema enums on both
  light components).
- **GPU resources.** Unchanged.
- **Shader changes.** None.
- **ECS changes.** Tier/policy/softness/bias fields on both light components.
- **Editor changes.** Lighting panel budget readout + over-budget warnings;
  `render.shadow.invalidate` console command; atlas debug view shows tiers
  and ages.
- **Validation.** Walkthrough across three zones: steady-state shadow views
  rendered = 0 (counters); moving a caster through a `OnChange` volume
  invalidates exactly the overlapped lights; 20-light over-budget scene shows
  stable slot assignment (no flicker over 1000 frames, captured).
- **Completion criteria.** `ShadowResidency` unit tests (deterministic
  assignment, hysteresis, tier downgrade, eviction) green; cache hit rate
  visible in stats; budgets tunable by cvar at runtime.

### Stage 5: Point-light shadows (M)

- **Goal.** Optional cube shadows on point lights under the same budget
  machinery.
- **Dependencies.** Stage 4 (residency) and Stage 3 (image service cube
  support).
- **Systems affected.** `ShadowCubePool`, `ShadowResidency` (point slots,
  6-face views, per-face caster cull), `ShadowRenderFeature`,
  `MeshForwardPass`/StandardLit (cube sampling), a `Contribute()` override
  enabling `imageCubeArray`.
- **New data structures.** `GpuPointShadow` params array.
- **GPU resources.** 512 D16 cube array x 4 (24 layers), cube-array
  comparison sampler.
- **Shader changes.** `VectorToDepth` major-axis reconstruction + 5-tap
  directional filter in `shadow_sampling.glsli`; point branch consumes
  `ShadowIndex`.
- **ECS changes.** Shadow fields already on `PointLightComponent` from
  Stage 4 (points simply start being granted slots).
- **Editor changes.** Cost line in the lighting panel ("6 faces per update");
  point shadows in viewport preview.
- **Validation.** Light-in-a-cage scene (all 6 faces occluded differently);
  face-edge continuity check; wall-adjacent light shows per-face cull savings
  in caster-draw counters; bias verified radially (sphere around light).
- **Completion criteria.** 4 shadowed point + 8 shadowed spot lights render
  in budget (Section 14) on the reference GPU; exceeding the point budget
  degrades per policy with the editor warning naming the losers.

### Stage 6: Baked irradiance probe volumes (L)

- **Goal.** Stable environmental light, color, and mood from a zone-streamed
  bake; hemispheric ambient becomes the fallback only.
- **Dependencies.** Stage 1 (shader structure); Stage 2 (light records carry
  ids); independent of Stages 3-5 except shared set-2 bindings.
- **Systems affected.** `math/spatial` (new `GridTransform3d`),
  `render/probes/*` (grid, resident set, sampling), `VulkanImageService`
  (3D textures from Stage 3 widening), `LightBindings` (binding 2),
  `MeshForwardPass` (volume headers in UBO), `WorldPartitionManifest`
  (+`CookedProbeRef`/hash), zone load recipes in the template game
  (`template/src/TemplateGame.cpp:684-717`), `WorldCook`/`DocumentCook`
  (bake-if-stale), kyusu `LightingBake` + panel.
- **New data structures.** `GridTransform3d`, `IrradianceProbeGrid`,
  `IrradianceVolumeComponent` (chunk `'IRVL'`), `.sprobe` format
  (`assets/probes/ProbeVolumeFormat`), `ProbeVolumeSet` (resident volumes,
  per zone), `GpuProbeVolume` headers in the frame UBO.
- **GPU resources.** Per resident volume: three RGBA16F 3D textures +
  validity R8 3D texture; cap 8 resident volumes.
- **Shader changes.** `probe_sampling.glsli`: volume selection, trilinear L1
  SH evaluation, validity weighting, hemi fallback; probe debug views.
- **ECS changes.** New component in the manifest.
- **Editor changes.** Volume gizmo + cell overlay, invalid-probe tinting,
  lighting panel bake section with progress/cancel, bake staleness hash.
- **Validation.** Determinism test (serial vs parallel bake bit-identical);
  round-trip test (`.sprobe` write/read); leak scene (two rooms, one lit,
  volumes per room: dark room stays dark); streaming test (zone unload frees
  volume textures, counters to zero); dynamic object driven between rooms
  picks up each room's tint.
- **Completion criteria.** Template-game world bakes from the lighting panel
  with progress, streams per zone, renders probe ambient with < 0.3 ms GPU
  added at 1080p, and survives editor geometry edit -> staleness warning ->
  rebake round trip.

### Stage 7: Evidence-based forward-renderer review and tuning (M)

- **Goal.** The Section 14 budgets are confirmed or the named escalations are
  triggered with numbers, not vibes; cheap fixes land.
- **Dependencies.** Stages 0-6 (content and counters exist).
- **Systems affected.** Measurement only, plus targeted fixes: per-registry
  query caches if the rebuild counter is hot (finding 2.11); anything the
  captures convict.
- **New data structures.** None (benchmark scene definitions as content).
- **GPU resources.** None.
- **Shader changes.** None unless convicted by captures.
- **ECS changes.** None.
- **Editor changes.** None.
- **Validation / method.** Three committed benchmark captures (small room,
  template-game hub with 12 lights / 6 shadowed, worst-case stress: 64
  lights, 8+4 shadows, probes resident) x 720p/1080p/1440p on the reference
  GPU, exported JSON attached to the PR; a findings section appended to this
  document.
- **Explicit thresholds** (also Section 14): the light loop earns per-object
  light lists only when MainColor GPU time exceeds 8 ms at 1080p on the
  reference GPU with the light-complexity view showing > 16 average
  lights per fragment in representative (not stress) content; tiled/clustered
  culling is considered only after per-object lists exist and visible lights
  regularly exceed 64 or per-object lists average > 8; a renderer redesign
  (deferred, visibility buffer) has no trigger inside this game's scope and is
  explicitly out of plan.
- **Completion criteria.** Findings appended here with capture references;
  either "current architecture comfortable at target workloads" is stated
  with numbers, or the specific next optimization is scheduled with its
  trigger metric quoted.

---

## 12. Files and systems likely to be added or modified

New engine files:

| Path | Content |
|---|---|
| `engine/{include,src}/render/shadow/ShadowAtlas.{h,cpp}` | Quadtree tile allocator + atlas image ownership |
| `engine/{include,src}/render/shadow/ShadowCubePool.{h,cpp}` | Cube-array slots |
| `engine/{include,src}/render/shadow/ShadowResidency.{h,cpp}` | Budget/score/hysteresis/cache arbiter |
| `engine/{include,src}/render/shadow/ShadowCasterSet.{h,cpp}` | Caster records + extraction system |
| `engine/include/render/shadow/ShadowView.h` | Per-view render job record |
| `engine/{include,src}/render/shadow/ShadowDepthPass.{h,cpp}` | Depth-only draw recording (editor-reusable) |
| `engine/{include,src}/render/shadow/ShadowRenderFeature.{h,cpp}` | `RenderPhase::Shadow` feature, barriers |
| `engine/{include,src}/render/LightBindings.{h,cpp}` | Set-2 layout/sets for atlas + cubes + probes |
| `engine/include/render/SpotLightComponent.h` | Component + schema (`'SLGT'`) |
| `engine/{include,src}/render/probes/IrradianceProbeGrid.{h,cpp}` | Grid3d + GridTransform3d composition |
| `engine/include/render/probes/IrradianceVolumeComponent.h` | Component + schema (`'IRVL'`) |
| `engine/{include,src}/render/probes/ProbeVolumeSet.{h,cpp}` | Resident volumes, GPU upload |
| `engine/{include,src}/render/probes/ProbeBakeMath.{h,cpp}` | Ray table, SH projection, deterministic gather |
| `engine/{include,src}/assets/probes/ProbeVolumeFormat.{h,cpp}` | `.sprobe` chunked binary IO |
| `engine/include/math/spatial/GridTransform3d.h` | World-cell mapping value type |
| `engine/{include,src}/graphics/vulkan/GpuTimestampPool.{h,cpp}` | Timestamp queries |
| `engine/{include,src}/graphics/vulkan/VulkanDebugLabels.{h,cpp}` | Debug-utils labels/names |
| `engine/include/render/RenderStats.h`, `engine/{include,src}/render/RenderCapture.{h,cpp}` | Counters + capture export |
| `engine/{include,src}/debug/RenderStatsPanel.{h,cpp}` | Runtime panel |
| `engine/shaders/`: `mesh_unlit.frag.glsl`, `shadow_depth.vert.glsl`, `shadow_depth.frag.glsl`, `frame_uniforms.glsli`, `lighting.glsli`, `shadow_sampling.glsli`, `probe_sampling.glsli` | Shader families + shared includes |

Modified engine files (primary): `render/RenderLight.h`,
`render/LightExtractionSystem.{h,cpp}`, `render/PointLightComponent.h`,
`render/StaticMeshComponent.h`, `render/Material.h`,
`assets/material/{MaterialFormat.h,MaterialLoader.cpp,MaterialWriter.cpp,MaterialAssetLoader.cpp}`,
`render/MeshForwardPass.{h,cpp}`, `render/MeshRenderFeature.{h,cpp}`,
`render/RenderQueue.cpp`, `graphics/vulkan/{Renderer.h,Renderer.cpp}`,
`graphics/vulkan/VulkanImageService.{h,cpp}`,
`graphics/vulkan/VulkanSamplerCache.{h,cpp}`,
`graphics/vulkan/VulkanDescriptorCache.{h,cpp}`,
`graphics/vulkan/TimingSampler.cpp`, `time/TimingHistory.h`,
`app/DefaultRenderPipeline.{h,cpp}`, `app/EngineConsoleBuiltins.cpp`,
`world/ComponentManifest.h`, `zone/WorldPartitionManifest.{h,cpp}`,
`engine/CMakeLists.txt`, `engine/shaders/mesh_forward.{vert,frag}.glsl`,
`render/Camera.h` and `render/Camera.cpp` (comments only),
`template/src/TemplateGame.cpp` (probe recipe).

Editor files: kyusu `render/LightVisualRenderer.{h,cpp}` (new),
`render/EditorRenderFeature.{h,cpp}`, `render/SceneRenderQueueBuilder.cpp`,
`viewport/{WorldViewSettings.h,Picking.cpp}`, `ui/{EditorToolbar.cpp,
LightingPanel.{h,cpp} (new)}`, `editmodes/` cone manipulator (new) +
`ManipulatorSession.cpp`, `document/LightingBake.{h,cpp}` (new),
`document/{DocumentCook.cpp,WorldCook.cpp}`, `app/EditorServices.cpp`;
shudei `MaterialInspectorPanel.cpp`.

Docs: update `docs/plans/engine-roadmap.md` Track B items 2/5/8 to record this
plan's ordering; append Stage 7 findings here.

---

## 13. Testing strategy

All new logic that can run without a device is CPU-tested (the suite has no
GPU harness, Section 1.9, and stays that way this phase). New test files drop
into existing globbed directories (`test/CMakeLists.txt:32-34`).

- `test/engine_features/ShadowAtlasTests.cpp`: quadtree allocate/free across
  tiers, fragmentation behavior, determinism of allocation order, full-atlas
  denial.
- `test/engine_features/ShadowResidencyTests.cpp`: scoring, hysteresis
  (no flicker on near-tie scores), tier downgrade under pressure, policy
  matrix (EveryFrame/OnChange/Static x light-moved/caster-moved/none),
  moved-bounds invalidation overlap math, per-frame view clamp deferral,
  deterministic slot assignment across registry orderings.
- `test/runtime/LightExtractionTests.cpp` (extended): spot packing
  (`DirectionCone`, cone params), cone/range clamping, frustum cull, stable
  importance sort with entity-id tie-break, cap behavior dropping
  lowest-scored not latest-added.
- `test/engine_features/RenderQueueTests.cpp` (extended): sort-key pipeline
  bits keep runs pipeline-pure; material field truncation still merges only
  identical items (existing guarantee, `RenderQueue.h:33-35`).
- `test/core/MaterialAssetTests.cpp` (extended): `.smat` v1 defaults, v2
  round-trip of all six new fields, unknown-key rejection unchanged.
- `test/math_geometry/GridTransform3dTests.cpp`: world/cell mapping, trilinear
  weights sum to 1, boundary clamping.
- `test/engine_features/ProbeBakeMathTests.cpp`: SH projection of analytic
  inputs (constant sky = band 0 only; single direction = expected band 1),
  serial (`worker_count == 0`) vs parallel bake bit-identical (pattern from
  `test/runtime/ZoneParallelTests.cpp:169-202`), fixed ray table stability.
- `test/core/ProbeVolumeFormatTests.cpp`: `.sprobe` write/read round trip,
  unknown-chunk skip, version rejection.
- `test/runtime/ShadowCasterExtractionTests.cpp`: `CastShadows` filtering
  (component and material), moved-caster gather via `Changed<WorldTransform>`
  chunk conservatism (asserted conservative, not exact).
- Layout guards: compile-time static asserts for the 80-byte `GpuLight`, the
  shadow slot arrays, and the new push-constant block extend the existing
  blocks in `MeshForwardPass.cpp:15-31`.
- Capture schema: a test parses a generated capture with `JsonParse` and
  checks the version key and required counters, so the AI-analysis interface
  cannot drift silently.

GPU-dependent behavior (bias tuning, filtering look, barrier correctness) is
validated by the staged validation scenes under the Vulkan validation layer
plus RenderDoc inspection, recorded per stage in Section 11. If llvmpipe CI
lands later (engine-roadmap.md:523), the capture tool doubles as its
assertion source.

---

## 14. Performance budgets

Reference target: 1920x1080 on a GTX 1060 / RX 580 class GPU (the "high
quality 2000s at high framerate" floor), 60 fps, total GPU frame <= 12 ms
leaving 4.6 ms headroom.

| Item | Budget |
|---|---|
| MainColor GPU (representative room, lights + shadows + probes on) | <= 6.0 ms |
| Shadow phase GPU, steady state (caches warm) | <= 0.3 ms |
| Shadow phase GPU, worst invalidation frame (view clamp active) | <= 2.5 ms |
| One 512 spot view | <= 0.15 ms |
| One 512 point cube update (6 faces, per-face cull) | <= 0.8 ms |
| Probe sampling added cost (fragment) | <= 0.3 ms full-screen |
| Shadow memory (atlas 8 MiB + cubes 12 MiB) | <= 20 MiB fixed |
| Probe memory per zone (default density) | <= 2 MiB (typical room volume ~50 KiB) |
| Frame UBO | <= 8 KiB (16 KiB hard line, `RenderLight.h:42-44`) |
| CPU extraction (meshes + lights + casters) at 5k queue items | <= 1.2 ms |
| `ShadowResidency` + probe residency CPU | <= 0.15 ms |
| Visible lights after cull (design guidance) | <= 24 typical, 64 hard cap |
| Shadowed lights simultaneously | 8 spot + 4 point (cvars) |
| Editor bake, default density, per zone | <= 30 s, editor responsive throughout |

Escalation triggers (from Stage 7, restated as the standing rule):

- **Per-object light lists** when representative content shows MainColor
  > 8 ms at 1080p with average per-fragment light iterations > 16. The
  implementation would be a CPU range-vs-bounds cull writing a small per-draw
  light index list; it is deliberately not built ahead of the metric.
- **Tiled/clustered culling** only after per-object lists exist and visible
  lights regularly exceed 64 or lists average > 8 lights per object.
- **Architecture change (deferred etc.)**: no trigger within the target
  game space; out of scope by decision.

---

## 15. Risks, rejected alternatives, and deferred work

Risks and mitigations:

- **`imageCubeArray` unavailable on some target device.** Core feature, near
  universal on desktop; mitigation if ever hit: fall back to 6 atlas tiles per
  point light behind the same `ShadowResidency` interface (sampling switches
  to face selection in the shader). Contingency only; not built now.
- **D16 precision on long spot ranges.** Near-plane scaling covers the target
  ranges; the atlas format probes with D32 fallback, and per-light
  `ShadowBiasScale` is the manual escape.
- **Chunk-conservative invalidation storms** (one moving entity in a dense
  chunk invalidates neighbors' shadows). Bounded by the per-frame view clamp;
  if captures show churn, the moved-bounds gather gains an entity-level
  position-delta filter.
- **Probe leaks from careless volume authoring.** Mitigated by per-room
  authoring guidance, validity dilation, and the invalid-probe editor
  overlay; occlusion-weighted interpolation is the known next step if content
  demands it.
- **Frame UBO growth.** At the chosen caps the block stays ~6.5 KiB; if caps
  rise, the recorded escape is moving lights + shadow slots to a storage
  buffer (scratch already carries STORAGE usage,
  `VulkanFrameScratch.h:32-34`).
- **`.smat` v2 vs older tooling.** Version gate is explicit and loud by
  design; shudei and loaders land in the same stage.
- **Editor bake blocking on huge worlds.** The bake is per-volume tasked and
  cancelable; Bake Zone exists precisely so Bake World is optional.

Rejected alternatives (each with its reason recorded above): metallic-
roughness BRDF evaluation and image-based lighting (Section 3); GGX and
unnormalized Phong (3.2); per-material diffuse-wrap and ramp textures (3.3);
VSM/EVSM/ESM, rotated-Poisson noise filters, per-light dedicated shadow
textures (Section 4); dual-paraboloid, octahedral, and radial-distance point
shadows (Section 5); surface lightmaps and per-vertex bake as primary
(Section 7); per-instance probe volume indices (Section 7); a generalized
spatial-field/voxel framework (Section 8); Forward+/clustered now
(Sections 11/14); reversed-Z migration (Section 2).

Deferred work, anchored to triggers:

- **Directional lights + cascaded shadow maps**: explicitly out of Phase 3 by
  request; the `GpuLight` type enum and the shadow-slot mechanism are already
  shaped to receive them (roadmap Track B row to be updated).
- **Skinned shadow casters**: blocked on skinned rendering (Track B item 1,
  engine-roadmap.md:318-322); `ShadowDepthPass` takes a second vertex shader
  when posed buffers exist (Decision N).
- **Alpha-masked shadow casters and receivers, transparency lighting**: land
  with the transparency pass (Track B item 3).
- **Real post-processing tonemap/exposure**: the in-shader shoulder moves to
  the Post phase when it exists (Track B item 5); the cvars keep their
  meaning.
- **Light channels/layers**: `StaticMeshComponent.LayerMask` is extracted but
  unused today (`RenderExtractionSystem.cpp` never reads it); it is the
  natural mechanism if per-light receiver masking is ever needed.
- **Per-object light lists, clustered culling**: metric-gated (Section 14).
- **Probe occlusion weights, specular ambient from probes, per-vertex bake
  for hero meshes, light cookies via the spot atlas**: each waits for a
  content-driven need; cookies in particular are a natural atlas extension
  already anticipated by the slot record's scale/bias addressing.
- **Multi-viewport editor shadow preview** (context zones + all viewports):
  focus-viewport-only in Phase 3; extend when editors ask.
- **Generalized spatial-field primitive**: waits for its second concrete user
  (water, voxels, occupancy), per directive 4.
