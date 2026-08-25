#pragma once

#include "viewport/EditorViewport.h"

#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/GpuFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <math/Mat.h>
#include <math/Vec.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

// What one editor immediate-mode pipeline differs by: its shader pair, vertex
// attribute layout, topology, and whether it writes depth. Everything else the
// editor overlay pipelines share — scratch upload, viewport/scissor, the single
// ViewProjection push constant, format-cached pipeline, and the depth-off "on
// top" variant — lives in the template below.
struct EditorImmediatePipelineConfig
{
    const std::uint32_t* VertexSpirv = nullptr;
    std::size_t          VertexWordCount = 0;
    const char*          VertexName = "";
    const std::uint32_t* FragmentSpirv = nullptr;
    std::size_t          FragmentWordCount = 0;
    const char*          FragmentName = "";
    std::vector<VertexInputAttributeDesc> Attributes;
    VkPrimitiveTopology  Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags      CullMode = VK_CULL_MODE_NONE;
    bool                 DepthWrite = false;
    // 0 = binding 0 is per-vertex data, one vertex drawn per element.
    // N > 0 = binding 0 is per-instance data; each submitted record draws N
    // vertices that the vertex shader derives from gl_VertexIndex (the
    // wide-line quad expansion), cutting upload size by the expansion factor.
    std::uint32_t        InstanceExpansion = 0;
    ColorBlendAttachmentDesc Blend{}; // default = opaque (no blend)
    // Polygon offset applied to the depth-tested slot only (on-top draws ignore depth).
    float                DepthBiasConstant = 0.0f;
    float                DepthBiasSlope = 0.0f;
};

// The shared editor immediate-mode pipeline: uploads a per-frame vertex span to
// scratch and draws it into a viewport under one ViewProjection push constant.
// Templated on the vertex type only for the stride; the layout comes from the
// config, so a new editor vertex format is a config + shaders — never another
// copy of this Vulkan plumbing. (Consolidates the former line + solid pipelines,
// extending the W5 line consolidation rather than re-duplicating it.)
template <typename TVertex>
class EditorImmediatePipeline
{
public:
    void Setup(const RendererServices& services, EditorImmediatePipelineConfig config)
    {
        Config = std::move(config);
        Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;
        Shaders = services.Shaders;
        Pipelines = services.Pipelines;
        Scratch = services.Scratch;
        Buffers = services.Buffers;
        Log = services.Logging != nullptr ? &services.Logging->GetLogger<EditorImmediatePipeline>() : nullptr;

        VertexShader = Shaders->CreateModuleFromSpirv(
            Config.VertexSpirv, Config.VertexWordCount, Config.VertexName);
        FragmentShader = Shaders->CreateModuleFromSpirv(
            Config.FragmentSpirv, Config.FragmentWordCount, Config.FragmentName);

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;
        vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &PipelineLayout);
    }

    // onTop = true draws without depth-testing (overlays visible through geometry);
    // false depth-tests against the scene. DepthWrite comes from the config.
    // The camera is the view's, handed in rather than rebuilt from the
    // viewport: one view renders through one camera, and a submission that
    // derived its own could disagree with the pass beside it.
    void Submit(const FrameContext& frame, const EditorViewport& viewport,
                const CameraRenderData& camera,
                std::span<const TVertex> vertices, bool onTop = false)
    {
        if (PipelineLayout == VK_NULL_HANDLE || Scratch == nullptr || Buffers == nullptr
            || frame.DepthFormat == VK_FORMAT_UNDEFINED || vertices.empty())
            return;

        const VkPipeline pipeline = EnsurePipeline(frame, onTop);
        if (pipeline == VK_NULL_HANDLE)
            return;

        const float vpWidth = viewport.RegionMax.x - viewport.RegionMin.x;
        const float vpHeight = viewport.RegionMax.y - viewport.RegionMin.y;
        if (vpWidth <= 1.0f || vpHeight <= 1.0f)
            return;

        if (vertices.size() > std::numeric_limits<VkDeviceSize>::max() / sizeof(TVertex))
        {
            if (Log != nullptr && !LoggedOverflow)
            {
                Log->Error("{}: dropped submission of {} vertices; byte count overflowed",
                           Config.VertexName, vertices.size());
                LoggedOverflow = true;
            }
            return;
        }

        const VkDeviceSize byteCount = sizeof(TVertex) * vertices.size();
        const auto allocation = Scratch->AllocateVertex(byteCount, ScratchTag::ImmediateVertices);
        if (!allocation.IsValid())
        {
            // The scratch error names no caller; identify the pipeline and the
            // vertex count so an oversized submission can be traced to its
            // builder. Logged once per failure streak to avoid per-frame spam.
            if (Log != nullptr && !LoggedOverflow)
            {
                Log->Error("{}: dropped submission of {} vertices ({} bytes) that exceeded frame scratch",
                           Config.VertexName, vertices.size(), static_cast<uint64_t>(byteCount));
                LoggedOverflow = true;
            }
            return;
        }
        LoggedOverflow = false;

        std::memcpy(allocation.Mapped, vertices.data(), static_cast<size_t>(byteCount));

        VkViewport vkViewport{};
        vkViewport.x = viewport.RegionMin.x;
        vkViewport.y = viewport.RegionMin.y;
        vkViewport.width = vpWidth;
        vkViewport.height = vpHeight;
        vkViewport.minDepth = 0.0f;
        vkViewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {
            static_cast<int32_t>(viewport.RegionMin.x),
            static_cast<int32_t>(viewport.RegionMin.y),
        };
        scissor.extent = { static_cast<uint32_t>(vpWidth), static_cast<uint32_t>(vpHeight) };

        const PushConstants push{
            .ViewProjection = camera.ViewProjection.Transposed(),
            .ViewportPixels = Vec2d{ vpWidth, vpHeight },
        };

        VkBuffer vertexBuffer = Buffers->GetBuffer(allocation.Buffer);
        VkDeviceSize vertexOffset = allocation.Offset;

        vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetViewport(frame.Cmd, 0, 1, &vkViewport);
        vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);
        vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &vertexOffset);
        vkCmdPushConstants(frame.Cmd, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        if (Config.InstanceExpansion > 0)
            vkCmdDraw(frame.Cmd, Config.InstanceExpansion, static_cast<uint32_t>(vertices.size()), 0, 0);
        else
            vkCmdDraw(frame.Cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    }

    // Switch cull mode at runtime (cvar toggle). Nulls the cached pipelines so the
    // next Submit re-fetches the variant from the shared cache (which keys on cull mode).
    void SetCullMode(VkCullModeFlags mode)
    {
        if (mode == Config.CullMode)
            return;
        Config.CullMode = mode;
        Pipeline = VK_NULL_HANDLE;
        PipelineOnTop = VK_NULL_HANDLE;
    }

    [[nodiscard]] std::size_t MaxScratchVerticesPerSubmit() const
    {
        if (Scratch == nullptr)
            return 0;
        return static_cast<std::size_t>(Scratch->GetBytesPerFrame() / sizeof(TVertex));
    }

    void Teardown()
    {
        if (Shaders != nullptr)
        {
            Shaders->Destroy(VertexShader);
            Shaders->Destroy(FragmentShader);
            VertexShader = {};
            FragmentShader = {};
        }
        if (Device != VK_NULL_HANDLE && PipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
            PipelineLayout = VK_NULL_HANDLE;
        }
        Pipeline = VK_NULL_HANDLE;
        PipelineOnTop = VK_NULL_HANDLE;
        CachedColor = VK_FORMAT_UNDEFINED;
        CachedDepth = VK_FORMAT_UNDEFINED;
        Buffers = nullptr;
        Scratch = nullptr;
        Pipelines = nullptr;
        Shaders = nullptr;
        Log = nullptr;
        Device = VK_NULL_HANDLE;
    }

private:
    struct PushConstants
    {
        Mat4  ViewProjection;
        Vec2d ViewportPixels; // viewport size in pixels, for screen-space shaders
    };

    VkPipeline EnsurePipeline(const FrameContext& frame, bool onTop)
    {
        if (CachedColor != frame.TargetFormat || CachedDepth != frame.DepthFormat)
        {
            Pipeline = VK_NULL_HANDLE;
            PipelineOnTop = VK_NULL_HANDLE;
            CachedColor = frame.TargetFormat;
            CachedDepth = frame.DepthFormat;
        }

        VkPipeline& slot = onTop ? PipelineOnTop : Pipeline;
        if (slot != VK_NULL_HANDLE)
            return slot;

        GraphicsPipelineDesc desc{};
        desc.VertexShader = VertexShader;
        desc.FragmentShader = FragmentShader;
        desc.Layout = PipelineLayout;
        desc.Topology = Config.Topology;
        desc.VertexBindings = { { 0, sizeof(TVertex),
                                  Config.InstanceExpansion > 0 ? VK_VERTEX_INPUT_RATE_INSTANCE
                                                               : VK_VERTEX_INPUT_RATE_VERTEX } };
        desc.VertexAttributes = Config.Attributes;
        desc.CullMode = Config.CullMode;
        desc.DepthTest = !onTop; // on-top overlays ignore depth so they're never occluded
        desc.DepthWrite = Config.DepthWrite;
        desc.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
        desc.DepthBiasEnable = !onTop && (Config.DepthBiasConstant != 0.0f || Config.DepthBiasSlope != 0.0f);
        desc.DepthBiasConstant = Config.DepthBiasConstant;
        desc.DepthBiasSlope = Config.DepthBiasSlope;
        desc.ColorBlend = { Config.Blend };
        desc.ColorFormats = { frame.TargetFormat };
        desc.DepthFormat = frame.DepthFormat;
        slot = Pipelines->GetGraphicsPipeline(desc);
        return slot;
    }

    EditorImmediatePipelineConfig Config;
    VulkanBufferService* Buffers = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    GpuFrameScratch* Scratch = nullptr;
    Logger* Log = nullptr;
    bool LoggedOverflow = false;
    VkDevice Device = VK_NULL_HANDLE;
    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;       // depth-tested
    VkPipeline PipelineOnTop = VK_NULL_HANDLE;  // depth-test disabled
    VkFormat CachedColor = VK_FORMAT_UNDEFINED;
    VkFormat CachedDepth = VK_FORMAT_UNDEFINED;
};
