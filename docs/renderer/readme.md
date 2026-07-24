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
synchronization2, descriptor indexing). One opaque forward pass over a sorted,
instanced draw queue, preceded by an offscreen depth-only shadow phase. Lighting
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
| Transparency | Not implemented. Blend materials warn and render opaque |
| Post | Not implemented. Exposure and a tonemap shoulder run inside the forward shader |
| Directional lights | Not implemented |
| Skinning | Mesh data and caches exist; no GPU skinning pass yet |
| Compute | No compute pipelines |

## Documents

Read in this order on a first pass.

| Document | Covers |
|---|---|
| [architecture.md](architecture.md) | Layering, ownership, module topology, dependency graphs, allowed direction of reference |
| [frame.md](frame.md) | The frame from `FramePhase::ExtractRenderPacket` to present: what runs where, synchronization objects, swapchain lifecycle |
| [vulkan-backend.md](vulkan-backend.md) | Every service in `GraphicsServices`: bootstrap policy, device floor, memory, descriptors, caches, scratch, barriers |
| [features-and-passes.md](features-and-passes.md) | `IRenderFeature` contract, phase buckets, `MeshForwardPass`, `ShadowDepthPass`, the queue and its sort key |
| [lighting.md](lighting.md) | Light components, extraction and selection, GPU packing, the shading equation, style cvars |
| [shadows.md](shadows.md) | Atlas allocation, the residency arbiter, update policies, caster diff invalidation, sampling and bias |
| [baked-lighting.md](baked-lighting.md) | Lightmap atlas, AO plane, probe volumes, zone-scoped residency, shader sampling |
| [resources.md](resources.md) | Meshes, materials, material sets, textures, bindless slots, ref-counting and hot reload |
| [shaders.md](shaders.md) | GLSL layout, include topology, offline compile and embed, CPU/GPU struct contracts, specialization |
| [instrumentation.md](instrumentation.md) | Profile mode ladder, counters, CPU and GPU scopes, capture export, debug views, the bench harness |
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
| Where opaque geometry is drawn | `engine/src/render/MeshForwardPass.cpp` |
| Where shadow depth is drawn | `engine/src/render/ShadowDepthPass.cpp` |
| Who owns a shadow slot | `engine/src/render/ShadowResidency.cpp` |
| The fragment shading model | `engine/shaders/lighting.glsli`, `engine/shaders/mesh_forward.frag.glsl` |
| The frame uniform block | `engine/include/render/MeshForwardPass.h` and `engine/shaders/mesh_frame.glsli` |

## Conventions used here

- Paths are repo-relative.
- "Set 0/1/2" always means the descriptor set index in the forward pipeline
  layout. See [vulkan-backend.md](vulkan-backend.md#descriptor-sets).
- Numbers stated as budgets are the reference targets from the executed
  lighting plan, kept here because the counters are still measured against
  them. See [constraints.md](constraints.md#performance-budgets).
- Anything guarded by `SENCHA_ENABLE_RENDER_PROFILING` is called out
  explicitly. That define is ON in `dev` and OFF in the shipping preset.
