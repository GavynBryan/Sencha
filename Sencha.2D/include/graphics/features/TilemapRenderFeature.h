#pragma once

#include <graphics/Renderer.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <vector>

class VulkanBufferService;
class VulkanFrameScratch;

//=============================================================================
// TilemapRenderFeature
//
// IRenderFeature that drives the tilemap draw pass each frame.
//
// Usage model (mirrors SpriteFeature):
//
//   FeatureRef<TilemapRenderFeature> tiles =
//       renderer.AddFeature(std::make_unique<TilemapRenderFeature>());
//
//   // Each frame, before renderer.DrawFrame():
//   tiles->Submit(gpuTileSpan, layerZIndex);
//   renderer.DrawFrame();
//
// The feature is a dumb batcher: it accumulates pre-baked GpuTile instances
// submitted by game or system code, stable-sorts the batches by sort key,
// uploads the tile data through VulkanFrameScratch as a per-instance vertex
// buffer, and issues one instanced draw per batch. No staging, no retained
// state, no per-tile pointer chasing.
//
// Tile baking (world-space transforms, UV rect computation, sin/cos) is the
// caller's responsibility. A TilemapRenderSystem or similar should track dirty
// state and only rebuild tile arrays when the map, transform, or render state
// actually changes.
//
// GPU program:
//   - Reuses the sprite vertex/fragment SPIR-V (instanced quads, bindless
//     texture array). GpuTile is layout-compatible with SpriteFeature::GpuInstance.
//=============================================================================
class TilemapRenderFeature : public IRenderFeature
{
public:
    // 48-byte per-tile instance record — layout-compatible with
    // SpriteFeature::GpuInstance so the sprite SPIR-V can be reused directly.
    struct GpuTile
    {
        float    Center[2];       // World-space centre, screen pixels
        float    HalfExtents[2];  // Half tile dimensions in pixels
        float    UvMin[2];        // UV rect bottom-left on the tileset
        float    UvMax[2];        // UV rect top-right on the tileset
        uint32_t Color;           // Packed rgba8 tint (0xFFFFFFFF = opaque white)
        uint32_t TextureIndex;    // Bindless tileset slot
        float    SinRot;          // sin(worldRotation) — 0.0f for axis-aligned tiles
        float    CosRot;          // cos(worldRotation) — 1.0f for axis-aligned tiles
    };
    static_assert(sizeof(GpuTile) == 48, "GpuTile must remain 48 bytes to match sprite layout");

    TilemapRenderFeature() = default;
    ~TilemapRenderFeature() override = default;

    // -- IRenderFeature -------------------------------------------------------

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

    // -- Public submission API ------------------------------------------------

    // Append a pre-baked tile layer to this frame's batch.
    // tiles   — caller-owned span; contents are copied immediately.
    // sortKey — ascending draw order (lower draws first / behind).
    void Submit(std::span<const GpuTile> tiles, int32_t sortKey = 0);

    // Drop all pending tile data without drawing. OnDraw clears automatically;
    // call this only when aborting a frame before DrawFrame.
    void ClearPending();

    [[nodiscard]] bool IsValid() const { return Valid; }

private:
    // Per-draw UBO written into the frame scratch ring.
    // std140-padded to 16 bytes; must match frame_ubo.glsli.
    struct FrameUbo
    {
        float InvViewport[2];
        float _pad[2];
    };

    // One entry per Submit call, sorted by SortKey in OnDraw.
    struct Batch
    {
        int32_t  SortKey;
        uint32_t Offset; // First tile index in TileData
        uint32_t Count;  // Number of tiles in this batch
    };

    [[nodiscard]] bool BuildPipeline(VkFormat colorFormat);

    // Cached service pointers (populated in Setup; never re-queried on hot path).
    LoggingProvider*       Logging       = nullptr;
    VulkanDeviceService*   DeviceService = nullptr;
    VulkanBufferService*   Buffers       = nullptr;
    VulkanShaderCache*     Shaders       = nullptr;
    VulkanPipelineCache*   Pipelines     = nullptr;
    VulkanDescriptorCache* Descriptors   = nullptr;
    VulkanFrameScratch*    Scratch       = nullptr;

    ShaderHandle     VertexShader;
    ShaderHandle     FragmentShader;
    VkPipelineLayout PipelineLayout    = VK_NULL_HANDLE;
    VkPipeline       CachedPipeline    = VK_NULL_HANDLE;
    VkFormat         CachedColorFormat = VK_FORMAT_UNDEFINED;

    // Flat tile store — tiles from all Submit calls concatenated in arrival order.
    // Batches index into this array via Offset + Count.
    std::vector<GpuTile> TileData;
    std::vector<Batch>   Batches;

    bool Valid = false;
};
