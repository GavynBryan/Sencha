# Vulkan Backend

Everything under `engine/*/graphics/vulkan`. One service per mechanism, all
owned by value in `GraphicsServices` in construction order.

## Bootstrap policy

`VulkanBootstrapPolicy` (`engine/include/graphics/vulkan/VulkanBootstrapPolicy.h`)
is the single input to instance, physical-device, and device creation. The game
constructs it; nothing reads config below this point.

`GraphicsServices::BuildPolicy` fills it from `EngineGraphicsConfig` plus the
window service:

| Policy field | Source | Default |
|---|---|---|
| `AppName` | `config.App.Name` | `"Sencha"` |
| `ApiVersion` | fixed | `VK_API_VERSION_1_3` |
| `EnableValidation` | `config.Graphics.EnableValidation` | `true` in debug builds, `false` in optimized builds (`NDEBUG`) |
| `ValidateSynchronization` | `config.Graphics.ValidateSynchronization` | `false` |
| `ValidateGpuAssisted` | `config.Graphics.ValidateGpuAssisted` | `false` |
| `ValidateBestPractices` | `config.Graphics.ValidateBestPractices` | `false` |
| `DeviceIndex` | `config.Graphics.DeviceIndex` | `-1` (score normally) |
| `RequiredQueues.Present` | fixed | `true` |
| `RequiredInstanceExtensions` | `SdlWindowService::GetRequiredVulkanInstanceExtensions` | platform WSI set |
| `RequiredDeviceExtensions` | fixed | `VK_KHR_swapchain` |
| `OptionalDeviceExtensions` | fixed | `VK_KHR_present_id`, `VK_KHR_present_wait` |
| `DeviceFeatures.samplerAnisotropy` | constructor | `VK_TRUE` |
| `DeviceFeatures.textureCompressionBC` | constructor | `VK_TRUE` |
| `DeviceFeatures.imageCubeArray` | constructor | `VK_TRUE` |

The policy is built inside `GraphicsServices`, which brings the Vulkan stack up
during `Engine::Initialize`. That runs before any game hook that could construct
a render feature, so a feature cannot influence device creation: it is handed to
the `Renderer` afterwards and acquires its GPU resources in `Setup`. A game that
needs an extra extension or feature bit would need a hook at engine
configuration time, before the device is built. There is no such hook today, and
`IRenderFeature` no longer advertises one.

## Instance and validation

`VulkanInstanceService` creates the instance, and when validation is enabled
also `VK_LAYER_KHRONOS_validation` plus a `VK_EXT_debug_utils` messenger routed
into the engine logger at Warning and Error severity. Messages arrive prefixed
`[Vulkan]`.

The three extra validation switches map to `VkValidationFeatureEnableEXT`
entries chained into `VkInstanceCreateInfo`. They have no effect unless
`EnableValidation` is on, and each is logged when enabled. They cost CPU time in
every frame, which is why they default off: validation alone already costs about
4.8x on CPU render recording.

**Synchronization validation does not report on the development machine's layer
build.** A deliberately deleted barrier produced core-validation layout errors
and zero `SYNC-HAZARD` lines across three enabling paths. Any future "syncval
clean" claim must re-prove the instrument with that same negative control
first. See [constraints.md](constraints.md#the-dead-instrument).

## Device selection floor

`VulkanPhysicalDeviceService::RateDevice` rejects a device (score -1) unless all
of the following hold. This is the renderer's hardware floor; every service
downstream assumes it.

**Vulkan 1.3 core features**

- `synchronization2` (every barrier in the tree is sync2)
- `dynamicRendering` (there are no `VkRenderPass` objects anywhere)
- `shaderDemoteToHelperInvocation` (SPIR-V 1.6 lowers `discard` to
  `OpDemoteToHelperInvocation`; the editor grid shader uses it). Core-mandatory
  in 1.3, but checked here because the device service enables it, and no feature
  is enabled without a matching check.

**Vulkan 1.2 descriptor indexing**

- `descriptorIndexing`
- `runtimeDescriptorArray`
- `descriptorBindingPartiallyBound`
- `descriptorBindingVariableDescriptorCount`
- `descriptorBindingSampledImageUpdateAfterBind`
- `shaderSampledImageArrayNonUniformIndexing`

**Policy features** (`VkPhysicalDeviceFeatures`, only those set in the policy
are required): `samplerAnisotropy`, `textureCompressionBC`, `imageCubeArray`.

**Extensions**: every entry in `RequiredDeviceExtensions`, plus `VK_KHR_swapchain`
when a present queue is required.

**Queues**: the families must satisfy `RequiredQueues`.

Scoring among survivors: `+1000` discrete (when `PreferDiscreteGpu`), `+100`
integrated, `+ maxImageDimension2D / 1024`, `+50` for a compute family distinct
from graphics, `+50` for a transfer family distinct from both. `DeviceIndex >= 0`
overrides scoring entirely and **fails selection** rather than falling back if
that index is out of range or unsuitable, because a silent fallback would
misattribute every measurement taken afterward.

Queue family discovery prefers a dedicated compute family (no graphics bit) and
a dedicated transfer family (neither graphics nor compute), falling back to the
graphics family for both.

## Logical device

`VulkanDeviceService` enables exactly what selection verified: the 1.3 trio, the
1.2 descriptor-indexing set, the policy's `VkPhysicalDeviceFeatures`, and
`presentId` / `presentWait` when their extensions made it into the enabled list.
One queue per unique family at priority 1.0.

## Memory

`VulkanAllocatorService` owns the single `VmaAllocator`. Nothing below it calls
`vkAllocateMemory`. It is a strictly serial resource: call sites that touch it
during frame recording must be on the thread that owns the command buffer.

### Buffers

`VulkanBufferService` owns every `VkBuffer`, addressed by a generational
`BufferHandle` (slot 0 reserved, so a zero-initialized handle is always
invalid).

| `BufferMemory` | VMA usage | Flags | Upload path |
|---|---|---|---|
| `GpuOnly` | `AUTO_PREFER_DEVICE` | `TRANSFER_DST` added automatically | transient staging buffer, one-shot submit, fence wait |
| `HostVisible` | `AUTO_PREFER_HOST` | `HOST_ACCESS_SEQUENTIAL_WRITE \| MAPPED`, `requiredFlags = HOST_COHERENT` | memcpy into the persistent mapping, then `vmaFlushAllocation` |
| `Readback` | `AUTO_PREFER_HOST` | `HOST_ACCESS_RANDOM \| MAPPED` | same as host visible |

The `HOST_COHERENT` requirement on `HostVisible` is load bearing: the frame
scratch writes through its mapped pointer and submits without flushing.
`SEQUENTIAL_WRITE` asks for write-combine-friendly memory and says nothing about
coherency, so without the explicit requirement a driver exposing a non-coherent
host-visible type could satisfy it and every per-frame uniform would reach the
GPU stale. The spec guarantees at least one `HOST_VISIBLE | HOST_COHERENT` type
exists.

`Upload()` is synchronous by design and runs on the **graphics** queue, which
avoids queue-family ownership transfers entirely. Moving to a dedicated transfer
queue is a later optimization behind a release/acquire barrier pair, not an API
change.

### Images

`VulkanImageService` mirrors the buffer service for `VkImage` plus one default
`VkImageView`. It centers on the 90 percent path: 2D color, single array layer,
optional mip chain, whole-image view.

| `ViewType` | Supported operations |
|---|---|
| `VK_IMAGE_VIEW_TYPE_2D` | create, view, `Upload`, `UploadMips`, runtime mip generation |
| `VK_IMAGE_VIEW_TYPE_CUBE_ARRAY` | create, view, clear through a command buffer (what the shadow cube pool needs) |
| `VK_IMAGE_VIEW_TYPE_3D` | create, view, `Upload` as one region spanning all depth slices (what probe volumes need) |

Anything else is rejected. `Upload` and `UploadMips` leave the image in
`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, which is the layout the bindless
descriptor array assumes for every bound image. `UploadMips` takes a packed blob
plus a per-level region table and is the path cooked textures use, since the
runtime never generates mips for cooked content.

### Upload context

`VulkanUploadContextService` is one transient command pool plus one fence on the
graphics family, shared by every service that copies data outside the frame
loop.

```cpp
VkCommandBuffer cmd = upload.Begin();
// record copies / blits / barriers
if (!upload.Submit(cmd)) { /* handle */ }
```

Not thread safe. One pool and one fence means the whole upload path is
serialized. A future concurrent asset streamer grows per-worker pools here; the
public API does not need to change.

The fence wait is bounded (10 s, far beyond any healthy upload) and
error-checked, so a lost or wedged device turns into a reported upload failure
rather than a process hang. On timeout the command buffer is deliberately
leaked, because it may still be pending and freeing a pending buffer is invalid
use.

### Deletion queue

`VulkanDeletionQueueService` is the deferred-destroy ring. Any Vulkan object
freed at runtime routes through it instead of calling `vkDestroyX` directly. It
holds each pending destroy for `framesInFlight + 1` frames.

The correctness argument: `VulkanFrameService::BeginFrame` calls `AdvanceFrame()`
immediately after waiting on this slot's in-flight fence, and that wait proves
all GPU work submitted `framesInFlight` frames ago has retired. The destructor
flushes everything synchronously; `DestroyFrameData` has already called
`vkDeviceWaitIdle` by then.

Adding a resource type means: a `DeferredXxxDestroy` struct, a vector in
`Bucket`, an `EnqueueXxxDestroy`, and a flush loop in both `AdvanceFrame` and
the destructor.

## Frame scratch

`GpuFrameScratch` is one persistently mapped host-visible ring buffer split
into `FramesInFlight` equal slices. `BeginFrame` rotates to the next slice and
resets its bump cursor. Callers write straight through the returned pointer:
no staging, no flush, no fence on the scratch itself.

- Usage flags: `UNIFORM | STORAGE | VERTEX`. Index buffers and transfer-src are
  out of scope.
- Default budget: `EngineGraphicsConfig::FrameScratchBytesPerFrame`, 1 MiB per
  slice. The editor raises it, since it re-uploads the scene for every viewport
  into one slice per frame.
- The per-slice size is padded up to `minUniformBufferOffsetAlignment` so slice
  boundaries themselves land on a legal dynamic-offset base.
- The single backing `BufferHandle` is stable for the service's life. It is what
  `VulkanDescriptorCache::RequireFrameUniformRange` points at during setup.

Four allocation entry points:

| Call | Alignment | Semantics |
|---|---|---|
| `Allocate(size, alignment)` | caller's | all or nothing |
| `AllocateUniform(size)` | `minUniformBufferOffsetAlignment` | all or nothing |
| `AllocateVertex(size)` | 16 | all or nothing |
| `AllocateVertexElements(maxElements, stride)` | 16 | **partial grant**: returns as many whole elements as the slice can still serve |

The partial grant exists because an instance stream that does not fit whole is
the normal case at scene scale, and an all-or-nothing request there means the
caller drops the entire pass. Both passes draw the prefix that fits and count
the rest as `InstancesDropped` / `ShadowCastersDropped`.

The offset arithmetic lives in `FrameScratchRing` (`engine/include/graphics/FrameScratchRing.h`),
which owns no memory and no Vulkan objects, so the boundary conditions that
decide whether a frame renders (exact fit, one byte over, partial grants near
the end of a slice) are unit-testable without a device.

Counters exposed each frame: high water, this frame's used bytes, the per-frame
budget they are measured against, and the number of requests the slice could not
serve.

## Descriptor sets

`VulkanDescriptorCache` owns the two global sets, and `LightBindings` (in the
render layer) owns the third. The forward pipeline layout binds all three; the
shadow pipeline layout binds only the first two.

| Set | Binding | Type | Count | Owner | Contents |
|---|---|---|---|---|---|
| 0 | 0 | `UNIFORM_BUFFER_DYNAMIC` | 1 | `VulkanDescriptorCache` | the frame scratch ring; each draw supplies its own dynamic offset |
| 1 | 0 | `COMBINED_IMAGE_SAMPLER` | 1024 | `VulkanDescriptorCache` | bindless material textures, partially bound, update-after-bind, variable count |
| 2 | 0 | `COMBINED_IMAGE_SAMPLER` | 1 | `LightBindings` | spot shadow atlas, comparison sampled (`sampler2DShadow`) |
| 2 | 1 | `COMBINED_IMAGE_SAMPLER` | 1 | `LightBindings` | point shadow cube array, comparison sampled (`samplerCubeArrayShadow`) |
| 2 | 2 | `COMBINED_IMAGE_SAMPLER` | 24 | `LightBindings` | probe volumes, `sampler3D[8 * 3]`, update-after-bind |
| 2 | 3 | `COMBINED_IMAGE_SAMPLER` | 1 | `LightBindings` | atlas again through a nearest, non-comparison sampler. Profiling builds only |
| 2 | 4 | `COMBINED_IMAGE_SAMPLER` | 1 | `LightBindings` | cube pool again, nearest, non-comparison. Profiling builds only |

Set 0 lives in its own layout because a dynamic UBO binding may not coexist with
`UPDATE_AFTER_BIND` bindings in the same layout (VUID-03001).

Set 1 is allocated at full capacity with a variable-count tail so every slot is
addressable even though partially-bound leaves them empty.
`RegisterSampledImage(image, sampler)` is idempotent and returns a stable slot;
`UpdateSampledImage` repoints a slot in place, which is the hot-reload primitive
(every material whose descriptor index points there renders the new image with
no further work).

Pipeline layouts are cached by push-constant signature. Every layout the cache
returns uses sets 0 and 1; `MeshForwardPass` builds its own three-set layout
directly because set 2 belongs to `LightBindings`.

### The frame UBO range

There is one set 0, and both mesh passes bind it: `ShadowDepthPass` reads
`sizeof(Mat4)` (64 bytes), `MeshForwardPass` reads `sizeof(MeshFrameUniforms)`
(5712 bytes). Each calls `VulkanDescriptorCache::RequireFrameUniformRange` in
its `Setup` to declare what its own shader block covers, and the cache keeps the
largest anyone has declared. The shadow pass then reads the first 64 bytes of a
5712-byte range, which is legal.

It used to be a trap. The call assigned the range rather than declaring a
minimum, so the last writer won and correctness rested on feature registration
order -- the shadow feature registering first in
`DefaultRenderPipeline::AddMeshRenderFeature`, and `ShadowPass.Setup` running
before `Forward.Setup` in the editor, each host carrying the rule in a comment.
Reversing either would have left a 64-byte range under a shader declaring the
larger block. A third pass that binds set 0 now needs no ordering care: declare
the block it reads.

The 8 KiB budget in `constraints.md` is a design line rather than a hardware
limit, so it is reported rather than enforced -- clamping would hand a shader a
range shorter than the block it declares, which is the failure the mechanism
exists to prevent. `RequireFrameUniformRange` warns past it, and
`FrameUniformRangeTests` fails if `MeshFrameUniforms` crosses it.

## Pipelines and shaders

`VulkanShaderCache` owns every `VkShaderModule` behind a generational
`ShaderHandle`.

| Entry point | Availability | Use |
|---|---|---|
| `CreateModuleFromSpirv(words, count, name)` | always | engine-internal shaders, whose SPIR-V is a `constexpr uint32_t[]` baked into the binary at build time. No file IO, no compiler dependency |
| `LoadSpirv(path, stage, name)` | always | pre-compiled `.spv` from disk, for game shaders during development and the future runtime asset loader |
| `LoadFromFile(path, stage)` | `SENCHA_ENABLE_HOT_RELOAD` | compiles GLSL through glslang, writes a `.spv` side-car invalidated by mtime |
| `CompileFromSource(source, stage, name)` | `SENCHA_ENABLE_HOT_RELOAD` | in-memory GLSL, for generated shaders and test fixtures |

glslang links only under `SENCHA_ENABLE_HOT_RELOAD`, which is OFF in release.
Shipping binaries contain no GLSL compiler.

`VulkanPipelineCache` content-hashes a `GraphicsPipelineDesc` (FNV-1a over every
field including the vertex layout, blend state, formats, and fragment
specialization constants), compares the full desc on hash hit, and owns every
`VkPipeline` it hands out. Callers must not destroy them.

Fixed pipeline state for every pipeline the cache creates: viewport and scissor
are dynamic (count 1 each), rasterization line width 1.0, multisample count 1,
no stencil. Everything else comes from the desc. `VkPipelineRenderingCreateInfo`
carries the color and depth formats, since there are no render pass objects.

There is no cross-run pipeline cache persistence: the driver-side
`VkPipelineCache` dedups within a process, prewarming at load hides the compile
cost, and desktop drivers maintain their own on-disk shader caches. A
persistence API existed briefly, was never wired, and was deleted (see
[open-work.md](open-work.md#resolved-2026-07-24)).

`VulkanSamplerCache` deduplicates `VkSampler` by `SamplerDesc` value and hands
back raw handles, which is deliberate: samplers outlive any single caller, are
shared across draws, and go straight into descriptor writes. Do not destroy
anything it returns.

## Compute

`VulkanPipelineCache::GetComputePipeline` mirrors the graphics path over a
`ComputePipelineDesc` -- a shader handle and a layout, because a compute
pipeline has no other state. Entries are compared linearly rather than
hashed: there are a handful of them and the desc compares in two loads.

The one consumer is `SkinnedPosePass`, the pre-skin dispatch (pipeline
Decision N). It records in the Offscreen phase, which opens no rendering
scope, so a compute-only feature needs no new phase and no special casing;
it owns its own descriptor pools (one per frame in flight, reset before
that slot's jobs allocate) and its own barrier.

## Barriers

`VulkanBarriers` is a namespace of free functions, not a service: barriers are a
hot-path concern and the caller owns the command buffer. Everything is sync2,
which the device floor guarantees.

Buffer-visibility barriers are written inline by the pass that needs them --
there is one (`SkinnedPosePass`, compute storage writes to vertex-attribute
reads) and a helper for a single caller would be indirection, not
abstraction. `VulkanBarriers` stays image-only until a second consumer
appears.

`ImageTransition` carries the full pair of scopes explicitly. Convenience
wrappers exist for the swapchain image transitions (`TransitionForColorAttachment`,
`TransitionFromColorAttachmentToPresent`, and the clear pair).

**Never name `TOP_OF_PIPE` as a first scope when you mean "order against
previous writes".** `TOP_OF_PIPE` names no stage, so the first scope is empty
and the barrier orders nothing. The per-frame depth barrier in
`Renderer::RecordMainColorPhase` names the early and late fragment-test stages
with depth attachment read/write access precisely because one depth image serves
every frame in flight and a frame only waits on the fence two slots back.

## Depth target

`VulkanDepthTarget` owns the main color pass's depth image and view. Format is
chosen at `Create()` by probing the device, preferring `D32_SFLOAT` with
D24/D32-stencil fallbacks. `Recreate()` is a no-op when the extent has not
changed, so it is safe to call every frame.

Depth convention: standard Vulkan `[0,1]` depth with a Y flip in the projection
matrix. Not reversed Z. `VK_COMPARE_OP_LESS_OR_EQUAL` everywhere, depth clear
value 1.0.

## Debug labels and timestamps

`VulkanDebugLabels` resolves `VK_EXT_debug_utils` entry points once per process
and no-ops when the extension is absent. When render profiling is compiled out,
inline stubs take over so call sites carry no `#ifdef`. Command labels are
emitted only while the profile mode is Gpu or above; object names are
creation-time metadata and cost nothing per frame (the forward and shadow
pipelines are named at creation).

`GpuTimestampPool` holds one `VkQueryPool` per frame in flight, two timestamps
per `GpuScope`. Pools are created lazily on the first frame at mode Gpu or
above, and kept until `Destroy()`. Collection runs at frame begin because the
frame service has already fence-waited that slot there, so `vkGetQueryPoolResults`
never needs `WAIT`; spans whose pair is unavailable read as invalid.
