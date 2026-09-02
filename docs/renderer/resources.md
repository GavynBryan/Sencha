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
