# Sencha Renderer

Reference documentation for the render layer: the render-domain code under
`engine/include/render` / `engine/src/render`, the Vulkan backend under
`engine/*/graphics`, the instrumentation under `engine/*/profiling`, and the
GLSL under `engine/shaders`.

This directory describes the renderer **as it exists in the tree**. It is not a
plan and it carries no roadmap. Where a document and the code disagree, the
code is right and the document is a bug.

## What the renderer is

A single-pipeline forward renderer on Vulkan 1.3 core (dynamic rendering,
synchronization2, descriptor indexing). A sorted, instanced forward pass over an
opaque queue and a back-to-front transparent one, preceded by an offscreen phase
that runs the skinning dispatch and the depth-only shadow renders. Lighting
is punctual point and spot lights evaluated per fragment against a fixed
64-light frame budget, plus baked lightmaps, baked ambient occlusion, and baked
L1 irradiance probe volumes streamed per zone.

There is one pipeline family, selected by data (material shading mode, cull
mode, specialization constant). There are no fidelity tiers and no per-scenario
code paths.

| Property | Value |
|---|---|
| API | Vulkan 1.3 core, no render passes (dynamic rendering), sync2 barriers |
| Shading | Stylized: wrapped diffuse, normalized Blinn-Phong specular, hemispheric or probe ambient, emission |
| Lights | Point and spot, punctual, per-fragment loop, 64 per frame max |
| Shadows | Spot: one 2048 D16 quadtree atlas. Point: 4-cube D16 array, 512 per face |
| Baked | Per-zone lightmap atlas (RGB9E5), AO plane (R8), L1 SH probe volumes (RGBA16F 3D) |
| Transparency | Blend materials draw back-to-front per view, depth write off, after opaque |
| Post | Not implemented. Exposure and a tonemap shoulder run inside the forward shader |
| Directional lights | Not implemented |
| Skinning | Compute pre-skin: one dispatch poses vertices, every later pass draws them as static geometry |
| Compute | One pipeline, the skinning pre-pass |

## Documents

Read in this order on a first pass.

| Document | Covers |
|---|---|
| [architecture.md](architecture.md) | Layering, ownership, module topology, dependency graphs, allowed direction of reference |
| [frame.md](frame.md) | The frame from `FramePhase::ExtractRender` to present: what runs where, synchronization objects, swapchain lifecycle |
| [vulkan-backend.md](vulkan-backend.md) | Every service in `GraphicsServices`: bootstrap policy, device floor, memory, descriptors, caches, scratch, barriers |
| [features-and-passes.md](features-and-passes.md) | `IRenderFeature` contract, phase buckets, `MeshForwardPass`, `ShadowDepthPass`, the queue and its sort key |
| [lighting.md](lighting.md) | Light components, extraction and selection, GPU packing, the shading equation, style cvars |
| [shadows.md](shadows.md) | Atlas allocation, the residency arbiter, update policies, caster diff invalidation, sampling and bias |
| [baked-lighting.md](baked-lighting.md) | Lightmap atlas, AO plane, probe volumes, zone-scoped residency, shader sampling |
| [resources.md](resources.md) | Meshes, materials, material sets, textures, bindless slots, ref-counting and hot reload |
| [shaders.md](shaders.md) | GLSL layout, include topology, offline compile and embed, CPU/GPU struct contracts, specialization |
| [instrumentation.md](instrumentation.md) | Profile mode ladder, counters, CPU and GPU scopes, capture export, debug views, the bench harness |
| [golden-images.md](golden-images.md) | The pixel-level regression net: what it catches that headless tests cannot, and what to do when it fails |
| [constraints.md](constraints.md) | Hard limits, invariants, traps, determinism and portability rules |
| [extending.md](extending.md) | Step-by-step recipes: new feature, new pass, new shader, new material parameter, new counter, new debug view |
| [open-work.md](open-work.md) | Known gaps carried forward from the executed renderer plans, with what each is blocked on |

## Fast orientation by file

| You want | Start at |
|---|---|
| Where a frame is driven | `engine/src/app/EngineFramePhases.cpp` |
| Where scene state becomes render data | `engine/src/app/DefaultRenderPipeline.cpp` |
| Where the swapchain image is acquired and presented | `engine/src/graphics/vulkan/VulkanFrameService.cpp` |
| Where phases are recorded | `engine/src/graphics/vulkan/Renderer.cpp` |
| Where opaque geometry is drawn | `engine/src/render/pass/MeshForwardPass.cpp` |
| Where shadow depth is drawn | `engine/src/render/pass/ShadowDepthPass.cpp` |
| Who owns a shadow slot | `engine/src/render/ShadowResidency.cpp` |
| The fragment shading model | `engine/shaders/lighting.glsli`, `engine/shaders/mesh_forward.frag.glsl` |
| The view uniform block | `engine/include/render/pass/MeshForwardPass.h` and `engine/shaders/mesh_view.glsli` |

## Conventions used here

- Paths are repo-relative.
- "Set 0/1/2" always means the descriptor set index in the forward pipeline
  layout. See [vulkan-backend.md](vulkan-backend.md#descriptor-sets).
- Numbers stated as budgets are the reference targets from the executed
  lighting plan, kept here because the counters are still measured against
  them. See [constraints.md](constraints.md#performance-budgets).
- Anything guarded by `SENCHA_ENABLE_RENDER_PROFILING` is called out
  explicitly. That define is ON in `dev` and OFF in the shipping preset.

## Verifying a render change against a GPU

The headless suite covers render *policy*; it cannot tell you whether a change
still records legal Vulkan. That needs a live run with the validation layer, and
two things make a naive attempt silently useless.

**Use a level with runtime shadow-casting lights.** No authored level in the
repo had a single light until `shadow_probe.level.json`, and a light with
`bake_contribution: direct` leaves the runtime forward set entirely — it
schedules no shadow view, so the shadow pass never executes and the run proves
nothing.

**Use enough frames.** Zones load through `AsyncZoneLoader`, so nothing renders
for roughly the first hundred frames. A 30- or 60-frame run exits before the
zone is resident and reports no validation errors because *nothing was
recorded*. Use 300.

```sh
# Cook the fixture. SENCHA_COOK_LEVEL is the authored document path, not a level
# name, and only *.level.json files are authored documents. Run the binary
# directly: ctest truncates the failure message.
SENCHA_COOK_LEVEL=$PWD/template/assets/levels/shadow_probe.level.json \
SENCHA_COOK_ROOT=$PWD/template/assets \
  ./build/test/level_cook_tests --gtest_filter='CookLevel.Generate'

# Run it. The cooked artifact takes the full stem, so the map is
# levels/shadow_probe.level.
cd template && SENCHA_PRESENT_MODE=IMMEDIATE \
  ../build/example/SceneViewer/app +map levels/shadow_probe.level \
  +set app.exit_after_frames 300 2>&1 | grep -E 'VUID-|Validation Error'
```

**A clean validation run says nothing about the picture.** The layers check API
misuse, not whether the right pixels came out, and most of this renderer's draw
paths are content-dependent -- an editor booted with no document reaches almost
none of them. `render_golden_tests` is the detector that does look at the image;
see [golden-images.md](golden-images.md).

**Prove the instrument before trusting a clean result.** A clean run and a run
that never executed your code look identical. Either drop a temporary `printf`
in the path under test, or inject a known-bad call — a `vkCreateBuffer` with
`size = 0` fires `VUID-VkBufferCreateInfo-size-00912` — and confirm it appears.
Synchronization validation specifically is a separate story: it accepts its
enable flag and still emits nothing on this distro's layer build, so a
"syncval clean" result needs its own negative control.
