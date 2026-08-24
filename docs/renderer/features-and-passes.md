# Features, Phases, and Passes

## `IRenderFeature`

The one runtime seam between render policy and the backend. Declared in
`engine/include/graphics/RenderFeature.h`, and backend-neutral: a feature is
declarable without a graphics API in scope.

```cpp
class IRenderFeature
{
public:
    virtual RenderPhase GetPhase() const = 0;
    virtual bool Setup(const RenderFeatureServices& services) = 0;
    virtual void OnDraw(const RenderFrame& frame) = 0;
    virtual void Teardown() {}
};
```

| Hook | When | Contract |
|---|---|---|
| `GetPhase` | any time | one feature, one phase, constant for its lifetime |
| `Setup` | inside `Renderer::AddFeature` | cache what the feature needs, create up-front GPU resources. Returning `false` means the feature is unusable: `AddFeature` tears it down and refuses to register it, rather than leaving an inert feature in a phase bucket |
| `OnDraw` | once per frame, in phase order, in registration order within a phase | record commands, through the passes the feature drives. For `MainColor` the command stream is already inside the swapchain rendering scope. Other phases open their own |
| `Teardown` | in `~Renderer`, after the device is idle, before any backend service unwinds | release everything the feature still holds |

Degradation versus failure is a real distinction here. `ShadowRenderFeature::Setup`
returns `true` even when `LightBindings::Setup` fails, because the frame still
presents without lit shadows; that is deliberate policy, not a broken feature.
Returning `false` is for "this feature cannot record legal commands".

### Phases

```cpp
enum class RenderPhase : uint8_t { Offscreen = 0, MainColor = 1, Count };
```

| Phase | Rendering scope | Used by |
|---|---|---|
| `Offscreen` | none is opened. The feature owns its own passes, targets, and image barriers | `ShadowRenderFeature`, `EditorRenderFeature` |
| `MainColor` | the swapchain color plus depth scope is already open | `SkyRenderFeature`, `MeshRenderFeature`, the ImGui debug overlay |

The enum comment reserves Shadow, Opaque, Transparent, UI, and Post. Adding one
means adding an enum value before `Count` and a `RecordXPhase` in `Renderer`;
the feature interface does not change. The phase bucket array is sized by
`RenderPhase::Count`, and an empty bucket costs one branch.

### `RenderFeatureServices` and `RenderFrame`

`Setup` receives everything at once and features cache what they need. There
are no service lookups in the hot path.

| `RenderFeatureServices` field | Meaning |
|---|---|
| `Logging` | the logging provider |
| `Instrumentation` | the profiling bundle (see below) |
| `Buffers` / `Images` | create, upload, and destroy GPU resources by neutral description |
| `Scratch` | the per-frame bump allocator |
| `Backend` | the active backend's service bundle. Only a feature driving a recording pass touches it, and only to hand it through |

`RenderFrame` is the entire per-frame payload handed to `OnDraw`:

| Field | Meaning |
|---|---|
| `FrameInFlightIndex` | slot index, for anything the feature keeps per slot |
| `TargetExtent` | render-target dimensions |
| `Phase` | which bucket is being recorded |
| `Retirement` | fence-anchored frame clock, for releasing GPU resources safely |
| `Instrumentation` | the bundle again, so `BeginGpuScope` needs nothing else |
| `Backend` | the backend `FrameContext` -- the command stream, attachment formats, and depth view a recording pass needs |

The backend halves (`RendererServices`, `FrameContext`, declared in
`graphics/vulkan/Renderer.h`) are what the recording set consumes. A second
backend defines its own behind the same forward declarations.

`Instrumentation` is a stable pointer to a bundle whose **members flip** with
`render.profile.mode`. Cache the bundle, re-read its members per frame, never
cache the members.

## The two built-in features

### `ShadowRenderFeature` (Offscreen)

Owns the lighting bindings' lifetime for the game renderer and records the
residency arbiter's scheduled views each frame through `ShadowDepthPass`.

```
Setup:  LightBindings::Setup  (layout, set, dummy images)
        LightBindings::CreateAtlas      -> warns and continues on failure
        LightBindings::CreateCubePool   -> warns and continues on failure
        ShadowDepthPass::Setup          (shaders, layout, prewarm pipelines)
OnDraw: ShadowDepthPass::Draw(frame, lights, scheduledViews, scheduledFaces,
                              casters, meshes, &residency)
        publish DrawStats into RenderStats
Teardown: pass, then bindings
```

### `SkyRenderFeature` (MainColor)

Drives `SkyGradientPass`, and is the only place that decides where the
background's colours come from — today `RenderLightSet::AmbientSky` and
`AmbientGround`, which is where `render.ambient.*` lands. Registered **before**
`MeshRenderFeature`, because registration order is draw order within a phase and
the pass fills the view without a depth test.

`render.sky.enabled false` skips it, leaving the host's flat clear.

### `MeshRenderFeature` (MainColor)

A thin wrapper that holds the queue, caches, camera, and light set, and drives
`MeshForwardPass`. The draw itself lives in the pass so the editor can reuse it.

Both features wrap their `OnDraw` body in a `CpuScopeTimer` and, when the mode
is Gpu or above, in a debug label plus a GPU timestamp scope.

## `RenderQueue`

`engine/include/render/RenderQueue.h`. Transient per-frame draw list. Reset at
the top of extraction, appended by `RenderExtractionSystem`, sorted once, then
consumed by the forward pass.

A `RenderQueueItem` is one section of one mesh instance:

| Field | Notes |
|---|---|
| `Mesh`, `Material`, `SectionIndex` | what to draw |
| `WorldMatrix` | row-major CPU side, transposed on upload |
| `WorldBounds` | world AABB, used by the frustum test during extraction |
| `CameraDepth` | view-space depth of the bounds center, positive forward |
| `Pass` | `ShaderPassId`, currently only `ForwardOpaque` |
| `Pipeline` | `OpaquePipelineId`, derived from the material by `SelectOpaquePipeline` |
| `LightmapTextureIndex`, `AoTextureIndex` | bindless slots of the owning zone's baked planes, or `UINT32_MAX` |
| `LightmapScaleBias` | per-instance remap of lightmap UVs into the atlas rect |
| `SortKey` | computed on insert |

### Sort key layout

```
bits: [63:56 pass][55:54 pipeline][53:40 material][39:20 mesh][19:16 section][15:0 depth]
```

Depth contributes the top 16 bits of the float's bit pattern, so ascending key
order is front to back for positive depths. Material and mesh contribute
truncated slot indices, which is why run merging never trusts the key.

### Runs

`SortOpaque` produces two outputs: `OpaqueOrder()`, the item indices in draw
order, and `OpaqueRuns()`, maximal spans of consecutive order entries that share
**all** of pipeline, mesh, mesh section, material, pass, lightmap index, and AO
index. Each run becomes one `vkCmdDrawIndexed` with `instanceCount = run.Count`
and `firstInstance = run.First`.

Run identity is tested against the actual item fields, not the sort key, so the
truncated key bits cannot merge two draws that differ. The lightmap and AO
indices are part of the identity because they are uniform-per-draw push
constants: the same mesh resident in two zones must not share a run. The
lightmap scale/bias is per-instance vertex data and varies freely inside a run.

The sort scratch (`vector<pair<uint64_t,uint32_t>>`) is kept across frames so
the per-frame sort does not reallocate.

## `MeshForwardPass`

`engine/src/render/pass/MeshForwardPass.cpp`. Draws every opaque run.

### Pipeline variants

Eight opaque pipelines, indexed by `OpaquePipelineId`. The index is three
independent bits rather than a list: bit 0 double-sided, bit 1 unlit, bit 2
alpha-masked. `SelectOpaquePipeline` sets them from the material, and the pass
reads them back the same way.

| Index | Name | `constant_id 0` (`MATERIAL_UNLIT`) | `constant_id 1` (`MATERIAL_ALPHA_MASK`) | Cull |
|---|---|---|---|---|
| 0 | `Forward/StandardLitBack` | 0 | 0 | back |
| 1 | `Forward/StandardLitDoubleSided` | 0 | 0 | none |
| 2 | `Forward/UnlitBack` | 1 | 0 | back |
| 3 | `Forward/UnlitDoubleSided` | 1 | 0 | none |
| 4 | `Forward/StandardLitBackMasked` | 0 | 1 | back |
| 5 | `Forward/StandardLitDoubleSidedMasked` | 0 | 1 | none |
| 6 | `Forward/UnlitBackMasked` | 1 | 1 | back |
| 7 | `Forward/UnlitDoubleSidedMasked` | 1 | 1 | none |

All eight share one vertex and one fragment module. Unlit and alpha masking are
specialization constants, not branches and not second shaders. Masking is a
variant rather than a branch for a reason worth keeping: a fragment shader that
can `discard` gives up early depth testing, and an opaque scene should not pay
that to serve the masked materials in it. Common state: front face
counter-clockwise, depth test and write on, `LESS_OR_EQUAL`, no blend, one color
attachment in the swapchain format, depth in the depth target's format.

The masked variants discard when the sampled base-colour alpha falls below
`MeshPushConstants::AlphaCutoff`. Shadow casters do not: `ShadowCasterItem`
carries no material and the shadow vertex layout no UVs, so a masked surface
still casts its whole silhouette.

Under `SENCHA_ENABLE_RENDER_PROFILING` there are two more families of four built
from `mesh_debug_view.frag.glsl`: `Forward/Debug{Back,DoubleSided}{,Masked}` and
`Forward/Overdraw{Back,DoubleSided}{,Masked}`. The overdraw family disables depth
test and write and uses additive blending; the pass clears the color attachment
before drawing with it. Those families carry the cull and mask axes but not
lit/unlit -- the channel is a frame-uniform value, not a variant -- so
`DebugPipelineIndex` folds a queue item's id onto them. Masking survives the
fold deliberately: a debug view that kept the fragments the lit pass discards
would describe geometry that is not on screen.

Pipelines are built at the end of `Setup` (prewarm) rather than on the first
draw. Both formats are already known there, and driver compilation of the
variants costs tens of milliseconds: paid at load it is invisible, paid on the
first visible frame it is a hitch. `EnsurePipelines` still rebuilds if a format
changes later.

### Vertex input

Two bindings. Binding 0 is the mesh's `StaticMeshVertex` stream at vertex rate;
binding 1 is the per-instance stream written into frame scratch.

| Location | Binding | Format | Source |
|---|---|---|---|
| 0 | 0 | `R32G32B32_SFLOAT` | `StaticMeshVertex::Position` |
| 1 | 0 | `R32G32B32_SFLOAT` | `StaticMeshVertex::Normal` |
| 2 | 0 | `R32G32_SFLOAT` | `StaticMeshVertex::Uv0` |
| 3..6 | 1 | `R32G32B32A32_SFLOAT` | `MeshInstanceData::World` rows |
| 7 | 0 | `R32G32B32A32_SFLOAT` | `StaticMeshVertex::Tangent` (xyz tangent, w handedness) |
| 8 | 0 | `R16G16_UNORM` | `StaticMeshVertex::LightmapU/V` |
| 9 | 1 | `R32G32B32A32_SFLOAT` | `MeshInstanceData::LightmapScaleBias` |

`MeshInstanceData` is 80 bytes: a `Mat4` world matrix plus the lightmap
scale/bias.

### Draw sequence

```
Draw(frame, camera, lights, queue, meshes, materials, tint)
  LastStats = { QueueItems = order.size() }
  bail if the pipeline layout is null or the depth format is undefined
  bail if the queue is empty                          (not a skip: there is no work)
  EnsurePipelines / EnsureDebugPipelines              (failure => Skipped)
  UploadFrameUniforms   -> scratch AllocateUniform    (failure => Skipped)
  BindInstanceStream    -> scratch AllocateVertexElements, partial allowed
                           writes transposed world matrices + scale/bias
                           binds binding 1            (zero grant => Skipped)
  InstancesDropped = QueueItems - streamed
  BindFrameState: viewport, scissor, sets 0 (dynamic offset), 1, 2
  DrawRuns: for each run whose First < streamed
      clamp the instance count to the streamed prefix
      bind pipeline / vertex buffer / index buffer only when they change
      push constants (always, one per run)
      vkCmdDrawIndexed(section.IndexCount, drawCount, section.IndexOffset, 0, run.First)
```

Every early return after the "there is work" point sets `Skipped` and counts the
whole queue as dropped, so a frame that failed to render its scene cannot read
as a cheap frame.

The short-grant rule: a partial instance grant is treated as a **prefix** of the
draw order, not a gap in it. Draws index instances by queue position, and every
run left over would need slice space that the short grant just proved is gone.

### Push constants

`MeshPushConstants`, 80 bytes, visible to vertex and fragment stages. One
`vkCmdPushConstants` per run. `MaterialSwitches` counts these; it equals
`DrawCalls` until the pass starts skipping redundant material state, and the
counter exists to show exactly that.

| Offset | Field |
|---|---|
| 0 | `BaseColor` (material base color times the pass tint) |
| 16 | `EmissiveFactor` (rgb factor, w strength) |
| 32 | `NormalScale` |
| 36 | `RoughnessFactor` |
| 40 | `MetallicFactor` |
| 44 | `SpecularIntensity` |
| 48 | `BaseColorTextureIndex` |
| 52 | `NormalTextureIndex` |
| 56 | `OrmTextureIndex` |
| 60 | `EmissiveTextureIndex` |
| 64 | `ReceiveShadows` |
| 68 | `LightmapTextureIndex` |
| 72 | `AoTextureIndex` |
| 76 | `Pad2` |

The `tint` parameter of `Draw` multiplies base color at the draw level. The game
passes white; the editor uses it to dim context zones.

### Frame uniform block

`MeshFrameUniforms`, 5712 bytes, uploaded once per frame into scratch and
addressed through set 0's dynamic offset. Layout is asserted field by field in
`MeshForwardPass.cpp` and mirrored in `engine/shaders/mesh_frame.glsli`.

| Offset | Field | Notes |
|---|---|---|
| 0 | `ViewProjection` | transposed on upload |
| 64 | `ViewPositionTime` | xyz camera position, w unused |
| 80 | `AmbientSky` |  |
| 96 | `AmbientGround` |  |
| 112 | `StyleParams` | x diffuse wrap, y minimum ambient, z exposure, w tonemap knee |
| 128 | `LightCount` |  |
| 132 | `TonemapEnabled` |  |
| 136 | `ShadowDarkness` |  |
| 140 | `BakedDirectEnabled` |  |
| 144 | `Lights[64]` | `GpuLight`, 64 bytes each |
| 4240 | `SpotShadowCount` |  |
| 4244 | `BakedAoEnabled` |  |
| 4256 | `SpotShadows[8]` | `GpuSpotShadow`, 96 bytes each |
| 5024 | `PointShadowCount` |  |
| 5040 | `PointShadows[4]` | `GpuPointShadow`, 32 bytes each |
| 5168 | `ProbeVolumeCount` |  |
| 5184 | `ProbeVolumes[8]` | `GpuProbeVolume`, 64 bytes each |
| 5696 | `DebugView` | always present in the struct, written only in profiling builds |

## `ShadowDepthPass`

`engine/src/render/pass/ShadowDepthPass.cpp`. Records the arbiter's scheduled views:
spot tiles into the atlas, point faces into the cube pool, one depth-only
dynamic-rendering scope per view.

### Pipelines

Three, all from `shadow_depth.vert/frag` with depth bias enabled and depth
format `D16_UNORM`:

| Variant | Front face | Cull | Used for |
|---|---|---|---|
| `ShadowPipelineId::Back` | counter-clockwise | back | spot views, single-sided casters |
| `ShadowPipelineId::FlippedBack` | clockwise | back | point faces, single-sided casters |
| `ShadowPipelineId::DoubleSided` | counter-clockwise | none | double-sided casters in either kind of view |

They live in a `PipelineVariantSet` keyed on `ShadowDepthBias`, so retuning
`render.shadow.bias_const` or `bias_slope` rebuilds the family once;
`SelectShadowPipeline` picks the variant.

The flipped variant exists because the unflipped cube-face projection mirrors
winding.

Bias values come from `render.shadow.bias_const` and `render.shadow.bias_slope`,
so they are baked into the pipeline. `EnsurePipelines` caches the last pair and
rebuilds all three when either changes. `Setup` prewarms the defaults.

Vertex input is deliberately narrow: binding 0 supplies only `Position` at
location 0, binding 1 supplies a bare `Mat4` per instance at locations 3 to 6.
The location numbers match the forward layout so both shaders can share the
same instance-matrix convention.

### Per-view sequence

```
Draw(frame, lights, views, pointFaces, casters, meshes, residency)
  bail unless (atlas exists and views nonempty) or (cube pool exists and faces nonempty)
  EnsurePipelines: on failure, mark every scheduled view failed, revoke every
                   grant, set Skipped, and return without touching a target

  spot:  TransitionAtlasForWrite
         for each view: RecordView into the tile
         TransitionAtlasForRead

  point: TransitionCubePoolForWrite
         for each face: RecordView into that cube layer
         TransitionCubePoolForRead
```

`RecordView`:

1. `GatherVisibleCasters`: for every caster, optionally reject with one
   light-sphere test (point faces only: a light cannot cast past its range, so
   one test rejects a caster for all six faces), then a frustum test against the
   view's own view-projection, then resolve the mesh and section. Survivors are
   sorted into draw-run order by (double-sided, mesh, section).
2. Upload the view-projection into scratch, then allocate the instance transform
   stream. **Either allocation failing returns false before the target is
   touched**, so cached content stays valid.
3. Open the depth scope with `LOAD_OP_CLEAR` and `STORE_OP_STORE`. A view that
   nothing casts into still renders: a cleared target is the correct depth for
   "nothing occludes".
4. Walk the sorted survivors, collapsing equal draws into one instanced call.

Spot tile viewports are inset by `kSpotShadowGuardTexels` on every side, while
the scissor covers the whole tile. The clear therefore covers the guard band and
the geometry cannot write into it.

### Failure and revocation

A view whose recording could not proceed is reported twice:

- `ShadowResidency::MarkViewFailed` / `MarkPointFaceFailed`, so the slot
  re-queues and its cached content is not treated as fresh.
- `RevokeGrant`, which clears `ShadowIndex` on every packed light pointing at
  that slot, so the forward pass this frame does not sample content against a
  record it was not rendered with.

## `SkinnedPosePass` and `SkinnedPoseRenderFeature`

`engine/src/graphics/vulkan/SkinnedPosePass.cpp` plus its Offscreen feature
in `engine/src/render/feature/SkinnedPoseRenderFeature.cpp`. The pre-skin dispatch
(pipeline Decision N, resolved to compute pre-skin): one compute invocation
per vertex blends the joint palette into the rest geometry and writes a
posed vertex buffer, which every geometry pass then draws exactly as static
geometry -- `MeshForwardPass` swaps which buffer it binds and nothing else
changes.

The split follows `SkyGradientPass`: the pass takes plain data (buffer
handles, counts, byte offsets) and owns the GPU resources -- the compute
pipeline, its descriptor pools, the posed buffers, and the one barrier
making the dispatch's storage writes visible to vertex fetches. The feature
owns the policy: which instances pose, where their palettes come from, and
the per-frame-in-flight buffer lifecycle (a frame still reading slot N's
buffer must not race the next frame's dispatch into it).

`SkinnedPoseFrameData` is the channel. Extraction fills one entry per
visible skinned entity, with its palette, and stamps the matching
`RenderQueueItem::PoseSlot`; the feature dispatches and fills in the posed
buffers; the forward pass reads them. A slot with no posed buffer -- the
pass hit its per-frame dispatch budget, or scratch had no room for the
palettes -- falls back to rest geometry, which is a visible degradation
rather than a dropped draw. `PoseSlot` joins the run-merge identity: two
entities sharing one skinned mesh pose independently and must not collapse
into one instanced draw.

An instance with no clip player poses at the bind identity, so the dispatch
reproduces its rest vertices bit-for-bit -- which is why `skinned_rest`
stays byte-identical through the compute path and is the gate for it.
`AnimationClipPlayerComponent` supplies the pose for the rest: extraction
samples the clip at the player's current time
(`anim/AnimationClipSampling.h`), composes model-space transforms, and
builds the palette through the existing `BuildSkinningPalette`. Pose
evaluation happens once per rendered frame, not once per fixed tick; the
player's time is the only thing the tick advances.

## `SkyGradientPass`

`engine/src/graphics/vulkan/SkyGradientPass.cpp`. Fills the view with a vertical
gradient before anything else draws into it.

It lives in the backend rather than in `render/` because it takes a matrix and
two colours and names no render-domain type — no camera, no light set, no queue,
no cache. `SkyRenderFeature` and `EditorRenderFeature` each decide where those
values come from; the pass cannot know.

The gradient is not a decorative ramp. `mesh_forward.frag.glsl` lights every
surface with `mix(AmbientGround, AmbientSky, 0.5 + 0.5 * n.y)`, and this shades
the background with the same expression for the direction the eye is looking. So
the background *is* what the ambient term already claims the surroundings are,
and the two cannot drift apart.

### Why it draws first, without a depth test

Drawing it last at the far plane against cleared depth would skip the pixels
geometry already covers. It would also make correctness depend on each host's
draw order, and the editor viewport interleaves a backdrop, a grid, bodies, and
overlays whose depth behaviour differs. The game and the editor agreeing is the
requirement; one full-screen fill of a ten-instruction shader is the price.

A consequence worth keeping: with no depth state, the pass needs no depth
attachment and no second pipeline variant for hosts that lack one.

### Inputs

| Input | Source |
|---|---|
| inverse view-projection | `MakeInverseSkyViewProjection` in `render/CameraProjection.h`, which strips the view translation so the result is a direction and the gradient does not slide with the camera |
| `SkyGradientParams` colours | two linear-RGB values. The seam a future authored environment record fills instead of the cvars |
| `SkyGradientParams` output transform | exposure, tonemap knee, tonemap on/off, applied through the shared `tonemap.glsli`. Without it the background sits in a different display space than the geometry, and raising `render.exposure` brightens the scene but not the sky it is lit by |

Everything fits in a 96-byte push block, so the pass binds no descriptor sets
and depends on neither the frame uniform nor the lighting bindings.

### Hosts

| Host | Where |
|---|---|
| `SkyRenderFeature` | game, `MainColor`, registered before `MeshRenderFeature` |
| `EditorRenderFeature` | inside the per-viewport `RenderScope`, **perspective viewports only** — ortho views keep `ViewportBackdropRenderer`, since a sky in a 2D working view describes nothing |
