#include "ViewportBackdropRenderer.h"

#include "viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <shaders/kViewportBackdropVertSpv.h>
#include <shaders/kViewportBackdropFragSpv.h>

#include <algorithm>
#include <cmath>

namespace
{
struct BackdropPushConstants
{
    float TopColor[4]; // linear RGBA
    float BotColor[4]; // linear RGBA
};
static_assert(sizeof(BackdropPushConstants) == 32, "BackdropPushConstants must be 32 bytes");

float ToLinear(float s)
{
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

// sRGB hex -> linear, into dst[0..3] (alpha forced opaque).
void Hex(float dst[4], unsigned int rgb)
{
    dst[0] = ToLinear(((rgb >> 16) & 0xFF) / 255.0f);
    dst[1] = ToLinear(((rgb >> 8) & 0xFF) / 255.0f);
    dst[2] = ToLinear((rgb & 0xFF) / 255.0f);
    dst[3] = 1.0f;
}
}

void ViewportBackdropRenderer::Setup(const RendererServices& services)
{
    Device    = services.Device->GetDevice();
    Shaders   = services.Shaders;
    Pipelines = services.Pipelines;

    VertexShader = Shaders->CreateModuleFromSpirv(
        kViewportBackdropVertSpv, kViewportBackdropVertSpvWordCount, "Viewport backdrop vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kViewportBackdropFragSpv, kViewportBackdropFragSpvWordCount, "Viewport backdrop fragment");

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

void ViewportBackdropRenderer::DrawViewport(VkCommandBuffer cmd,
                                            const EditorViewport& viewport,
                                            VkExtent2D targetExtent,
                                            VkFormat colorFormat,
                                            VkFormat depthFormat)
{
    const float vpWidth  = viewport.RegionMax.x - viewport.RegionMin.x;
    const float vpHeight = viewport.RegionMax.y - viewport.RegionMin.y;
    if (vpWidth <= 1.0f || vpHeight <= 1.0f || PipelineLayout == VK_NULL_HANDLE)
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

    const int32_t  offX = static_cast<int32_t>(viewport.RegionMin.x);
    const int32_t  offY = static_cast<int32_t>(viewport.RegionMin.y);
    const uint32_t endX = std::min(targetExtent.width, static_cast<uint32_t>(viewport.RegionMax.x));
    const uint32_t endY = std::min(targetExtent.height, static_cast<uint32_t>(viewport.RegionMax.y));

    VkRect2D scissor{};
    scissor.offset = { offX, offY };
    scissor.extent = {
        endX > static_cast<uint32_t>(offX) ? endX - static_cast<uint32_t>(offX) : 0u,
        endY > static_cast<uint32_t>(offY) ? endY - static_cast<uint32_t>(offY) : 0u,
    };
    if (scissor.extent.width == 0 || scissor.extent.height == 0)
        return;

    VkViewport vkViewport{};
    vkViewport.x        = static_cast<float>(offX);
    vkViewport.y        = static_cast<float>(offY);
    vkViewport.width    = vpWidth;
    vkViewport.height   = vpHeight;
    vkViewport.minDepth = 0.0f;
    vkViewport.maxDepth = 1.0f;

    // Near-black backdrop. Perspective gets a faint top->horizon lift; ortho is flat.
    BackdropPushConstants push{};
    if (viewport.Camera.ActiveMode == EditorCamera::Mode::Perspective)
    {
        Hex(push.TopColor, 0x07090B);
        Hex(push.BotColor, 0x0C1417);
    }
    else
    {
        Hex(push.TopColor, 0x0A0D0F);
        Hex(push.BotColor, 0x0A0D0F);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
    vkCmdSetViewport(cmd, 0, 1, &vkViewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void ViewportBackdropRenderer::Teardown()
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
