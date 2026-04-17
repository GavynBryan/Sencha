#include <render/features/TilemapRenderFeature.h>

// Reuse the sprite SPIR-V blobs: tiles are instanced quads with bindless
// texture sampling — the same GPU program as SpriteFeature. GpuTile is
// layout-compatible with SpriteFeature::GpuInstance (both 48 bytes).
#include <shaders/kSpriteVertSpv.h>
#include <shaders/kSpriteFragSpv.h>

#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <algorithm>
#include <cstring>

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
    ClearPending();
    Valid = false;
}

// ── Public submission API ─────────────────────────────────────────────────────

void TilemapRenderFeature::Submit(std::span<const GpuTile> tiles, int32_t sortKey)
{
    if (!Valid || tiles.empty()) return;

    const auto offset = static_cast<uint32_t>(TileData.size());
    TileData.insert(TileData.end(), tiles.begin(), tiles.end());
    Batches.push_back({ sortKey, offset, static_cast<uint32_t>(tiles.size()) });
}

void TilemapRenderFeature::ClearPending()
{
    TileData.clear();
    Batches.clear();
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
    if (!Valid || Batches.empty()) return;

    if (!BuildPipeline(frame.TargetFormat))
    {
        ClearPending();
        return;
    }

    // Sort batches ascending by SortKey so lower layers draw first (behind).
    std::stable_sort(Batches.begin(), Batches.end(),
        [](const Batch& a, const Batch& b) { return a.SortKey < b.SortKey; });

    // ── Upload frame UBO ──────────────────────────────────────────────────────

    const VulkanFrameScratch::Allocation uboAlloc =
        Scratch->AllocateUniform(sizeof(FrameUbo));
    if (!uboAlloc.IsValid())
    {
        ClearPending();
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
        sizeof(GpuTile) * static_cast<VkDeviceSize>(TileData.size());
    const VulkanFrameScratch::Allocation vboAlloc =
        Scratch->AllocateVertex(instanceBytes);
    if (!vboAlloc.IsValid())
    {
        ClearPending();
        return;
    }

    std::memcpy(vboAlloc.Mapped, TileData.data(), instanceBytes);

    const VkBuffer ringBuffer = Buffers->GetBuffer(vboAlloc.Buffer);
    if (ringBuffer == VK_NULL_HANDLE)
    {
        ClearPending();
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

    // One draw call per batch, in sorted order.
    // firstInstance offsets into the flat tile VBO so batches are drawn
    // in z-order without any re-copy or reorder of the tile data.
    for (const Batch& batch : Batches)
        vkCmdDraw(frame.Cmd, 6, batch.Count, 0, batch.Offset);

    ClearPending();
}
