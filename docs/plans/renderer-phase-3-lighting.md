# Renderer Phase 3: Lighting, Shading, Shadows, Baked Irradiance, and Renderer Profiling

Status: IN EXECUTION, revision 4, 2026-07-19. No longer plan-only: Phase 3.0
(instrumentation) and the Phase 3A dynamic-lighting and shadow ladder have
landed in code, and revision 4 records the first shipped slice of Phase 3B
territory, baked static direct lighting, added in response to the measured
64-light forward cap. It shipped twice in one series: a per-vertex channel
(Section 7B, superseded and deleted), then per-zone lightmap atlases (Section
7C, current). Revision 2 incorporated a cross-agent design review; revision 3
worked baked ambient occlusion into the plan after a second adversarial pass.
Dispositions are recorded in Section 0 (newest first).

This document is the lighting portion of the "render ladder plan" that
`docs/assets/pipeline.md` repeatedly defers to ("Decision L ships the data, not
the lighting", pipeline.md:643). It supersedes the ordering of
`docs/plans/engine-roadmap.md` Track B item 2 (engine-roadmap.md:324-328), which
scheduled directional cascaded shadows first: this plan ships spot and point
shadows and no directional lights. It also pulls Track B item 8 ("GI (v2.0
baked)", engine-roadmap.md:345-346) forward as a first zone-scoped baked
irradiance solution. Both roadmap rows were updated at revision 4. Where this
document and the code disagree, the code as cited was inspected on 2026-07-10
at commit 962a3aa; revision-4 additions cite code as of 2026-07-18.

Scope summary of the standing decisions:

- Shading: keep the Decision L material data schema; evaluate it with a
  stylized model (wrapped diffuse, normalized Blinn-Phong specular, hemispheric
  or probe ambient, emission), not a metallic-roughness BRDF.
- Spot shadows: one 2D depth atlas (D16, quadtree tiers 256/512/1024 with
  guard-band insets), hardware-compare PCF with a 3x3 tent filter.
- Point shadows: a small depth cube-map array (D16, 512 per face, budgeted at 4
  lights), the same filter adapted to directions.
- Baked lighting: zone-scoped irradiance probe volumes storing L1 spherical
  harmonics, baked in the editor against static render geometry (with a
  read-only neighbor halo), dilated at bake time so runtime sampling is plain
  hardware trilinear, streamed with zones.
- Baked ambient occlusion: room-scale enclosure is already carried by the
  probe irradiance (no separate probe visibility payload, no bent normal in
  v1); the one new baked datum is a per-vertex AO scalar packed into the
  `.smesh` vertex, produced by the same bake, welded across duplicate render
  vertices, made seamless across zones by the halo, and densified only near
  occluders by occluder-gated adaptive tessellation. AO modulates the ambient
  term only and is never a substitute for a shadow map (Section 7A).
- Baked static direct lighting (shipped, revision 4): a light authored
  `LightBakeContribution::Direct` has its diffuse baked into the zone's
  lightmap atlas (charts grown across authored soft edges over brush
  geometry) and is excluded from the runtime light set entirely, so it costs
  nothing per frame and never counts against the 64-light cap. A separate
  additive term, not the AO datum and not a shadow substitute (Section 7C;
  the earlier per-vertex form is Section 7B, superseded).
- Profiling: an explicit instrumentation mode ladder (Off / Counters / Gpu /
  Capture) whose Off path performs no profiling work, plus a compile-time
  option that removes instrumentation, debug labels, debug pipelines, and
  panels from shipping builds.

Delivery is split into four independently mergeable phases: 3.0 (renderer
instrumentation, a preliminary standalone change), 3A (the dynamic-lighting
renderer: StandardLit, spot lights, spot and point shadows), 3B (baked
irradiance and baked AO), and 3C (the evidence-based performance review). 3A
is a complete, shippable renderer without 3B, and within 3B the vertex-AO
sub-stage (3B.3) is independently mergeable and can even ship against the
hemispheric-ambient fallback without probes.

---

## 0. Disposition

### 0.0 Revision 4: baked static direct lighting (shipped, twice)

Not a design review: this revision records an implemented feature and its
deviations, in two generations landed in one series. The measured
investigation under `docs/plans/evidence/point-light-cost/` convicted the
forward renderer's 64-light cap as a correctness wall (96 visible lights
silently drop 32) with a real ~80 us per light cost.

**Generation one (per-vertex, deleted):** the owner first chose per-vertex
baked direct lighting. It shipped end to end (v4 `.smesh` channel, cook bake,
distance-graded tessellation; history in Section 7B and
`docs/plans/evidence/baked-direct/`), then was deleted in the same series
when the owner's spec sharpened to whole-world baked lighting on streamed,
weak-hardware targets: the vertex lattice scales with lit area, streams at
52 B per sample, and cannot carry per-light animation.

**Generation two (per-zone lightmap atlases, current):** Section 7.1's
surface-lightmap rejection is REVERSED for direct light only (Section 7C).
Charts grow across authored soft edges (cut at hard edges, cone-split on
curves), pack into one deterministic RGBM atlas per zone, and bake through
the same per-sample evaluator; geometry stays at raw brush density. Evidence:
`docs/plans/evidence/lightmap-atlas/` (96 lights = 0.019 ms MainColor over 12
raw triangles). Consequences threaded into this document:

- A light authored `LightBakeContribution::Direct` bakes into the zone atlas
  and leaves the runtime light set entirely (amendment in 7.1, doctrine note
  in 7A.2). Runtime shadow maps remain the only authoritative direct-shadow
  representation; the AO doctrine is untouched.
- `.smesh` v5: the 4-byte slot at offset 48 (v4's baked-direct RGBM) is now
  two unorm16 lightmap UVs, attribute location 8. Vertex AO, if it still
  lands as vertex data, takes offset 52, location 9, `.smesh` v6 (amendment
  in 7A.6), but with the atlas machinery live, AO as an atlas channel is
  the cheaper first question for 3B.3. Single version live: the loader
  rejects prior versions; content recooks via `kCookedCacheIndexVersion`.
- 7A.5's error-driven adaptive tessellation was implemented for generation
  one and rejected on screen (irregular refinement streaks smooth falloffs);
  generation two deletes lighting-driven tessellation entirely.
- Bake math lives in the engine under `assets/cook/` (`BakeBvh`,
  `DirectLightBake`, `LightmapAtlasPack`, `LightmapRaster`), gated behind
  `SENCHA_ENABLE_COOK`, not under `render/probes/` as 3B.1 sketched: the bake
  is cook infrastructure and must never link into the runtime. 3B modules
  should follow this placement (amendment in Section 11, Phase 3B).
- What 3B.1 can now reuse: the median-split triangle BVH with segment
  occlusion and first-hit-backface queries, chart generation over brush
  topology, deterministic atlas packing, luxel rasterization with
  buried-sample invalidation, the cook-integration seam with staleness
  hashing, and per-zone texture artifacts with a scene component + preload.
  Still unbuilt: grid math, the hemisphere ray table, SH projection,
  dilation, `.sprobe` IO, and the neighbor halo.

### 0.1 Revision 3: baked ambient occlusion

**Verdict on the proposed probe-AO + cooked-vertex-AO split.** Accept the
vertex scale; reject the probe scale as a *separate baked payload*. Room-scale
ambient visibility and ambient direction are already carried by the Phase 3B
L1 SH probe irradiance (its magnitude is enclosure, its band-1 is a
bent-normal-equivalent), so no second per-probe visibility grid and no
octahedral bent normal are baked in v1. The one genuinely new baked datum is a
per-vertex AO scalar for sub-probe-cell contact darkening. Full argument in
Section 7A.1.

**Assumptions from the brief that I rejected or simplified:**

- Rejected the separate probe ambient-visibility payload and the probe bent
  normal for v1 (redundant with L1 SH irradiance; the only consumer that would
  need a sharp bent normal, ambient specular, does not exist yet). 7A.1.
- Rejected "the AO baker can lean on a cell-grid tessellation baseline": the
  cook buckets whole brushes into cells and never splits them
  (`BrushClustering.h:22,49`), so large faces stay sparse and there is no free
  baseline. This *promotes* adaptive tessellation from optional to required
  for the mechanism to work at all, while the occluder-proximity gate keeps it
  cheap. 7A.5.
- Simplified the composition question to a bounded dialed multiply
  (scale-separated radius + strength + floor) rather than an authored
  composition model, and deferred the physically-motivated residual-ambient
  model because it would reintroduce the rejected probe visibility payload.
  7A.7.
- Deferred prop self-AO / authored object-space cavity AO to a follow-up
  (needs a mesh-asset cook, not the world cook); v1 props take placement
  grounding from probes and carry neutral vertex AO. 7A.8.

**Problems found independently in this pass:**

- The Phase 3B probe bake had a latent zone-boundary defect: a per-zone BVH
  gives boundary probes a one-sided enclosure and a lighting seam between
  zones. Fixed by a read-only neighbor halo that both the probe and AO bakes
  now share (amended into Section 7.2). This is a correctness fix for probes
  that the AO review surfaced, not AO-specific.
- The AO bake, the probe bake, and their halo, ray kernel, determinism, and
  incremental-invalidation keys are the same machinery; consolidated into one
  `LightingBake` per zone that builds the BVH once and emits `.sprobe` plus the
  v4 `.smesh` AO channel together, rather than two bake systems (7A.9).
- `StaticMeshVertex` is 48 bytes and float-backed (`Vec.h:381-382`), so AO
  packs cleanly as a 4-byte `R8G8B8A8_UNORM` attribute at offset 48 (stride
  52, no padding), `.smesh` v3 -> v4, old files neutral. Precision and
  alignment checked, not assumed (7A.6).

**Consequences for the deferred directional light (Section 15).** AO modulates
ambient only, so it structurally cannot contain sunlight or replace a
directional shadow map: the future sun disc is a direct term shadowed by CSM,
and applying AO to it is explicitly forbidden. The trap and the rule are
recorded in the deferral list.

**Direct-shadow doctrine is unchanged.** Runtime shadow maps remain the only
authoritative direct-shadow representation. Nothing in Sections 4-6 changed.

Revisions 1 and 2 are preserved below unchanged for history.

### 0.2 Decisions retained from revision 1

- The StandardLit material model over Decision L data, wrapped Lambert,
  normalized Blinn-Phong, roughness-to-exponent mapping, metallic as specular
  tint, `emissive_strength` as a separate field, and the `.smat` v2 field set
  (Section 3).
- The renderer-level style cvar set, minus wording changes to the tonemap row
  (Section 3.3).
- Two shader families (StandardLit, Unlit) with the tiny pipeline matrix and
  the sort-key pipeline bits (Section 3.5).
- The single shared spot-shadow atlas with quadtree tiers, hardware-compare
  3x3 tent PCF, D16 with probed D32 fallback, front-face caster rendering, and
  the full bias stack (Section 4), now with guard-band insets.
- The point-light depth cube array, major-axis depth reconstruction, and the
  rejection of dual-paraboloid/octahedral projections (Section 5).
- Renderer-owned shadow residency: budgets, scoring, hysteresis, tiers, update
  policies, per-frame view clamp (Section 6), with the invalidation mechanism
  replaced (0.3).
- Zone-scoped L1 SH probe volumes, the `.sprobe` chunked sidecar following the
  collision precedent, `ZoneLoadRecipe` streaming, per-fragment volume
  selection over a small resident cap (Sections 7, 8).
- Reuse of `math/spatial/Grid3d<T>` plus a new `GridTransform3d` value type;
  no generalized spatial-field framework (Section 8).
- The editor workflow: schema-driven component UI, `LightVisualRenderer`
  gizmos, cone manipulator, billboard picking, lighting panel with budget
  warnings, bake orchestration on the existing async lane (Section 10).
- The capture exporter as the AI-analysis interface, structured JSON/CSV with
  stable keys (Section 9.5).
- Standard [0,1] Z retained; reversed-Z migration still rejected (Section 2
  item 1).

### 0.3 Decisions changed by this review

1. **Profiling gained a disabled-path contract** (review item 1). Adopted the
   `RenderProfileMode { Off, Counters, Gpu, Capture }` ladder and a nullable
   `RenderInstrumentation` pointer bundle resolved at pass boundaries, plus a
   `SENCHA_ENABLE_RENDER_PROFILING` compile-time option following the existing
   `SENCHA_ENABLE_*` pattern (`cmake/SenchaOptions.cmake:13-33`). Off mode
   issues no GPU commands, no query resets or readbacks, no history or capture
   writes, no labels, no allocations. Phase 3.0 validation now includes a
   RenderDoc command-stream inspection and a statistical A/B methodology
   (Section 9.7). One deviation from the strictest reading is documented and
   bounded: pass-scoped counter accumulation into stack locals at run/chunk
   granularity remains unconditional, because the equivalent counters already
   exist unconditionally today as a test seam (`MeshForwardPass.h:72-77`); the
   granularity policy in Section 9.2 forbids anything finer on the Off path.
2. **Debug shader views moved out of StandardLit** (review item 2). Revision 1
   claimed a uniform debug branch was near zero cost; that claim was wrong (a
   whole-shader branch changes instruction count and register allocation even
   when not taken) and is retracted. Debug views now live in a separate
   development-only fragment shader built from the same `.glsli` includes,
   selected at pass level, created lazily, compiled out of shipping builds
   (Section 9.6).
3. **Shadow atlas gained guard bands** (review item 3). Revision 1 relied on
   sampler border color, which only guards the outer image edge, not interior
   tile boundaries. Tiles are now allocated at power-of-two physical sizes
   with the logical shadow map inset by a fixed 8-texel guard band; the
   per-view render clears the physical tile to depth 1 and draws only the
   interior; `AtlasScaleBias` maps to the interior; the softness clamp is
   derived from the guard constant so no filter tier can reach a neighboring
   tile (Section 4.2). The per-tier texture-array alternative was evaluated
   and rejected (Section 15).
4. **Shadow invalidation now diffs previous and current caster state**
   (review item 4). Revision 1 invalidated from new bounds only, which leaves
   ghost shadows behind departing casters, and used `Changed<WorldTransform>`,
   which cannot see removals, zone detaches, or visibility changes at all.
   Replaced with a renderer-owned sorted caster table diffed frame to frame,
   producing add/remove/change events with union bounds (Section 6.4). Note:
   inspection for this revision found `Changed<WorldTransform>` is more
   precise than revision 1 assumed (propagation bumps versions only for
   chunks actually written, `TransformPropagation.cpp:272-274`); it was
   replaced anyway because no change filter can supply previous bounds or
   disappearance events.
5. **Probe validity no longer participates at runtime** (review item 5).
   Revision 1 contradicted itself (hardware trilinear filtering plus
   validity-weighted blending). Resolution: classify and dilate at bake time
   until every texel holds usable SH data; runtime is plain hardware
   trilinear; validity ships in `.sprobe` for editor diagnostics only and the
   runtime validity texture is deleted from the plan. Dilation neighborhood
   and its leak behavior are specified (Section 7.3).
6. **The bake traces render geometry, not collision** (review item 6).
   Revision 1 raycast against cooked Jolt collision. Replaced with a
   transient CPU triangle BVH built from the zone's cooked static render
   meshes loaded via `MeshLoader::LoadFromFile`
   (`engine/include/assets/static_mesh/MeshLoader.h:29`). This removes a
   lighting-to-collision fidelity coupling and, as a side effect, removes the
   bake's dependency on the physics module entirely (Section 7.2).
7. **Probe volume precedence is now priority, then smallest, then stable id**
   (review item 7). Artist priority overrides incidental size. The stable id
   is (ZoneId, volume index in `.sprobe` file order), which is
   cook-deterministic (Section 7.4).
8. **The tonemap is a knee-plus-shoulder curve, not luminance Reinhard**
   (review item 8). `c / (1 + luminance(c))` darkens the whole image
   (0.5 maps to about 0.37) and would force a lighting retune. Replaced with
   a per-channel identity-below-knee curve that leaves values at or below the
   knee (default 0.8) exactly unchanged and rolls everything above it
   smoothly to 1 (Section 3.2). Exposure and emissive were evaluated against
   it together: at exposure 1.0 existing scenes render byte-comparable below
   the knee, and `emissive_strength` pushes into the shoulder instead of hard
   clipping.
9. **Phases restructured for independent merges** (review item 9).
   Instrumentation is now Phase 3.0, a preliminary standalone change with no
   Phase 3 feature dependencies. Dynamic lighting (3A) is complete and
   shippable without baked irradiance (3B). 3B depends on 3.0, 3A.1, and
   3A.2 only, not on any shadow stage (Section 11).

### 0.4 Additional problems found during this revision

- **Set 2 needs always-valid descriptors before content exists.** The forward
  pipeline binds the lighting descriptor set from 3A.3 onward, including
  frames with zero resident shadows or probes. The set is backed by dummy
  resources (1x1 depth texture, 1-layer cube array, 1x1x1 volumes) written at
  creation so no partially-bound descriptor feature is needed (Section 6.6).
- **Frame scratch sizing.** Shadow views write per-view instance streams into
  the same 1 MiB per-frame scratch slice the forward pass uses
  (`VulkanFrameScratch.h:41-58`). A worst-case invalidation frame can
  overflow it. The scratch size becomes a config value surfaced at engine
  init, allocation failure skips the view with a one-shot warning and a
  counter, and the budget table accounts for it (Sections 6.5, 14).
- **Shadow view scheduling starvation.** With the per-frame view clamp,
  EveryFrame lights could starve invalidated cached lights indefinitely.
  Deterministic service order is specified: never-rendered slots, then
  EveryFrame, then invalid slots oldest-first (Section 6.3).
- **Mode transitions must latch at a frame boundary.** The instrumentation
  bundle is immutable during a frame; the `render.profile.mode` cvar latches
  a pending mode applied before extraction of the next frame (Section 9.1).
- **Command labels and object names are different mechanisms.** Per-frame
  command labels are gated by mode (Gpu and above); one-time object naming at
  resource creation is gated by the compile option plus
  `VulkanBootstrapPolicy::EnableValidation`, and is not a per-frame cost
  (Section 9.4).
- **The runtime keeps no CPU mesh geometry** (`GpuStaticMesh` is GPU-only,
  `GpuStaticMesh.h:16-27`), so the bake loads `.smesh` payloads from disk in
  the editor process; this constrained the bake-geometry decision in 0.2
  item 6.
- **`DrawStats` is a test seam, not just profiling** (`MeshForwardPass.h:77`
  "For profiling and tests"), which is why pass-local counter accumulation
  survives in all modes while publication is gated (Section 9.2).

### 0.5 Alternatives considered and rejected in this revision

- Per-tier shadow texture arrays instead of one guarded atlas (Section 15).
- Validity-weighted manual trilinear probe filtering (8 texel fetches x 3
  textures plus weight renormalization per fragment; Section 15).
- Luminance-normalized Reinhard as the interim tonemap (Section 15).
- Duplicated profiled/unprofiled renderer implementations, and templating the
  draw loops on an instrumentation flag (Section 9.2).
- `Changed<WorldTransform>` as the caster-motion signal, and as a prefilter in
  front of the caster diff (Section 6.4; prefilter recorded as measure-first).

### 0.6 Measure before committing

- Per-registry query caches (finding 2.11): fix only if the Phase 3.0 rebuild
  counter shows real cost.
- Caster-diff CPU cost: the sorted-vector diff is expected to be well under
  0.1 ms at a few thousand casters; if captures disagree, add the
  `Changed<>`-prefilter or a static/dynamic caster split, in that order.
- Per-object light lists and clustered culling: unchanged metric gates
  (Section 14).
- Probe volume per-fragment scan: revisit only if content exceeds the
  8-resident-volume cap.
- Capture-mode overhead: measured in 3.0 validation; the ring is bounded and
  serialization happens only on explicit write commands.

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
  `renderer.DrawFrameScheduled()` at `:204`. Extraction and submission are
  sequential on one thread within one frame, so renderer-side scheduling
  state written at extraction is safely consumed at render with no
  cross-frame handoff.
- Two frames in flight (`VulkanFrameService.h:64`), per-frame transient data
  through `VulkanFrameScratch`, a persistently mapped ring with 1 MiB per
  frame slice by default (`VulkanFrameScratch.h:41-58`).

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
  order array plus instanced runs of consecutive identical
  mesh+section+material (`RenderQueue.h:36-40`).
- `MeshForwardPass::Draw` uploads one `MeshFrameUniforms` block per view into
  the scratch (dynamic-offset UBO, set 0 binding 0), writes the per-instance
  world-matrix stream (binding 1, instance rate), and records one
  `vkCmdDrawIndexed` per run with push constants `{BaseColor,
  BaseColorTextureIndex}` (`engine/src/render/MeshForwardPass.cpp:101-246`).
  There is exactly one graphics pipeline for all opaque meshes
  (`MeshForwardPass.cpp:55-99`): back-face cull, CCW front face, LESS_OR_EQUAL
  depth, no blending. The pass already maintains `DrawStats { QueueItems,
  DrawCalls }` unconditionally, documented "For profiling and tests"
  (`MeshForwardPass.h:72-77`).
- Descriptors: two global sets owned by `VulkanDescriptorCache`. Set 0 is a
  dynamic-offset UBO; set 1 is a 1024-entry update-after-bind bindless
  combined-image-sampler array (`VulkanDescriptorCache.h:14-57`). Pipeline
  layouts are cached by push-constant signature and always use exactly these
  two set layouts (`VulkanDescriptorCache.h:38-40, 85-89`). Descriptor
  indexing (including non-uniform sampled-image indexing) is enabled
  unconditionally (`engine/src/graphics/vulkan/VulkanDeviceService.cpp:74`).

### 1.3 Lighting today

- `PointLightComponent { Color, Intensity, Range, Enabled }` with schema-driven
  serialization and editor UI
  (`engine/include/render/PointLightComponent.h:25-50`, chunk `'PLGT'`).
- `LightExtractionSystem` gathers every enabled point light in every active
  registry into `RenderLightSet` with no culling, no sorting, in
  chunk-iteration order; lights beyond the cap are dropped first-come
  (`engine/src/render/LightExtractionSystem.cpp:3-32`, `RenderLight.h:60-76`).
- `GpuLight` is a tagged 64-byte std140 record with `DirectionCone` and
  `GpuLightType::Spot/Directional` already reserved, and `ShadowIndex`
  reserved for "a future shadow atlas"
  (`engine/include/render/RenderLight.h:22-45`). `kMaxForwardLights = 64`
  with the stated rationale that the forward pass loops every light per
  fragment (`RenderLight.h:42-45`).
- The fragment shader computes hemispheric ambient (sky/ground blend on N.y)
  plus, per light, Lambert N.L with windowed inverse-square attenuation:
  `window = saturate(1 - (d/range)^4)^2 / d^2`
  (`engine/shaders/mesh_forward.frag.glsl:49-76`). There is no specular, no
  normal mapping, no emission, no shadow term. The light loop branches on
  `TypeShadow.x` with the comment "switch grows for spot/directional"
  (`mesh_forward.frag.glsl:60`).
- Ambient defaults live in `RenderLightSet` (`RenderLight.h:52-53`); the
  `render.ambient.*` cvars that override them are registered only in the
  editor (`editor/kyusu/src/app/EditorServices.cpp:331-336`, applied at
  `editor/kyusu/src/render/EditorRenderFeature.cpp:161-169`). The runtime
  never registers or reads them, so the comment at `RenderLight.h:51`
  overstates the current wiring.

### 1.4 Material and texture data

- `Material` is the full Decision L glTF metallic-roughness data model: four
  bindless texture slots (base color, normal, ORM, emissive), factors, alpha
  mode; "The current forward shader consumes BaseColor only"
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
  Lighting math already happens in linear space and is encoded once at the
  end. This foundation is sound; Phase 3 builds on it unchanged.
- `StaticMeshVertex` already carries a `Vec4 Tangent` (glTF convention,
  bitangent = cross(N, T.xyz) * T.w), generated by MikkTSpace at cook when the
  source lacks it (Decision M)
  (`engine/include/render/static_mesh/StaticMeshVertex.h:9-19`). The forward
  pipeline strides over it but exposes no attribute yet
  (`MeshForwardPass.cpp:77-81`). Normal mapping is therefore a shader and
  pipeline-desc change, not an asset change.
- The runtime does not retain CPU vertex data after upload; `GpuStaticMesh`
  holds buffers and section metadata only (`GpuStaticMesh.h:16-27`). CPU
  geometry access goes through `MeshLoader::LoadFromFile(path, MeshGeometry&)`
  (`engine/include/assets/static_mesh/MeshLoader.h:29`), which matters for
  the bake (Section 7.2).

### 1.5 Depth and projection conventions (audit result)

- `MakeVulkanPerspective` (`engine/src/render/Camera.cpp:9-22`) claims
  "reversed-Z: maps near->1, far->0" and `CameraRenderData`'s header repeats
  it (`engine/include/render/Camera.h:19`). Working the matrix through
  disproves the comments: with `result[2][2] = far/(near-far)` and
  `result[2][3] = far*near/(near-far)`, a view-space point at `z = -near`
  lands at NDC depth 0 and `z = -far` at depth 1. The pipeline agrees with the
  matrix, not the comment: depth clear is 1.0
  (`engine/src/graphics/vulkan/Renderer.cpp:260`) and the compare op is
  LESS_OR_EQUAL (`MeshForwardPass.cpp:91`). The renderer is standard [0,1] Z.
  The comments are stale and must be fixed (Section 2 item 1).
- The vertex shader transforms normals with `mat3(world)`
  (`engine/shaders/mesh_forward.vert.glsl:33`). `Transform3d` carries a full
  per-axis `Vec<3> Scale` (`engine/include/math/geometry/3d/Transform3d.h:37`),
  so non-uniformly scaled meshes light incorrectly today.
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
  add-component menu, and JSON+binary scene serialization with no editor
  edits.
- The one shared overlay pipeline is `EditorLinePipeline`
  (`editor/kyusu/src/render/EditorLinePipeline.h`); per-component gizmos render
  through `ComponentVisualRenderer`, but `EditorVisual` supports only static
  meshes (`engine/include/core/metadata/EditorVisual.h:18-34`); no light
  visualization exists. Lights are currently selectable only in the hierarchy
  panel; viewport picking handles brush geometry only
  (`editor/kyusu/src/viewport/Picking.cpp:181-276`).
- View toggles live in `WorldViewSettings`
  (`editor/kyusu/src/viewport/WorldViewSettings.h:12-35`) with toolbar buttons
  (`EditorToolbar.cpp:252-255`). There is no long-running-operation or
  progress UI anywhere; level cook is synchronous (`DocumentCook.h:60-79`).
- Shudei's material panel is hand-written per field, with whole-value
  `EditMaterialCommand` undo
  (`editor/shudei/src/MaterialInspectorPanel.cpp:160-242`).

### 1.8 Profiling and diagnostics today

- CPU frame timing is mature: `TimingFrameSample` ring
  (`engine/include/time/TimingHistory.h:7-76`), assembled by `TimingSampler`
  (`engine/src/graphics/vulkan/TimingSampler.cpp:31-63`), displayed by the
  ImGui `TimingPanel` behind the runtime debug overlay
  (`engine/include/debug/IDebugPanel.h`, `ImGuiDebugOverlay`, gated by
  `SENCHA_ENABLE_DEBUG_UI`, `cmake/SenchaOptions.cmake:21`).
- There are zero GPU timestamps (no `VkQueryPool` anywhere), zero Vulkan debug
  labels or object names (only the validation messenger,
  `engine/src/graphics/vulkan/VulkanInstanceService.cpp:56-79`), and no
  structured stats export. A chrome-trace exporter exists but nothing wires it
  (`engine/include/runtime/FrameTrace.h:55-84`).
- Cvar and console infrastructure is complete: designated-initializer
  `RegisterCVar` with enum values, flags, and `OnChange`
  (`engine/src/app/EngineConsoleBuiltins.cpp:69-83` is the pattern), console
  commands via `RegisterCommand`
  (`engine/src/core/console/ConsoleService.cpp:227-237`), JSON writing via
  `JsonStringify` (`engine/include/core/json/JsonStringify.h:7`) with the
  documented caveat that `JsonValue` is not for hot paths
  (`JsonValue.h:19-22`).

### 1.9 Concurrency, change tracking, tests, skinned meshes

- The bake lane exists: `AsyncTaskQueue::Submit(work, commit)` with commits at
  `DrainAsyncTasks` and a zero-worker deterministic mode
  (`engine/include/jobs/AsyncTaskQueue.h:95-186`); `JobSystem::ParallelFor`
  with the `worker_count == 0` serial reference path
  (`engine/include/jobs/JobSystem.h:11-40`).
- Transform propagation is dirty-driven and maintains precise change
  versions: it bumps a chunk's `WorldTransform` column version only when it
  actually writes that chunk ("Precise change signal: only chunks actually
  written this sweep match Changed<WorldTransform> downstream",
  `engine/src/world/transform/TransformPropagation.cpp:271-274`). So
  `Changed<T>` is trustworthy here, at chunk granularity; its limitation for
  shadow invalidation is what it cannot express (previous bounds,
  disappearances), not imprecision (Section 6.4).
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
   comments in Phase 3.0. Shadow code uses the same standard-Z convention.

2. **Wrong normal transform under non-uniform scale.**
   `mat3(world) * inNormal` (`mesh_forward.vert.glsl:33`) with
   `Transform3d::Scale` being per-axis (`Transform3d.h:37`). Fix in the 3A.1
   vertex shader with the cofactor (adjugate) matrix built from three cross
   products of the world-matrix columns; no CPU plumbing, correct for
   non-uniform scale.

3. **`VulkanImageService` is deliberately 2D-single-layer-only.**
   "Cubemap, volumetric, and non-default-view images are out of scope until a
   feature actually needs them" (`VulkanImageService.h:26-27`). The trigger
   has arrived. It needs: array layers, cube-compatible creation, 3D images,
   per-layer/per-face views for rendering, depth-format usage, and a path
   that leaves images in layouts other than SHADER_READ_ONLY. This is a
   mechanical widening of `ImageCreateInfo` plus view helpers, done once in
   3A.3.

4. **`VulkanSamplerCache` cannot express comparison samplers.**
   `SamplerDesc` (`VulkanSamplerCache.h:27-39`) lacks compare enable/op and
   border color. Hardware PCF needs `compareEnable = VK_TRUE`, op
   LESS_OR_EQUAL, CLAMP_TO_BORDER addressing, and a white border (the border
   remains the outer-image backstop even with guard bands). Small struct
   extension in 3A.3; hash/equality already key on the whole struct.

5. **Pipeline layouts only know the two global sets.**
   `VulkanDescriptorCache::GetPipelineLayout` builds layouts from FrameSet +
   BindlessSet only (`VulkanDescriptorCache.h:38-40, 85-89`). Shadow maps and
   probe volumes cannot ride the bindless sampler2D array (different GLSL
   sampler types: `sampler2DShadow`, `samplerCubeArrayShadow`, `sampler3D`).
   Decision: one additional descriptor set (set 2) owned by the lighting
   module, containing the shadow atlas, the point cube array, and the probe
   volume textures; extend `GetPipelineLayout` to accept optional extra set
   layouts in the cache key. One set, fixed bindings, rewritten only between
   frames (double-buffered per frame in flight), no update-after-bind needed,
   dummy-backed from creation (Section 6.6).

6. **Graphics pipelines require a fragment shader.**
   `CreateGraphicsPipeline` errors on a null FS module
   (`engine/src/graphics/vulkan/VulkanPipelineCache.cpp:139-145`). Shadow
   depth passes use a trivial empty fragment shader rather than changing the
   cache contract. Depth bias state already exists in the desc
   (`VulkanPipelineCache.h:100-102`).

7. **Light gather is unbounded, unsorted, and order-unstable at the cap.**
   Lights are packed in chunk order per registry and dropped first-come past
   64 (`RenderLight.h:60-66`, `LightExtractionSystem.cpp:17-31`). With
   multiple zones, which light gets dropped depends on zone attach order.
   3A.2 adds camera-frustum sphere culling, importance sorting, and a stable
   tie-break so the packed set is deterministic for identical world state.

8. **Casters are not extractable today, and invalidation needs previous
   state.** The only mesh gather is camera-frustum-culled inline
   (`RenderExtractionSystem.cpp:69`), but casters outside the camera frustum
   still cast into it. Shadows need a camera-independent caster gather with a
   persistent previous-frame table for diff-based invalidation (Section 6.4)
   plus a `CastShadows` field on `StaticMeshComponent`. No ECS change filter
   substitutes for the table: `Changed<>` cannot report previous bounds,
   entity removal, zone detach, or visibility transitions.

9. **`render.ambient.*` cvars are editor-only.**
   Registered in `EditorServices.cpp:331-336`, absent from the runtime, while
   `RenderLight.h:51` implies otherwise. Phase 3.0 moves registration into
   the engine (`EngineConsoleBuiltins.cpp`) so runtime and editor share one
   definition; the editor keeps only its per-frame poll.

10. **Frame UBO growth budget.**
    `MeshFrameUniforms` is ~4.2 KiB today (`MeshForwardPass.cpp:18-31`).
    Phase 3 grows it (spot cone params, shadow slot matrices, probe volume
    headers) to ~6.5 KiB, still under the 16 KiB guaranteed dynamic-UBO range
    the current design leans on (`RenderLight.h:42-44`). No storage-buffer
    migration is needed in this phase; it is the recorded escape hatch if
    light or shadow caps ever rise.

11. **Query-cache thrash across registries (measure, then fix).**
    `RenderExtractionSystem`/`LightExtractionSystem` cache one `Query` keyed
    by a single `World*` sentinel (`RenderExtractionSystem.h:22-24`), but
    `DefaultRenderPipeline::ExtractRender` calls them once per active registry
    (`DefaultRenderPipeline.cpp:86-102`), busting the cache every call when
    more than one zone is resident. Counted in Phase 3.0; fixed in 3C only if
    the numbers justify it (a small per-registry cache map).

12. **`.smat` unknown-key strictness vs new fields.**
    Unknown keys are errors by design (pipeline.md:661-663). Adding fields
    requires a version bump to `kSmatVersion = 2` with the loader accepting 1
    and 2 (v1 files get defaults), and the writer emitting 2. Old binaries
    reading v2 files fail loudly, which is the intended failure mode.

13. **Editor has no async-operation or progress UI.**
    The probe bake is the first long-running editor operation. It rides
    `engine.Tasks()` (the existing cross-frame lane) with progress polled in
    `EditorServices::ProcessFrame` (`EditorServices.cpp:651-701`); no new
    concurrency mechanism is introduced.

14. **Instrumentation must have a disabled path and a compile-out.**
    Nothing in the current renderer distinguishes dev diagnostics from
    shipping cost (the debug overlay gate `SENCHA_ENABLE_DEBUG_UI` covers UI
    only). Phase 3.0 introduces both the runtime mode ladder and
    `SENCHA_ENABLE_RENDER_PROFILING` (Section 9).

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
ambientC = probeIrradiance(N)  if a probe volume covers the point (Phase 3B)
           else hemiAmbient(N)                    (existing sky/ground blend)
aoFactor = 1.0                                    (Phase 3A: AO absent)
           mix(1.0, mix(ao_min, 1.0, vertexAo), ao_strength)   (Phase 3B.3)
ambient  = ambientC * aoFactor                    (AO touches ambient ONLY)
diffuse  = sum over lights: wrap(N, L) * atten * shadow * lightColor
specular = sum over lights: normBlinnPhong(N, H, exponent) * specIntensity
           * specTint * atten * shadow * lightColor
emission = emissiveFactor.rgb * emissiveStrength * emissiveTex
color    = baseColor * (ambient + diffuse) + specular + emission
out      = kneeShoulder(color * exposure)
```

The single doctrinal invariant for baked AO lives in this equation: `aoFactor`
multiplies `ambientC` and nothing else. It never appears in `diffuse`,
`specular`, or `emission`. Direct light is shadowed only by runtime shadow
maps (Sections 4-6); AO is physically incapable of darkening a direct light
term because it is not a factor on any of them. This is what makes "AO
disabled proves direct lighting and runtime shadows are unaffected" a
one-line proof rather than a test campaign: with AO off, `aoFactor = 1.0` and
the equation is byte-identical to Phase 3A. The full AO design is Section 7A.

Component decisions:

- **Diffuse: wrapped Lambert.** `wrap(N, L) = saturate((dot(N,L) + w) / (1 + w))`
  with `w` a renderer-level cvar (`render.style.diffuse_wrap`, default 0.25,
  0 = pure Lambert). This is the softened-terminator look the current
  stylized response gestures at, made explicit and tunable in exactly one
  place. It is a style control, not a material control: per-material wrap is
  rejected to avoid toggle sprawl.
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
  default 0.5) added to the schema (3.4). Specular tint reuses the existing
  metallic data stylistically: `specTint = mix(white, baseColor.rgb,
  metallic)`. Metallic keeps a meaning (tinted highlights for metals) without
  a full metallic workflow. No separate specular color field in v1.
- **Emission.** `EmissiveFactor.rgb * EmissiveStrength * emissiveTexture`,
  strength a new scalar defaulting to 1 (the Vec4 factor's w is currently
  unused and defaults to 0, so overloading it would silently zero emission on
  existing materials; a separate field avoids that trap). Emission is added
  after diffuse/specular so lights do not modulate it.
- **Attenuation.** Keep the existing windowed inverse-square exactly as is
  (`mesh_forward.frag.glsl:68-72`); it is correct, local, and already tuned.
  Spot lights multiply the same attenuation by the cone falloff (Section
  3A.2 / Section 4).
- **Ambient occlusion (Phase 3B.3).** `vertexAo` is a per-vertex baked scalar
  in [0,1] (1 = fully open, neutral), interpolated to the fragment. It is
  bounded to `[ao_min, 1]` (cvar `render.ao.min`, default 0.15, so corners
  never crush to pure black) then dialed by `render.ao.strength` (default
  0.75). Room-scale enclosure is already carried by `ambientC` itself
  (probe irradiance darkens in corners at bake time, or the hemi fallback);
  `vertexAo` adds only sub-probe-cell contact darkening, so the multiply is
  between two scales that mostly do not overlap. Absent AO (Phase 3A, or a
  mesh with neutral AO, or `render.ao.enabled 0`), `aoFactor = 1.0` and the
  ambient term is unchanged. Full rationale and the double-darkening analysis
  are in Section 7A.
- **Exposure and tonemap: identity below a knee, shoulder above it.**
  Applied per channel at the end of the fragment shader, after exposure:

  ```
  kneeShoulder(x) = x                              for x <= k
                    k + (1 - k) * s / (1 + s)      for x >  k, s = (x - k) / (1 - k)
  ```

  with `k = render.tonemap.knee` (default 0.8) and `render.exposure`
  (default 1.0). The curve is continuous and C1 at the knee (both one-sided
  derivatives are 1), leaves every value at or below the knee exactly
  unchanged, and asymptotically approaches 1. Consequences, evaluated
  together with exposure and emission rather than in isolation: at the
  default exposure existing scenes render identically wherever they already
  fit under the knee, so no lighting retune is required; emissive surfaces
  and tight speculars driven past the knee by `emissive_strength` or
  intensity roll off smoothly instead of hard-clipping per channel; the
  per-channel form desaturates overdriven colors toward white, which is the
  filmic-style highlight behavior appropriate to the target look. With
  `render.tonemap` off the shader clamps at 1.0, which is today's behavior.
  This block is explicitly the interim stand-in for the reserved Post phase
  (engine-roadmap.md:336-338): the forward pass renders straight to the sRGB
  swapchain, so there is no HDR intermediate to tonemap in post yet; when the
  Post phase lands, the function moves there verbatim and the cvars keep
  their meaning. Rejected: `c / (1 + luminance(c))` (Reinhard by luminance)
  because it rescales the entire range (0.5 maps to about 0.37), which is a
  global tone curve demanding a lighting retune, not a highlight shoulder.

### 3.3 Renderer-level style controls (the complete list)

All are engine-registered cvars (Phase 3.0 / 3A.1), archived, polled per frame
by the pipelines exactly as the editor already polls ambient
(`EditorRenderFeature.cpp:161-169`):

| Cvar | Default | Meaning |
|---|---|---|
| `render.ambient.sky_r/g/b`, `render.ambient.ground_r/g/b` | current defaults | Hemispheric fallback ambient (moved from editor-only registration) |
| `render.style.diffuse_wrap` | 0.25 | Wrapped-diffuse width |
| `render.style.min_ambient` | 0.0 | Floor added to ambient (legibility guard in unbaked rooms) |
| `render.exposure` | 1.0 | Pre-tonemap scale |
| `render.tonemap` | true | Knee-shoulder curve on/off (off = clamp) |
| `render.tonemap.knee` | 0.8 | Identity-region upper bound |
| `render.shadow.darkness` | 1.0 | Global shadow attenuation scale (1 = fully dark shadows) |
| `render.shadow.softness` | 1.0 | Global multiplier on per-light softness |
| `render.ao.enabled` | true | Master toggle for baked ambient occlusion (Phase 3B.3) |
| `render.ao.strength` | 0.75 | How strongly `vertexAo` modulates ambient (0 = neutral) |
| `render.ao.min` | 0.15 | Floor on the AO factor (corners never crush to black) |

Bake-time AO parameters (read by the editor bake, not per-frame runtime knobs)
live under `render.bake.ao.*` and are listed in Section 7A.6.

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
| `shading` | `"standard_lit"` or `"unlit"` | `standard_lit` | `Material::Shading` (enum `MaterialShading`) |
| `double_sided` | bool | false | `Material::DoubleSided` |
| `receive_shadows` | bool | true | `Material::ReceiveShadows` |
| `cast_shadows` | bool | true | `Material::CastShadows` |

`kSmatVersion` bumps to 2; the loader accepts 1 and 2 (v1 loads with
defaults), the writer always writes 2. Unknown keys remain errors.

### 3.5 Shader families and pipeline variants

Two production fragment families, one shared vertex shader, all built from
shared `.glsli` includes (`frame_uniforms.glsli`, `lighting.glsli`,
`shadow_sampling.glsli`, `probe_sampling.glsli`) so the vert/frag UBO
declaration mismatch (finding 1.5) disappears:

- **StandardLit** (`mesh_forward.frag.glsl`, extended): the full model above,
  and nothing else. It contains no debug-view branch, no debug-only
  calculations, and no instrumentation; development-only views live in a
  separate shader (Section 9.6) so the production shader's instruction count
  and register allocation cannot be affected by disabled tooling.
- **Unlit** (`mesh_unlit.frag.glsl`, new): `baseColor * texture + emission`,
  no lights, no shadows received, still depth-tested and depth-written, still
  a shadow caster if its material says so. The emissive-unlit use case
  (glowing panels, screens) is Unlit with an emissive texture and strength;
  it is not a third family.

Pipeline count is deliberately tiny. Properties that genuinely require
separate `VkPipeline`s: fragment family (2) x cull mode (back, none for
double-sided) = 4 production opaque pipelines, plus the shadow-depth
pipelines (Sections 4, 5) and development-only pipelines (debug-view and
overdraw, Section 9.6, compiled out of shipping). Everything else (textures,
factors, specular, emission, shadow receive) is push-constant or UBO data in
the one StandardLit pipeline. There is no per-feature shader permutation: a
material with no normal map samples the flat-normal default the neutral-slot
system already guarantees (`Material.h:42-45`).

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

From Sections 1.4/1.5: linear lighting, sRGB sampling, sRGB framebuffer
encoding, BC5 normal handling (always reconstruct Z from XY; works identically
for uncompressed linear RGBA normals), and attenuation are already correct.
The three defects to fix are the stale reversed-Z comments (Phase 3.0), the
non-uniform-scale normal transform (3A.1, cofactor matrix), and tangent
consumption (3A.1, attribute location 7 plus TBN in the vertex shader,
MikkTSpace convention already documented at `StaticMeshVertex.h:15-17`).
Exposure/tonemap do not exist today; Section 3.2 adds the minimal version.

---

## 4. Recommended shadow technique for spot lights

**Decision: conventional depth shadow maps in one shared 2D atlas with
guard-band insets, sampled with hardware-comparison PCF through a 3x3 tent
filter.**

### 4.1 Storage, projection, rendering

- **Storage.** One `2048x2048` D16_UNORM depth image (8 MiB), usage
  DEPTH_STENCIL_ATTACHMENT | SAMPLED, owned by the renderer (never by
  components). Physical tiles allocated at 256/512/1024 by a 3-level
  quadtree. D16 is sufficient for room-scale spot ranges with the near plane
  pushed out; the format is probed at startup with D32_SFLOAT fallback,
  mirroring `VulkanDepthTarget::ChooseDepthFormat` (`VulkanDepthTarget.h:46`).
- **Projection.** Standard [0,1] Z (matching the audited main-pass
  convention), perspective FOV = 2 x outer cone angle, near plane =
  `max(0.05, 0.02 * Range)` and far = `Range`. The near plane is the first
  acne lever: pushing it out spreads depth precision over the lit volume.
- **Rendering.** Depth-only pipeline: shared `shadow_depth.vert.glsl`
  (position from the same per-instance matrix stream the forward pass uses)
  plus an empty fragment shader (finding 2.6). Pipeline-level slope-scaled +
  constant depth bias (`GraphicsPipelineDesc::DepthBias*`,
  `VulkanPipelineCache.h:100-102`). Casters keep back-face culling (front
  faces render), same as the main pass: kyusu brush geometry cooks to closed
  solids, but thin authored props exist, and front-face rendering avoids
  their peter-panning; double-sided materials render both faces in the shadow
  pass too. Each `ShadowView` records one `vkCmdBeginRendering` whose
  renderArea is the **physical** tile with `loadOp = CLEAR` (clear value
  1.0), and draws with viewport/scissor set to the **logical interior**; the
  guard band is therefore re-cleared to "no occluder" on every tile render at
  zero extra cost.

### 4.2 Guard bands (tile isolation)

A sampler border color applies only outside the whole image, not between
tiles, so filtering across a tile edge would otherwise read a neighboring
light's depths. The atlas therefore separates physical and logical tiles:

- Physical tiles stay power-of-two (256/512/1024) so the quadtree allocator
  stays trivial and alignment-friendly.
- The logical shadow map is the physical tile inset by
  `kShadowTileGuardTexels = 8` on every side (a 512 physical tile carries a
  496 logical map; the ~3% resolution loss is imperceptible at these sizes).
- `GpuShadowSlot::AtlasScaleBias` maps light-space UV [0,1] to the logical
  interior, so all sampling math is expressed in logical UVs and no shader
  clamp against tile edges is needed for in-range fragments.
- The guard band is cleared to depth 1.0 with the tile (4.1), so any filter
  tap that lands in it compares against "no occluder" and returns lit. That
  is the correct limit behavior at the cone rim, where the spot falloff has
  already attenuated the light to zero (the projection FOV equals the outer
  cone exactly, so only rim fragments can push taps into the band).
- The filter cannot reach past the band by construction: per-light softness
  is clamped to `kShadowSoftnessMaxTexels = 4.0`, the tent places taps at
  ±1.5 x softness texels, and each hardware tap adds at most 1 texel of
  bilinear footprint, so worst-case reach is `ceil(1.5 * 4) + 1 = 7 < 8`.
  The two constants live beside each other in one header with the derivation
  in a comment and a unit test enforcing the inequality (Section 13).
- Out-of-frustum robustness is standard and independent of the band: before
  filtering, the shader rejects samples with `w <= 0` or projected z outside
  [0,1] and returns lit (cone attenuation already zeroed those fragments).
- Occupancy, allocation pressure, memory accounting, and tests all deal in
  physical tiles; the editor UI reports "512 (496 usable)" so the inset is
  visible, not hidden.
- The white-border comparison sampler is retained purely as the outer-image
  backstop.

Alternatives weighed for tile isolation: per-tier `sampler2DArrayShadow`
texture arrays would make every layer edge a real image edge (border color
then suffices, no insets), but cost three bindings, a per-sample array
selection, fixed per-tier budgets instead of one fungible atlas (tier
downgrade under pressure is a stated feature), and more total memory for the
same worst case. Duplicated edge texels do not compose with depth-compare
sampling. Dedicated per-light textures multiply bindings and barriers. The
inset atlas keeps one binding, one sampling path, and ~15 lines of extra CPU
code; it wins.

### 4.3 Filtering

`sampler2DShadow` with `compareEnable`, LESS_OR_EQUAL, white border. Filter: 9
hardware-PCF taps in a 3x3 tent, tap spacing = per-light `ShadowSoftness`
(default 1.5 texels, clamp [0.5, 4.0]) times `render.shadow.softness`. Each
hardware tap is a 2x2 comparison, so the effective kernel is about 6x6:
soft-edged, stable (no per-pixel rotation noise), and it deliberately reads
as slightly blurry, which hides 256/512 texel footprints exactly as intended.
A 5-tap variant is selected automatically for tiles at the 256 tier. Softness
is specified in logical texels, so a tier downgrade keeps the kernel size in
texels (slightly wider in world terms); this is documented behavior.

### 4.4 Acne and peter-panning controls

Each independently debuggable (Section 9.6):

1. Slope-scaled bias (pipeline, cvar `render.shadow.bias_slope`, default 2.0).
2. Constant bias (pipeline, cvar `render.shadow.bias_const`, default 4,
   format-relative units).
3. Receiver normal-offset: shift the sampled world position along the
   geometric normal by `texelWorldSize * ShadowBiasScale` before projecting
   into light space. Texel world size is computed per light from logical tile
   resolution and cone angle and uploaded in the shadow slot record. This is
   the primary defense; it scales with resolution so low tiers do not acne.
4. Near-plane selection (4.1).
5. Front-face rendering (4.1); back-face rendering is the rejected
   alternative (trades acne for light leaks through walls thinner than the
   bias, and brush levels have many door-frame-thickness walls).
6. Per-light `ShadowBiasScale` multiplier for the rare problem light.

Raising resolution is explicitly not the acne strategy; the defaults are tuned
at 512 and verified at 256.

**Rejected for spots.** Variance and exponential-variance maps: two extra
color targets, a separable blur pass per light, light bleeding between
overlapping occluders, and mip/filter machinery, all to buy softness the tent
filter already provides at our light counts. Poisson-disc with per-pixel
rotation: shimmering under motion, which reads modern and wrong for the
target look; the tent is stable.

---

## 5. Recommended shadow technique for point lights

**Decision: depth cube-map array with hardware-comparison sampling, small
fixed budget, per-face depth reconstruction.**

- **Storage.** One `VkImage` cube array: 512x512 D16, 6 faces x
  `kMaxShadowedPointLights = 4` cubes = 24 layers (12 MiB). Created
  cube-array-compatible; `imageCubeArray` is a core 1.0 device feature
  enabled through the existing `IRenderFeature::Contribute` bootstrap seam
  (`Renderer.h:116-120`). Per-face 2D views for rendering; one cube-array
  view for sampling. Cube faces need no guard bands: hardware cube-map
  filtering is seamless across faces, and each cube's layers are addressed
  by the sampler as one logical cube, never as adjacent atlas memory.
- **Rendering.** Six per-face passes with 90-degree perspective (near =
  `max(0.05, 0.02 * Range)`, far = Range), the same depth-only pipeline and
  bias stack as spots. Each face frustum-culls the caster set independently,
  so a light against a wall renders roughly half its faces empty. Six faces
  is the honest cost of an omni shadow; it is budgeted, not hidden: the
  editor UI displays it (Section 10) and update policies amortize it
  (Section 6).
- **Sampling.** `samplerCubeArrayShadow`. The comparison reference is
  reconstructed from the fragment-to-light vector: take the major-axis
  distance `z = max(|v.x|, |v.y|, |v.z|)` and project through the face
  near/far to [0,1] depth. Filter: 5 taps (center + 4 offsets perpendicular
  to the sample direction, spaced by softness), each a hardware 2x2
  comparison.
- **Radial-distance representation (store `length(v)/far` in a color target
  and compare manually): rejected.** It simplifies bias reasoning (uniform
  world-space bias) but costs an extra R16 color attachment per face, loses
  hardware-comparison filtering, and its one advantage is already covered by
  normal-offset biasing. Kept in Section 15 as the fallback if per-face depth
  reconstruction proves fiddly on some driver.
- **Dual-paraboloid and octahedral projections: rejected.** Both halve or
  quarter the pass count but warp straight edges of low-poly brush geometry
  unless casters are tessellated, and both introduce seam filtering work. At
  a budget of 4 point shadows the cube array is simpler and visually safer.

Point and spot shadows share: the depth-only shader pair, the caster set and
its diff, the bias stack, the residency arbiter, the budget cvars, and the
debug views. They differ only in storage object and projection/sampling math.

---

## 6. Shadow allocation, caching, and invalidation architecture

### 6.1 Ownership split

Components describe intent; the renderer owns every GPU resource and all
scheduling state. Nothing on a component references an atlas slot, an image,
or a frame.

Authoring state (ECS, serialized, schema-driven UI for free):

```
PointLightComponent (extended)      SpotLightComponent (new)
  Color, Intensity, Range, Enabled    Color, Intensity, Range, Enabled
  CastShadows        = false          direction = WorldTransform forward axis
  ShadowResolution   = Medium         InnerAngleDegrees = 25, OuterAngleDegrees = 35
  ShadowUpdate       = OnChange       CastShadows, ShadowResolution, ShadowUpdate,
  ShadowSoftness     = 1.5            ShadowSoftness, ShadowBiasScale
  ShadowBiasScale    = 1.0            (same shadow fields and defaults)
```

`ShadowResolution` is `ShadowResolutionTier { Low = 256, Medium = 512,
High = 1024 }` (physical tile size; the logical map is inset per Section 4.2;
points clamp High to 512 per face). `ShadowUpdate` is `ShadowUpdatePolicy
{ EveryFrame, OnChange, Static }`. Shadow enablement defaults off: a light is
cheap until someone opts in.

Renderer state (new `engine/{include,src}/render/shadow/`):

- `ShadowAtlas`: the 2D depth image + 3-level quadtree slot allocator over
  physical tiles, plus the logical-inset scale/bias math. Allocation logic is
  Vulkan-free and unit-tests headlessly.
- `ShadowCubePool`: the cube array + a 4-slot allocator.
- `ShadowResidency`: the arbiter. Input: this frame's packed lights (with
  stable identities), camera, budgets, and the caster-diff events (6.4).
  Output: per-light `ShadowIndex` and the ordered list of `ShadowView`s to
  render this frame. Owns per-slot cache state (light-state hash, valid flag,
  ages, score history). CPU-only, deterministic, unit-testable.
- `ShadowView`: one render job = { kind (spot / point face), target (atlas
  physical rect + logical viewport, or cube layer+face), view-projection
  matrix, caster cull volume }.
- `ShadowCasterSet` + `ShadowCasterExtractionSystem`: the camera-independent
  caster gather and its previous-frame table (6.4). Runs inside
  `DefaultRenderPipeline::ExtractRender` beside the existing extractors
  (`DefaultRenderPipeline.cpp:86-102`).
- `ShadowDepthPass`: records the depth-only draws for one `ShadowView`
  (per-view frustum cull of the caster set, mesh-sorted instanced runs, same
  instancing mechanism as `MeshForwardPass::BindInstanceStream`,
  `MeshForwardPass.cpp:121-140`). Factored like `MeshForwardPass` so kyusu
  viewports reuse it.
- `ShadowRenderFeature (IRenderFeature)`: runs in a new `RenderPhase::Shadow`
  bucket ordered before `Offscreen` (`Renderer.h:57-65` reserves the name;
  before-Offscreen ordering means editor viewports can sample shadows too).
  Owns barriers: per-tile DEPTH_ATTACHMENT rendering, then one atlas /
  cube-array transition to DEPTH_READ_ONLY + SHADER_READ before anything
  samples it (sync2 helpers, `VulkanBarriers.h:21-39`). It executes the view
  list decided at extraction; no scheduling happens during command recording.
- `LightBindings`: the set-2 descriptor set (finding 2.5): binding 0 = atlas
  `sampler2DShadow`, binding 1 = cube array `samplerCubeArrayShadow`,
  binding 2 = probe volume `sampler3D` array (Phase 3B), double-buffered per
  frame in flight, dummy-backed from creation (6.6).

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
frames. Ties break on the stable light identity (6.4), so allocation is
deterministic and does not flicker when two lights hover at equal importance.
Over-budget lights render unshadowed (`ShadowIndex` stays UINT32_MAX); a
counter and an editor warning surface it (Sections 9, 10). Tier downgrade
under pressure (High request served at Medium when the atlas is full) is
applied before outright denial.

### 6.3 Update policies and view scheduling

Per-light `ShadowUpdate`:

- `EveryFrame`: re-render whenever resident (flashlights, lights on movers).
- `OnChange` (default): re-render when the light's state hash changes or a
  caster-diff event intersects its volume (6.4).
- `Static`: render once on slot acquisition; afterwards only explicit
  invalidation (editor edit, console command `render.shadow.invalidate`)
  re-renders. For editor-authored fixed scene lighting. By definition it
  ignores caster motion; the default policy is OnChange precisely so that
  Static is an explicit authored promise.

Per-frame shadow view count is clamped by `render.shadow.max_views_per_frame`
(default 12). Service order is deterministic and starvation-free: (1) slots
acquired this frame that have never rendered, (2) EveryFrame slots, (3)
invalidated slots, oldest invalidation first; ties break on slot index. If
EveryFrame demand alone exceeds the clamp, category 3 still drains because
categories rotate: an invalidated slot's age strictly grows until served,
and category 1 is bounded by the budget. A worst-case zone load therefore
amortizes over a few frames instead of spiking one, and no cached light waits
forever behind flashlights.

### 6.4 Invalidation: the caster table diff

Invalidation must know what changed and where it was before it changed.
Neither `Changed<T>` (precise but memoryless: no previous bounds, no
removal/detach/visibility events, chunk granularity) nor component hooks
(would couple ECS lifecycle to the renderer against the layering rule) can
supply that. The mechanism is a renderer-owned diff of consecutive caster
snapshots, entirely inside extraction:

- During the caster gather, `ShadowCasterExtractionSystem` appends one record
  per caster to this frame's table:

  ```
  CasterKey   = (RegistryId, EntityId)        // EntityIds are per-registry;
                                              // Registry.Id disambiguates
                                              // (Registry.h:68-76)
  CasterState = { WorldBounds (quantized), StaticMeshHandle (slot+generation),
                  SectionMask }
  ```

  Only entities passing the caster filter (component `CastShadows` AND
  material `CastShadows` AND `Visible`) are recorded, so any toggle of those
  flags manifests as appearance or disappearance in the table.
- After the gather, sort the table by key (a few thousand 32-byte records;
  the render queue already sorts more per frame) and linear-merge against the
  previous frame's sorted table:
  - in current only: **added** event, bounds = current bounds.
  - in previous only: **removed** event, bounds = previous bounds. This
    covers entity destruction, zone detach (the registry's keys vanish while
    the previous table still holds them), visibility off, `CastShadows` off
    at either level, and extraction filter changes.
  - in both with different state: **changed** event, bounds =
    `Union(previousBounds, currentBounds)`. This covers transform movement
    (no ghost at the departure site), mesh replacement (handle generation
    differs), and section-mask edits, i.e. every silhouette-affecting change
    representable in the extracted state.
- `ShadowResidency` intersects event bounds against each resident
  `OnChange` shadow volume (sphere for points, cone-bounding sphere for
  spots) and invalidates on overlap. The previous table is swapped, never
  cleared, only after the diff runs, so zone unload invalidates affected
  cached shadows before the previous state is discarded.
- Cost control at a coarse boundary: the sort+diff runs only when at least
  one resident slot has the OnChange policy; otherwise the gather still
  refreshes the table but skips the merge. The whole mechanism lives in
  extraction; no ECS component calls into the renderer. If captures ever show
  the diff itself hot, the recorded escalations are a
  `Changed<WorldTransform>`-driven prefilter (now known to be precise,
  finding 1.9) or a static/dynamic caster split, in that order (0.6).
- Determinism: tables are sorted by key, events are emitted in key order, and
  invalidation processing follows slot index order, so identical world state
  produces identical cache behavior. The stable light identity used for
  scoring ties and cache keying is the same `(RegistryId, EntityId)` pair,
  captured at light extraction.

### 6.5 Scratch and failure behavior

Every `ShadowView` writes its instance stream through
`VulkanFrameScratch::AllocateVertex` into the same per-frame slice as the
forward pass (`VulkanFrameScratch.h:41-58`, 1 MiB default). Worst-case math
(12 views x 1000 casters x 64 B = 768 KiB plus the main pass) can exceed the
default, so: the slice size becomes an `EngineRuntimeConfig` value; shadow
view recording that fails allocation skips that view, leaves the slot
invalid (it re-queues next frame), warns once, and increments a counter. The
budget table carries the sizing guidance (Section 14).

### 6.6 Dummy resources

`LightBindings` is created in 3A.3 with always-valid descriptors: a 1x1 D16
depth texture (cleared to 1.0) for the atlas binding, a 6-layer 1x1 cube
array for the point binding, and 1x1x1 RGBA16F volumes for the probe binding
(written in 3B, dummy until then). The forward pipeline can therefore bind
set 2 unconditionally from 3A.3 onward, with no partially-bound descriptor
features and no shader-side "does the set exist" special case; a fragment
with `ShadowIndex == UINT32_MAX` never samples the dummies anyway.

### 6.7 Caching effect

Steady state for an indoor scene of `Static`/`OnChange` lights is zero shadow
draws: the pass renders only invalidated views. `ShadowCacheHits/Misses`
counters make the cache observable, and the atlas debug view shows slot ages
(Section 9.6).

---

## 7. Baked-lighting recommendation

**Decision: zone-scoped irradiance probe volumes, baked in the editor against
static render geometry, dilated at bake time, streamed with zones, sampled
per fragment with plain hardware trilinear filtering. No surface lightmaps in
this phase.**

### 7.1 Why probes, why zone-scoped

Compared against the alternatives, grounded in this codebase:

1. **Traditional surface lightmaps: rejected.** They require a second UV set
   (the cooked vertex just moved to tangents, Decision M; another vertex
   stream bump plus an unwrapper/packer is a large tool investment),
   per-texel density management over brush geometry that recooks per edit
   (`DocumentCook` re-cells geometry per cook,
   `editor/kyusu/src/document/DocumentCook.cpp:238-258`, so packing would
   churn), streaming of per-zone lightmap pages, seam handling, and long
   bakes. They also do nothing for dynamic objects, which still need probes.
   (Amended in revision 4: REVERSED for DIRECT light only, Section 7C. The
   unwrapper objection dissolved for brush geometry: charts grow from the
   authored soft/hard edge topology, no general unwrapping; the owner's
   whole-world-baked spec made texel storage the right medium. This
   rejection stands for the ambient/indirect payload: probes remain the
   mechanism that also covers dynamic objects.)
2. **Baked per-vertex irradiance: rejected as the primary mechanism.** Fits
   the target era and is cheap to sample, but couples bake output to vertex
   density (brush-cooked cells are coarse), invalidates on every geometry
   cook, needs a vertex-format bump, and still leaves dynamic objects
   unsolved. Recorded as a possible later addition for hero static meshes.
   (The shipped per-vertex *direct* payload, Section 7B, is a different term
   with a different motivation and does not reopen this rejection: probes
   remain the primary baked-ambient mechanism.)
3. **Irradiance probes in zone-scoped volumes (chosen).** One mechanism
   covers static and dynamic receivers per directive 3 (behavior from data,
   one pipeline); storage is decoupled from geometry so a brush edit
   invalidates the bake logically, not structurally; zone scoping gives
   streaming, eviction, and cross-room leak containment via the existing
   registry lifecycle (Section 1.6).
4. **Hybrid direct-dynamic / indirect-baked (chosen by construction).**
   Direct lighting stays fully dynamic (points, spots, their shadows); probes
   replace only the hemispheric ambient term. This is the smallest bake that
   changes how rooms feel. (Amended in revision 4: direct stays dynamic *by
   default*; a light explicitly authored `LightBakeContribution::Direct` bakes
   its diffuse into cooked cell vertices and leaves the runtime set entirely,
   Section 7B. Probes still replace only the ambient term.)

What the first bake computes (deliberately modest): for each probe, N = 128
rays in a fixed precomputed direction table; each ray traced against the bake
BVH (7.2); a miss contributes sky/ground hemispheric color by ray direction;
a hit contributes the direct lighting at the hit point (Lambert from the
zone's static shadow-casting lights, occlusion tested with one shadow ray
each) times a constant bounce albedo (`render.bake.albedo` cvar, default
0.35). The result is projected into L1 spherical harmonics (12 fp16
coefficients = 24 bytes per probe). That yields sky occlusion, one indirect
bounce, and colored room mood. It is not modern GI and does not try to be.
Emissive surfaces occlude but do not emit in v1 (recorded as deferred).

Determinism: ray directions come from a precomputed table indexed by probe
index (no time- or address-seeded randomness), each probe writes only its own
output slot, and the bake parallelizes per probe row via
`JobSystem::ParallelFor` with the `worker_count == 0` path as the reference;
a test asserts serial and parallel bakes are bit-identical (pattern:
`test/runtime/ZoneParallelTests.cpp:169-202`).

### 7.2 Bake geometry: static render meshes, not collision

The bake must see the surfaces the player sees. Cooked collision happens to
be derived from the same brush geometry today, but that is an implementation
coincidence, not a contract: collision may adopt primitives, hulls, or
gameplay-only shapes at any point, and nothing would flag the divergence
until lighting silently changed. The bake therefore builds its own transient
acceleration structure:

- Input: the zone's cooked scene entities whose caster filter passes (the
  same component AND material `CastShadows` rule the shadow system uses),
  with their `.smesh` geometry loaded CPU-side via
  `MeshLoader::LoadFromFile(path, MeshGeometry&)`
  (`MeshLoader.h:29`); triangles transformed to world space per entity.
  The runtime keeps no CPU vertex data (`GpuStaticMesh.h:16-27`), which is
  fine: the bake runs in the editor process where the cooked files are at
  hand.
- Structure: a median-split triangle BVH, built per zone, used for the bake,
  discarded. CPU-only, no new engine subsystem, no physics dependency (a
  layering win: the render bake no longer touches the physics module at
  all).
- **Read-only neighbor halo (added in revision 3).** A per-zone BVH built
  from only that zone's geometry gives probes near a zone boundary a wrong,
  one-sided view of enclosure, producing a lighting seam between adjacent
  zones. So the BVH also ingests, as read-only occluders that receive no
  samples, the cooked geometry of every spatially adjacent zone within the
  bake's maximum ray distance of this zone's bounds. Zone bounds are AABBs in
  the manifest (`WorldPartitionManifest.h:56`), and the world cook has every
  zone's manifest in hand (`WorldCook.cpp`), so "which zones are within
  distance D of zone Z" is a trivial offline AABB-expansion query, not the
  runtime cross-zone spatial query the world-partition invariant forbids
  (`world-partition-authoring.md:167-169`). The halo geometry is assembled in
  a deterministic order (sorted by cooked-mesh content hash) so two zones that
  share a boundary see byte-identical halo triangles and, with the fixed ray
  table, compute byte-identical boundary results. This halo is shared with the
  vertex-AO bake (Section 7A) and is the reason both bakes can be per-zone yet
  seamless. It was a latent defect for probes in revision 2; the AO review
  surfaced it.
- Content identity: the bake input hash = per-cell `.smesh` content hashes
  (already tracked by the cook index, `DocumentCook.cpp:312-320`) + the halo
  zones' `.smesh` hashes + static light state + volume placement/params + bake
  settings. Hashing the halo means editing zone B correctly invalidates zone
  A's boundary bake. It is stable because it hashes cooked artifacts, not live
  editor state.
- Using Jolt collision instead was rejected rather than kept as a first step:
  binding the physics query API is not meaningfully less work than a small
  BVH over `MeshGeometry`, and the BVH also provides exact triangle normals
  for the inside-geometry classification below.

### 7.3 Validity: classify, dilate at bake, sample plainly at runtime

Hardware trilinear filtering interpolates before the shader sees anything,
so per-texel validity cannot weight the blend at runtime. The plan is
therefore:

- **Classify at bake.** A probe is invalid when its short-ray backface hit
  ratio exceeds a threshold (it sits inside or intersects geometry).
- **Dilate at bake.** Iteratively fill each invalid probe with the average of
  its face-adjacent (6-neighborhood) valid neighbors until every texel in
  the uploaded grids holds usable SH data; probes still unfilled (a fully
  invalid volume) get the hemispheric ambient projected into SH.
- **Sample plainly at runtime.** Ordinary hardware trilinear on the SH
  textures. No validity texture is uploaded; the runtime validity texture
  from revision 1 is deleted.
- **Keep validity for diagnostics.** The `.sprobe` file retains the validity
  bitset; the editor overlay tints invalid (dilated) probes and the lighting
  panel counts them (Section 10). The runtime ignores the chunk.

Leak behavior of dilation, stated honestly: dilation never crosses volume
boundaries (each volume's grid is independent by construction, and volumes
are authored per room), so the cross-room leak the zone scoping exists to
prevent cannot re-enter through dilation. Within one volume, a thin interior
wall whose in-wall probes get filled from the lit side can still leak into
the dark side at the wall. Countermeasures, in order: author cell size at or
above wall thickness (guidance surfaced in the lighting panel), split the
volume at the wall, and read the dilated-probe overlay which marks exactly
where filling happened. True occlusion-aware interpolation (per-probe
visibility weights, manual 8-corner fetch) is the recorded escalation with
its real cost written down (Section 15), not a default.

### 7.4 Volume selection and sampling

Runtime sampling is per fragment: test the world position against the
resident volume headers in the frame UBO (at most
`kMaxResidentProbeVolumes = 8` active), select by (1) highest explicit
`Priority`, (2) smallest volume among equal priorities, (3) stable volume id,
then sample three RGBA16F 3D textures (one per color channel, each texel
holding that channel's four L1 SH coefficients) with hardware trilinear
filtering and evaluate irradiance for N. The stable id is
`(ZoneId, volume index in .sprobe file order)`, which is cook-deterministic,
so precedence cannot flicker between runs or zone reloads. Artist priority
overriding size means a deliberate "hallway override" volume can sit inside
a room volume regardless of relative extents.

Fragments covered by no volume fall back to the existing hemispheric ambient,
so unbaked zones look exactly as they do today. Per-fragment selection is
chosen over per-instance volume indices because instanced runs merge across
zones (`RenderQueue` merges by mesh/material only, Section 1.2) and because
room-sized cooked cells need intra-draw ambient variation; the cost is eight
AABB tests per fragment, an always-on cost of the probe feature that 3B
validation measures against its budget (Section 14). The volume list is
rebuilt at extraction from resident zones; a `ProbeVolumeSet` owns GPU
textures per zone and drops them with the zone.

Dynamic objects sample the same volumes per fragment; nothing special-cases
them. Normal-dependent response comes from L1 SH evaluation.

### 7.5 Storage and streaming

- New chunked binary `.sprobe` (FourCC `'SPRB'`, `kProbeFormatVersion = 1`,
  `BinaryHeader` + `ChunkHeader` per `core/serialization/BinaryFormat.h`),
  one file per zone: per-volume header chunk (grid transform, dims,
  priority, stable index), SH payload chunk (dilated, upload-ready), validity
  chunk (editor-only). Unknown chunks skip forward-compatibly, matching
  `.smesh`/`'SCNE'` practice.
- `ZoneHeader` gains `CookedProbeRef` + `CookedProbeContentHash` beside the
  existing cooked trio (`WorldPartitionManifest.h:60-62`), written only when
  nonempty (the manifest reader tolerates unknown keys,
  `WorldPartitionManifest.h:112-114`).
- Runtime load rides `ZoneLoadRecipe`: bytes read and parsed in `Build`
  (off-thread), GPU volume textures created and registered in `Finalize`
  (main thread at `DrainAsyncTasks`), residency dropped on zone destroy.

---

## 7A. Baked ambient occlusion

Added in revision 3. AO lives inside the baked-lighting phase, not beside it:
it reuses Section 7's triangle BVH, ray kernel, halo, and determinism, and it
extends the same `LightingBake` orchestrator. This section is the canonical AO
design; Sections 3.2, 9.6, 10, 11, 12, 13, 14, 15 carry the threaded
consequences.

### 7A.1 Architectural verdict

Two scales, but not the two the brief proposed. The proposed split was
"probe ambient visibility with a bent normal" plus "cooked vertex AO." I
**reject the separate probe visibility payload** and keep only the second
scale as new baked data:

- **Room-scale enclosure is already baked, in the L1 SH probe irradiance
  (Section 7).** A probe deep in a corner already receives fewer sky and
  bounce rays, so its irradiance is already darker; a large room already
  darkens gradually toward its walls in the probe field. That is precisely
  the "broad room-scale enclosure so cooked vertex density is not needed for a
  large room gradually darkening" requirement, and it is met by data the plan
  already stores. Storing a *second* per-probe scalar visibility grid at the
  same cell density would re-derive occlusion the irradiance already encodes,
  at the same resolution, buying nothing: a sub-cell alcove is unresolved by
  either grid, and the fix for sub-cell features is the vertex scale, not a
  denser second probe payload.
- **The bent normal is redundant with the SH we already store.** L1 SH is a
  constant term (band 0) plus a linear directional gradient (band 1, three
  coefficients that encode a dominant direction and magnitude). Because probes
  bake sky plus one indirect bounce and hold no direct local light (the hybrid
  split, Section 7.1), the SH band-1 dominant direction is, to the fidelity
  this renderer targets, the direction of greatest openness: a
  bent-normal-equivalent, free. The only consumer that would demand a sharper,
  separately stored bent normal is ambient specular, and StandardLit has no
  ambient specular in v1 (specular is direct-light only, Section 3.2). So the
  bent normal is deferred with ambient specular, and the "bent-normal
  direction" debug view visualizes the SH band-1 direction, labeled honestly.

So the AO work that is genuinely new is entirely at the surface/contact scale:
**cooked per-vertex AO** for corners, recesses, wall-floor contacts,
undersides, carved openings, and permanent architectural intersections. The
probe scale needs a doctrine note (Section 11, Phase 3B.2) and the halo fix
(Section 7.2), not new payload.

This verdict keeps the solution Sencha-scale: one new baked scalar per vertex,
one shared bake, no second volume format, no bent-normal encoding to validate.

### 7A.2 Doctrine (the hard boundaries)

- AO represents ambient visibility and static geometric enclosure only.
- It modulates the ambient term only (Section 3.2, `aoFactor`), never diffuse,
  specular, or emission.
- It is never baked direct lighting, never a shadow mask, never a stand-in for
  a point, spot, or (future) directional shadow map. Runtime shadow maps stay
  the only authoritative direct-shadow representation (the Section 4-6
  doctrine is unchanged).
- No lightmap textures, no secondary lightmap UVs, no chart generation, no
  atlas packing, no offline direct-shadow artifacts. AO rides existing vertex
  attributes, produced by derived cook data.

Baked static direct lighting (Section 7B) shipped without touching these
rules: it is a separate vertex channel and a separate additive diffuse term,
not this AO datum. Its bake-time visibility ray prevents leaks through walls;
it is not a shadow-map substitute, and sharp static shadows remain cached
runtime maps (`ShadowUpdate::Static`). The AO scalar still never encodes
direct light.

The rejection list in Section 15 makes these enforceable, not aspirational.

### 7A.3 Bake-time shading topology (the weld) and hard edges

Runtime vertex identity is not AO topology. Kyusu and the cooker duplicate
geometrically coincident vertices for section/material splits, batching, UV
seams, hard-normal splits, and abutting-brush cell boundaries (brushes are
bucketed whole into cells and never split, `BrushClustering.h:22,49`, so cell
boundaries create coincidence only between *different* abutting brushes, never
within one face). Baking AO independently per render vertex would give
duplicates on one continuous surface slightly different values, i.e. a visible
seam down a flat floor split into two draw calls.

The bake therefore constructs a transient **AO sample topology** independent of
the final GPU vertex layout:

- Weld render vertices into one AO sample when they share: quantized world
  position (a global quantization grid, quantum `render.bake.ao.weld_quantum`,
  default 1 mm, so a boundary vertex quantizes identically in either zone's
  bake), a compatible shading normal (same hemisphere and within
  `weld_normal_degrees`, default 25), the same surface side, and continuous
  geometric context.
- Bake one AO value per welded sample, then scatter it back into every
  contributing render vertex. Coplanar sections and cross-zone boundary
  duplicates therefore carry identical AO by construction, not by luck.
- **Hard edges stay independent.** A wall and a floor sharing a position but
  using different normals fail the normal-compatibility test, so they are
  separate samples, sample different hemispheres, and may correctly receive
  different AO. This is the same "smooth-group" boundary the normal split
  already encodes; the weld reuses it rather than inventing a new one.

The weld is deterministic (quantized keys, stable sort), so it composes with
the bit-identical-bake requirement (Section 7.1).

### 7A.4 Cross-zone seams

Per-zone cooking would give a continuous surface split across a zone boundary
two independent AO solutions. Three mechanisms, all already needed for probes,
make boundaries seamless with no inter-zone data flow at cook time:

- The read-only neighbor halo (Section 7.2) means a boundary sample in zone A
  sees zone B's occluders and vice versa, so neither computes a one-sided AO.
- The global weld quantization (7A.3) means both zones place the shared
  boundary sample at the identical quantized position.
- Determinism (fixed ray table, sorted halo, no RNG) means both zones, seeing
  the same sample position and the same halo triangles, compute byte-identical
  AO. No shared-authority handshake, no boundary-owner zone, no cook ordering
  dependency.

A shared-boundary-authority scheme (one zone bakes the edge, neighbors copy)
was rejected: it adds cook-order coupling and an inter-zone data channel to
buy what determinism-plus-halo already gives for free.

### 7A.5 Adaptive tessellation for sparse geometry

Because brushes are never split (`BrushClustering.h:49`), a massive floor is
one brush with a handful of vertices in one cell. Baking AO at those vertices
captures nothing of a wall crossing the middle, and there is no free cell-grid
baseline to lean on. So render-only densification is **required**, not
optional, for cooked vertex AO to mean anything on Kyusu's large faces, and it
cannot be uniform (a fine uniform tessellation of a warehouse floor is
unacceptable) nor demand manual tiling.

The cooker adds render-only vertices without touching Kyusu's editable brush
(the cooked mesh is generated fresh, so inserting vertices there is already
within the cook's remit). The refinement is occluder-gated error-driven
subdivision:

1. Bake AO at the triangle's existing vertices.
2. **Gate:** skip the triangle entirely unless at least one vertex is within
   `render.bake.ao.radius` of some occluder in the BVH. An open floor slab far
   from any wall is never touched. This is the mechanism that satisfies "a
   large open floor stays nearly untouched" and "extra vertices cluster around
   walls, pillars, recesses, stairs, contacts."
3. For a gated triangle, bake AO at candidate points (edge midpoints first,
   then the centroid) and compare against the value linearly interpolated from
   the current vertices.
4. Subdivide (insert the candidate, retriangulate) only where the error
   exceeds `render.bake.ao.tess_tolerance` (default 0.1 in AO units).
5. Recurse to `render.bake.ao.tess_max_depth` (default 2) or until an edge is
   shorter than `render.bake.ao.tess_min_edge` (default 0.25 world units).
6. Edge midpoints are inserted at the deterministic quantized average of the
   endpoints, so a shared edge between two cooked meshes (or across a zone
   boundary) subdivides identically on both sides and stays watertight and
   weldable.

Verdict on complexity: error-driven adaptive tessellation is worth it for v1
here specifically because the "never split brushes" cook makes the sparse
degenerate case the common case, and the occluder-proximity gate keeps it
cheap and local. The conservative escape is real and supported: setting
`tess_max_depth = 0` disables refinement entirely and ships vertex AO at raw
brush-vertex density (useful only on already-dense authored geometry). The
recommended v1 default is depth 2; the debug density view (9.6) shows where
vertices were added so the tolerance can be tuned against real levels.

Amended in revision 4: the baked-direct payload implemented exactly this
error-driven scheme first and rejected it on screen. Error-driven insertion
produces irregular, skinny triangles, and interpolating a smooth signal across
them streaks visibly; every acceptable capture came from a regular lattice.
Direct light shipped with distance-graded uniform subdivision instead (a pure
per-edge length predicate, proximity-gated, no rays during refinement; Section
7B.4). AO differs in its favor (a scalar, lower-frequency, modulating an
already-dim ambient term), so error-driven refinement may still be right here,
but 3B.3 must judge it against a graded-uniform variant with a screenshot A/B
before committing; `DirectLightTessellate` is the working reference for the
graded shape. This does not collide with Section 15's rejection of "uniform
tessellation": that rejects ungated global densification, while distance-graded
subdivision keeps the proximity gate, so open surfaces still stay untouched.

### 7A.6 Storage, packing, and memory

Amended in revision 4: the offset-48 / location-8 slot described below is
occupied by the lightmap UVs (Section 7C; briefly by 7B's RGBM channel before
that), and `.smesh` is at v5. If vertex AO still lands as vertex data it takes
offset 52 (stride 52 to 56), location 10 (9 is the per-instance lightmap
scale/bias), and `.smesh` v6, with the loader rejecting prior versions per the
single-version doctrine (cooked content recooks via `kCookedCacheIndexVersion`)
instead of loading old files as neutral. With the per-zone atlas machinery now
live, 3B.3 should first weigh AO as an atlas channel instead of vertex data:
the sampling density question that forced adaptive tessellation dissolves in
texel space. The packing analysis below otherwise stands.

- `StaticMeshVertex` (48 bytes today: `Vec3d` position + `Vec3d` normal +
  `Vec2d` uv + `Vec4` tangent, all float-backed since `Vec3d = Vec<3>` float,
  `Vec.h:381-382`) gains a 4-byte `R8G8B8A8_UNORM` attribute at offset 48
  (stride 48 -> 52, still 4-byte aligned, no padding): byte 0 = AO (unorm8),
  bytes 1-3 reserved (default 0xFF; a future octahedral vertex bent normal
  could take bytes 1-2 if ambient specular ever lands). Vertex attribute
  location 8 (0-2 base, 3-6 instance matrix, 7 tangent from 3A.1, 8 AO).
- unorm8 precision: 256 levels over [0,1]. AO is smooth and low-frequency, it
  modulates an already-dim ambient term, and it passes through the
  `ao_strength`/`ao_min` dials, so 1/256 banding is below perceptible. Checked,
  as required, rather than assumed: a full float AO channel would cost 4x the
  bytes for no visible gain at this use.
- `.smesh` bumps `kSmeshFormatVersion` 3 -> 4 (`StaticMeshFormat.h:16`). v3
  files load with AO defaulted to 1.0 (neutral): old cooked meshes render
  exactly as before. The cook regenerates meshes at v4.
- Memory: +4 bytes per render vertex, across static and skinned rest geometry
  (the shared `MeshGeometry`, Decision M). A cooked room of ~50k render
  vertices costs +200 KiB on disk and in VRAM; adaptive tessellation is capped
  so a zone cannot exceed `render.bake.ao.max_vertex_growth` (default 2.0x
  base vertices, logged if hit). Prop assets that never bake AO still pay the
  4 bytes at neutral; that is the price of one uniform vertex format and one
  draw path, and it is negligible.
- No new streamed file for vertex AO: it rides the `.smesh` the zone already
  cooks and streams. Only the probe payload keeps its own `.sprobe`.

### 7A.7 Composition with probe visibility

`ambient = ambientC * aoFactor` with `aoFactor = mix(1.0, mix(ao_min, 1.0,
vertexAo), ao_strength)` (Section 3.2). The double-darkening the brief warns
about (probe irradiance already dark in a corner, then multiplied again by
vertex AO) is bounded three ways, deliberately rather than by a heavier model:

- **Scale separation.** `render.bake.ao.radius` (default 0.5 world units) is
  kept well below the probe cell size (default 1.0+). Vertex AO then captures
  occlusion finer than the probe grid resolves, so the two mostly darken
  disjoint bands and the multiply is close to correct rather than a literal
  double count.
- **Strength dial.** `ao_strength` (0.75) pulls the whole effect back from a
  literal multiply.
- **Floor.** `ao_min` (0.15) and the existing additive `render.style.min_ambient`
  keep corners legible instead of crushed black.

A physically-motivated composition (treating vertex AO as modulating only the
residual ambient the probe could not resolve) was considered and deferred: it
needs a stored per-probe visibility to know the residual, which reintroduces
the payload 7A.1 rejected. The dialed multiply is the deliberate v1 model; the
"probe + vertex AO without crushed blacks" validation scene (Section 13) is
what tunes the two defaults against real content, and a richer authored
composition is an explicit later option, not a v1 obligation.

### 7A.8 Static instances and dynamic objects

The instancing path (one shared vertex buffer drawn many times, Section 1.2)
must not be broken by placement AO.

- **Cooked level geometry is not instanced.** Brush cells cook to unique
  per-cell meshes (`BrushClustering.h`), so world-space vertex AO baked into
  them costs no instancing: they were always unique. This is the primary and
  only v1 carrier of world-placement AO.
- **Placed props are instanced, and get no world AO.** A reusable mesh placed
  many times shares one vertex buffer; baking placement AO per instance would
  fork that buffer per placement and kill the instanced run. So props carry
  neutral vertex AO (1.0) and take their placement grounding from the probe
  irradiance they already sample per fragment (Section 7.4). A crate in a dark
  corner is grounded by the probe field, not by baked vertex AO.
- **Self-AO** (a mesh's own concavities, intrinsic to the asset and identical
  in every placement) can later be baked once into the shared `.smesh` from
  the isolated mesh and shared by all instances without breaking instancing.
  This is the same mechanism as authored object-space cavity AO for props and
  characters. It is deferred to a follow-up (it needs a mesh-asset cook step,
  not the world cook) and is not required for v1.
- **Dynamic objects** sample probe irradiance per fragment and may carry
  authored self/cavity AO in their asset, but never receive world-placement
  vertex AO baked for one static pose. The rule is structural: world AO is
  written only into cooked cell meshes, which dynamic objects are not.

### 7A.9 Bake implementation and incremental invalidation

The AO bake reuses Section 7's authority verbatim: cooked render triangles, the
transient renderer-owned BVH with the neighbor halo, no collision/Jolt
dependency, finite-distance cosine-weighted hemisphere sampling over the
shading normal, normal-offset ray origins against self-intersection,
`render.bake.ao.rays` (default 128) directions from the same fixed table the
probe bake uses, and deterministic output. Thin-wall/backface policy: rays are
traced only into the sample's own upper hemisphere and only to
`render.bake.ao.radius`; a hit within radius (including a backface hit, which
is real geometry) contributes occlusion with a smooth distance falloff to
neutral at the radius; rays never tunnel to far geometry, so a carved window or
a thin door frame does not pull dirty occlusion from the opposite wall.

Incremental invalidation composes with the probe staleness key (7.2): a zone's
AO output is stale when its own cell `.smesh` hashes, its halo zones' `.smesh`
hashes, or the AO bake settings change. Editing one zone rebakes that zone plus
the spatially adjacent zones whose halo included it, not the world. Because AO
and probe irradiance share the BVH, the halo, and the ray kernel, they are
produced in one pass of the `LightingBake` orchestrator per zone: build the
BVH once, cast for probes and for vertex-AO samples, emit `.sprobe` and the
`.smesh` AO channel (v5 after the 7A.6 amendment) together.

### 7A.10 Debug views (Section 9.6 additions)

Through the development-only debug shader and the editor line/overlay batch:
raw vertex AO (grayscale), probe visibility (probe irradiance luminance),
bent-normal / ambient direction (SH band-1 dominant direction, labeled as the
SH-derived proxy), final combined ambient visibility (`ambientC * aoFactor`),
and AO sample density / adaptive tessellation (added-vertex heat or wireframe
over the base brush edges, so a reviewer sees refinement clustering at walls
and absent on open floor). Plus the doctrine proof view: AO term isolated, to
confirm by eye it never bleeds into a direct-lit surface.

---

## 7B. Baked static direct lighting, generation one: per-vertex (SUPERSEDED)

SUPERSEDED within the same series by the per-zone lightmap atlas (Section 7C):
the vertex channel, the tessellator, and the tuning cvars described below were
deleted per the remove-don't-deprecate doctrine when the owner's spec
sharpened to whole-world baked lighting on streamed weak-hardware targets.
This section is retained as the design record of generation one; its evidence
lives under `docs/plans/evidence/baked-direct/`. The lessons that carried
forward: bake shading not shadows, CPU/shader model parity, additive-zero
neutrality, single-version formats, and the on-screen rejection of
error-driven tessellation.

Added and implemented in revision 4. Sections 7 and 7A describe designed but
unbuilt payloads; this section described shipped code when it was current.
Motivation: the measured investigation under
`docs/plans/evidence/point-light-cost/` convicted the 64-light forward cap as
a correctness wall (at 96 visible lights, 32 drop silently) with a real ~80 us
per light cost, and the target art direction (dark rooms, dozens of small
static accent lights per view) exceeds the cap in real levels. Baking a static
light's diffuse into the vertices it reaches removes it from the runtime set
entirely. Every claim below has a capture or a test under
`docs/plans/evidence/baked-direct/`.

### 7B.1 Authoring contract and limitations

`LightBakeContribution` gained `Direct` (schema string "direct") alongside
`None` and `Indirect`, on both point and spot lights. `Direct` means: this
light's diffuse is baked into static cell vertices at cook, and the light is
skipped at extraction (`LightExtractionSystem`), so it never scores against
the budget, never packs into `frame.Lights`, never counts toward the 64 cap,
and never requests a shadow tile. The runtime adds one view-independent term
after the light loop, `lit += baseColor.rgb * bakedDirect`, gated by
`render.baked_direct.enabled`; the unlit family skips it.

The limitations are structural, not tuning, and bound what designers may
author as `Direct`:

- No specular from baked lights (the term is view-independent). Hero lights
  that need a highlight stay dynamic.
- Dynamic objects receive nothing from baked lights until 3B.2 probes exist
  (the term lives on static cell vertices). Keep key lights dynamic; author
  `Direct` lights as true static fill.
- Baked lights cannot move, flicker, or animate.
- Instanced props carry the channel at neutral (the 7A.8 instancing rule);
  only unique per-cell cooked geometry is baked.

### 7B.2 Storage

`.smesh` v4: `StaticMeshVertex` grew from 48 to 52 bytes with an RGBM
`R8G8B8A8_UNORM` attribute at offset 48, vertex attribute location 8
(`StaticMeshFormat.h`, `MeshForwardPass.cpp`). Neutral is additive zero, so an
unbaked mesh renders byte-identical to before (proven by the parity capture).
`kSmeshFlagBakedDirect` marks a meaningful channel. The RGBM multiplier
`kBakedDirectRange = 16` (`DirectLightBake.h`) must equal
`BAKED_DIRECT_RANGE` (`mesh_frame.glsli`); summed radiance clips at that
ceiling, visible only as a tonemap-shoulder white core under extreme pool
overlap. Single version live, per codebase doctrine and against the 7A.6
sketch: the loader rejects v3 outright and cooked content recooks via the
`kCookedCacheIndexVersion` bump.

### 7B.3 Bake and staleness

The bake runs at cook time inside the document cook (`DocumentCook.cpp`), so
PIE and cooked runs are always fresh with no interactive step. The math is
engine-side and unit-tested under `assets/cook/` (`BakeBvh`,
`DirectLightBake`), gated behind `SENCHA_ENABLE_COOK`; it deliberately does
not live under `render/` because cook-only code never links into the runtime.
Per cook: collect `Direct` point and spot lights from the live registry (spot
cones packed via the same `MakeSpotGpuLight` path the runtime uses), build one
median-split triangle BVH over every cell's cooked triangles, tessellate each
cell near its reaching lights (7B.4), then per vertex sum
`wrap(N,L) * atten * visibility * color * intensity` with the CPU model kept
exactly equal to `lighting.glsli` (the wrap factor is read from
`render.style.diffuse_wrap` at cook, so baked and dynamic shading share one
model). Visibility is one occlusion ray per vertex-light pair with a small
normal offset: leak prevention through walls, not a penumbra. Deterministic by
construction (fixed geometry order, no RNG).

Staleness: `Direct` light state and the bake and tessellation parameters fold
into the existing brush-input cook hash, so touching a light or retuning
restales the level. The parameters are hashed only when `Direct` lights
exist, so levels without baked lights do not recook on a tuning change.

### 7B.4 Tessellation: distance-graded uniform subdivision

The quality-maker, and a deliberate departure from 7A.5's error-driven scheme,
which was implemented first and rejected on screen (7A.5 amendment). The
shipped scheme (`assets/cook/DirectLightTessellate`) is a pure per-edge
predicate: split an edge while it is longer than
`max(minEdge, d * gradingFactor)`, where `d` is the light's distance to the
closest point ON the edge segment, considering only lights within
`range + margin` of the edge. Gating on the closest point rather than the
midpoint matters: midpoint gating leaves lit vertices wired to distant corners
and streaks radially. The predicate converges to a regular lattice near
lights, needs no rays during refinement, and, with memoized per-edge decisions
and quantized midpoints, conforms with no T-junctions (verified watertight on
the cooked mesh). Geometry beyond every light's reach is never touched.

The vertex-growth cap is a runaway backstop only (a multiplier over the cell's
base vertex count); a binding cap starves refinement mid-pool and shows
immediately. Defaults: grading 0.15, min edge 0.25, max depth 6, proximity
margin 1.0, growth 256x. Large brush faces (32 units) need depth 8 to reach
their graded targets; the stress scene sets it. Depth 0 disables refinement
entirely.

### 7B.5 Tooling

- Tunables are archived editor cvars: `editor.cook.bake_grading`,
  `bake_min_edge`, `bake_max_depth`, `bake_margin`, `bake_growth_cap`. A
  deviation from the `render.bake.*` sketch: these are cook inputs, not
  runtime state, so the editor owns them.
- `render.debug.view baked_direct` isolates the raw baked vertex irradiance;
  dynamic-lit and unbaked surfaces read black. This is the doctrine proof
  view: baked light visibly never appears where it was not baked.
- The kyusu lighting panel shows the authored `Direct` light count;
  `DocumentCookResult` carries the baked-light and added-vertex counts and the
  PIE cook logs them.
- No dedicated Bake button: the bake rides every cook, so Cook and PIE are the
  button. The Section 10 interactive-bake flow becomes worthwhile only if bake
  time ever dominates cook time.

### 7B.6 Validation (the closed loop)

The baked counterpart of the original measurement, same rig and capture
tooling:

| | dynamic 96 | baked 96 |
| --- | --- | --- |
| LightsVisible (median) | 64 (capped) | 0 |
| LightsDroppedAtCap | 32 (lights vanish) | 0 |
| MainColor median | 5.08 ms | 0.041 ms |
| Triangles | 2116 | 49682 |

All 96 pools render; the cap wall is gone; the per-frame cost is noise even at
23x the triangles. Reproduction is committed with the evidence
(`BakedLightingStressGen`, env-gated). Unit coverage: RGBM round trip,
analytic single-light match against the shader model, occlusion, range,
determinism, tessellation grading and conformity, end-to-end cook. The rim
stair-stepping visible in captures is renderer-wide rasterization aliasing (no
MSAA in the forward pass), present on every hard edge, unrelated to the bake.

### 7B.7 Deferred, with triggers

- **Cross-zone halo.** The bake is per-document today: a `Direct` light near a
  zone boundary neither lights nor is occluded by the neighbor zone. Fold in
  with 3B.1's halo (Section 7.2) when real worlds put baked lights at zone
  seams.
- **The weld (7A.3).** Exactly-coincident duplicate vertices already bake
  identically by determinism plus quantized tessellation midpoints; the full
  weld is needed only if hard-edge seams ever show on real content.
- **Soft baked penumbra** (a few jittered visibility rays from a fixed table):
  an authoring nicety once real content wants softer pool edges.
- **glTF export of the channel**: baked light is not a glTF concept; map to
  COLOR_0 if an external tool ever needs it.

---

## 7C. Baked static direct lighting, generation two: per-zone lightmap atlases (shipped)

The current mechanism, replacing Section 7B's vertex channel. Owner spec:
UE1-era look, whole-world baked static lighting, hitch-free zone streaming,
120 FPS on weak hardware. Full design and phase record in the approved plan
(the lightmap plan in the owner's plan directory) and evidence under
`docs/plans/evidence/lightmap-atlas/`; the mechanism in brief:

- **Authoring is unchanged**: `LightBakeContribution::Direct` on point and
  spot lights; excluded from the runtime set at extraction; additive diffuse
  term; `render.baked_direct.enabled` and the `baked_direct`/`lightmap_texels`
  debug views.
- **Charts** grow across authored SOFT edges (union-find mirroring the
  smoothing-group normals), are cut at hard edges, and cone-split on curves
  (45 degrees, `editor.cook.lightmap_cone`). Chart identity and chart UVs are
  part of the cook staleness hash (a coplanar soft-edge toggle changes no
  vertex byte but must restale).
- **One RGBM RGBA8 atlas per zone** (`.stex`, LinearData, no mips), shelf
  packed with 2-texel gutters, first and last row/column reserved black (the
  wrap-safe sentinel unbaked items sample). Overflow density-clamps by
  sqrt(2) steps; never multiple pages. Luxel default 0.25 m
  (`editor.cook.lightmap_luxel`), cap 2048 (`editor.cook.lightmap_max_size`).
- **Luxels** sample at grid points (N+1 texels per N luxels), rasterized with
  an interior-beats-edge rule and a half-diagonal edge reach for slivers,
  baked through the shared per-sample evaluator, dilated 2 ping-pong passes.
  **Buried-sample invalidation** (BakeBvh::FirstHitIsBackface along the
  sample normal) keeps luxels underneath overlapping brushes from baking
  black bleed: mandatory, since kyusu brushes overlap by design.
- **Vertices** carry unorm16 atlas UVs (`.smesh` v5, offset 48, location 8):
  absolute for cooked cells, [0,1] sheets for instanceable meshes, remapped
  per placement by a Vec4 scale/bias in the instance stream (location 9).
  The per-draw bindless atlas index rides push constants and joins the
  run-merge identity; per-instance scale/bias never does.
- **Placements**: baked brush meshes chart themselves into sheets; glTF
  imports TEXCOORD_1; the cook packs a per-placement rect (world density from
  the placed triangles), bakes it, and serializes the scale/bias on the
  component. Placed meshes occlude unless `AffectsBakedLighting` is off.
- **Editor**: the lighting panel's baked preview renders the last cook's
  scene (cells + atlas + placement rects) in Solid viewports with Direct
  lights excluded and a staleness badge; Cook refreshes it.
- **Streaming**: the atlas is a manifest asset preloaded in wave one; the
  streamed-world recipe warms it before attach.
- Closed loop (third generation of the measurement): 96 Direct lights render
  with LightsVisible 0, dropped 0, MainColor 0.019 ms, at 12 raw triangles
  (the tessellated vertex path needed 49682).

Deferred with triggers: cross-zone halo (with 3B.1's), per-light occlusion
masks x runtime intensity (the UE1 animated-static-light extension the atlas
medium exists to allow), soft baked penumbra, and BC compression of atlases
if memory ever asks.

---

## 8. Probe-volume and 3D-grid recommendation

**Decision: reuse the existing lattice types; add one small value type; do
not build a generalized spatial-field framework this phase.**

Inspection result: Sencha already has `Grid3d<T>`
(`engine/include/math/spatial/Grid3d.h`), a flat dense 3D array with index
math, alongside `Grid2d`, `GridPlane`, and `QuadTree`. What is missing is
only the world mapping.

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

The generalized chunked/streamed/GPU-resident spatial-field primitive (water,
voxels, occupancy) is explicitly deferred under directive 4: today it would
have exactly one consumer, and probe volumes, water, and voxel systems will
want radically different storage. `Grid3d` + `GridTransform3d` are the real
shared substrate; anything more is speculative abstraction until a second
concrete user exists.

---

## 9. Profiling and debugging architecture

Rebuilt in this revision around one rule: **when instrumentation is off, the
work does not happen.** No claim of "free" is made anywhere; disabled means
absent.

### 9.1 The mode ladder and the instrumentation bundle

```cpp
enum class RenderProfileMode : uint8_t
{
    Off,       // default
    Counters,  // CPU counters published at pass boundaries
    Gpu,       // + timestamp scopes and command labels
    Capture,   // + capture ring, metadata, export commands armed
};

struct RenderInstrumentation
{
    RenderStats* Stats = nullptr;            // non-null in Counters and above
    GpuTimestampPool* GpuTimestamps = nullptr; // non-null in Gpu and above
    RenderCapture* Capture = nullptr;        // non-null in Capture
};
```

- The bundle is owned by the Engine beside `TimingHistory`; a pointer to it
  is added to `RendererServices` (`Renderer.h:69-86`) so features cache it at
  `Setup()` per the existing no-lookups-in-the-hot-loop constraint
  (`Renderer.h:40-43`). `DefaultRenderPipeline` receives the same pointer at
  wiring.
- Mode is the cvar `render.profile.mode` (enum string, Transient). Its
  `OnChange` stores a pending mode; the pending mode is applied once per
  frame at the top of `FramePhase::ExtractRenderPacket`, before any
  extraction or recording reads the bundle, so a frame sees exactly one mode.
  Off nulls all three pointers.
- Each mode includes everything below it. Semantics:
  - **Off**: the bundle pointers are null. No timestamp writes, no query
    resets or readbacks, no history/capture writes, no command labels, no
    allocations, traversals, or string work on behalf of profiling. The only
    profiling-adjacent work that exists at all is the pass-local accumulation
    covered by the granularity policy in 9.2.
  - **Counters**: subsystems publish their pass-local totals into
    `RenderStats` at pass exit (one branch and a struct copy per pass), and
    `DefaultRenderPipeline` pushes the finished frame's stats into a small
    ring beside `TimingHistory`. Counters are plain integers written by their
    owning subsystem on one thread; nothing is atomic.
  - **Gpu**: plus `vkCmdResetQueryPool` once at frame start,
    `vkCmdWriteTimestamp2` pairs at phase/feature boundaries, fence-safe
    readback (9.3), and command labels (9.4).
  - **Capture**: plus appending `{TimingFrameSample, RenderStats, gpu scopes}`
    records to the capture ring and arming the export console commands (9.5).

### 9.2 Counters and the granularity policy

`render/RenderStats.h` is one plain aggregate, reset per frame, filled by the
systems that own each number, extending the existing
`MeshForwardPass::DrawStats` pattern (`MeshForwardPass.h:72-77`): visible and
culled objects, draw calls, instanced draws, submitted triangles, lights
visible / culled / dropped at cap, per-draw considered lights (equal to
visible lights under the global loop; the counter exists to expose exactly
that), shadow-casting lights, shadow views rendered, shadow caster draws,
atlas tiles used per tier (physical), shadow cache hits/misses, shadow memory,
pipeline switches, material (push-constant) switches, probe volumes resident,
probe memory, extraction query rebuilds (finding 2.11), scratch high-water
mark, and caster-diff event counts.

The cost policy that keeps the Off path honest without duplicating renderer
code:

- Values that fall out of existing control flow at **run/chunk/pass
  granularity** (a draw-call increment per run, an index-count add per run, a
  chunk-count add per chunk, `size()` reads at pass end) accumulate into
  stack locals unconditionally and are **published only behind the per-pass
  `Stats != nullptr` check**. This mirrors what the code already does today:
  `DrawStats` is maintained unconditionally and is a consumed test seam
  (`MeshForwardPass.h:77`). Bounded by draw-call and chunk counts (hundreds),
  not entity counts.
- Anything at **per-entity, per-light, per-instance, or per-fragment
  granularity** must be derived from aggregates the pass already needed, or
  it does not exist. No profiling-only branches, writes, or arithmetic inside
  those loops in any mode.
- No extra traversals in any mode: a counter that would require re-walking a
  container is computed from data gathered during the walk that already
  happens, or dropped.
- Enforcement is empirical, not rhetorical: the 9.7 A/B methodology treats
  any measurable Off-vs-compiled-out difference as a bug, and the fix is
  demoting the offending accumulation behind the pass-level branch (a local
  change, not a renderer fork).

Duplicating passes into profiled/unprofiled versions, and templating the draw
loops on an instrumentation flag, were both rejected: they double the tested
surface for a cost the policy above already bounds to publish-time branches.

### 9.3 GPU timing

`graphics/vulkan/GpuTimestampPool`: one `VkQueryPool` (TIMESTAMP, 64 queries)
per frame in flight, **created lazily the first time a frame begins with mode
at Gpu or above** and kept until shutdown (a query pool is a trivial object;
destroy/recreate churn buys nothing). Per frame in Gpu+ mode: reset at frame
start inside the command buffer, paired writes at scope boundaries, and
results read with `vkGetQueryPoolResults` (availability-checked, without
WAIT) for the frame slot whose fence `VulkanFrameService::BeginFrame` has
already waited, scaled by `VkPhysicalDeviceLimits::timestampPeriod`. Scopes
are a fixed table registered at feature setup (no per-frame strings): one per
`RenderPhase` bucket, one per feature, plus `Shadow/SpotViews`,
`Shadow/PointFaces`, and the forward pass. Results append to
`TimingFrameSample` (new fixed-size named-scope span array) via
`TimingSampler::PushRenderFrame` (`TimingSampler.cpp:31-50`) so the existing
panel plumbing carries them; the fields are zero when mode < Gpu.

### 9.4 Debug labels and object names

`graphics/vulkan/VulkanDebugLabels`: free functions
(`BeginLabel/EndLabel/InsertLabel/NameObject`) in the style of
`VulkanBarriers` (`VulkanBarriers.h:10-13`), loaded from
`VK_EXT_debug_utils`, compiled to no-ops when profiling is compiled out. Two
distinct gates because they are two distinct mechanisms:

- **Command labels** (per-frame `vkCmdBegin/EndDebugUtilsLabelEXT`) are
  emitted only when the frame's mode is Gpu or above. Off/Counters frames
  record zero label commands.
- **Object names** (`vkSetDebugUtilsObjectNameEXT`) are written once at
  resource creation when the build has profiling compiled in and
  `VulkanBootstrapPolicy::EnableValidation` is set (the existing dev
  default). They are metadata on objects, not commands in the frame, and
  therefore cannot appear in the 9.7 command-stream check; they cost nothing
  per frame.

### 9.5 Capture export

`render/RenderCapture`: a bounded ring of `{TimingFrameSample, RenderStats,
gpu scopes}` records plus one-shot metadata (build info, cvar snapshot, scene
name). Console commands `render.capture.start [n]` / `render.capture.stop` /
`render.capture.write <path>` (pattern: `ConsoleService.cpp:227-237`)
serialize to JSON (schema-versioned envelope) or CSV using `JsonStringify`
(`JsonStringify.h:7`). Records are plain structs buffered during the run and
serialized only inside the explicit `write` command, per the documented
`JsonValue` hot-path caveat (`JsonValue.h:19-22`). Ring memory is bounded
(default 4096 frames, ~16 MiB) and allocated when Capture mode first
activates, not at startup. This file format is the interface for AI-assisted
analysis: stable keys, explicit units (`_ms`, `_bytes`, `_count`),
machine-diffable. No heuristics live in the renderer.

### 9.6 Debug views (development-only pipelines)

The production StandardLit shader carries no debug functionality (3.5). Debug
views are a separate development-only fragment shader,
`mesh_debug_view.frag.glsl`, built from the same `.glsli` includes so views
cannot drift from the real lighting math. It switches on a view id in the
frame UBO and implements: world normals, tangent-space normal-map sample,
geometric vs mapped normal delta, diffuse only, specular only, emission only,
roughness/exponent, light complexity (it runs the light loop itself and
counts), shadow term only, raw shadow compare without the filter (for tuning
bias independently of softness), probe ambient only, baked-vs-dynamic split,
probe volume selection id, and the Section 7A.10 AO set: raw vertex AO, probe
visibility (probe irradiance luminance), SH-derived ambient direction (labeled
as the bent-normal proxy), combined ambient visibility (`ambientC * aoFactor`),
and the AO term isolated (the doctrine-proof view: it must never appear on a
purely direct-lit surface).

Selection is at pass level: when `render.debug.view != 0` (or the editor's
per-viewport override is set), `MeshForwardPass` substitutes the debug
pipeline for both production families for that draw call sequence. The debug
pipelines (back/none cull) are created lazily on first activation through the
existing `EnsurePipeline` pattern (`MeshForwardPass.cpp:55-99`), and the
debug SPIR-V, the selection code, and the pipelines are all compiled out of
shipping builds (9.8). Overdraw remains a separate additive-blend counting
pipeline under the same gates. Atlas contents, shadow-caster bounds, and
probe placement/validity/weights and AO sample density / adaptive-tessellation
(added-vertex heat or wireframe over base brush edges) are overlay- or
blit-level views (editor line batch and a blit quad), not shader branches. The
runtime exposes view selection through `RenderStatsPanel`; the editor through
`WorldViewSettings` (Section 10).

### 9.7 Phase 3.0 validation (disabled-path proof)

- **Command-stream inspection.** With `render.profile.mode off`, a RenderDoc
  capture of a representative frame contains zero `vkCmdWriteTimestamp*`,
  zero `vkCmdResetQueryPool`, zero `vkGetQueryPoolResults` on the timeline,
  and zero `vkCmdBegin/End/InsertDebugUtilsLabelEXT`. Repeated for a frame
  after switching Gpu -> Off to prove transitions clean up.
- **No profiling-only resources before activation.** Query pools and the
  capture ring do not exist until their modes first activate (asserted via
  logs/counters in a dev run and by inspection in the capture).
- **No history/capture writes.** With mode Off, the stats ring and capture
  ring version counters do not advance over a 1000-frame run.
- **Statistical A/B, noise-aware.** Two builds: profiling compiled out vs
  compiled in with mode Off. Identical scene, fixed deterministic camera
  path, fixed tick timing; at least 10 runs per build of a 2000-frame
  flythrough; compare per-run median frame times. Acceptance: the compiled-in
  Off build's median distribution is statistically indistinguishable from the
  compiled-out build (overlapping interquartile ranges and a
  difference-of-medians below typical run-to-run noise measured across the 10
  compiled-out runs). A single capture proves nothing and is not accepted as
  evidence; the methodology and raw numbers land in the PR.

### 9.8 Compile-time removal

New CMake option `SENCHA_ENABLE_RENDER_PROFILING` (default ON; OFF in the
shipping preset), following the `SENCHA_ENABLE_*` pattern
(`cmake/SenchaOptions.cmake:13-33`). When OFF: `GpuTimestampPool`,
`RenderCapture`, `VulkanDebugLabels` bodies, the capture console commands,
the debug-view and overdraw shaders/pipelines/selection code, and
`RenderStatsPanel` are not compiled; the embedded debug SPIR-V headers are
not generated; `RenderInstrumentation` remains as an always-null struct so
call sites need no `#ifdef`s beyond the publish points. `RenderStats` and the
pass-local `DrawStats`-style accumulation stay compiled in all builds (test
seam, 9.2). Panels additionally require the existing `SENCHA_ENABLE_DEBUG_UI`
gate.

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
  `EditorLinePipeline`. `EditorVisual` stays mesh-only; parametric gizmos
  read typed component fields directly (the generic seam cannot express
  field-driven shapes).
- **Cone editing.** A new `IManipulator` registered at the single manipulator
  site (`ManipulatorSession.cpp:32-35`): drag the cone rim for OuterAngle,
  inner ring for InnerAngle, tip-to-cap axis for Range, writing through
  `RawComponentEditCommand` so undo works like any inspector drag.
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
  (per-viewport override that selects the development debug pipeline,
  Section 9.6); toolbar buttons follow the `ShowZoneBounds` pattern
  (`EditorToolbar.cpp:252-255`).
- **Lighting panel.** New `IEditorPanel` registered with the other panels
  (`EditorServices.cpp:468-551`):
  - Shadow budget readout: requested vs granted shadowed lights per type,
    atlas occupancy by physical tier (with logical sizes shown as
    "512 (496 usable)"), and a persistent warning row when requests exceed
    budget, naming the denied lights (click to select).
  - Selected-light cost line: tier, memory, views per update ("Point, 512:
    6 faces per update" vs "Spot, 512: 1 view per update"), so point shadows
    read as visibly more expensive than spots at authoring time.
  - Probe section: per-volume probe counts and memory, invalid/dilated probe
    count (click to frame them; dilated probes tint in the cell overlay),
    cell-size guidance against wall thickness (7.3), bake staleness (input
    hash vs `.sprobe` stored hash), Bake Zone / Bake World buttons, progress
    bar, cancel.
  - AO section (Phase 3B.3): added-vertex count and the per-zone vertex growth
    ratio against `render.bake.ao.max_vertex_growth` (with a warning row if a
    zone hit the cap and refinement was clamped), AO memory delta, and the
    same Bake Zone / Bake World controls (AO and probes bake together, one
    progress bar). The AO density debug view (9.6) is the visual counterpart.
- **Bake execution.** Kyusu-side `LightingBake` orchestrator: snapshot the
  zone's static lights + cooked geometry list + halo zone list, submit per-zone
  bake work through `engine.Tasks()` (`AsyncTaskQueue`) that builds the BVH
  once and emits both probe SH and the v4 `.smesh` AO channel, report progress
  via an atomic counter polled in `EditorServices::ProcessFrame`
  (`EditorServices.cpp:651-701`), write `.sprobe` + the re-cooked `.smesh` +
  update the zone header on commit, and mark the world document dirty. Bake math
  lives engine-side (`ProbeBakeMath` for SH, `AoBakeMath` for the weld / halo /
  tessellation / hemisphere AO, both over the shared `BakeBvh`) so it is
  testable without the editor; the editor owns orchestration, IO, and UI. The
  cooked-manifest path (`WorldCook.cpp:69-85`) invokes the same bake when the
  probe or AO content hash is stale, so PIE and cooked runs stay fresh.
- **Shudei.** Hand-written rows for the v2 material fields in
  `MaterialInspectorPanel::OnDraw` beside the Surface section
  (`MaterialInspectorPanel.cpp:196-203`): specular intensity slider, emissive
  strength, shading combo, double-sided / receive / cast checkboxes. Undo is
  free via the existing whole-value `EditMaterialCommand`. The preview
  viewport picks up StandardLit automatically because it renders through
  `MeshForwardPass` (`editor/shudei/src/MaterialPreviewRenderFeature.h`).
- Editor-process instrumentation (per-viewport stats) is out of scope this
  phase; the editor benefits from debug views and the lighting panel, and the
  runtime owns the capture pipeline.

---

## 11. Ordered implementation phases

Four phases, each independently mergeable and independently useful. Stages
within a phase land as separate green PRs. Estimated sizes are relative
(S/M/L).

### Phase 3.0: Renderer instrumentation (preliminary, standalone) (M)

Deliberately not part of Phase 3A: it depends on nothing in Phase 3, other
work benefits from it immediately, and landing it first means every
subsequent stage ships with numbers attached.

- **Goal.** The Section 9 instrumentation ladder exists end to end with a
  proven disabled path; documented conventions match the code.
- **Dependencies.** None.
- **Systems affected.** `Renderer` (scope hooks around phase buckets),
  `RendererServices` (+`RenderInstrumentation*`), `TimingSampler`,
  `TimingHistory` (gpu scope fields), `DefaultRenderPipeline` (stats
  ownership, mode latch), `EngineConsoleBuiltins` (cvar moves + new cvars),
  `MeshForwardPass` (publish point), kyusu `EditorServices` (drop duplicate
  ambient registration), `cmake/SenchaOptions.cmake` (+
  `SENCHA_ENABLE_RENDER_PROFILING`).
- **New data structures.** `RenderProfileMode`, `RenderInstrumentation`,
  `RenderStats`, capture record + ring, GPU scope table.
- **GPU resources.** Timestamp query pools (lazily created on first Gpu
  activation, per frame in flight).
- **Shader changes.** None.
- **ECS changes.** None.
- **Editor changes.** None beyond cvar registration cleanup.
- **New files.** `graphics/vulkan/GpuTimestampPool.{h,cpp}`,
  `graphics/vulkan/VulkanDebugLabels.{h,cpp}`, `render/RenderStats.h`,
  `render/RenderInstrumentation.h`, `render/RenderCapture.{h,cpp}`,
  `debug/RenderStatsPanel.{h,cpp}`.
- **Also.** Fix the reversed-Z comments (`Camera.cpp:18,34`, `Camera.h:19`);
  register `render.ambient.*` in the engine; wire `render.profile.mode` and
  `render.capture.*`; name existing renderer-owned images/pipelines (9.4).
- **Validation.** The full Section 9.7 protocol: RenderDoc command-stream
  inspection in Off (zero timestamp/reset/readback/label commands), no
  profiling-only resources before activation, no ring writes in Off, and the
  10-run statistical A/B against a compiled-out build with the acceptance
  criterion stated there. Plus: capture 300 frames in Capture mode; JSON/CSV
  parse; GPU totals roughly match CPU-side `RecordSeconds`.
- **Completion criteria.** `RenderStatsPanel` shows live counters and GPU
  scopes in CubeDemo at mode Gpu; a committed capture file demonstrates the
  schema; the A/B evidence is attached to the PR; suite green.

### Phase 3A: The dynamic-lighting renderer

Complete and shippable on its own: StandardLit, spot lights, spot and point
shadows, debug views. 3A completion = "the dynamic renderer" with no baked
lighting anywhere in the build.

#### 3A.1: StandardLit shading, normal mapping, emission, Unlit (L)

- **Goal.** The full Section 3 material model renders; visual identity knobs
  exist; shading-channel debug views work through the development pipeline.
- **Dependencies.** Phase 3.0 (publish points, debug-view plumbing).
- **Systems affected.** `MeshForwardPass` (pipeline variants, push constants,
  UBO growth for style params + debug view id, debug-pipeline substitution),
  `RenderQueue` (sort-key pipeline bits), material
  loader/writer/asset-loader, shudei panel.
- **New data structures.** `MaterialShading` enum; extended `Material` /
  `MaterialDescription` / `MeshPushConstants`.
- **GPU resources.** None new (pipelines only; debug pipelines lazy,
  dev-only).
- **Shader changes.** Split shared `.glsli` includes; vertex shader gains
  tangent attribute (location 7), TBN, cofactor normal matrix; StandardLit
  fragment implements wrap diffuse, normalized Blinn-Phong, emission,
  exposure + knee-shoulder tonemap; new `mesh_unlit.frag.glsl`; new
  development-only `mesh_debug_view.frag.glsl` (Section 9.6); CMake blocks
  per new shader (`engine/CMakeLists.txt:55-80` pattern), debug shader gated
  by the profiling option.
- **ECS changes.** None.
- **Editor changes.** Shudei v2 field rows; kyusu picks everything up through
  `MeshForwardPass` automatically.
- **Validation.** A test scene with normal-mapped, emissive, rough/smooth,
  double-sided, and unlit materials; debug views inspected; `.smat` v1 files
  load unchanged (loader tests); a non-uniformly scaled mesh lights
  correctly (before/after screenshots); tonemap identity-below-knee verified
  against the CPU reference function; with `render.debug.view = 0` a capture
  shows the production pipelines bound, not the debug pipeline.
- **Completion criteria.** All six v2 fields round-trip through shudei;
  StandardLit and Unlit draw in one frame with 4 or fewer production pipeline
  switches reported by stats; suite green including new MaterialLoader v2 and
  tonemap-curve tests.

#### 3A.2: Spot lights and light culling (M)

- **Goal.** `SpotLightComponent` end to end, plus deterministic light
  culling/prioritization for all lights.
- **Dependencies.** 3A.1 (shader include structure).
- **Systems affected.** `RenderLight.h` (GpuLight grows to 80 bytes: new
  `Vec4 Params` row carrying cosInner + inverse cone delta; static asserts;
  `MAX_LIGHTS` unchanged), `LightExtractionSystem` (spot query, frustum
  sphere cull, importance sort, stable `(RegistryId, EntityId)` identities,
  cone clamping: outer <= 89.5 degrees, inner <= outer - 0.5, range > 0.01,
  one-shot warnings), `MeshForwardPass` (UBO asserts), forward shader (spot
  case in the existing type switch: cone falloff =
  `smoothstep(cosOuter, cosInner, dot(L, spotDir))` times the shared
  attenuation), `SceneRenderQueueBuilder::BuildLights`,
  `DefaultRenderPipeline` (cap warning covers both types).
- **New data structures.** `SpotLightComponent` (+ `TypeSchema`, chunk
  `'SLGT'`), `RenderLightSet::AddSpot`, CPU-side light identity records.
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

#### 3A.3: Shadow substrate + spot shadows, always-update (L)

- **Goal.** First shadows on screen: spot lights shadow correctly with guard
  bands and the full bias stack, updated every frame, fixed slot assignment
  (budget but no caching/policies yet).
- **Dependencies.** 3A.2.
- **Systems affected.** `VulkanImageService` (array layers, cube-compatible,
  3D, per-layer views, depth usage; `ImageCreateInfo` widened),
  `VulkanSamplerCache` (compare op + border color), `VulkanDescriptorCache`
  (`GetPipelineLayout` extra set layouts), `Renderer`
  (`RenderPhase::Shadow` bucket ordered first), `StaticMeshComponent`
  (+`CastShadows = true` schema field), new caster extraction in
  `DefaultRenderPipeline::ExtractRender`, `MeshForwardPass` (bind set 2,
  shadow sampling in StandardLit).
- **New data structures.** `ShadowCasterSet` + extraction system (current
  table only in this stage), `ShadowView`, `GpuShadowSlot` array in
  `MeshFrameUniforms`, `LightBindings` with dummy resources (6.6),
  `kShadowTileGuardTexels` / `kShadowSoftnessMaxTexels` constant pair (4.2).
- **GPU resources.** 2048x2048 D16 atlas (fixed 512 physical grid in this
  stage, insets active from day one), comparison sampler, set-2 descriptor
  sets (x frames in flight), dummy depth/cube/volume resources.
- **Shader changes.** `shadow_depth.vert.glsl` + empty fragment;
  `shadow_sampling.glsli` (normal offset, projection, validity rejection,
  3x3 tent over logical UVs); StandardLit multiplies the shadow term into
  diffuse and specular; shadow debug views land in the development shader.
- **ECS changes.** `CastShadows` fields on `StaticMeshComponent` and
  `SpotLightComponent`.
- **Editor changes.** Focus-viewport shadow preview wiring in
  `EditorRenderFeature`.
- **Validation.** Bias tuning scene (grazing walls, thin door frames,
  double-sided sheets) at 256/512/1024 with acne and peter-panning checked
  via the raw-compare debug view; a two-adjacent-tiles scene with maximum
  softness on both lights showing no cross-tile contamination (the guard-band
  proof); barrier correctness under the validation layer; GPU scope shows
  shadow pass cost.
- **Completion criteria.** Up to 8 spot lights cast filtered shadows in game
  and editor; caster gather excludes `CastShadows = false` at both levels;
  the guard-reach unit test enforces filter reach < guard band; suite green
  with atlas inset math and caster-extraction tests.

#### 3A.4: Shadow residency: budgets, tiers, policies, diff invalidation (M)

- **Goal.** Shadow cost becomes a managed budget with a correct cache:
  quadtree tiers, scoring, hysteresis, update policies, previous/current
  caster diffing, deterministic view scheduling.
- **Dependencies.** 3A.3.
- **Systems affected.** New `ShadowAtlas` quadtree allocator replaces the
  fixed grid; new `ShadowResidency` arbiter; `ShadowCasterExtractionSystem`
  gains the previous-frame table and the sorted diff (6.4);
  `ShadowRenderFeature` renders only scheduled views under the per-frame
  clamp in the 6.3 service order.
- **New data structures.** Slot cache records (state hash, valid flag, ages,
  score history), the caster table pair, `ShadowResolutionTier`,
  `ShadowUpdatePolicy` (schema enums on both light components).
- **GPU resources.** Unchanged.
- **Shader changes.** None.
- **ECS changes.** Tier/policy/softness/bias fields on both light components.
- **Editor changes.** Lighting panel budget readout + over-budget warnings;
  `render.shadow.invalidate` console command; atlas debug view shows tiers,
  insets, and slot ages.
- **Validation.** Walkthrough across three zones: steady-state shadow views
  rendered = 0 (counters); moving a caster through an `OnChange` volume
  invalidates exactly the overlapped lights **including at the departure
  position** (ghost-shadow regression test: caster leaves a light's cone,
  the vacated shadow disappears); deleting an entity and detaching a zone
  both invalidate; a 20-light over-budget scene shows stable slot assignment
  (no flicker over 1000 captured frames); EveryFrame + invalidated mixed load
  drains in the documented order.
- **Completion criteria.** `ShadowResidency` and caster-diff unit tests
  (deterministic assignment, hysteresis, tier downgrade, eviction, the full
  add/remove/change event matrix) green; cache hit rate visible in stats;
  budgets tunable by cvar at runtime.

#### 3A.5: Point-light shadows (M)

- **Goal.** Optional cube shadows on point lights under the same budget
  machinery.
- **Dependencies.** 3A.4 (residency), 3A.3 (image service cube support).
- **Systems affected.** `ShadowCubePool`, `ShadowResidency` (point slots,
  6-face views, per-face caster cull), `ShadowRenderFeature`,
  `MeshForwardPass`/StandardLit (cube sampling), a `Contribute()` override
  enabling `imageCubeArray`.
- **New data structures.** `GpuPointShadow` params array.
- **GPU resources.** 512 D16 cube array x 4 (24 layers), cube-array
  comparison sampler.
- **Shader changes.** Major-axis depth reconstruction + 5-tap directional
  filter in `shadow_sampling.glsli`; point branch consumes `ShadowIndex`.
- **ECS changes.** Shadow fields already on `PointLightComponent` from 3A.4
  (points simply start being granted slots).
- **Editor changes.** Cost line in the lighting panel ("6 faces per update");
  point shadows in viewport preview.
- **Validation.** Light-in-a-cage scene (all 6 faces occluded differently);
  face-edge continuity check; wall-adjacent light shows per-face cull savings
  in caster-draw counters; bias verified radially (sphere around light).
- **Completion criteria.** 4 shadowed point + 8 shadowed spot lights render
  in budget (Section 14) on the reference GPU; exceeding the point budget
  degrades per policy with the editor warning naming the losers. **This
  completes Phase 3A: a shippable dynamic-lighting renderer.**

### Phase 3B: Baked irradiance and baked ambient occlusion

Depends on 3.0, 3A.1 (shader structure), and 3A.2 (spot lights exist as bake
inputs). Independent of 3A.3-3A.5; it can merge before, after, or in parallel
with the shadow stages, and shipping without it is a supported configuration
(hemispheric ambient remains). Within 3B, the shared bake core (3B.1) lands
first; probe streaming (3B.2) and vertex AO (3B.3) are independently mergeable
consumers of it, and 3B.3 works against the hemi-ambient fallback even if 3B.2
never ships.

Revision 4 status: the baked-direct payload (Section 7B) shipped a slice of
this substrate early: the median-split triangle BVH with segment occlusion
(`assets/cook/BakeBvh`), light-proximity tessellation
(`assets/cook/DirectLightTessellate`), the `.smesh` vertex-format bump with
its attribute plumbing, and the `DocumentCook`/`WorldCook` bake seam with
staleness hashing. 3B.1's remaining scope is the grid math, the hemisphere ray
table, SH projection, dilation, `.sprobe` IO, and the neighbor halo; new bake
modules belong beside the shipped ones under `assets/cook/` (cook-gated), not
under `render/probes/` as sketched below. 3B.3 carries two amendments: its
storage numbers moved (7A.6: offset 52, location 9, `.smesh` v5) and its
tessellation scheme must be judged on screen against a distance-graded uniform
variant before committing (7A.5).

#### 3B.1: Shared bake core, grid math, probe format (M)

- **Goal.** Everything CPU-testable lands first: grid mapping, the shared
  triangle BVH + neighbor halo + hemisphere ray kernel + determinism, SH
  projection, dilation, `.sprobe` IO. No editor UI, no GPU residency, no
  shader changes yet.
- **Dependencies.** 3.0 (none functionally; counters for bake timing), 3A.2
  (light records as bake input shapes).
- **Systems affected.** `math/spatial` (new `GridTransform3d`), new
  `render/probes/BakeBvh` (median-split triangle BVH, halo assembly, cosine
  hemisphere sampler, fixed ray table, shared by SH and AO), new
  `render/probes/ProbeBakeMath` (classification, dilation, SH projection over
  `BakeBvh`), new `assets/probes/ProbeVolumeFormat`.
- **New data structures.** `GridTransform3d`, `BakeBvh`, `IrradianceProbeGrid`,
  `IrradianceVolumeComponent` (chunk `'IRVL'`, registered in the manifest),
  `.sprobe` chunks.
- **GPU resources.** None.
- **Shader changes.** None.
- **ECS changes.** New component in `ComponentManifest.h:33-39` (UI arrives
  free; it renders nothing yet).
- **Editor changes.** None yet (component edits already work via schema).
- **Validation.** Unit tests: grid mapping and trilinear weights; BVH trace on
  hand-built geometry; halo assembly determinism (a boundary sample computes
  identically given either zone's halo); SH projection of analytic inputs;
  dilation fill order and volume-boundary containment; serial vs parallel bake
  bit-identical; `.sprobe` round trip and unknown-chunk skip.
- **Completion criteria.** A headless test bakes a synthetic two-room volume
  and asserts the dark room's probes stay dark; a boundary-halo test asserts
  two adjacent zones compute an identical shared-boundary sample; suite green.

#### 3B.2: Editor bake, streaming, runtime sampling (L)

- **Goal.** Stable environmental light, color, and mood from a zone-streamed
  bake; hemispheric ambient becomes the fallback only.
- **Dependencies.** 3B.1; 3A.3's `LightBindings` if it has landed (otherwise
  this stage creates the set with dummy shadow bindings; whichever lands
  second fills its binding).
- **Systems affected.** `VulkanImageService` 3D textures (from 3A.3's
  widening or done here if 3B lands first), `LightBindings` binding 2,
  `MeshForwardPass` (volume headers in UBO), `WorldPartitionManifest`
  (+`CookedProbeRef`/hash), zone load recipes in the template game
  (`template/src/TemplateGame.cpp:684-717`), `WorldCook`/`DocumentCook`
  (bake-if-stale), kyusu `LightingBake` + lighting panel probe section,
  `ProbeVolumeSet` residency.
- **New data structures.** `ProbeVolumeSet` (resident volumes per zone),
  `GpuProbeVolume` headers in the frame UBO.
- **GPU resources.** Per resident volume: three RGBA16F 3D textures (dilated
  SH, upload-ready from the file); cap 8 resident volumes.
- **Shader changes.** `probe_sampling.glsli`: volume selection
  (priority/smallest/stable-id), trilinear L1 SH evaluation, hemi fallback;
  probe debug views in the development shader.
- **ECS changes.** None beyond 3B.1.
- **Editor changes.** Volume gizmo + cell overlay, dilated-probe tinting,
  lighting panel bake section with progress/cancel, staleness hash, cell-size
  guidance.
- **Validation.** Leak scene (two rooms, one lit, volumes per room: dark room
  stays dark at runtime); streaming test (zone unload frees volume textures,
  counters to zero); dynamic object driven between rooms picks up each room's
  tint; probe sampling GPU cost measured against budget with 3.0 tooling.
- **Completion criteria.** Template-game world bakes from the lighting panel
  with progress, streams per zone, renders probe ambient within budget
  (Section 14), and survives editor geometry edit -> staleness warning ->
  rebake round trip. Also fold the read-only neighbor halo (Section 7.2) into
  the probe bake here, with a two-zone boundary scene proving no lighting seam.

#### 3B.3: Cooked vertex ambient occlusion (L)

- **Goal.** Sub-probe-cell contact darkening from the same bake: welded,
  seamless across zones, densified only near occluders, packed into the vertex,
  modulating the ambient term only.
- **Dependencies.** 3B.1 (`BakeBvh`, halo, ray kernel). Independent of 3B.2:
  vertex AO multiplies whatever ambient exists (probe or hemi fallback).
- **Systems affected.** New `render/probes/AoBakeMath` (weld topology,
  occluder-gated adaptive tessellation, hemisphere AO, scatter-to-vertex),
  `StaticMeshVertex` + `.smesh` format (v3 -> v4, packed AO attribute),
  `MeshLoader`/`MeshSerializer` (read/write/default the AO channel),
  `MeshForwardPass` (vertex attribute location 8, AO into StandardLit ambient),
  `RenderExtractionSystem`/instancing untouched, `DocumentCook`/`WorldCook`
  (AO bake integrated into the per-zone `LightingBake`), kyusu lighting panel
  AO section + density debug view.
- **New data structures.** AO sample-topology (transient bake weld set), the
  4-byte packed vertex attribute, `render.bake.ao.*` bake cvars, `render.ao.*`
  runtime cvars.
- **GPU resources.** None (rides the existing `.smesh` vertex buffer).
- **Shader changes.** Vertex shader passes the AO varying; StandardLit and
  Unlit apply `aoFactor` to the ambient term (Section 3.2); the AO debug views
  land in the development shader (9.6).
- **ECS changes.** None (AO is mesh data, not a component).
- **Editor changes.** AO section in the lighting panel (added-vertex counts,
  growth-cap warning), AO density / tessellation debug view.
- **Validation.** The Section 13 AO test set (weld, hard-edge split, cross-zone
  determinism, occluder-gated refinement, open-floor near-zero growth,
  thin-wall/window policy, instance neutrality, composition without crushed
  blacks, AO-disabled equals Phase 3A ambient); the loader rejects prior
  `.smesh` versions (single-version doctrine, 7A.6 amendment).
- **Completion criteria.** A coplanar floor split across two draw calls and
  across a zone boundary shows no AO seam; a massive floor beside one wall
  refines only near the wall; AO composes with probes without crushed corners;
  toggling `render.ao.enabled 0` yields byte-identical direct lighting and
  shadows; suite green.

### Phase 3C: Evidence-based forward-renderer review and tuning (M)

- **Goal.** The Section 14 budgets are confirmed or the named escalations are
  triggered with numbers; cheap convicted fixes land.
- **Dependencies.** 3.0 and 3A; includes 3B content if landed (the review is
  rerun cheaply when 3B lands later).
- **Systems affected.** Measurement first; then only what captures convict
  (candidate list: per-registry query caches, finding 2.11; caster-diff
  prefilter, 0.5; scratch sizing).
- **New data structures.** None (benchmark scenes as content).
- **GPU resources.** None.
- **Shader changes.** None unless convicted by captures.
- **ECS changes.** None.
- **Editor changes.** None.
- **Validation / method.** Three committed benchmark captures (small room;
  template-game hub with 12 lights / 6 shadowed; worst-case stress: 64
  lights, 8+4 shadows, probes resident if 3B landed) x 720p/1080p/1440p on
  the reference GPU, exported JSON attached to the PR; a findings section
  appended to this document.
- **Explicit thresholds** (also Section 14): the light loop earns per-object
  light lists only when MainColor GPU time exceeds 8 ms at 1080p on the
  reference GPU with the light-complexity view showing > 16 average lights
  per fragment in representative (not stress) content; tiled/clustered
  culling is considered only after per-object lists exist and visible lights
  regularly exceed 64 or per-object lists average > 8; a renderer redesign
  (deferred, visibility buffer) has no trigger inside this game's scope and
  is explicitly out of plan.
- **Completion criteria.** Findings appended here with capture references;
  either "current architecture comfortable at target workloads" is stated
  with numbers, or the specific next optimization is scheduled with its
  trigger metric quoted.

---

## 12. Files and systems likely to be added or modified

New engine files:

| Path | Content |
|---|---|
| `engine/{include,src}/render/shadow/ShadowAtlas.{h,cpp}` | Quadtree physical-tile allocator, guard-band inset math, atlas image ownership |
| `engine/{include,src}/render/shadow/ShadowCubePool.{h,cpp}` | Cube-array slots |
| `engine/{include,src}/render/shadow/ShadowResidency.{h,cpp}` | Budget/score/hysteresis/cache arbiter, view scheduling |
| `engine/{include,src}/render/shadow/ShadowCasterSet.{h,cpp}` | Caster records, previous/current tables, sorted diff |
| `engine/include/render/shadow/ShadowView.h` | Per-view render job record |
| `engine/{include,src}/render/shadow/ShadowDepthPass.{h,cpp}` | Depth-only draw recording (editor-reusable) |
| `engine/{include,src}/render/shadow/ShadowRenderFeature.{h,cpp}` | `RenderPhase::Shadow` feature, barriers |
| `engine/{include,src}/render/LightBindings.{h,cpp}` | Set-2 layout/sets for atlas + cubes + probes, dummy resources |
| `engine/include/render/SpotLightComponent.h` | Component + schema (`'SLGT'`) |
| `engine/{include,src}/render/probes/IrradianceProbeGrid.{h,cpp}` | Grid3d + GridTransform3d composition |
| `engine/include/render/probes/IrradianceVolumeComponent.h` | Component + schema (`'IRVL'`) |
| `engine/{include,src}/render/probes/ProbeVolumeSet.{h,cpp}` | Resident volumes, GPU upload |
| `engine/{include,src}/assets/cook/BakeBvh.{h,cpp}` | Shared triangle BVH with segment occlusion (shipped, revision 4); halo assembly, cosine hemisphere ray kernel, fixed ray table still to add |
| `engine/{include,src}/assets/cook/ProbeBakeMath.{h,cpp}` | Classification, dilation, SH projection over `BakeBvh` (placement per the revision-4 amendment: bake modules are cook-gated, not under `render/`) |
| `engine/{include,src}/assets/cook/AoBakeMath.{h,cpp}` | Vertex-AO weld topology, tessellation (scheme per the 7A.5 amendment), hemisphere AO, scatter-to-vertex |
| `engine/{include,src}/assets/probes/ProbeVolumeFormat.{h,cpp}` | `.sprobe` chunked binary IO |
| `engine/include/math/spatial/GridTransform3d.h` | World-cell mapping value type |
| `engine/{include,src}/graphics/vulkan/GpuTimestampPool.{h,cpp}` | Lazily created timestamp queries |
| `engine/{include,src}/graphics/vulkan/VulkanDebugLabels.{h,cpp}` | Mode-gated labels, creation-time object names |
| `engine/include/render/RenderStats.h`, `engine/include/render/RenderInstrumentation.h`, `engine/{include,src}/render/RenderCapture.{h,cpp}` | Counters, mode bundle, capture export |
| `engine/{include,src}/debug/RenderStatsPanel.{h,cpp}` | Runtime panel (debug-UI + profiling gates) |
| `engine/shaders/`: `mesh_unlit.frag.glsl`, `mesh_debug_view.frag.glsl` (dev-only), `shadow_depth.vert.glsl`, `shadow_depth.frag.glsl`, `frame_uniforms.glsli`, `lighting.glsli`, `shadow_sampling.glsli`, `probe_sampling.glsli` | Shader families + shared includes |

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
`app/Engine.{h,cpp}` (instrumentation ownership),
`world/ComponentManifest.h`, `zone/WorldPartitionManifest.{h,cpp}`,
`engine/CMakeLists.txt`, `cmake/SenchaOptions.cmake`,
`engine/shaders/mesh_forward.{vert,frag}.glsl`,
`render/Camera.h` and `render/Camera.cpp` (comments only),
`template/src/TemplateGame.cpp` (probe recipe),
`render/static_mesh/StaticMeshVertex.h` (one packed 4-byte attribute per bake
payload: baked direct shipped at offset 48, AO takes offset 52),
`assets/static_mesh/{StaticMeshFormat.h,MeshSerializer.{h,cpp},MeshLoader.cpp}`
(`.smesh` v4 baked-direct channel shipped; v5 adds AO; the loader rejects
prior versions).

Editor files: kyusu `render/LightVisualRenderer.{h,cpp}` (new),
`render/EditorRenderFeature.{h,cpp}`, `render/SceneRenderQueueBuilder.cpp`,
`viewport/{WorldViewSettings.h,Picking.cpp}`, `ui/{EditorToolbar.cpp,
LightingPanel.{h,cpp} (new)}`, `editmodes/` cone manipulator (new) +
`ManipulatorSession.cpp`, `document/LightingBake.{h,cpp}` (new),
`document/{DocumentCook.cpp,WorldCook.cpp}`, `app/EditorServices.cpp`;
shudei `MaterialInspectorPanel.cpp`.

Docs: `docs/plans/engine-roadmap.md` Track B items 2 and 8 were updated at
revision 4; item 5 updates when the tonemap moves to the Post phase. Append 3C
findings here.

---

## 13. Testing strategy

All new logic that can run without a device is CPU-tested (the suite has no
GPU harness, Section 1.9, and stays that way this phase). New test files drop
into existing globbed directories (`test/CMakeLists.txt:32-34`).

- `test/engine_features/ShadowAtlasTests.cpp`: quadtree allocate/free across
  physical tiers, fragmentation, determinism of allocation order, full-atlas
  denial, and the inset math (logical rect and `AtlasScaleBias` for each
  tier; guard band respected on all four sides including atlas-edge tiles).
- `test/engine_features/ShadowFilterReachTests.cpp`: the constant-pair
  invariant `ceil(1.5 * kShadowSoftnessMaxTexels) + 1 <
  kShadowTileGuardTexels`, evaluated from the real constants, so nobody can
  widen the filter or shrink the band independently.
- `test/engine_features/ShadowResidencyTests.cpp`: scoring, hysteresis (no
  flicker on near-tie scores), tier downgrade under pressure, policy matrix
  (EveryFrame/OnChange/Static x light-moved/caster-event/none), view
  scheduling order and starvation freedom under a saturated EveryFrame load,
  per-frame clamp deferral, deterministic slot assignment across registry
  orderings.
- `test/engine_features/ShadowCasterDiffTests.cpp`: the full event matrix of
  Section 6.4: add, remove (entity destroyed, visibility off, component
  `CastShadows` off, material `CastShadows` off, zone table dropped), change
  (moved: event bounds equal the union of previous and current; mesh handle
  swapped; section mask edited); the ghost-shadow regression (a caster
  leaving a volume produces an event overlapping the departure position);
  diff skipped when no OnChange slot is resident; determinism of event order.
- `test/runtime/LightExtractionTests.cpp` (extended): spot packing
  (`DirectionCone`, cone params), cone/range clamping, frustum cull, stable
  importance sort with identity tie-break, cap behavior dropping
  lowest-scored not latest-added.
- `test/engine_features/RenderQueueTests.cpp` (extended): sort-key pipeline
  bits keep runs pipeline-pure; material field truncation still merges only
  identical items (existing guarantee, `RenderQueue.h:33-35`).
- `test/core/MaterialAssetTests.cpp` (extended): `.smat` v1 defaults, v2
  round-trip of all six new fields, unknown-key rejection unchanged.
- `test/math_geometry/GridTransform3dTests.cpp`: world/cell mapping,
  trilinear weights sum to 1, boundary clamping.
- `test/engine_features/ProbeBakeMathTests.cpp`: SH projection of analytic
  inputs (constant sky = band 0 only; single direction = expected band 1);
  BVH trace correctness on hand-built geometry; inside-geometry
  classification; dilation fills every texel, never crosses volume bounds,
  and converges on a fully-invalid volume to the hemispheric fallback; serial
  (`worker_count == 0`) vs parallel bake bit-identical (pattern from
  `test/runtime/ZoneParallelTests.cpp:169-202`); fixed ray-table stability.
- `test/core/ProbeVolumeFormatTests.cpp`: `.sprobe` write/read round trip,
  unknown-chunk skip, version rejection, validity chunk ignored by the
  runtime reader.
- `test/engine_features/AoBakeTests.cpp` (the AO correctness set, all CPU):
  the weld groups coincident + normal-compatible render vertices into one
  sample and splits a genuine hard wall/floor corner into two (face-appropriate
  independent AO); two coplanar sections split into separate meshes carry
  byte-identical welded AO (no seam); a continuous surface split across a
  simulated zone boundary computes byte-identical boundary AO given either
  zone's halo (cross-zone determinism); occluder-gated adaptive tessellation
  refines a sparse floor near one wall and adds near-zero vertices to an open
  floor far from occluders, respecting max depth / min edge / growth cap;
  deterministic edge-midpoint insertion keeps shared edges watertight across
  meshes; thin-wall / carved-window rays do not pull occlusion from the
  opposite side (finite radius, own-hemisphere); a reusable mesh instanced in
  several placements keeps one shared vertex buffer with neutral AO (instancing
  preserved); the composition `mix` never crushes below `ao_min`; and
  `render.ao.enabled 0` reproduces the Phase 3A ambient exactly.
- `test/core/StaticMeshFormatTests.cpp` (extended): `.smesh` v3 loads with AO
  defaulted to 1.0; v4 round-trips the packed AO channel; the 52-byte vertex
  stride and attribute offset are asserted.
- `test/engine_features/TonemapCurveTests.cpp`: the CPU reference of
  `kneeShoulder` (mirrored by the shader): exact identity at and below the
  knee, continuity and C1 at the knee, monotonicity, asymptote below 1.
- `test/engine_features/RenderInstrumentationTests.cpp`: with a null bundle,
  publish points write nothing (probed with a canary stats struct); mode
  latch applies at frame boundaries; capture ring bounds respected; a
  generated capture parses with `JsonParse` and carries the schema version
  and required counter keys, so the AI-analysis interface cannot drift
  silently.
- Layout guards: compile-time static asserts for the 80-byte `GpuLight`, the
  shadow slot arrays, and the new push-constant block extend the existing
  blocks in `MeshForwardPass.cpp:15-31`.

GPU-dependent behavior (bias tuning, filter look, guard-band proof scene,
barrier correctness, the 9.7 disabled-path protocol, and the AO visual scenes:
coplanar split floor with no seam, cross-zone-boundary continuous surface, hard
corner, sparse floor beside one wall with localized refinement, open floor with
almost no added vertices, carved windows / thin door frames without opposite-wall
dirt, a static instance in several placements, probe + vertex AO composition
without crushed black, dynamic objects crossing differently enclosed probe
regions, and AO disabled proving direct lighting and shadows unaffected) is
validated by the staged scenes under the Vulkan validation layer plus RenderDoc
inspection, recorded per stage in Section 11. If llvmpipe CI lands later
(engine-roadmap.md:523), the capture tool doubles as its assertion source.

---

## 14. Performance budgets

Reference target: 1920x1080 on a GTX 1060 / RX 580 class GPU, 60 fps, total
GPU frame <= 12 ms leaving headroom.

| Item | Budget |
|---|---|
| MainColor GPU (representative room, lights + shadows + probes on) | <= 6.0 ms |
| Shadow phase GPU, steady state (caches warm) | <= 0.3 ms |
| Shadow phase GPU, worst invalidation frame (view clamp active) | <= 2.5 ms |
| One 512 spot view | <= 0.15 ms |
| One 512 point cube update (6 faces, per-face cull) | <= 0.8 ms |
| Probe sampling added cost (fragment, volumes resident) | <= 0.3 ms full-screen |
| Vertex AO runtime cost (one varying + `aoFactor` on ambient) | negligible (<= 0.02 ms) |
| Vertex AO disk/VRAM cost | +4 B/vertex; ~+200 KiB per 50k-vertex room |
| AO adaptive-tessellation vertex growth per zone | <= 2.0x base (`max_vertex_growth`, logged if hit) |
| Shadow memory (atlas 8 MiB + cubes 12 MiB) | <= 20 MiB fixed |
| Probe memory per zone (default density) | <= 2 MiB (typical room volume ~50 KiB) |
| Frame UBO | <= 8 KiB (16 KiB hard line, `RenderLight.h:42-44`) |
| Frame scratch slice (config default with shadows) | 2 MiB; high-water counter <= 75% in benchmarks |
| CPU extraction (meshes + lights + casters) at 5k queue items | <= 1.2 ms |
| Caster table diff (3k casters, OnChange slots resident) | <= 0.10 ms |
| `ShadowResidency` + probe residency CPU | <= 0.15 ms |
| Instrumentation, mode Off vs compiled out | statistically indistinguishable (9.7) |
| Instrumentation, Counters mode CPU | <= 0.05 ms |
| Instrumentation, Gpu mode CPU + GPU overhead | <= 0.10 ms |
| Capture ring memory (Capture mode only, lazily allocated) | <= 16 MiB default |
| Visible lights after cull (design guidance) | <= 24 typical, 64 hard cap |
| Shadowed lights simultaneously | 8 spot + 4 point (cvars) |
| Editor bake (probes + AO together), default density, per zone | <= 30 s, editor responsive throughout |

Escalation triggers (from 3C, restated as the standing rule):

- **Per-object light lists** when representative content shows MainColor
  > 8 ms at 1080p with average per-fragment light iterations > 16. The
  implementation would be a CPU range-vs-bounds cull writing a small per-draw
  light index list; it is deliberately not built ahead of the metric.
- **Tiled/clustered culling** only after per-object lists exist and visible
  lights regularly exceed 64 or lists average > 8 lights per object.
- **Architecture change (deferred etc.)**: no trigger within the target game
  space; out of scope by decision.

---

## 15. Risks, rejected alternatives, and deferred work

Risks and mitigations:

- **`imageCubeArray` unavailable on some target device.** Core feature, near
  universal on desktop; mitigation if ever hit: fall back to 6 atlas tiles
  per point light behind the same `ShadowResidency` interface (sampling
  switches to face selection in the shader). Contingency only; not built now.
- **D16 precision on long spot ranges.** Near-plane scaling covers the target
  ranges; the atlas format probes with D32 fallback, and per-light
  `ShadowBiasScale` is the manual escape.
- **Caster-diff cost on caster-heavy frames.** Bounded by the coarse gate
  (diff only when OnChange slots are resident) and budgeted (Section 14);
  recorded escalations: `Changed<>` prefilter, then a static/dynamic caster
  split (0.6).
- **Dilation leaks at thin interior walls within one volume.** Countermeasures
  and their limits are stated in 7.3 (cell-size guidance, volume splitting,
  dilated-probe overlay); occlusion-aware interpolation is the recorded
  escalation, with its real cost (manual 8-corner fetch x 3 textures, weight
  renormalization, a weights texture) written down so the decision is made
  against numbers, not hope.
- **Frame UBO growth.** At the chosen caps the block stays ~6.5 KiB; if caps
  rise, the recorded escape is moving lights + shadow slots to a storage
  buffer (scratch already carries STORAGE usage, `VulkanFrameScratch.h:32-34`).
- **Scratch overflow on worst-case invalidation frames.** Sized by config,
  measured by a high-water counter, degraded by skipping views (they re-queue
  next frame), never by corruption (6.5).
- **`.smat` v2 vs older tooling.** Version gate is explicit and loud by
  design; shudei and loaders land in the same stage.
- **Editor bake blocking on huge worlds.** The bake is per-volume/per-zone
  tasked and cancelable; Bake Zone exists precisely so Bake World is optional.
- **AO double-darkening in corners.** Bounded by scale-separated radius,
  strength dial, and floor (7A.7); the "probe + vertex AO without crushed
  black" scene tunes the defaults; the crushed-black debug view catches
  regressions.
- **Adaptive tessellation exploding vertex count on pathological geometry.**
  Capped by `max_vertex_growth` (default 2.0x, logged), min-edge, and max-depth;
  the occluder-proximity gate makes open surfaces free; `tess_max_depth 0` is
  the conservative escape (raw brush-vertex AO).
- **Cross-zone AO seam if determinism breaks.** The halo must be assembled in a
  stable order and the ray table fixed; the cross-zone determinism unit test
  (Section 13) fails loudly if a nondeterministic input creeps in.
- **`.smesh` version bumps vs older tooling.** Superseded in revision 4 by the
  single-version doctrine as shipped: the loader rejects prior versions
  outright and cooked content recooks via `kCookedCacheIndexVersion` (done
  this way for v4 baked direct; v5 AO follows suit). The failure mode is a
  loud reject plus recook, never a silently stale channel.
- **Instrumentation cost regressions over time.** The 9.7 A/B protocol is
  rerun whenever the instrumentation surface changes; the granularity policy
  (9.2) gives reviewers a bright line.

Rejected alternatives (reasons recorded in the cited sections): metallic-
roughness BRDF evaluation and image-based lighting (Section 3); GGX and
unnormalized Phong (3.2); per-material diffuse-wrap and ramp textures (3.3);
luminance-normalized Reinhard as the interim tonemap (3.2); VSM/EVSM/ESM,
rotated-Poisson noise filters, per-light dedicated shadow textures, per-tier
shadow texture arrays, duplicated edge texels (Section 4); dual-paraboloid,
octahedral, and radial-distance point shadows (Section 5);
`Changed<WorldTransform>` as the invalidation signal, and ECS lifecycle hooks
calling into the renderer (6.4); surface lightmaps and per-vertex bake as
primary (7.1); collision geometry as the bake tracing source (7.2);
validity-weighted runtime interpolation (7.3); per-instance probe volume
indices (7.4); a generalized spatial-field/voxel framework (Section 8);
duplicated or templated profiled/unprofiled render passes (9.2); a debug-view
branch inside the production StandardLit shader (9.6); Forward+/clustered now
(Sections 11/14); reversed-Z migration (Section 2). AO-specific rejections
(Section 7A): a separate per-probe ambient-visibility payload and a probe bent
normal for v1 (redundant with L1 SH irradiance, 7A.1); AO lightmap textures,
secondary lightmap UVs, chart/atlas generation, and any offline direct-shadow
artifact (banned by doctrine, 7A.2); uniform tessellation and any dependence on
manual tiling (7A.5); a shared-boundary-authority cross-zone scheme
(determinism-plus-halo replaces it, 7A.4); per-instance world-placement vertex
AO (breaks instancing, probes ground props instead, 7A.8); a full-float vertex
AO channel (unorm8 suffices, 7A.6); a physically-motivated residual-ambient
composition (reintroduces the rejected probe visibility, 7A.7); and runtime
contact AO / SSAO (deferred until real scenes prove it necessary).

Deferred work, anchored to triggers:

- **Directional lights + cascaded shadow maps**: explicitly out of Phase 3 by
  request; the `GpuLight` type enum and the shadow-slot mechanism are already
  shaped to receive them (roadmap Track B row to be updated). **AO consequence
  (binding rule for that future work):** the directional sun is a *direct* term
  and is contained only by its cascaded shadow maps and by geometry, never by
  AO. Baked AO modulates the ambient term exclusively (Section 3.2), so it
  structurally cannot darken the sun disc. The specific trap to forbid in
  review: "reuse AO to keep sunlight out of interiors" (multiplying the sun
  contribution, or a sky/sun ambient that leaks into the direct sum, by AO).
  An interior stays dark from the sun because CSM shadows it or because the sun
  never reaches it, not because AO attenuates a direct term. The probe
  irradiance and vertex AO may modulate the *sky ambient* (indirect); the sun
  key light may not.
- **Skinned shadow casters**: blocked on skinned rendering (Track B item 1,
  engine-roadmap.md:318-322); `ShadowDepthPass` takes a second vertex shader
  when posed buffers exist (Decision N).
- **Alpha-masked shadow casters and receivers, transparency lighting**: land
  with the transparency pass (Track B item 3).
- **Real post-processing tonemap/exposure**: the knee-shoulder block moves to
  the Post phase when it exists (Track B item 5); the cvars keep their
  meaning.
- **Light channels/layers**: `StaticMeshComponent.LayerMask` is extracted but
  unused today (`RenderExtractionSystem.cpp` never reads it); it is the
  natural mechanism if per-light receiver masking is ever needed.
- **Per-object light lists, clustered culling**: metric-gated (Section 14).
- **Probe occlusion-aware interpolation, emissive surfaces contributing to
  the bake, specular ambient from probes, per-vertex bake for hero meshes,
  light cookies via the spot atlas**: each waits for a content-driven need;
  cookies in particular are a natural atlas extension already anticipated by
  the slot record's scale/bias addressing.
- **Prop self-AO / authored object-space cavity AO** (bake a reusable mesh's
  own concavities once into its shared `.smesh`, shared by all instances,
  usable by dynamic objects): deferred to a follow-up mesh-asset cook; v1 props
  carry neutral vertex AO and take grounding from probes (7A.8). The vertex AO
  channel and the bake kernel already exist, so this is additive.
- **Probe bent normal and probe ambient-visibility scalar** (a sharper
  ambient direction / cone than the SH band-1 proxy): land only with ambient
  specular, which would be their first real consumer (7A.1). The vertex AO
  attribute reserves 3 bytes for a future octahedral vertex bent normal if that
  need reaches surface scale.
- **Richer AO composition** (authored per-material AO strength, occlusion-aware
  probe/vertex blend): the dialed multiply is the v1 model; a stronger model
  waits for content that the defaults cannot satisfy (7A.7).
- **`Changed<>` prefilter for the caster diff, static/dynamic caster split**:
  measure-first (0.6).
- **Multi-viewport editor shadow preview** (context zones + all viewports):
  focus-viewport-only in Phase 3; extend when editors ask.
- **Editor-process instrumentation and per-viewport stats**: after the
  runtime capture pipeline proves its schema.
- **Generalized spatial-field primitive**: waits for its second concrete user
  (water, voxels, occupancy), per directive 4.
