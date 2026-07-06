# RHI Backend Seam: Design and Migration Plan

Status: **approved design, not yet started**. Supersedes the "Second RHI and
further platforms" deferral in `engine-roadmap.md` Section 11 by owner
decision (2026-07-06). The roadmap's supporting claim remains true and is the
reason this plan is cheap: the render-domain vs `graphics/vulkan` split
already isolates the backend, with exactly three known leak points.

Decisions fixed by the owner, not revisited here:

1. Backend selection is compile-time, one API per binary, via CMake.
2. D3D12 is the second target. D3D11 is analyzed and descoped (Section 8).
3. No virtual dispatch and no interface types in the RHI. `IRenderFeature`
   is the one virtual seam and it stays (game-binary boundary, already
   earned under directive 4).
4. This document is the deliverable of the planning pass. Code lands via
   the migration steps in Section 6, each independently green.

---

## 1. The mechanism: link-time backend selection behind neutral concrete types

### 1.1 The three candidate mechanisms

**(a) Link-time selection.** One set of neutral headers in
`engine/include/rhi/` declares concrete types (`rhi::BufferService`,
`rhi::CommandList`, value structs, enums) and their member and free
functions. Each backend directory (`engine/src/rhi/vulkan/`,
`engine/src/rhi/d3d12/`) provides the definitions for the same declarations.
CMake compiles exactly one backend's sources into the binary. Backend-private
state lives behind two shapes: long-lived services hold a `State*` to a
backend-defined struct (the physics firewall pattern, already proven in this
repo), and hot value types (`rhi::CommandList`) hold a fixed opaque native
word that the backend casts.

**(b) Preprocessor-selected typedef header.** `rhi/Rhi.h` does
`#if SENCHA_RHI_VULKAN` then includes backend headers and aliases
`namespace rhi { using BufferService = VulkanBufferService; }`. Zero
indirection, full inlining. But every render-layer translation unit then
transitively includes `vulkan/vulkan.h` today and `d3d12.h` plus `windows.h`
tomorrow. `windows.h` contamination of the entire render layer (macro
pollution: `min`, `max`, `CreateWindow`, `near`, `far`) is a concrete cost,
compile times regress across the whole engine, and the isolation firewall
can no longer be "no Vk token outside the backend directory" because every
TU legitimately sees the tokens. Accidental direct use of `VkFormat` in the
render layer would compile; only textual review catches it.

**(c) CRTP / template-parameter threading.** A `Backend` template parameter
threaded through `Renderer<B>`, `MeshForwardPass<B>`, `FrameContext<B>`, and
every feature. This is what `AssetCache` does for a single small type, but
here it infects the whole render layer's spelling, forces headers to carry
definitions, and makes `IRenderFeature` (which must stay a runtime seam at
the game boundary) awkward: a virtual interface cannot take a template-typed
`FrameContext<B>` without either instantiating the interface per backend or
erasing the type again at the boundary. It buys inlining that (a) mostly
recovers via LTO, at the cost of making every consumer uglier.

### 1.2 Decision: (a), link-time selection

Justification against the codebase's own rules:

- **Directive 4 (earn every abstraction).** Option (a) adds no new
  abstraction kind to the codebase. It is the physics firewall pattern
  applied to the renderer boundary, which CLAUDE.md lists explicitly as a
  real boundary ("a renderer or platform boundary"). No interfaces, no
  factories, no adapters. The seam is a directory plus a linker decision.
- **No virtual dispatch.** Nothing is virtual. A missing backend function is
  a link error, which is stronger and earlier than any runtime check.
- **Drift prevention is structural.** With one shared set of headers there
  is nothing to drift: both backends implement the same declarations or the
  binary does not link. This is better than a concept pinning two parallel
  header sets, because a concept checks presence and signature compatibility
  but not that the declarations are identical (default arguments,
  `[[nodiscard]]`, overload sets can still diverge across duplicated
  headers). Option (a) makes divergence unrepresentable.
- **The firewall stays textual and airtight.** Render-layer TUs never see
  `vulkan/vulkan.h` or `windows.h`. A grep-based ctest (Section 6, step 0)
  enforces "no `Vk`/`ID3D12` token outside `rhi/<backend>/`" exactly like
  `CheckPhysicsIsolation.cmake` enforces "no `JPH::` outside `src/physics/`".
- **Concurrency and determinism.** The RHI adds no threads, no lane, no
  locks. All RHI calls happen where the equivalent Vulkan calls happen
  today: on the render path driven by `FramePhase::Render`, plus the
  existing upload and deletion frame-phased queues, which stay
  backend-internal. Serial vs parallel behavior is unaffected because
  nothing about dispatch changes at runtime.

Cost accepted: services gain one `State*` indirection (one pointer hop on
calls that already cross into driver code; services are constructed once and
features cache pointers per the existing `RendererServices` contract, so
this is noise). `rhi::CommandList` recording functions are ordinary
non-virtual calls defined in a backend TU; without LTO they do not inline,
but each one wraps a `vkCmd*` or `ID3D12GraphicsCommandList` call anyway.
Enable LTO on shipping configs if profiling ever shows it matters; do not
pre-optimize.

### 1.3 Surface pinning

Two pins, both cheap:

1. **The linker.** Identical declarations, one definition set per binary.
   Missing or mismatched implementations fail the build.
2. **A conformance test file** (`test/rhi/RhiSurfaceConformance.cpp`)
   containing `static_assert`s for the value-type contracts the linker
   cannot check: `std::is_trivially_copyable_v<rhi::CommandList>`,
   `sizeof(rhi::CommandList) <= 16`, layout pins on the enums, and the
   defaulted `operator==` expectations that pipeline-desc hashing depends
   on. This TU compiles under whichever backend is selected, so both
   backends are held to it by CI legs. A C++20 `concept` is not needed
   because there are no template consumers of the backend; add one only if
   a real template consumer appears.

Behavioral conformance (not just shape) is pinned by running the same
RHI-level tests against each backend in CI, the same way the test suite plus
the llvmpipe render gate pins Vulkan today.

### 1.4 How `IRenderFeature` interacts

`IRenderFeature` remains a virtual interface, unchanged in kind. Its
signatures become fully neutral:

```cpp
class IRenderFeature
{
public:
    virtual ~IRenderFeature() = default;
    [[nodiscard]] virtual RenderPhase GetPhase() const = 0;
    virtual void Contribute(rhi::DeviceBootstrapPolicy&) {}
    virtual void Setup(const RendererServices& services) = 0;   // neutral pointers
    virtual void OnDraw(const FrameContext& frame) = 0;         // rhi::CommandList inside
    virtual void Teardown() {}
};
```

Game binaries implement features against neutral types only. The runtime
dispatch (virtual, at the game boundary) and the backend dispatch
(link-time, at the renderer boundary) are orthogonal and never meet: a
feature compiled for the Vulkan binary calls the Vulkan definitions of the
rhi functions because that is what the binary links.

### 1.5 Runtime multi-backend later, without changing the RHI

Because selection is at link time, a future runtime choice layers on without
touching the RHI surface: either the launcher (`kettle`) selects which
per-backend game binary to start, or the engine ships as per-backend shared
libraries (`sencha_engine_vk.so`, `sencha_engine_d3d12.dll`) and the loader
picks one before any rhi symbol is bound. Neither requires virtuals or
changes to a single neutral header. This is the sanctioned path; do not
build it now.

---

## 2. Directory and header layout

```
engine/include/rhi/
    RhiTypes.h              # Format, Extent2D, CompareOp, CullMode, BlendFactor,
                            # PrimitiveTopology, TextureUsage, BufferUsage (enums + tiny values)
    RhiHandles.h            # aggregates BufferHandle, ImageHandle, ShaderHandle,
                            # SamplerHandle, PipelineHandle over core/handle/Handle.h
    GraphicsPipelineDesc.h  # neutral desc structs (moved + scrubbed from VulkanPipelineCache.h)
    BufferService.h         # class rhi::BufferService { ...; struct State; State* S; };
    ImageService.h
    SamplerCache.h
    ShaderCache.h
    PipelineCache.h
    DescriptorCache.h       # bindless registry + engine-standard set layouts (Section 5)
    FrameScratch.h
    UploadContext.h
    SwapchainService.h
    FrameService.h          # acquire / begin / end / submit / present, frames in flight
    CommandList.h           # struct CommandList { void* Native; }; + recording free functions
    DeviceBootstrapPolicy.h # neutral policy + backend escape hatch (Section 2.3)
    DeviceService.h         # wait-idle, capability queries; State* like the rest

engine/include/graphics/   # neutral render front end (moved out of graphics/vulkan/)
    Renderer.h              # Renderer, RenderPhase, RendererServices, FrameContext, IRenderFeature
    TextureCache.h
    ShaderMetadata.h
    TimingSampler.h
    GraphicsServices.h      # the composition root struct; construction order preserved

engine/src/rhi/vulkan/      # all current graphics/vulkan sources, renamed/moved.
                            # Instance/Surface/PhysicalDevice/Queue/Allocator/DeletionQueue/
                            # Barriers/DepthTarget stay backend-internal files here.
engine/src/rhi/d3d12/       # future; identical public surface, its own internals
engine/src/rhi/null/        # step 5: no-op definitions for headless/CI
```

Rules:

- The render layer (`engine/src/render/`) and orchestration include only
  `rhi/*.h` and `graphics/*.h`. Nothing outside `engine/src/rhi/<backend>/`
  includes an API header (`vulkan/vulkan.h`, `d3d12.h`, `dxgi1_6.h`,
  `vk_mem_alloc.h`).
- Backend-internal headers live beside their sources under
  `src/rhi/<backend>/` and are never installed. This removes the current
  public `vk_mem_alloc.h` install once the move completes.
- Native-handle escape hatches are permitted only inside allowlisted files
  (the debug UI backend, tests); the allowlist lives in the firewall check,
  on the record, exactly like the physics one.

### 2.1 The service shape

Every neutral service keeps its current name minus the `Vulkan` prefix and
its current API minus Vk types. The pattern (one hop, no virtual, no
allocation per call):

```cpp
// rhi/BufferService.h (neutral, complete public surface)
class BufferService
{
public:
    BufferService(LoggingProvider& logging, DeviceService& device /*...*/);
    ~BufferService();
    // non-copyable, non-movable, same as today

    [[nodiscard]] BufferHandle Create(const BufferCreateInfo& info);
    void Destroy(BufferHandle handle);
    bool Upload(BufferHandle handle, const void* data, uint64_t size, uint64_t offset = 0);
    [[nodiscard]] uint64_t GetSize(BufferHandle handle) const;

private:
    struct State;   // defined per backend in src/rhi/<backend>/BufferService.cpp
    State* S = nullptr;
};
```

`BufferCreateInfo` scrubs `VkDeviceSize` to `uint64_t` and
`VkBufferUsageFlags` to a neutral `rhi::BufferUsage` flag enum;
`BufferMemory` is already neutral and moves as is.

### 2.2 `rhi::CommandList` and `FrameContext`

`CommandList` is a trivially copyable value wrapping the backend's native
recorder:

```cpp
// rhi/CommandList.h
struct CommandList
{
    void* Native = nullptr;   // VkCommandBuffer | ID3D12GraphicsCommandList*
    [[nodiscard]] bool IsValid() const { return Native != nullptr; }
};

// Recording surface: free functions defined per backend. The initial verb
// set is exactly what the render layer records today; grow on demand,
// never speculatively.
void CmdBindGraphicsPipeline(CommandList cmd, PipelineHandle pipeline);
void CmdBindFrameSet(CommandList cmd, DescriptorCache& descriptors, uint32_t dynamicUniformOffset);
void CmdBindBindlessSet(CommandList cmd, DescriptorCache& descriptors);
void CmdPushConstants(CommandList cmd, ShaderStage stages, const void* data, uint32_t size);
void CmdBindVertexBuffer(CommandList cmd, uint32_t binding, BufferHandle buffer, uint64_t offset);
void CmdBindIndexBuffer(CommandList cmd, BufferHandle buffer, uint64_t offset, IndexType type);
void CmdSetViewportScissor(CommandList cmd, Extent2D extent);
void CmdDrawIndexed(CommandList cmd, uint32_t indexCount, uint32_t instanceCount,
                    uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
// Offscreen-phase features additionally need:
void CmdBeginRenderTarget(CommandList cmd, const RenderTargetBeginInfo& info);
void CmdEndRenderTarget(CommandList cmd);
void CmdTransitionForSampling(CommandList cmd, ImageHandle image);
```

Free functions rather than methods keep `CommandList` trivially copyable and
match the codebase register (values, free functions, concrete types).
Buffer and pipeline parameters are handles, which the backend resolves
through its own services; the Vulkan definitions are one-line casts plus the
existing `vkCmd*` calls.

Barriers: the render layer never gets a raw barrier API. It gets named
transitions for the cases it actually has (an offscreen target rendered then
sampled). Raw `VulkanBarriers` stays backend-internal. D3D12 implements the
same named transitions with resource barriers. This keeps the surface narrow
and prevents the render layer from encoding Vulkan's synchronization model.
These verbs are also the primitive a future render graph (Section 9) would
drive: the graph decides which transitions to emit and when, and calls these
same functions.

`FrameContext` becomes:

```cpp
struct FrameContext
{
    rhi::CommandList Cmd;
    uint32_t FrameInFlightIndex = 0;
    rhi::Extent2D TargetExtent{};
    rhi::Format TargetFormat = rhi::Format::Undefined;
    rhi::ImageHandle DepthImage;
    rhi::Format DepthFormat = rhi::Format::Undefined;
    RenderPhase Phase = RenderPhase::MainColor;
};
```

`RendererServices` keeps its exact shape with neutral pointer types
(`rhi::BufferService*` and so on) and `rhi::Format DepthFormat`.

### 2.3 `DeviceBootstrapPolicy`

`VulkanBootstrapPolicy` generalizes to:

```cpp
struct DeviceBootstrapPolicy
{
    std::string AppName = "Sencha";
    bool EnableValidation = true;         // Vulkan validation layers | D3D12 debug layer
    bool PreferDiscreteGpu = true;
    QueueRequirements RequiredQueues;     // Graphics/Present/Compute/Transfer, unchanged
    // Neutral capability requests replace VkPhysicalDeviceFeatures fields as
    // they are actually needed (anisotropy and BC textures are baseline on
    // both targets and become unconditional backend requirements, not policy).
    struct BackendExtras;                 // escape hatch: defined per backend
    BackendExtras* Extras = nullptr;      // null means defaults; owned by the caller
};
```

The escape hatch is the same opaque-state trick: a feature or game that
genuinely needs a Vulkan extension defines and populates `BackendExtras`
from a backend-allowlisted TU. Today's instance and device extension lists
move into the Vulkan backend's own bring-up defaults. Audit whether any
game-side `Contribute()` actually adds extensions before building the extras
hatch at all (open question 4).

---

## 3. Type inventory

| Type / surface | Today | Disposition |
|---|---|---|
| `Handle<Tag>`, `BufferHandle`, `ImageHandle`, `ShaderHandle`, `TextureHandle`, `MaterialHandle` | `core/handle/` | Stays. Already the RHI handle vocabulary. `rhi/RhiHandles.h` only aggregates declarations. |
| `Owned<H>` / `ILifetimeOwner` | `core/handle/` | Stays. Backend services keep implementing `ILifetimeOwner`. |
| `BufferMemory`, `ShaderStage`, `SamplerDesc` | `graphics/vulkan/` headers | Moves to `rhi/RhiTypes.h` (already Vk-free or trivially so; verify `SamplerDesc` field types during step 1). |
| `BufferCreateInfo` | `VulkanBufferService.h` | Moves + scrubbed: `VkDeviceSize` to `uint64_t`, `VkBufferUsageFlags` to `rhi::BufferUsage`. |
| `GraphicsPipelineDesc`, `VertexInputBindingDesc`, `VertexInputAttributeDesc`, `ColorBlendAttachmentDesc` | `VulkanPipelineCache.h` | Moves + scrubbed: `VkFormat` to `rhi::Format`; blend, compare, topology, cull, front-face, polygon enums to rhi enums; `VkColorComponentFlags` to `rhi::ColorWriteMask`; `VkPipelineLayout Layout` replaced per open question 1. Defaulted `operator==` and content hashing survive unchanged. |
| `rhi::Format` and friends | new | New. Enumerate only formats the engine uses (swapchain, depth, vertex attributes, cooked BC formats). Do not mirror all of `VkFormat`. Backend maps with a small closed switch (sanctioned: tiny format distinction). |
| `rhi::CommandList` + `Cmd*` free functions | new (replaces raw `VkCommandBuffer` in `FrameContext`) | New. Section 2.2. |
| `PipelineHandle`, `SamplerHandle` | raw `VkPipeline` / `VkSampler` | New handles. `PipelineCache` returns `PipelineHandle`; sampler handles come from `SamplerCache`. |
| `BufferService`, `ImageService`, `SamplerCache`, `ShaderCache`, `PipelineCache`, `DescriptorCache`, `FrameScratch`, `UploadContext`, `SwapchainService`, `FrameService`, `DeviceService` | `Vulkan*` classes | Rename + split: neutral declaration in `rhi/`, backend `State` + definitions in `src/rhi/vulkan/`. Public APIs scrubbed of Vk types (`GetBuffer(BufferHandle) -> VkBuffer` becomes backend-internal). |
| `VulkanInstanceService`, `VulkanSurfaceService`, `VulkanPhysicalDeviceService`, `VulkanQueueService`, `VulkanAllocatorService`, `VulkanDeletionQueueService`, `VulkanBarriers`, `VulkanDepthTarget`, VMA impl | `graphics/vulkan/` | Backend-internal. Move to `src/rhi/vulkan/`, keep names, no neutral counterpart. D3D12 has its own internals (DXGI factory/adapter, descriptor heap allocator, D3D12MA, fence pool). |
| `VulkanBootstrapPolicy` | `graphics/vulkan/` | Generalizes to `rhi::DeviceBootstrapPolicy` + backend extras (Section 2.3). |
| `Renderer`, `RenderPhase`, `RendererServices`, `FrameContext`, `IRenderFeature`, `RenderFrameResult` | `graphics/vulkan/Renderer.h` | Moves to neutral `graphics/`. Renderer's swapchain-image layout tracking and depth-target handling move behind `FrameService` and named transitions; phase buckets, feature ownership, timing stay as is. |
| `GraphicsServices` | `graphics/vulkan/` | Moves to neutral `graphics/`; remains the composition root struct with members in construction order. Member list becomes neutral service types; backend-internal services fold into the backend `State` of `DeviceService`/`FrameService` or a single backend root inside `src/rhi/vulkan/`. Exact split decided in step 3. |
| `TextureCache`, `ShaderMetadata`, `TimingSampler` | `graphics/vulkan/` | Moves to neutral `graphics/` (they consume services and handles; scrub any incidental Vk types found during step 1). |
| `MeshForwardPass` | `render/`, fused pass logic + Vk submission | Refactors in place (step 2): same file, same public API, private stages rewritten against rhi services and `Cmd*` functions. Raw `VkPipeline`/`VkPipelineLayout`/`VkDevice`/`VkFormat` members become `PipelineHandle`/`rhi::Format`. |
| `Camera::Build(VkExtent2D ...)` | `render/Camera.h` | Scrubbed: `rhi::Extent2D` (or a width/height pair). One-line leak. |
| `ImGuiDebugOverlay` | `debug/` | Allowlisted, then split: debug UI is per-backend by nature (imgui ships `imgui_impl_vulkan` and `imgui_impl_dx12`). Short term: firewall allowlist entry. Long term: a per-backend overlay TU selected by the same CMake variable. Debug-UI-only, so it may lag the main migration. |
| `RenderPacket`, `RenderQueue`, `MaterialCache`, extraction systems, frame orchestration | `render/`, `runtime/`, `app/` | Unchanged. Section 7. |

---

## 4. Shaders: HLSL single source, DXC to SPIR-V and DXIL

Current state: exactly two shader files exist
(`engine/shaders/mesh_forward.{vert,frag}.glsl`), compiled by glslc via
`sencha_compile_shader` and embedded as `constexpr uint32_t[]` headers. The
conversion surface is two files. Do it early, while it is two files; that is
the strongest argument for not deferring the shader-language decision.

1. **HLSL becomes the single shader source language.** Port the two GLSL
   files to HLSL. DXC compiles each source twice at build time:
   `-spirv -fspv-target-env=vulkan1.3` for the Vulkan backend, and the
   native DXIL target (SM 6.6, Section 5) for D3D12. glslc and GLSL retire
   once the port is green on llvmpipe.
2. **`SenchaShaders.cmake` grows one function and keeps the embed steps.**
   `sencha_compile_shader` gains a DXC-based implementation with a
   `TARGET spirv|dxil` argument; `sencha_embed_spirv` generalizes to
   `sencha_embed_shader_binary` (a DXIL blob embeds the same way, as
   bytes). The engine CMakeLists compiles and embeds only the selected
   backend's binaries, keyed off `SENCHA_RHI_BACKEND`. DXC emits depfiles
   (`-MF`), preserving the include-tracking behavior.
3. **The binding contract is authored once, in HLSL, with Vulkan
   annotations.** The descriptor convention (Section 5) is fixed:
   `[[vk::binding(0, 0)]]` dynamic uniform buffer, `[[vk::binding(0, 1)]]`
   bindless texture array, `[[vk::push_constant]]` for push constants. For
   DXIL the same declarations map to root-signature slots: a root CBV for
   set 0, SM 6.6 `ResourceDescriptorHeap` for the bindless array (no
   declared table needed), root constants for the push-constant block. The
   mapping is a fixed engine convention, documented in a shared shader
   include header, not per-shader configuration.
4. **`ShaderMetadata` / `.shader` JSON stays neutral and unchanged in
   shape.** It describes stages, entry points, and pipeline-state intent;
   nothing in it is Vulkan-specific. `PipelineCache` keying by the neutral
   `GraphicsPipelineDesc` content hash is backend-independent after
   migration step 1; each backend folds its own compiled-artifact identity
   (SPIR-V vs DXIL) into its driver-level cache (`VkPipelineCache` blob on
   Vulkan, `ID3D12PipelineLibrary` on D3D12, same LoadFromDisk/SaveToDisk
   surface).
5. **Toolchain.** DXC is required for both backends after the port (the
   Vulkan SDK ships it; standalone releases exist for CI). This removes the
   glslc dependency rather than adding a second compiler.

---

## 5. The bindless contract, specified at RHI level

The contract, stated so both backends satisfy it identically:

- `DescriptorCache::RegisterSampledImage(ImageHandle, SamplerHandle) -> BindlessImageIndex`
  returns a `uint32` slot in `[0, kBindlessImageCapacity)`. The index is
  stable for the registration's lifetime, is what `MaterialCache` stores and
  `RenderPacket` carries, and is the only thing shaders need to sample the
  texture.
- `UpdateSampledImage(index, image, sampler)` repoints a live slot without
  changing the index (hot reload). `UnregisterSampledImage(index)` frees the
  slot for reuse. Slot updates respect frames-in-flight: a slot's previous
  contents stay valid until in-flight frames retire. The discipline is part
  of the contract; the mechanism (update-after-bind plus the deletion queue
  on Vulkan) is not.
- Shaders index dynamically and non-uniformly: `NonUniformResourceIndex`
  semantics are assumed.

Backend realizations:

- **Vulkan (exists):** descriptor indexing, one update-after-bind
  combined-image-sampler array at set 1 binding 0; the index is the array
  element.
- **D3D12 (SM 6.6 dynamic resources, the target):** one shader-visible
  CBV_SRV_UAV heap sized for the capacity plus non-bindless needs;
  `RegisterSampledImage` writes an SRV at a fixed base offset plus index;
  shaders do `Texture2D t = ResourceDescriptorHeap[base + index]`. D3D12
  has no combined image sampler, so the backend either pairs a parallel
  sampler-heap slot per index or uses a static-sampler convention (cheap
  given the small `SamplerDesc` domain; open question 3). Backend-internal
  either way; the RHI contract stays "one uint32 in, sampled texture out."
  Requires SM 6.6 and Resource Binding Tier 3; declare that the D3D12
  backend's minimum spec.
- **Fallback (only if a shipping target demands pre-6.6 hardware):** a
  classic unbounded SRV table bound once per frame; same contract, same
  indices, one extra root-signature table. Noted, not built.

`DescriptorCache` also becomes the owner of the engine-standard pipeline
layout (frame set + bindless set + push-constant range). On D3D12 that
convention names a root signature built the same way. This closes the
current loose end where `VkPipelineLayout` is owned externally and passed
around raw.

---

## 6. Migration steps

Each step lands green with Vulkan-only, verified by the existing test suite
plus the llvmpipe render gate. Behavior-identical refactors are the
majority; the render gate is the tool that proves identical for anything
that draws.

**Step 0: firewall check + seal the trivial leak. Cheap.**
Add `cmake/CheckRenderBackendIsolation.cmake` cloned from
`CheckPhysicsIsolation.cmake`: no `#include <vulkan/`, no `\bVk[A-Z]`, no
`\bvk[A-Z]\w*\(`, no `VmaAllocator` outside the allowed list
(`graphics/vulkan/` initially, plus allowlisted `render/MeshForwardPass.*`
and `debug/ImGuiDebugOverlay.*`). Wire it as a ctest. Fix `Camera.h`
(`VkExtent2D` to a neutral extent) immediately so it never enters the
allowlist. The allowlist then shrinks step by step; the check makes every
later step's containment claim mechanical instead of reviewed.
Verify: ctest green; no behavior change possible.

**Step 1: lift neutral types into `rhi/` headers. Cheap.**
Create `rhi/RhiTypes.h`, `rhi/RhiHandles.h`, `rhi/GraphicsPipelineDesc.h`
with the scrubbed enums and desc structs per the inventory table. The
Vulkan code consumes them and maps to Vk enums internally (small closed
switches per format and enum, sanctioned). `VulkanPipelineCache` keys and
hashes the neutral desc.
Verify: full suite + llvmpipe gate; pipeline-cache tests confirm identical
keying behavior (same desc, same pipeline reuse).

**Step 2: neutralize the feature-facing surface and split MeshForwardPass.
Moderate.**
Introduce `rhi::CommandList` + the initial `Cmd*` verb set (defined in the
Vulkan backend), `PipelineHandle`, the named transitions, and rewrite
`RendererServices`/`FrameContext`/`IRenderFeature` signatures to neutral
types. Rewrite `MeshForwardPass`'s private stages against the rhi surface;
its members lose all raw Vk state. Update `MeshRenderFeature`, editor
viewport features, and the debug overlay call sites. This is the porting
hotspot and the step with the most diff, but it is mechanical: every
`vkCmd*` in the render layer becomes exactly one `rhi::Cmd*`.
Verify: full suite + llvmpipe image-level render gate (this step must be
pixel-identical); MeshForwardPass draw-stats tests unchanged; firewall
allowlist drops MeshForwardPass.

**Step 3: move the backend behind the surface. Moderate, mostly renames.**
`graphics/vulkan/` sources move to `src/rhi/vulkan/`; each `VulkanXService`
splits into a neutral `rhi/X.h` declaration plus a backend `State`
definition; `Renderer`/`GraphicsServices`/`TextureCache`/`ShaderMetadata`/
`TimingSampler` move to neutral `graphics/`; `Renderer`'s image-layout
tracking and depth target sink into `FrameService` and backend internals;
`VulkanBootstrapPolicy` becomes `DeviceBootstrapPolicy`. `Engine` and
`EngineFramePhases` keep calling the same `Renderer` API. The public
`vk_mem_alloc.h` install goes away.
Verify: full suite + llvmpipe gate; firewall allowlist is now exactly
`src/rhi/vulkan/` plus the debug overlay; a game module builds against the
installed tree without a Vulkan SDK on the include path.

**Step 4: CMake backend selection + surface pinning. Cheap.**
`SENCHA_RHI_BACKEND=vulkan|null` (later `d3d12`) in `SenchaOptions.cmake`;
`SENCHA_ENABLE_VULKAN` becomes derived (`backend STREQUAL vulkan`) until
callers migrate. The existing GLOB + `list(FILTER)` pattern selects
`src/rhi/<backend>/` sources. Add the conformance static_assert TU. Shader
compilation keys off the same variable.
Verify: the vulkan value configures and builds exactly as before; CI matrix
unchanged.

**Step 5: null backend. Cheap, high value.**
`src/rhi/null/` defines every rhi symbol as a no-op returning valid-shaped
nothing (handles from a trivial slot pool so generation checks still
exercise). This proves the seam is complete (it links), gives CI a no-GPU
leg for render-layer logic tests, and delivers the roadmap's anticipated
headless/null-render build. Default stance: the null backend is additive;
`Engine`'s existing headless mode (null `GraphicsServices` behind the
ifdef) stays until there is a reason to collapse it.
Verify: null-backend binary links and boots the frame loop; render-layer
unit tests pass against it; the vulkan leg untouched.

**Step 6: D3D12 backend. Expensive, incremental within itself.**
Order of work, each sub-stage runnable: (1) device/adapter/queue/fence
bring-up plus `DeviceService`/`FrameService`/`SwapchainService` (DXGI flip
model, frames-in-flight mapped to fence values), clearing the swapchain;
(2) `BufferService`/`ImageService`/`UploadContext` over D3D12MA plus copy
queue staging; (3) descriptor heap allocator plus `DescriptorCache` bindless
per Section 5 plus the root signature; (4) `ShaderCache` (DXIL blobs) plus
`PipelineCache` (PSOs keyed by the same neutral desc, `ID3D12PipelineLibrary`
persistence); (5) `CommandList` verbs plus named transitions; (6)
mesh_forward renders the template scene; (7) debug overlay via
`imgui_impl_dx12`. The HLSL/DXC work from Section 4 lands before or with
sub-stage 4.
Verify: the same RHI conformance and render tests run on a D3D12 CI leg. A
Windows agent with WARP is the llvmpipe analogue: WARP is the software
rasterizer that makes a GPU-less D3D12 gate possible, and it supports SM
6.6 dynamic resources.

Cost summary: steps 0, 1, 4, 5 are cheap; steps 2 and 3 are moderate
refactors fully covered by existing tests and the render gate; step 6 is
the only genuinely expensive step and it is pure addition behind a proven
seam.

---

## 7. What explicitly does not change

- **Frame orchestration:** `FrameDriver`/`FramePhase`, `RuntimeFrameLoop`,
  `Engine`, `EngineFramePhases` wiring `FramePhase::Render` to
  `DrawFrameScheduled()` returning `RenderFrameResult`. Untouched.
- **`RenderPacket`:** already handle-based and backend-free. Untouched.
- **`RenderQueue`/`RenderQueueItem`,** sort keys, instanced runs. Untouched.
- **Material and texture cache contracts:** `MaterialCache` keeps storing
  `uint32` bindless indices; `.smat`/`.stex` and the asset pipeline
  unchanged.
- **`Handle<Tag>` vocabulary and `Owned`/`ILifetimeOwner`:** the RHI
  consumes them, it does not replace them.
- **The two concurrency lanes and determinism rules:** the RHI introduces
  no threads, no locks, no lane; recording remains where it is today; the
  serial reference path is unaffected.
- **`IRenderFeature` as a virtual seam:** kind unchanged, signatures
  neutralized.

---

## 8. D3D11: analysis and descope

The conflict is structural, not incidental. Sencha's RHI contract bakes in
two things D3D11 does not have:

1. **Bindless.** Feature level 11 caps at 128 SRV slots per stage with no
   unbounded arrays and no dynamic indexing across a heap. The `uint32`
   index that `MaterialCache` stores and `RenderPacket` ships is
   unrepresentable as a direct GPU concept.
2. **The SM 6.6 shader model.** The single-source HLSL from Section 4
   targets `ResourceDescriptorHeap`; D3D11 needs SM 5.0 compiles with
   per-resource register declarations, meaning a second shader permutation
   per pass, diverging exactly where the other two backends converge.
   (Dynamic rendering itself is a non-issue: `OMSetRenderTargets` covers
   it.)

**The viable strategy if it must happen** (backend-internal emulation, same
RHI surface): the D3D11 backend keeps a CPU-side table mapping bindless
index to `ID3D11ShaderResourceView*`. The recording path already pushes the
material's texture indices per run via `CmdPushConstants`; the layout of
that block is engine convention, so the backend can locate the indices (or
the convention grows an explicit annotation marking which push bytes are
texture indices) and translate each to a per-draw `PSSetShaderResources`
slot binding, patching the index the SM 5.0 shader sees to the slot number.
Documented costs: per-draw descriptor staging and state churn (the exact
overhead bindless exists to eliminate), a second shader compile target with
divergent resource declarations, a per-layout annotation, and a permanent
testing surface for a backend that is slower by construction. No render-layer
or contract change is required, which is the point of stating it: the RHI
surface holds even for this worst case.

**Decision: descoped, with a recorded revisit trigger,** matching the repo's
deferral pattern. Trigger: a shipping target whose hardware or OS floor
cannot run either Vulkan 1.3 or D3D12 with SM 6.6. Given the current
Windows install base, that trigger describes hardware that also fails the
game's likely performance floor. The emulation strategy above is recorded so
the descope is a decision with a known escape path, not an unknown.

---

## 9. Render graph: adjacent, deferred, non-blocking

A render graph is the paradigm that carries rendering past forward shading
with a handful of hand-ordered passes. It is recorded here because the
question comes up naturally against this plan, and because the answer
constrains nothing in the RHI: the two are orthogonal.

**Relationship to the RHI.** The graph sits above the neutral RHI and drives
its named-transition verbs (Section 2.2). It ships independently whenever
pass count earns it, and neither depends on nor blocks backend selection.
The RHI is designed without assuming a graph; a graph plugs in later without
touching the backend seam.

**Why the graph's value is orthogonal to the dispatch question.** The
current feature loop (`Renderer::RecordMainColorPhase`,
`for (IRenderFeature* feat : bucket) feat->OnDraw(ctx)`) is cold: single-digit
indirect calls per frame. The hot path (`MeshForwardPass::DrawRuns`) is
already a virtual-free data loop over sort-keyed POD `RenderQueueItem` with
instanced-run coalescing. So the graph's wins are structural (synchronization,
memory, parallel recording), not a change to dispatch cost.

**Extensibility without virtual dispatch, across the game-module boundary.**
A pass is a POD descriptor (declared resource reads and writes, formats,
sizes, an ordering key) plus a registered record function pointer,
`void (*)(rhi::CommandList, const void* passData)`. The build and compile
phase is pure data with zero dispatch: the game module appends a descriptor,
and the compiler (dependency resolution, barrier derivation, transient
aliasing, culling, scheduling) is a concrete engine-side algorithm over
arrays. Execute is one cold function-pointer call per pass per frame: a
registered operation on CLAUDE.md's dispatch ladder, not a runtime interface
(no `IPass` base, no per-object vtable). Adopting a graph this way retires
the `IRenderFeature` virtual rather than adding a seam. The honest caveat:
crossing a compiled module boundary where the engine drives game-supplied
pass code requires one runtime indirection per pass per frame; virtual,
`std::function`, and a function pointer are isomorphic in cost. Only
compile-time dispatch removes it, and that requires compile-time knowledge
of every pass type, which negates the module boundary. The function-pointer
registered operation is the shape that honors "no virtual dispatch" and
keeps the indirection off the hot path.

**What it offers over the phase-bucket model.** Each is tied to a Track B
item:

- **Computed synchronization.** Barriers derived from declared read and
  write intent, a deterministic function of data, replacing hand-placed
  `VulkanBarriers`. The biggest win, and it grows as shadows, transparency,
  and post add write-then-sample dependencies that are error-prone to
  transition by hand.
- **Transient-target aliasing.** Lifetime-based memory reuse across targets
  whose live ranges do not overlap, versus each offscreen feature owning a
  full-frame-resident target today. Real VRAM against the v2.0 post stack.
- **Pass culling.** Drop a pass and free its transients when its output
  feeds nothing this frame, propagating data-driven feature toggles through
  the whole resource subtree.
- **Parallel command recording.** Independent passes record on separate
  JobSystem workers and submit in dependency order, versus today's serial
  main-thread recording. The performance headline, and it fits the
  ~1ms-gate concurrency doctrine.
- **An inspectable, testable frame.** The compiled graph is data: assert the
  barrier and alias plan with no GPU, validate read-before-write at compile
  instead of as a hang, and dump the pass DAG.
- **Async-compute scheduling** further out, for GPU particles or GI.

**Runtime cost, stated honestly.** One real cost: the per-frame compile
(topological sort, resource-lifetime computation, transient-alias solve,
barrier derivation), on the order of tens of microseconds at tens of passes,
a sub-fraction of a frame, growing with pass count and not draw count. The
real trap is per-frame allocation churn, avoided by building the graph into
the existing `VulkanFrameScratch` per-frame arena (reset each frame, zero
heap traffic). The execute indirection is one cold function-pointer call per
pass, negligible. Net: the compile cost buys parallel recording, so it is a
net CPU win past the earning trigger and small unearned overhead below it,
and computed barriers that see the whole frame typically beat hand-placed
ones on the GPU.

**Earning trigger (matches the repo's deferral pattern).** Not shadows
alone. The crossover is shadows plus transparency plus a real post stack
together: several transient targets with overlapping-but-distinct lifetimes
and chained write-then-sample dependencies, where hand-managing barriers and
target lifetimes stops being obviously correct. Below that, manual is
clearer and a graph is unearned abstraction (directive 4). A deep post stack
plus clustered many-light shading plus async compute or GI is where it
becomes effectively non-optional.

---

## 10. Open questions

1. **Pipeline layout modeling.** One engine-standard layout exists today
   (frame set + bindless set + push range). Is a `PipelineLayoutHandle`
   earned, or does the RHI hardcode the single convention in
   `DescriptorCache` until a second real layout exists? Directive 4 says
   hardcode; decide before step 2 since `GraphicsPipelineDesc` currently
   carries the field.
2. **Offscreen render targets.** Editor viewports and
   `RenderPhase::Offscreen` need target creation (`ImageService`) plus the
   begin/end/transition verbs. Confirm during step 2 whether
   `VulkanDepthTarget` generalizes to an rhi `DepthTarget` or stays a
   backend detail behind `FrameService`.
3. **Combined image samplers.** Vulkan's bindless array is combined
   image+sampler; D3D12 separates them. Decide the D3D12 sampler convention
   (static samplers vs a parallel sampler heap) before step 6 sub-stage 3;
   the `SamplerDesc` domain is small enough that static samplers may cover
   it entirely.
4. **`Contribute()` reality check.** Audit whether any current feature
   actually folds extensions into `VulkanBootstrapPolicy` from game code.
   If none do, `DeviceBootstrapPolicy::BackendExtras` is speculative and is
   omitted until a real user exists.
5. **CI hardware for the D3D12 leg.** A Windows agent with WARP (SM
   6.6-capable) is required for step 6 verification; confirm availability
   before scheduling that step.
6. **`SENCHA_ENABLE_VULKAN` retirement timeline.** Downstream game modules
   and the installed-package interface reference it; decide the deprecation
   window when step 4 lands.
