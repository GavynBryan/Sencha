# Renderer

The render system is built around `IRenderFeature` and `Renderer`.  `Renderer`
owns an ordered list of features, drives them through a single `DrawFrame()`
call each tick, and hands each feature a small `FrameContext` on the hot path —
no `ServiceHost` lookups in the loop.  Features extend the renderer by
implementing `IRenderFeature`; the engine ships two: `SpriteFeature` (immediate
sprite batching) and `TilemapRenderFeature` (tilemap layer rendering).

---

## Location

```
engine/include/render/Renderer.h
engine/include/render/features/SpriteFeature.h
engine/include/render/features/TilemapRenderFeature.h
engine/include/render/features/TilemapRenderState.h
```

```cpp
#include <render/Renderer.h>
#include <render/features/SpriteFeature.h>
#include <render/features/TilemapRenderFeature.h>
#include <render/features/TilemapRenderState.h>
```

---

## Core types

**`IRenderFeature`** is the extension point.  Implement it to add a new draw
pass.  One feature belongs to exactly one `RenderPhase`.  The Renderer calls
`Setup()` once at `AddFeature()` time (the device already exists), and
`Teardown()` in `~Renderer` before any Vulkan service is torn down.  `OnDraw()`
is called every frame inside the appropriate phase.

**`Renderer`** owns all features, runs the frame loop, and is itself an
`IService`.  It is neither copyable nor movable.

**`RenderPhase`** is an enum that buckets features.  Only `MainColor` exists
today; the renderer is designed so adding `Offscreen`, `Shadow`, `UI`, and
`Post` phases later does not change the feature interface.

**`RendererServices`** is a plain struct of non-owning service pointers handed
to `Setup()`.  Features cache whichever pointers they need and never touch the
`ServiceHost` again.

**`FrameContext`** is the small, cache-dense payload passed to every `OnDraw()`
call.  It contains the command buffer, frame-in-flight index, target extent,
target format, and current phase — nothing else.

---

## API

```cpp
// ---- IRenderFeature (implement to add a draw pass) ----------------------

class MyFeature : public IRenderFeature
{
public:
    RenderPhase GetPhase() const override { return RenderPhase::MainColor; }

    // Optional: fold device extensions / feature bits into the bootstrap
    // policy before VkDevice creation. Renderer never calls this directly —
    // game code calls it before standing up the Vulkan stack.
    void Contribute(VulkanBootstrapPolicy& policy) override;

    // Called once inside Renderer::AddFeature. Cache service pointers here.
    void Setup(const RendererServices& services) override;

    // Called every frame. Command buffer is already inside vkCmdBeginRendering
    // for MainColor features.
    void OnDraw(const FrameContext& frame) override;

    // Called in ~Renderer before any Vulkan service tears down.
    void Teardown() override;
};

// ---- Renderer -----------------------------------------------------------

// Add a feature and run its Setup() immediately. Returns a raw pointer
// whose lifetime is tied to the Renderer.
T* raw = renderer.AddFeature(std::make_unique<MyFeature>());

// Drive one frame: acquire → scratch rotate → phase iterate → present.
Renderer::DrawStatus status = renderer.DrawFrame();
// status == Ok | SwapchainOutOfDate | Skipped | Error

// Call after VulkanSwapchainService::Recreate to reset per-image tracking.
renderer.NotifySwapchainRecreated();

// ---- SpriteFeature submission -------------------------------------------

sprites->Submit({
    .CenterX  = 100.0f, .CenterY  = 200.0f,  // screen pixels, origin top-left
    .Width    = 32.0f,  .Height   = 32.0f,
    .UvMinX   = 0.0f,   .UvMinY   = 0.0f,
    .UvMaxX   = 1.0f,   .UvMaxY   = 1.0f,
    .Color    = 0xFFFFFFFFu,                   // packed rgba8, opaque white
    .Texture  = bindlessSlot,                  // BindlessImageIndex
    .Rotation = 0.0f,                          // radians around Center
    .SortKey  = 0,                             // ascending; smaller draws first
});

sprites->ClearPending();  // abort without drawing (rarely needed)
```

---

## Idiomatic setup

### Bootstrap sequence

Features that need Vulkan extensions (e.g. for ray tracing or mesh shaders)
must call `Contribute()` on the feature before the device is created.  All
other wiring happens after the device exists.

```cpp
// 1. Collect contributions from features that need device extensions.
SpriteFeature sprite;
sprite.Contribute(vulkanPolicy);   // no-op for SpriteFeature today

// 2. Bring up the Vulkan stack (Instance → Surface → PhysicalDevice →
//    Device → Queues → Allocator → Upload → Buffers → Images → Samplers
//    → Shaders → Pipelines → Descriptors → Scratch → Swapchain → Frames).
//    See QuadTreeDemoRender.cpp for the full sequence.

// 3. Construct the Renderer, then hand it the features.
auto renderer = std::make_unique<Renderer>(
    logging, *device, *physicalDevice, *queues, *swapchain, *frames,
    *allocator, *buffers, *images, *samplers, *shaders, *pipelines,
    *descriptors, *scratch, *upload);

SpriteFeature* sprites =
    renderer->AddFeature(std::make_unique<SpriteFeature>());
```

### Sprite rendering per frame

```cpp
// Before renderer->DrawFrame():
sprites->Submit({ .CenterX = x, .CenterY = y, .Width = w, .Height = h,
                  .Texture = mySlot, .SortKey = layer });
sprites->Submit({ ... });

auto status = renderer->DrawFrame();
if (status == Renderer::DrawStatus::SwapchainOutOfDate)
{
    swapchain->Recreate(window->GetExtent());
    frames->ResetAfterSwapchainRecreate();
    renderer->NotifySwapchainRecreated();
}
```

`SpriteFeature` stable-sorts by `SortKey` ascending before upload, then issues
a single instanced draw.  The accumulator is cleared automatically after
`OnDraw` consumes it.

### Tilemap rendering

`TilemapRenderFeature` takes non-owning references to the game's
`DataBatch<Tilemap2d>`, `DataBatch<TilemapRenderState>`, and
`TransformStore<Transform2f>` at construction time.  These batches must outlive
the `Renderer`.

```cpp
DataBatch<Tilemap2d>          maps;
DataBatch<TilemapRenderState> renderStates;
// ... populate maps and transforms ...

auto* tilemap =
    renderer->AddFeature(std::make_unique<TilemapRenderFeature>(
        maps, renderStates, world.Transforms));

// Add a render state to make a map visible.
TilemapRenderState state;
state.MapKey        = mapHandle.GetToken();
state.TransformKey  = mapHandle.GetTransformKey();
state.TilesetTexture = tilesetSlot;  // BindlessImageIndex
state.TilesetColumns = 8;
state.TilesetRows    = 8;
state.LayerZIndex    = 0;            // drawn before higher z-indices
renderStates.Emplace(state);
```

### Texture registration

All textures are registered with `VulkanDescriptorCache` before being handed
to any feature.  The returned `BindlessImageIndex` is what goes on sprite and
tilemap state structs.

```cpp
ImageHandle img = images->Create(info);
images->Upload(img, pixels, size);

VkSampler sampler = samplers->GetNearestRepeat();
BindlessImageIndex slot = descriptors->RegisterSampledImage(img, sampler);
// slot.IsValid() == true

// When the image is no longer needed:
descriptors->UnregisterSampledImage(slot);
images->Destroy(img);
```

Re-registering the same `(image, sampler)` pair is idempotent and returns the
same slot cheaply.

---

## Coordinate space (SpriteFeature)

Screen pixels, origin top-left, +X right, +Y down.  `CenterX`/`CenterY` are
the sprite's center in that space.  `Width`/`Height` are the full extents; the
shader halves them internally.  There is no camera or view matrix at this
layer — world-space projection is out of scope for `SpriteFeature` and belongs
to a future `CameraFeature` or a view-matrix push constant.

---

## Constraints

**Do not call `ServiceHost` inside `OnDraw()`.**  `OnDraw()` is the hot path.
Every service the feature needs must be cached into member pointers during
`Setup()`.  The `RendererServices` bundle is provided specifically for this.

**Do not submit sprites after `DrawFrame()` for the same frame.**  The
accumulator is cleared by `OnDraw()`.  Submissions must happen between the
previous `DrawFrame()` returning and the next one being called.

**Do not hold raw `IRenderFeature*` pointers across `~Renderer`.**  `AddFeature`
returns a raw pointer whose lifetime is tied to the `Renderer` instance.  Once
the `Renderer` is destroyed, all feature pointers are dangling.

**Do not implement multi-phase features.**  Each `IRenderFeature` reports
exactly one `RenderPhase`.  A feature that needs to draw in two phases must be
split into two separate feature objects.

**`Contribute()` must be called before the device is created.**  The `Renderer`
never calls `Contribute()` itself — that is a pre-device hook for game boot
code.  If a feature folds extensions into the bootstrap policy, the game must
call `Contribute()` on the feature before constructing `VulkanDeviceService`.

**`TilemapRenderFeature` data batches must outlive the `Renderer`.**  The
feature stores non-owning pointers to the game's `DataBatch<Tilemap2d>` and
`DataBatch<TilemapRenderState>`.  Destroying either batch while the renderer is
alive is undefined behaviour.

---

## Relationship to the sprite and tilemap pipeline

`Renderer` is the top of the draw stack.  Everything beneath it is Vulkan
plumbing; everything that feeds it is game logic writing into `DataBatch`
arrays or calling `Submit()`:

```
Game logic / systems
       │  calls Submit() on SpriteFeature
       │  populates DataBatch<TilemapRenderState>
       │
Renderer::DrawFrame()
       │  iterates PhaseBuckets[MainColor]
       │
IRenderFeature::OnDraw(FrameContext)
       │
SpriteFeature            reads Pending[], stable-sorts by SortKey,
       │                 uploads via VulkanFrameScratch, one instanced draw
       │
TilemapRenderFeature     sweeps DataBatch<TilemapRenderState> by LayerZIndex,
                         resolves MapKey → Tilemap2d, reads world transform,
                         emits GpuTile instances into VulkanFrameScratch,
                         one instanced draw per tilemap layer
```

`SpriteFeature` and `TilemapRenderFeature` share the same SPIR-V (sprite
vertex/fragment shaders) and the same 48-byte per-instance GPU layout
(`GpuInstance` / `GpuTile`).  They do not share state at runtime; each
maintains its own per-frame accumulator and issues its own draw call.
