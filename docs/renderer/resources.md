# Render Resources

Meshes, materials, material sets, and textures. All four are ref-counted asset
caches built on `AssetCache<Derived, Handle, Entry>`, keyed by asset path, and
handing out generational handles.

## Handles

Every render resource handle is a `Handle<Tag>` (index plus generation, slot 0
reserved so a zero-initialized handle is invalid):

| Handle | Owner | Backs |
|---|---|---|
| `StaticMeshHandle` | `StaticMeshCache` | `GpuStaticMesh` |
| `MaterialHandle` | `MaterialCache` | `Material` |
| `MaterialSetHandle` | `MaterialSetCache` | ordered list of `MaterialHandle` |
| `TextureHandle` | `TextureCache` | `ImageHandle` plus a bindless slot |
| `BufferHandle` | `VulkanBufferService` | `VkBuffer` plus its VMA allocation |
| `ImageHandle` | `VulkanImageService` | `VkImage` plus a default view |
| `ShaderHandle` | `VulkanShaderCache` | `VkShaderModule` |

None of these are stable across a save. Scene data persists **asset paths**, and
the loaders resolve them to handles at load. Do not serialize a handle.

`Owned<T>` (`StaticMeshCacheHandle`, `MaterialCacheHandle`,
`TextureCacheHandle`) is the RAII form: acquiring one retains, dropping it
releases.

## Geometry

```
MeshGeometry           CPU: vertices, indices, local bounds, sections
  -> UploadMeshGeometryToGpu
GpuStaticMesh          GPU: vertex buffer, index buffer, counts, bounds, sections
```

`MeshGeometry` is the shared core. Static meshes **are** this; skinned meshes
embed it and add a separate skinning stream plus a skeleton reference. Skinning
influences are never interleaved into `StaticMeshVertex`, so the static vertex
layout is byte-identical whether or not a mesh is skinned.

`StaticMeshVertex`:

| Field | Type | Notes |
|---|---|---|
| `Position` | `Vec3d` (float3) | |
| `Normal` | `Vec3d` | |
| `Uv0` | `Vec2d` | material UV |
| `Tangent` | `Vec4` | xyz tangent, w handedness. glTF convention: `bitangent = cross(N, T.xyz) * T.w`. Generated at cook by MikkTSpace when the source lacks them |
| `LightmapU`, `LightmapV` | `uint16` | unorm16, read as `R16G16_UNORM` |

Sections (`StaticMeshSection`) carry an index range, a vertex range, a material
slot, and local bounds. Indices are always `uint32`.

**A mesh may have at most 32 sections** (`kMaxMeshSections`). The limit is not
arbitrary: `StaticMeshComponent::SectionMask` is a `uint32` and extraction
shifts by the section index to test it, so a 33rd section would shift past the
mask's width. The cap is enforced at cook, serialize, load, and upload, and a
`static_assert` in `StaticMeshComponent.h` ties the two together.

`StaticMeshCache::ReloadInPlace` swaps geometry on a resident entry, keeping the
slot, generation, refcount, and handle, and retires the old GPU buffers through
the deletion queue.

## Materials

`Material` (`engine/include/render/Material.h`) is the runtime form of `.smat`
data. Texture slots hold **bindless descriptor indices**, not handles;
`UINT32_MAX` means no texture, and the shader substitutes the slot's neutral
default:

| Slot unbound | Shader default |
|---|---|
| base color | `pushData.BaseColor` alone |
| normal | the interpolated geometric normal, no TBN work |
| ORM | `(1, 1, 0)`: unoccluded, fully rough, non-metallic |
| emissive | `vec3(1)` scaled by the emissive factor |
| lightmap | baked direct term is zero |
| AO | 1.0, ambient unmodulated |

A material with no textures is therefore complete, not broken.

Fields that drive renderer behavior rather than shading math:

| Field | Effect |
|---|---|
| `Pass` | `ShaderPassId`, classified at material load. `ForwardOpaque` items sort for state; `ForwardTransparent` items order per view, because order is their correctness |
| `Shading` | `StandardLit` or `Unlit`, selects the specialization constant |
| `DoubleSided` | selects the cull-none pipeline in both the forward and shadow passes |
| `ReceiveShadows` | pushed per draw; the shader returns full visibility when clear |
| `CastShadows` | filters sections out of the caster set, and feeds the caster state hash |
| `AlphaMode` | `Mask` selects a masked pipeline variant; the fragment shader discards below `AlphaCutoff`. `Blend` classifies the material into `ForwardTransparent` at load: drawn after every opaque item, back-to-front per view, depth test on, depth write off, straight-alpha over |
| `AlphaCutoff` | pushed per draw at offset 76, read by the masked variants only. **Shadow casters ignore it**: `ShadowCasterItem` carries no material and the shadow vertex layout carries no UVs, so a masked surface still casts its whole silhouette |

`MaterialCache` entries own the `TextureCacheHandle` references their descriptor
indices point at, so releasing a material releases its textures. The ownership
is type-erased through `ILifetimeOwner`, which keeps `MaterialCache.h` free of
backend headers.

### Material sets

`MaterialSetCache` exists because a `StaticMeshComponent` must bind one material
per mesh section and still be trivially copyable (archetype rows are memcpy'd).
The component holds an 8-byte `MaterialSetHandle`; the variable-length array
lives in the cache.

Sets are content-deduplicated on the ordered member handles, so two placements
that resolve to the same materials share one entry. A set retains a reference to
each member for its lifetime.

Material assignment is per instance by design, not baked into the mesh asset, so
the same mesh can be placed and reskinned independently. Instancing, array and
mirror modifiers, and reused baked tiles all depend on that.

Section-to-material resolution during extraction:

```cpp
slot = mesh->Sections[sectionIndex].MaterialSlot;
handle = slot < set->size() ? (*set)[slot] : set->back();
```

The clamp to `back()` means a mesh with more material slots than the set
provides renders with the last material rather than dropping sections.

## Textures

`TextureCache` composes an `ImageHandle` with a sampler into a bindless slot.
`GetBindlessIndex` returns the index materials write into their descriptor
slots.

| Entry point | Dedup | Use |
|---|---|---|
| `Acquire(path, sampler)` | by path | filesystem load. The sampler is consulted only on the first load for a path |
| `CreateFromImage(image, sampler, name)` | none | direct upload from a CPU `Image` |
| `CreateFromImage(name, image, sampler)` | by name | `AssetSystem` registers loaded bytes under the asset path |
| `CreateFromTextureData(name, texture, sampler)` | by name | cooked textures: explicit mip chain, block-compressed formats included |

The runtime never generates mips for cooked content;
`CreateFromTextureData` takes the format-tagged, mip-tabled `TextureData` as-is
and uploads it through `VulkanImageService::UploadMips`.

`SamplerForTextureData` is the one place `TextureData` maps to Vulkan sampler
state: nearest-filtered textures (pixel art) sample point-filtered at every
stage with anisotropy off; everything else keeps linear/repeat.

Format mapping lives in `engine/src/graphics/vulkan/TextureCache.cpp`, including
`RGB9E5 -> VK_FORMAT_E5B9G9R9_UFLOAT_PACK32` and `R8 -> VK_FORMAT_R8_UNORM`,
which are what the baked lightmap atlas and AO plane use.

`GetGpuImage` is **not stable across a hot reload**: `ReloadInPlace` swaps the
entry's image. A caller holding a view or descriptor built from it must compare
against the current value each frame and rebuild when it changes.

## Render targets

`RenderTargetStore` owns offscreen colour and depth images, resized on request and
retired through the deletion queue and the descriptor cache's gate. A target's
`RenderTargetLifetime` says how many images it needs.

`PerFrameInFlight` keeps one image per frame slot, for a reader that samples an
earlier frame's image while the next one renders, as the UI does with a viewport.

`WithinFrame` keeps one image for every frame in flight. `RenderTargetAccess` records the
layout, stage, and access that last touched it, and `RenderTargetSession` names them in a
barrier before each write.

### Why one image is correct

Frames overlap on the GPU, so this rests on Vulkan's rules rather than on fences. Each step
below is an execution or memory dependency the specification defines.

1. **One queue, one order.** Every frame's commands reach one graphics queue through
   `vkQueueSubmit`, and submission order runs across command buffers and batches on a queue.
   Uploads use that queue too. No code submits to a compute or transfer queue, and nothing
   records a secondary command buffer. `RenderTargetStore::Create` asserts no second queue
   was required.
2. **Barriers reach across frames.** The first synchronization scope of a pipeline barrier
   outside a render pass includes all commands earlier in submission order, not just those in
   its own command buffer. So frame N+1's write barrier orders against frame N's commands with
   no semaphore or fence.
3. **Write to sample, inside a frame.** `WithinFrameReadBarrier` has `COLOR_ATTACHMENT_OUTPUT`
   with `COLOR_ATTACHMENT_WRITE` as its first scope and `FRAGMENT_SHADER` with
   `SHADER_SAMPLED_READ` as its second. That makes the write available, makes it visible to
   the read, and performs the layout transition between the two scopes.
4. **Sample to the next write.** `WithinFrameWriteBarrier` has `FRAGMENT_SHADER` as its first
   scope, the stage the last read ran in. A read needs no availability operation, so an
   execution dependency is enough for it. The barrier's layout transition is itself a write,
   ordered after the first scope and before the second, and `COLOR_ATTACHMENT_OUTPUT` with
   `COLOR_ATTACHMENT_WRITE` covers the next frame's load operation and draws. The old layout is
   `UNDEFINED` because the contents are cleared.
5. **No gap when nothing sampled.** Step 3's second scope and step 4's first scope both name
   `FRAGMENT_SHADER`, so they form a dependency chain even if no draw read the image.
6. **Frames that do not record.** A presentation that was not acquired records nothing and
   leaves `RenderTargetAccess` as it was, so its next barrier still names the last real access.
7. **The first use after a resize.** A new image has a fresh `RenderTargetAccess`, whose
   `NONE` scope is correct because nothing has touched it. The old image and its bindless slot
   go through the deletion queue and the descriptor cache's gate, which wait on the fences of
   every frame that could reference them, so memory is never reused under a frame still running.
   Fences matter only here, for CPU-side lifetime.
8. **Stale descriptors.** The bindless binding is `PARTIALLY_BOUND` and `UPDATE_AFTER_BIND`, so
   layout rules apply to what a draw dynamically accesses. A pass samples the image only after
   step 3.

The cost is that the next frame's scene write waits for this frame's read, the same ordering
the shared depth image imposes.

Async compute, a second queue, secondary command buffers recorded on other threads, or CPU
readback of the target would each break step 1 or 2, so adopting one means revisiting this.

### What verifies it

`WithinFrameBarriers` tests pin the stages, accesses, and layouts above, including the chain in
step 5, so a change to any of them fails without a device. `SceneTargetSynchronizationTest` runs
under synchronization validation at two and three frames in flight, through resizes and a
minimize, with the effect chain active. That run fails if step 3's barrier loses its first
scope, but it still passes if step 4's does: the layer does not track reads through the bindless
array. Between frames, the argument above and those unit tests are the evidence, not the layer.

## Hot reload

Three caches implement in-place reload, all preserving the handle, slot,
generation, and refcount so nothing downstream is invalidated:

| Cache | What is swapped | Old resource |
|---|---|---|
| `StaticMeshCache::ReloadInPlace` | vertex and index buffers | deletion queue |
| `MaterialCache::ReloadInPlace` | the `Material` value and its owned texture references | none: a material owns no GPU resource of its own |
| `TextureCache::ReloadInPlace` | the GPU image, with the **same bindless index** repointed at it | deletion queue |

The texture case is the interesting one:
`VulkanDescriptorCache::UpdateSampledImage` rewrites the slot in place, so every
material whose descriptor index points there renders the new pixels with no
further work. It is legal while frames are in flight because binding 1 is
`UPDATE_AFTER_BIND`, and the old image stays alive in the deletion queue until
in-flight frames retire.

## Component lifetime

`ComponentTraits` hooks tie GPU residency to ECS membership, so no system has to
remember to retain or release:

| Component | `OnAdd` | `OnRemove` |
|---|---|---|
| `StaticMeshComponent` | retains mesh and material set | releases material set, then mesh |
| `ZoneLightmapComponent` | retains the atlas and AO textures | releases both |

Both are `SchemaAssetOwnership` traits: the fields are the ones the schema
tags `.AsAsset()`, and the stores are found in the World's `AssetStoreTable`
resource. If the table is absent, the hooks do nothing, which is what lets
tests build worlds with no graphics services.
