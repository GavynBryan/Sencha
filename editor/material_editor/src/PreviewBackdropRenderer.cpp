#include "PreviewBackdropRenderer.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <shaders/kPreviewBackdropVertSpv.h>
#include <shaders/kPreviewBackdropFragSpv.h>

namespace
{
struct BackdropPushConstants
{
    float ExtentX;  // target size in pixels
    float ExtentY;
    float CellPx;
    float GlowPx;
    Vec4 LineColor; // linear RGB; w = intensity
};
static_assert(sizeof(BackdropPushConstants) == 32, "BackdropPushConstants must be 32 bytes");
}

void PreviewBackdropRenderer::Setup(const RendererServices& services)
{
    Device    = services.Device->GetDevice();
    Shaders   = services.Shaders;
    Pipelines = services.Pipelines;

    VertexShader = Shaders->CreateModuleFromSpirv(
        kPreviewBackdropVertSpv, kPreviewBackdropVertSpvWordCount, "Preview backdrop vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kPreviewBackdropFragSpv, kPreviewBackdropFragSpvWordCount, "Preview backdrop fragment");

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(BackdropPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pcRange;

    vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &PipelineLayout);
}

void PreviewBackdropRenderer::Draw(VkCommandBuffer cmd,
                                   VkExtent2D targetExtent,
                                   VkFormat colorFormat,
                                   VkFormat depthFormat,
                                   const PreviewBackdropStyle& style)
{
    if (targetExtent.width == 0 || targetExtent.height == 0
        || PipelineLayout == VK_NULL_HANDLE)
        return;

    // Lazy pipeline creation — invalidated if formats change. Opaque fill, no depth.
    if (Pipeline == VK_NULL_HANDLE || CachedColor != colorFormat || CachedDepth != depthFormat)
    {
        ColorBlendAttachmentDesc blend{};
        blend.BlendEnable = false;

        GraphicsPipelineDesc desc{};
        desc.VertexShader   = VertexShader;
        desc.FragmentShader = FragmentShader;
        desc.Layout         = PipelineLayout;
        desc.CullMode       = VK_CULL_MODE_NONE;
        desc.FrontFace      = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        desc.DepthTest      = false;
        desc.DepthWrite     = false;
        desc.DepthCompare   = VK_COMPARE_OP_ALWAYS;
        desc.ColorBlend     = { blend };
        desc.ColorFormats   = { colorFormat };
        desc.DepthFormat    = depthFormat;

        Pipeline    = Pipelines->GetGraphicsPipeline(desc);
        CachedColor = colorFormat;
        CachedDepth = depthFormat;
    }
    if (Pipeline == VK_NULL_HANDLE)
        return;

    VkViewport viewport{};
    viewport.width    = static_cast<float>(targetExtent.width);
    viewport.height   = static_cast<float>(targetExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = targetExtent;

    BackdropPushConstants push{};
    push.ExtentX   = viewport.width;
    push.ExtentY   = viewport.height;
    push.CellPx    = style.CellPx;
    push.GlowPx    = style.GlowPx;
    push.LineColor = style.LineColor;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void PreviewBackdropRenderer::Teardown()
{
    if (Shaders != nullptr)
    {
        Shaders->Destroy(VertexShader);
        Shaders->Destroy(FragmentShader);
        VertexShader   = {};
        FragmentShader = {};
    }
    if (Device != VK_NULL_HANDLE && PipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        PipelineLayout = VK_NULL_HANDLE;
    }
    Pipeline    = VK_NULL_HANDLE;
    CachedColor = VK_FORMAT_UNDEFINED;
    CachedDepth = VK_FORMAT_UNDEFINED;
    Shaders     = nullptr;
    Pipelines   = nullptr;
    Device      = VK_NULL_HANDLE;
}
