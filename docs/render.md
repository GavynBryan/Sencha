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
Sencha.Core/include/graphics/Renderer.h
Sencha.2D/include/render/features/SpriteFeature.h
Sencha.2D/include/render/features/TilemapRenderFeature.h
Sencha.2D/include/render/features/TilemapRenderState.h
```

```cpp
#include <graphics/Renderer.h>
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

// Add a feature and run its Setup() immediately. Returns a FeatureRef<T>
// whose IsValid() becomes false once the Renderer is destroyed.
FeatureRef<MyFeature> ref = renderer.AddFeature(std::make_unique<MyFeature>());

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

FeatureRef<SpriteFeature> sprites =
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

`TilemapRenderFeature` is a dumb batcher, mirroring `SpriteFeature`.  Game or
system code is responsible for baking tile data into `GpuTile` arrays and
calling `Submit()` each frame.  The feature has no opinion on *how* tiles are
baked; a `TilemapRenderSystem` that dirty-tracks maps and transforms is the
expected driver.

```cpp
FeatureRef<TilemapRenderFeature> tilemap =
    renderer->AddFeature(std::make_unique<TilemapRenderFeature>());

// Each frame, before renderer.DrawFrame():
// Build the GpuTile array for one layer (world-space centres, UVs, sin/cos).
std::vector<TilemapRenderFeature::GpuTile> tiles = BakeTiles(map, transform, state);
tilemap->Submit(tiles, state.LayerZIndex);
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

**Do not submit sprites after `DrawFrame()` for the same frame.**  The
accumulator is cleared by `OnDraw()`.  Submissions must happen between the
previous `DrawFrame()` returning and the next one being called.

**Do not dereference a `FeatureRef<T>` after `~Renderer`.**  `AddFeature`
returns a `FeatureRef<T>` whose `IsValid()` returns false once the `Renderer`
is destroyed.  `operator->` and `operator*` assert in debug builds if called on
an invalid ref.

**`Contribute()` must be called before the device is created.**  The `Renderer`
never calls `Contribute()` itself — that is a pre-device hook for game boot
code.  If a feature folds extensions into the bootstrap policy, the game must
call `Contribute()` on the feature before constructing `VulkanDeviceService`.

**`TilemapRenderFeature` does not own or retain game data.**  The feature copies
submitted `GpuTile` spans into a per-frame scratch buffer and clears them after
`OnDraw`.  Callers are free to destroy or mutate their tile arrays immediately
after `Submit()` returns.

---

## Relationship to the sprite and tilemap pipeline

`Renderer` is the top of the draw stack.  Everything beneath it is Vulkan
plumbing; everything that feeds it is game logic writing into `DataBatch`
arrays or calling `Submit()`:

```
Game logic / systems
       │  calls Submit() on SpriteFeature
       │  calls Submit(tiles, sortKey) on TilemapRenderFeature
       │
Renderer::DrawFrame()
       │  iterates PhaseBuckets[MainColor]
       │
IRenderFeature::OnDraw(FrameContext)
       │
SpriteFeature            reads Pending[], stable-sorts by SortKey,
       │                 uploads via VulkanFrameScratch, one instanced draw
       │
TilemapRenderFeature     stable-sorts Batches[] by SortKey,
                         uploads flat GpuTile array via VulkanFrameScratch,
                         one instanced draw per batch (firstInstance offset)
```

`SpriteFeature` and `TilemapRenderFeature` share the same SPIR-V (sprite
vertex/fragment shaders) and the same 48-byte per-instance GPU layout
(`GpuInstance` / `GpuTile`).  They do not share state at runtime; each
maintains its own per-frame accumulator and issues its own draw call.
