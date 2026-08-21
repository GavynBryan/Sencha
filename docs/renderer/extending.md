# Extending the Renderer

Recipes for the changes that come up. Each lists every file that has to move,
because most of these are multi-file contracts and half a change compiles.

Before starting anything here, check the invariants in
[constraints.md](constraints.md#invariants). The most common review rejection is
a new parallel code path where data would have done.

---

## Add a render feature

A feature is the unit of "some subsystem draws things". Use one when the work
has its own GPU resources, its own pipelines, and its own place in the phase
order.

**1. Declare it.** One header and one `.cpp` under `engine/include/render` and
`engine/src/render`, named for the mechanism, not the content.

```cpp
class DecalRenderFeature : public IRenderFeature
{
public:
    DecalRenderFeature(DecalQueue& queue, StaticMeshCache& meshes);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    [[nodiscard]] bool Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

private:
    DecalQueue* Queue = nullptr;
    StaticMeshCache* Meshes = nullptr;
    const RenderInstrumentation* Instrumentation = nullptr;
    DecalPass Pass;   // the actual recording, factored out so hosts can reuse it
};
```

**2. Cache in `Setup`, never in `OnDraw`.** Take the pointers you need out of
`RendererServices` and build shaders and layouts there. Pipelines go in a
`PipelineVariantSet`; prewarm it if the formats are already known
(`services.Swapchain->GetFormat()` and `services.DepthFormat`), so the first
visible frame is not a hitch. See [record a pass](#record-a-pass).

Return `false` only when the feature cannot record legal commands. If it can
degrade to drawing nothing while the frame still presents, return `true` and log
a warning; that is the policy `ShadowRenderFeature` follows when the lighting
bindings fail.

**3. Release in `Teardown`.** It runs in `~Renderer` after `vkDeviceWaitIdle`
and before any Vulkan service unwinds. Destroy pipelines layouts, descriptor
pools, samplers, and image views you created directly; hand cache-owned handles
back to their caches.

**4. Register it.** In `DefaultRenderPipeline::AddMeshRenderFeature` (or the
game's own composition root):

```cpp
if (graphics.MainRenderer.AddFeature(std::make_unique<DecalRenderFeature>(...)) == nullptr)
    return false;
```

Registration order inside a phase is draw order. If your feature produces
something another feature samples, it must be registered earlier **and** be in
an earlier phase.

**5. Instrument it.** Wrap `OnDraw`'s body in a `CpuScopeTimer` and, under
`SENCHA_ENABLE_RENDER_PROFILING`, a debug label plus a GPU scope. Publish
pass-local totals into `RenderStats` when `Instrumentation->Stats` is non-null.
Both need new enum entries; see [add a scope](#add-a-cpu-or-gpu-scope) and
[add a counter](#add-a-counter).

**6. Test what you can.** A feature needs a live device, so the testable part is
whatever you factored out of it. Put the arithmetic and the policy in a plain
type with no Vulkan handles and unit-test that. The models are
`FrameScratchRing`, `ShadowAtlasAllocator`, `MakeScopeContext`, `MeshBindState`,
and `MeshDrawTally` — the last two are the shape to copy when the thing you
need to test is reachable only from inside a recording loop.

---

## Record a pass

A feature decides *what* to draw. Recording is *how* it reaches the command
buffer, and three mechanisms cover it. A pass that hand-writes one of them is
either doing something genuinely unusual — say so in a comment — or copying a
bug, which is how all three of these came to exist.

**The rendering scope.** `RenderScope`
(`engine/include/graphics/vulkan/RenderScope.h`) is the attachment structs,
`vkCmdBeginRendering`, and the matching end as an RAII pair, plus `Context()`:
the `FrameContext` describing what is bound *inside* the scope.

```cpp
RenderScopeDesc scope{};
scope.Area.extent = target.Extent;
scope.Color.View = target.ColorView;
scope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
scope.ColorFormat = target.ColorFormat;
scope.Phase = RenderPhase::Offscreen;

const RenderScope rendering(frame, scope);
Pass.Draw(rendering.Context(), ...);
```

Take the inner context from `Context()` rather than filling a `FrameContext`
yourself. A hand-built one silently zeroes every field it does not mention, and
the fields grow: when the fence-anchored retirement clock landed, two of the
three hand-built contexts would have handed their passes a clock reading zero —
"nothing has ever retired" — which frees a resource the GPU is still reading.

Barriers stay outside the scope, with the pass. The sites transition genuinely
different things at genuinely different granularities — a swapchain image, an
ImGui-sampled offscreen target, a shadow atlas transitioned once around *all*
its views — and that granularity is pass policy, not scope boilerplate.
Viewport and scissor are the caller's for the same reason: a pass drawing one
full-target quad and a pass drawing into an atlas tile want different answers,
and both are one call.

**The pipeline family.** `PipelineVariantSet<Count, Key>` owns a family and the
rule for discarding it — valid only for the key it was built against, and only
once every variant in it exists.

```cpp
return OpaquePipelines.Ensure(
    AttachmentFormatKey{ frame.TargetFormat, frame.DepthFormat },
    [&](std::size_t index) {
        GraphicsPipelineDesc desc = MakeBase();
        desc.CullMode = index == 0 ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        return Pipelines->GetGraphicsPipeline(desc);
    });
```

The key is whatever the pipelines actually depend on: attachment formats for
the mesh passes, the depth-bias pair for the shadow pass. Build the whole
description inside the callable — it does not run in the steady state, so the
vectors a `GraphicsPipelineDesc` holds are allocated on a rebuild rather than
once a frame. A build that fails part-way records nothing and retries the whole
family, so do not add a "did it work" flag beside the set; that is the
duplication it replaced.

Call `Reset()` in `Teardown`. The pipelines belong to `VulkanPipelineCache` for
the life of the service; the set only forgets them.

**The draw.** `MeshDrawSubmitter` emits the binds a run still needs, then the
indexed instanced draw, and counts both.

```cpp
MeshDrawCommand draw{};
draw.Pipeline = pipeline;
draw.VertexBuffer = Buffers->GetBuffer(mesh->VertexBuffer);
draw.IndexBuffer = Buffers->GetBuffer(mesh->IndexBuffer);
draw.IndexCount = section.IndexCount;
draw.IndexOffset = section.IndexOffset;
draw.InstanceCount = run.Count;
draw.FirstInstance = run.First;
Submitter.Submit(frame.Cmd, draw);
```

Call `Invalidate()` whenever anything else binds to the same command buffer.
The shadow pass rebinds its per-view instance stream at every view, so what the
submitter recorded for the previous view no longer describes the buffer;
missing that drops the first bind of every view after the first.

It deliberately does not own merge policy, per-view binds, the instance stream,
or push constants. That is where the two callers genuinely differ — the forward
pass merges on material identity and pushes per draw, the shadow pass merges on
mesh and section and pushes nothing — and parameterizing those is how this
becomes the generic command stream that was considered and rejected.

**What stays hand-written**, so nobody absorbs it into a mechanism:
`Renderer::RecordMainColorPhase`'s swapchain transitions and cross-frame depth
barrier, because no attachment declaration expresses "two frames ago";
`LightBindings`' pool transitions and clear-at-creation; `EditorBloomPass`'s
ping-pong aliasing, where sub-pass N's target is N+1's sampled source;
descriptor set 2 creation and the update-after-bind probe slot swap; and
frame-scratch partial-grant clipping, which runs before the run list is final
because the grant changes how many draws exist.

---

## Add a view

A host with more than one view declares what its frame is made of and lets
`FrameComposition` (`engine/include/render/FrameComposition.h`) order it, rather
than calling its passes in an order a comment explains. `EditorRenderFeature` is
the worked example: shadow arbitration is one piece of work, every live viewport
is a view that waits on it.

Two node kinds, one scheduler:

- **A view** names a target, carries the camera it renders through, and carries
  an opaque `User` pointer so the host can find the panel or capture face it
  belongs to. The composition hands the whole `FrameView` back to the record
  body, which is why the camera is built once by the declaring host instead of
  by every renderer inside the view.
- **Work** is a subsystem's contribution that is not a declared view. It owns
  whatever internal views and targets it needs and exposes only the dependency
  point it produces.

```cpp
// Once, in Setup:
Composition.Setup(services.Logging);
ShadowAtlasReady = Composition.DeclarePoint("ShadowAtlasReady");

// Per frame:
Composition.Clear();
Composition.AddWork({
    .Name = "shadow_residency",
    .Record = { [](void* self, const FrameContext& context)
                { static_cast<MyHost*>(self)->RecordShadows(context); }, this },
    .Produces = ShadowAtlasReady,
});
const DependencyPointId dependsOn[] = { ShadowAtlasReady };
for (ViewSlot& slot : Slots)
    Composition.AddView({
        .View = { .Name = "viewport", .Target = slot.Target, .Camera = slot.Camera,
                  .User = &slot },
        .Record = { [](void* self, const FrameContext& context, const FrameView& view)
                    { static_cast<MyHost*>(self)->RecordView(context, view); }, this },
        .DependsOn = dependsOn,
    });
Composition.Execute(frame);
```

Rules worth knowing before you use it:

- **A specialised subsystem publishes a dependency point; it does not contribute
  one node per internal view.** `ShadowResidency` owns budgets, priorities, and
  steal hysteresis, and moving its per-slot scheduling into a static frame
  description would leak dynamic policy upward. The boundary is about who owns
  the scheduling, not about which attachments a node writes.
- **The composition does not acquire targets, open scopes, or place barriers.**
  A view names its target so the frame can be described and validated; the
  record body resolves it and does its own transitions, because barrier
  granularity is pass policy.
- **A node that cannot run takes its dependents with it.** Missing producer,
  duplicate producer, no target, no body, or a cycle: each is reported once by
  name and the node is skipped, and so is anything waiting on it. A view that
  did not render must not be sampled as though it had.
- **Record bodies are function pointers, not `std::function`.** The composition
  is rebuilt every frame; a capture large enough to hold a real record body
  would allocate on every assignment. Bind with a capture-less lambda, and keep
  whatever the `User` pointer refers to alive for the frame.
- **Declare the points once and reuse the ids.** `Clear()` drops the nodes and
  keeps the points.

---

## Add a render phase

Only when a feature genuinely cannot run inside `Offscreen` or `MainColor`. A
phase is a point in the command stream with its own rendering scope policy.

1. Add the value to `RenderPhase` **before `Count`**
   (`engine/include/graphics/vulkan/Renderer.h`). The bucket array is sized by
   `Count`, so nothing else changes structurally.
2. Add a `RecordXPhase(const VulkanFrame&)` to `Renderer` and call it from
   `DrawFrameScheduled` in the right order.
3. Decide the scope policy and document it in the enum comment: does the phase
   open a rendering scope for its features (like `MainColor`), or do features own
   their own (like `Offscreen`)?
4. Add a `GpuScope` entry and wrap the phase in it, matching the existing
   `PhaseOffscreen` / `PhaseMainColor` pattern.
5. Fill the `FrameContext` fields that are meaningful for the phase, and leave
   the rest at their defaults. Features must tolerate an undefined
   `TargetFormat` / `DepthView` outside `MainColor`.

---

## Add an engine shader

**1. Write the GLSL** in `engine/shaders`. Include `mesh_frame.glsli` if you
need the frame UBO, and follow the include order in
[shaders.md](shaders.md#include-topology).

**2. Wire the build** in `engine/CMakeLists.txt`, inside the
`if(SENCHA_ENABLE_VULKAN)` block:

```cmake
sencha_compile_shader(
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/decal.frag.glsl"
    STAGE frag
    OUTPUT_SPV _decal_frag_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_decal_frag_spv}"
    VAR_NAME kDecalFragSpv
    OUT_HEADER _decal_frag_header
)
list(APPEND SENCHA_ENGINE_GENERATED_HEADERS "${_decal_frag_header}")
```

Put it inside an `if(SENCHA_ENABLE_RENDER_PROFILING)` too if it is a diagnostic
shader, the way `mesh_debug_view` is.

**3. Create the module** in the owning pass's `Setup`:

```cpp
#include <shaders/kDecalFragSpv.h>
...
FragmentShader = Shaders->CreateModuleFromSpirv(
    kDecalFragSpv, kDecalFragSpvWordCount, "Decal fragment");
```

and destroy it in `Teardown` with `Shaders->Destroy(FragmentShader)`.

**4. Build the pipeline** through `VulkanPipelineCache::GetGraphicsPipeline`
with a fully populated `GraphicsPipelineDesc`, from inside a
`PipelineVariantSet::Ensure` callable. Everything the desc does not carry is
fixed: dynamic viewport and scissor, line width 1.0, one sample, no stencil.

**5. Name it**, in that same callable, once the handle is non-null:
`VulkanDebugLabels::NameObject(device, VK_OBJECT_TYPE_PIPELINE, handle,
"Decal/Opaque")`. Object naming is creation-time metadata and costs nothing per
frame; it is what makes a GPU capture readable.

---

## Add a material parameter

The push constant path. Use this when the value varies per draw and comes from
the material.

1. **`Material`** (`engine/include/render/Material.h`): add the field with a
   default that reproduces current behavior.
2. **`.smat` schema and loader**: add the field so it round-trips. New fields
   need an explicit default or previously cooked assets fail to load.
3. **`MeshPushConstants`** (`engine/include/render/MeshForwardPass.h`): add the
   field. Watch std140 alignment; the struct is a full 80 bytes today, so a new
   field grows it -- there is room, the budget is the 128-byte guaranteed
   minimum and the block is pushed for the fragment stage only.
4. **`static_assert` block** in `engine/src/render/MeshForwardPass.cpp`: add an
   offset assert for the new field and update `sizeof`.
5. **`MeshForwardPass::DrawRuns`**: copy the field out of the `Material` into
   the push struct.
6. **`mesh_material.glsli`**: add the field to the push block there, at the
   same offset. That is the only GLSL mirror -- the vertex shader carried a
   second copy for years without reading a field of it, and the copy drifted,
   so it was deleted rather than shared. Both fragment shaders (lit and debug
   view) include this one block.
7. If the value changes the **pipeline** (a new cull mode, a new blend state)
   rather than shading math, it belongs in `OpaquePipelineId` and
   `SelectOpaquePipeline` instead, and the pipeline array grows.
8. If the value must be **uniform per run**, add it to the run-merge equality
   test in `RenderQueue::SortOpaque` and to `RenderQueueItem`. Forgetting this
   silently renders two different materials with the first one's value.

---

## Add a frame-uniform field

Use this when the value is constant for the whole frame.

1. **`MeshFrameUniforms`** (`engine/include/render/MeshForwardPass.h`): add the
   field. Respect std140: `vec3` occupies 16 bytes, and a scalar following an
   array needs the array to have ended on a 16-byte boundary. Add explicit pad
   members rather than relying on the compiler.
2. **`static_assert` block**: the asserts are chained -- each field starts
   where the previous one ends -- so inserting a field renumbers nothing below
   it. Add one chain link for the new field, repair the link of the field that
   now follows it, and update the absolute `sizeof(MeshFrameUniforms)` at the
   end, which is the one conscious edit.
3. **`mesh_frame.glsli`**: mirror the field at the same offset, with the same
   padding.
4. **`MeshForwardPass::UploadFrameUniforms`**: write it.
5. **Source**: usually `RenderLightSet`, filled from a cvar in
   `DefaultRenderPipeline::ApplyRendererCVars`.
6. Check the size against the 8 KiB soft budget in
   [constraints.md](constraints.md#performance-budgets). The block is 5712 bytes
   today. Nothing enforces it at runtime beyond a warning, because clamping the
   descriptor range would hand the shader less than the block it declares --
   `FrameUniformRangeTests` is what fails when the block crosses the line.

Nothing to do about the descriptor range itself: the pass declares the block it
reads through `RequireFrameUniformRange` and the cache keeps the largest
declaration, so growing the block cannot leave another pass short.

---

## Add a light property

1. Add the field to `PointLightComponent` or `SpotLightComponent` with a
   `.Default(defaults.Field)` entry in its `TypeSchema::Fields()`. Without the
   default, previously cooked scenes fail to load.
2. If the GPU needs it, add it to `GpuLight` (currently exactly 64 bytes and
   asserted) and to the `GpuLight` struct in `mesh_frame.glsli`. Prefer packing
   into an existing `vec4`'s spare component over growing the struct: 64 lights
   times the growth lands directly in the frame UBO budget.
3. Pack it in `MakePointGpuLight` / `MakeSpotGpuLight`
   (`engine/include/render/LightGpuTypes.h`). Precompute anything the shader
   would otherwise recompute per fragment, the way `ConeScale` and `ConeOffset`
   do.
4. If it affects shadow content, add it to `HashSpotShadowState` /
   `HashPointShadowState` so an `OnChange` slot re-renders when it changes.
5. If it affects selection, it belongs in `ForwardLightCandidate` and in
   `LightImportanceScore`.

---

## Add a debug view

`SENCHA_ENABLE_RENDER_PROFILING` only.

1. Add the enum value to `RenderDebugView`
   (`engine/include/render/RenderDebugView.h`), before `Count`. The numeric
   values are the frame-UBO contract.
2. Update `ToString`, `RenderDebugViewLabel`, and `ParseRenderDebugView` in
   `engine/src/render/RenderDebugView.cpp`.
3. Add the matching `const uint DEBUG_*` and its branch to
   `engine/shaders/mesh_debug_view.frag.glsl`.
4. Add the cvar string to the `EnumValues` list of `render.debug.view` in
   `engine/src/app/EngineConsoleBuiltins.cpp`, or the view exists but cannot be
   selected from the console.
5. If the channel needs a new resource (a raw, non-comparison sampler, for
   instance), add it to `LightBindings` behind
   `#ifdef SENCHA_ENABLE_RENDER_PROFILING` at a binding index above the shipping
   ones, the way bindings 3 and 4 are done.

---

## Add a counter

1. Add the field to `RenderStats` (`engine/include/profiling/RenderStats.h`)
   with a comment saying what it means and, if it is one of a pair, what it must
   be read against.
2. Accumulate it **unconditionally** in the producing pass's local `DrawStats`,
   at run granularity. Do not branch on the instrumentation bundle in the draw
   loop. Anything countable from a `MeshDrawCommand` and its bind delta belongs
   in `MeshDrawTally` instead, so both passes get it at once.
3. Copy it into `RenderStats` at pass exit, inside the existing
   `Instrumentation->Stats != nullptr` check.
4. Bump `RenderCapture::kSchemaVersion` and add the field to both
   `SerializeJson` and `SerializeCsv` with an explicit unit suffix
   (`_ms`, `_bytes`, `_count`).
5. Add it to `engine/src/debug/RenderStatsPanel.cpp` if it is worth watching
   live.

Every field in `RenderStats` must have a live producer. Fields for systems that
do not exist yet are added with those systems.

---

## Add a CPU or GPU scope

Both enums are closed on purpose: scope identity is compile-time, so no
per-frame string work exists anywhere.

**CPU**: add the value to `CpuScope`
(`engine/include/profiling/CpuScopeTimings.h`) before `Count`, add its case to
`ToString` in `CpuScopeTimings.cpp`, and wrap the producer in a
`CpuScopeTimer(Instrumentation->CpuScopes, CpuScope::Yours)`.

**GPU**: add the value to `GpuScope`
(`engine/include/profiling/RenderInstrumentation.h`) before `Count`, add its
case to `ToString` in `RenderInstrumentation.cpp`, and bracket the work:

```cpp
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (gpuScopes != nullptr)
    {
        VulkanDebugLabels::BeginLabel(frame.Cmd, ToString(GpuScope::Yours));
        gpuScopes->BeginScope(frame.Cmd, GpuScope::Yours);
    }
#endif
    // work
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (gpuScopes != nullptr)
    {
        gpuScopes->EndScope(frame.Cmd, GpuScope::Yours);
        VulkanDebugLabels::EndLabel(frame.Cmd);
    }
#endif
```

The query pool sizes itself from `kGpuScopeCount`, so nothing else changes.

---

## Add a renderer cvar

Tunables are cvars, not constants, and not recompiles.

1. Register it in `engine/src/app/EngineConsoleBuiltins.cpp`. Use the
   `registerRenderDouble` helper for scalars; use a full `RegisterCVar` for
   bools, strings, and enums. Give it a `Help` string, a `Min` where one
   applies, and `EnumValues` for string enums.
2. Read it in `DefaultRenderPipeline::ApplyRendererCVars` into `RenderLightSet`,
   or in whatever per-frame read point the consumer already has.
3. If it feeds pipeline state (the depth-bias pair does), it belongs in the
   owning pass's `PipelineVariantSet` key rather than in a comparison the pass
   writes itself. `ShadowDepthPass` keys on `ShadowDepthBias`, so changing
   either cvar rebuilds the family once.
4. If it changes what a cached shadow tile should contain, invalidate: the
   editor calls `ShadowResidency::InvalidateAll` when the bias cvars change,
   because the bias is baked into rendered tiles.

---

## Reuse the passes in a new host

`MeshForwardPass` and `ShadowDepthPass` are plain classes so a host other than
the game renderer can drive them. `EditorRenderFeature` is the worked example.

A host needs to provide, per frame:

| Pass input | What to build |
|---|---|
| `CameraRenderData` | `CameraRenderDataSystem::Build`, or fill the struct yourself: view, projection, view-projection, position, frustum |
| `RenderQueue` | append `RenderQueueItem` values, then `SortOpaque()` |
| `RenderLightSet` | pack lights, or leave `Count = 0` for an unlit view |
| `LightBindings` | `Setup`, plus `CreateAtlas` / `CreateCubePool` if the host wants shadows |
| `ShadowCasterSet` and `ShadowResidency` | only if the host renders shadow views |

The ordering constraints from the game path apply unchanged: the lighting
bindings must be set up before `MeshForwardPass::Setup` (which reads the set
layout), and shadow views must be recorded before the forward pass that samples
them. A host with more than one view states that second constraint as a
dependency rather than as call order — see "Add a view".

Two things the editor does that a host should copy:

- Reset the arbiter when the scene identity changes. Slot state describes one
  scene's lights, so a focus or document switch resets rather than letting stale
  holders age out through steal hysteresis.
- Use the `tint` parameter of `MeshForwardPass::Draw` for draw-level dimming
  instead of mutating materials.

---

## Things not to do

- Do not add a `switch (mode)` to a pass. Behavioral variation enters through
  material data, a specialization constant, a component, or a separate named
  pass.
- Do not add a second pipeline family "for the low-end path". There is one
  pipeline family, selected by data.
- Do not call `vkDestroyX` on a runtime-freed resource. Route it through the
  deletion queue.
- Do not reach for a mutex. The render path is single-threaded by construction.
- Do not cache `Instrumentation->Stats` or `Instrumentation->GpuTimestamps`
  across frames. Cache the bundle pointer and re-read the members.
- Do not add a field to `RenderStats` with no producer.
- Do not serialize a render handle. Scene data persists asset paths.
