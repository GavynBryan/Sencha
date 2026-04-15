#pragma once

#include <core/batch/DataBatch.h>
#include <math/geometry/2d/Transform2d.h>
#include <render/Renderer.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/backend/vulkan/VulkanPipelineCache.h>
#include <render/backend/vulkan/VulkanShaderCache.h>
#include <render/features/TilemapRenderState.h>
#include <world/tilemap/Tilemap2d.h>
#include <world/transform/TransformStore.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanBufferService;
class VulkanFrameScratch;

//=============================================================================
// TilemapRenderFeature
//
// IRenderFeature that drives the tilemap draw pass each frame.
//
// DOD contract:
//   - Maps and RenderStates live in separate DataBatch arrays owned by the
//     caller (typically the game bootstrap). This feature holds non-owning
//     references to both. Lifetimes must outlive the Renderer.
//   - OnDraw() sweeps DataBatch<TilemapRenderState> in LayerZIndex order,
//     resolves each state's MapKey and TransformKey, then emits one instanced
//     draw call per tilemap layer into the per-frame scratch buffer.
//   - No virtual dispatch, no inheritance, no heap allocation per tile.
//
// Tile encoding:
//   - Tile ID 0 is the "empty" sentinel; no quad is emitted for it.
//   - Tile IDs 1..N map to tileset index N-1, laid out row-major in the
//     spritesheet: col = (id-1) % TilesetColumns, row = (id-1) / TilesetColumns.
//
// GPU program:
//   - Reuses the sprite vertex/fragment SPIR-V (instanced quads, bindless
//     texture array). GpuTile is layout-compatible with SpriteFeature::GpuInstance.
//   - Tiles are axis-aligned at their natural size; SinRot=0, CosRot=1.
//   - Tilemap world transforms (translation, rotation, non-uniform scale) are
//     applied to each tile center via Transform2d::TransformPoint before upload.
//=============================================================================
class TilemapRenderFeature : public IRenderFeature
{
public:
    // Maps        — game-owned DataBatch<Tilemap2d>; feature reads, never writes.
    // RenderStates — game-owned DataBatch<TilemapRenderState>; feature reads, never writes.
    // Transforms  — world transform store; TryGetWorld(TransformKey) is called per-map per-frame.
    TilemapRenderFeature(
        DataBatch<Tilemap2d>&          maps,
        DataBatch<TilemapRenderState>& renderStates,
        TransformStore<Transform2f>&   transforms);

    ~TilemapRenderFeature() override = default;

    // -- IRenderFeature -------------------------------------------------------

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

private:
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
        float    SinRot;          // 0.0f — tiles are drawn axis-aligned
        float    CosRot;          // 1.0f — tiles are drawn axis-aligned
    };
    static_assert(sizeof(GpuTile) == 48, "GpuTile must remain 48 bytes to match sprite layout");

    // Per-draw UBO written into the frame scratch ring.
    // std140-padded to 16 bytes; must match frame_ubo.glsli.
    struct FrameUbo
    {
        float InvViewport[2];
        float _pad[2];
    };

    [[nodiscard]] bool BuildPipeline(VkFormat colorFormat);

    // Non-owning references to game-side DataBatch instances.
    DataBatch<Tilemap2d>*          Maps         = nullptr;
    DataBatch<TilemapRenderState>* RenderStates = nullptr;
    TransformStore<Transform2f>*   Transforms   = nullptr;

    // Cached service pointers (populated in Setup; never re-queried on hot path).
    LoggingProvider*       Logging      = nullptr;
    VulkanDeviceService*   DeviceService = nullptr;
    VulkanBufferService*   Buffers      = nullptr;
    VulkanShaderCache*     Shaders      = nullptr;
    VulkanPipelineCache*   Pipelines    = nullptr;
    VulkanDescriptorCache* Descriptors  = nullptr;
    VulkanFrameScratch*    Scratch      = nullptr;

    ShaderHandle     VertexShader;
    ShaderHandle     FragmentShader;
    VkPipelineLayout PipelineLayout    = VK_NULL_HANDLE;
    VkPipeline       CachedPipeline    = VK_NULL_HANDLE;
    VkFormat         CachedColorFormat = VK_FORMAT_UNDEFINED;

    // Per-frame tile instance accumulator. Cleared at the end of every OnDraw.
    std::vector<GpuTile>   Pending;
    // Scratch index array for sorting render states by LayerZIndex without
    // mutating the DataBatch. Retained across frames to avoid reallocation.
    std::vector<uint32_t>  SortedIndices;

    bool Valid = false;
};
