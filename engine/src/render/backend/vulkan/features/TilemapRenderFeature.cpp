#include <render/features/TilemapRenderFeature.h>

// Reuse the sprite SPIR-V blobs: tiles are instanced quads with bindless
// texture sampling — the same GPU program as SpriteFeature. GpuTile is
// layout-compatible with SpriteFeature::GpuInstance (both 48 bytes).
#include <shaders/kSpriteVertSpv.h>
#include <shaders/kSpriteFragSpv.h>

#include <render/backend/vulkan/VulkanBufferService.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/backend/vulkan/VulkanDeviceService.h>
#include <render/backend/vulkan/VulkanFrameScratch.h>
#include <render/backend/vulkan/VulkanPipelineCache.h>
#include <render/backend/vulkan/VulkanShaderCache.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// ── Construction ─────────────────────────────────────────────────────────────

TilemapRenderFeature::TilemapRenderFeature(
    DataBatch<Tilemap2d>&          maps,
    DataBatch<TilemapRenderState>& renderStates,
    TransformStore<Transform2f>&   transforms)
    : Maps(&maps)
    , RenderStates(&renderStates)
    , Transforms(&transforms)
{
}

// ── IRenderFeature: Setup ─────────────────────────────────────────────────────

void TilemapRenderFeature::Setup(const RendererServices& services)
{
    Logging       = services.Logging;
    DeviceService = services.Device;
    Buffers       = services.Buffers;
    Shaders       = services.Shaders;
    Pipelines     = services.Pipelines;
    Descriptors   = services.Descriptors;
    Scratch       = services.Scratch;

    if (!Logging || !DeviceService || !Buffers || !Shaders
        || !Pipelines || !Descriptors || !Scratch)
    {
        return;
    }

    Logger& log = Logging->GetLogger<TilemapRenderFeature>();

    // Load pre-compiled SPIR-V blobs (embedded by the build pipeline; no I/O).
    VertexShader = Shaders->CreateModuleFromSpirv(
        kSpriteVertSpv, kSpriteVertSpvWordCount, "tilemap.vert");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kSpriteFragSpv, kSpriteFragSpvWordCount, "tilemap.frag");

    if (!VertexShader.IsValid() || !FragmentShader.IsValid())
    {
        log.Error("TilemapRenderFeature: shader module creation failed");
        return;
    }

    PipelineLayout = Descriptors->GetDefaultPipelineLayout();
    if (PipelineLayout == VK_NULL_HANDLE)
    {
        log.Error("TilemapRenderFeature: failed to acquire pipeline layout");
        return;
    }

    // Point set-0 binding-0 at the frame scratch ring (same as SpriteFeature).
    Descriptors->SetFrameUniformBuffer(Scratch->GetBuffer(), 256);

    Valid = true;
}

// ── IRenderFeature: Teardown ──────────────────────────────────────────────────

void TilemapRenderFeature::Teardown()
{
    if (Shaders)
    {
        if (VertexShader.IsValid())   Shaders->Destroy(VertexShader);
        if (FragmentShader.IsValid()) Shaders->Destroy(FragmentShader);
    }
    VertexShader       = {};
    FragmentShader     = {};
    CachedPipeline     = VK_NULL_HANDLE;
    CachedColorFormat  = VK_FORMAT_UNDEFINED;
    PipelineLayout     = VK_NULL_HANDLE;
    Pending.clear();
    Pending.shrink_to_fit();
    Valid = false;
}

// ── Pipeline ──────────────────────────────────────────────────────────────────

bool TilemapRenderFeature::BuildPipeline(VkFormat colorFormat)
{
    if (CachedPipeline != VK_NULL_HANDLE && CachedColorFormat == colorFormat)
        return true;

    GraphicsPipelineDesc desc;
    desc.VertexShader   = VertexShader;
    desc.FragmentShader = FragmentShader;
    desc.Layout         = PipelineLayout;
    desc.ColorFormats   = { colorFormat };

    // One instance-rate binding at binding 0, stride = sizeof(GpuTile) = 48.
    {
        VertexInputBindingDesc vb{};
        vb.Binding   = 0;
        vb.Stride    = sizeof(GpuTile);
        vb.InputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        desc.VertexBindings.push_back(vb);
    }

    // Vertex attributes — must match GpuTile and sprite.vert.glsl locations.
    auto addAttr = [&](uint32_t loc, VkFormat fmt, uint32_t off)
    {
        VertexInputAttributeDesc a{};
        a.Location = loc;
        a.Binding  = 0;
        a.Format   = fmt;
        a.Offset   = off;
        desc.VertexAttributes.push_back(a);
    };
    addAttr(0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuTile, Center));
    addAttr(1, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuTile, HalfExtents));
    addAttr(2, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuTile, UvMin));
    addAttr(3, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuTile, UvMax));
    addAttr(4, VK_FORMAT_R32_UINT,      offsetof(GpuTile, Color));
    addAttr(5, VK_FORMAT_R32_UINT,      offsetof(GpuTile, TextureIndex));
    addAttr(6, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuTile, SinRot));

    desc.Topology    = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.CullMode    = VK_CULL_MODE_NONE;
    desc.FrontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.PolygonMode = VK_POLYGON_MODE_FILL;
    desc.DepthTest   = false;
    desc.DepthWrite  = false;
    desc.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;

    // Straight-alpha blending (same as sprites).
    {
        ColorBlendAttachmentDesc blend{};
        blend.BlendEnable = true;
        blend.SrcColor    = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.DstColor    = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.ColorOp     = VK_BLEND_OP_ADD;
        blend.SrcAlpha    = VK_BLEND_FACTOR_ONE;
        blend.DstAlpha    = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.AlphaOp     = VK_BLEND_OP_ADD;
        desc.ColorBlend.push_back(blend);
    }

    VkPipeline pipeline = Pipelines->GetGraphicsPipeline(desc);
    if (pipeline == VK_NULL_HANDLE)
        return false;

    CachedPipeline    = pipeline;
    CachedColorFormat = colorFormat;
    return true;
}

// ── IRenderFeature: OnDraw ────────────────────────────────────────────────────

void TilemapRenderFeature::OnDraw(const FrameContext& frame)
{
    if (!Valid) return;

    // ── Sweep render states ───────────────────────────────────────────────────
    //
    // Build a sorted view of active render states (ascending LayerZIndex).
    // We sort indices rather than the batch itself so the DataBatch is not
    // mutated and its generational keys stay stable.

    std::span<const TilemapRenderState> states = RenderStates->GetItems();
    if (states.empty()) return;

    // Collect indices sorted by LayerZIndex.
    SortedIndices.clear();
    SortedIndices.resize(states.size());
    for (size_t i = 0; i < states.size(); ++i)
        SortedIndices[i] = static_cast<uint32_t>(i);

    std::stable_sort(SortedIndices.begin(), SortedIndices.end(),
        [&](uint32_t a, uint32_t b)
        {
            return states[a].LayerZIndex < states[b].LayerZIndex;
        });

    // ── Generate tile instances ───────────────────────────────────────────────

    Pending.clear();

    for (uint32_t stateIdx : SortedIndices)
    {
        const TilemapRenderState& rs = states[stateIdx];
        if (!rs.TilesetTexture.IsValid()) continue;

        const Tilemap2d* map = Maps->TryGet(rs.MapKey);
        if (!map) continue;

        const Transform2f* worldXf = Transforms->TryGetWorld(rs.TransformKey);
        if (!worldXf) continue;

        const uint32_t cols     = map->Width();
        const uint32_t rows     = map->Height();
        const float    tileSize = map->GetTileSize();
        const float    half     = tileSize * 0.5f;

        // Derive per-tile scale from the world transform's Scale.
        // If scale is non-uniform, tiles are stretched; this is intentional.
        const float halfW = half * worldXf->Scale.X;
        const float halfH = half * worldXf->Scale.Y;

        const float sinRot = std::sin(worldXf->Rotation);
        const float cosRot = std::cos(worldXf->Rotation);

        const float invCols = rs.TilesetColumns > 0
            ? 1.0f / static_cast<float>(rs.TilesetColumns) : 1.0f;
        const float invRows = rs.TilesetRows > 0
            ? 1.0f / static_cast<float>(rs.TilesetRows) : 1.0f;

        for (uint32_t row = 0; row < rows; ++row)
        {
            for (uint32_t col = 0; col < cols; ++col)
            {
                const uint32_t tileId = map->GetTile(col, row);
                if (tileId == 0) continue; // 0 == empty cell

                // Tile IDs are 1-based; tileset index = tileId - 1.
                const uint32_t tsIndex = tileId - 1u;
                const uint32_t tsCol   = tsIndex % rs.TilesetColumns;
                const uint32_t tsRow   = tsIndex / rs.TilesetColumns;

                // Local centre of this tile in the tilemap's local space.
                const float localX = (static_cast<float>(col) + 0.5f) * tileSize;
                const float localY = (static_cast<float>(row) + 0.5f) * tileSize;

                // World centre via full TRS (TransformPoint applies scale then rotate then translate).
                const Vec<2, float> worldCentre =
                    worldXf->TransformPoint(Vec<2, float>(localX, localY));

                GpuTile& t        = Pending.emplace_back();
                t.Center[0]       = worldCentre.X;
                t.Center[1]       = worldCentre.Y;
                t.HalfExtents[0]  = halfW;
                t.HalfExtents[1]  = halfH;
                t.UvMin[0]        = static_cast<float>(tsCol)     * invCols;
                t.UvMin[1]        = static_cast<float>(tsRow)     * invRows;
                t.UvMax[0]        = static_cast<float>(tsCol + 1) * invCols;
                t.UvMax[1]        = static_cast<float>(tsRow + 1) * invRows;
                t.Color           = 0xFFFFFFFFu; // opaque white
                t.TextureIndex    = rs.TilesetTexture.Value;
                t.SinRot          = sinRot;
                t.CosRot          = cosRot;
            }
        }
    }

    if (Pending.empty()) return;

    if (!BuildPipeline(frame.TargetFormat))
    {
        Pending.clear();
        return;
    }

    // ── Upload frame UBO ──────────────────────────────────────────────────────

    const VulkanFrameScratch::Allocation uboAlloc =
        Scratch->AllocateUniform(sizeof(FrameUbo));
    if (!uboAlloc.IsValid())
    {
        Pending.clear();
        return;
    }

    FrameUbo ubo{};
    if (frame.TargetExtent.width  > 0)
        ubo.InvViewport[0] = 1.0f / static_cast<float>(frame.TargetExtent.width);
    if (frame.TargetExtent.height > 0)
        ubo.InvViewport[1] = 1.0f / static_cast<float>(frame.TargetExtent.height);
    std::memcpy(uboAlloc.Mapped, &ubo, sizeof(ubo));

    // ── Upload tile instance buffer ───────────────────────────────────────────

    const VkDeviceSize instanceBytes =
        sizeof(GpuTile) * static_cast<VkDeviceSize>(Pending.size());
    const VulkanFrameScratch::Allocation vboAlloc =
        Scratch->AllocateVertex(instanceBytes);
    if (!vboAlloc.IsValid())
    {
        Pending.clear();
        return;
    }

    std::memcpy(vboAlloc.Mapped, Pending.data(), instanceBytes);

    const VkBuffer ringBuffer = Buffers->GetBuffer(vboAlloc.Buffer);
    if (ringBuffer == VK_NULL_HANDLE)
    {
        Pending.clear();
        return;
    }

    // ── Record draw commands ──────────────────────────────────────────────────

    vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, CachedPipeline);

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(frame.TargetExtent.width);
    viewport.height   = static_cast<float>(frame.TargetExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = frame.TargetExtent;
    vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);

    const VkDescriptorSet sets[2] = {
        Descriptors->GetFrameSet(),
        Descriptors->GetBindlessSet(),
    };
    const uint32_t dynamicOffset = static_cast<uint32_t>(uboAlloc.Offset);
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            PipelineLayout, 0, 2, sets, 1, &dynamicOffset);

    const VkDeviceSize vboOffset = vboAlloc.Offset;
    vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &ringBuffer, &vboOffset);

    // 6 vertices per tile (two triangles, no index buffer), N instances.
    vkCmdDraw(frame.Cmd, 6, static_cast<uint32_t>(Pending.size()), 0, 0);

    Pending.clear();
}
