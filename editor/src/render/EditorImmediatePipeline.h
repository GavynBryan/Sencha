#pragma once

#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <math/Mat.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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
    void Submit(const FrameContext& frame, const EditorViewport& viewport,
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

        const VkDeviceSize byteCount = sizeof(TVertex) * vertices.size();
        const auto allocation = Scratch->AllocateVertex(byteCount);
        if (!allocation.IsValid())
            return;

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

        const CameraRenderData renderData = viewport.BuildRenderData();
        const PushConstants push{ .ViewProjection = renderData.ViewProjection.Transposed() };

        VkBuffer vertexBuffer = Buffers->GetBuffer(allocation.Buffer);
        VkDeviceSize vertexOffset = allocation.Offset;

        vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetViewport(frame.Cmd, 0, 1, &vkViewport);
        vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);
        vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &vertexOffset);
        vkCmdPushConstants(frame.Cmd, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
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
        Device = VK_NULL_HANDLE;
    }

private:
    struct PushConstants
    {
        Mat4 ViewProjection;
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
        desc.VertexBindings = { { 0, sizeof(TVertex), VK_VERTEX_INPUT_RATE_VERTEX } };
        desc.VertexAttributes = Config.Attributes;
        desc.CullMode = Config.CullMode;
        desc.DepthTest = !onTop; // on-top overlays ignore depth so they're never occluded
        desc.DepthWrite = Config.DepthWrite;
        desc.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
        desc.ColorBlend = { ColorBlendAttachmentDesc{} };
        desc.ColorFormats = { frame.TargetFormat };
        desc.DepthFormat = frame.DepthFormat;
        slot = Pipelines->GetGraphicsPipeline(desc);
        return slot;
    }

    EditorImmediatePipelineConfig Config;
    VulkanBufferService* Buffers = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanFrameScratch* Scratch = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;       // depth-tested
    VkPipeline PipelineOnTop = VK_NULL_HANDLE;  // depth-test disabled
    VkFormat CachedColor = VK_FORMAT_UNDEFINED;
    VkFormat CachedDepth = VK_FORMAT_UNDEFINED;
};
