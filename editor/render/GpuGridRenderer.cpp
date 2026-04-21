#include "GpuGridRenderer.h"

#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <shaders/kGridVertSpv.h>
#include <shaders/kGridFragSpv.h>

#include <algorithm>

namespace
{
struct GridPushConstants
{
    Mat4  ViewProj;        // 64 bytes — transposed for GLSL column-major
    Vec3d GridCenter;      // 12 bytes
    float HalfExtent;      //  4 bytes
    Vec3d AxisU;           // 12 bytes
    float Spacing;         //  4 bytes
    Vec3d AxisV;           // 12 bytes
    float SubdivSpacing;   //  4 bytes
    Vec3d CameraPos;       // 12 bytes
    float FadeEnd;         //  4 bytes
    // Total = 128 bytes
};
static_assert(sizeof(GridPushConstants) == 128, "GridPushConstants must be exactly 128 bytes");
}

void GpuGridRenderer::Setup(const RendererServices& services)
{
    Device    = services.Device->GetDevice();
    Shaders   = services.Shaders;
    Pipelines = services.Pipelines;

    VertexShader   = Shaders->CreateModuleFromSpirv(kGridVertSpv, kGridVertSpvWordCount, "Grid vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(kGridFragSpv, kGridFragSpvWordCount, "Grid fragment");

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(GridPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pcRange;

    vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &PipelineLayout);
}

void GpuGridRenderer::DrawViewport(VkCommandBuffer cmd,
                                   const EditorViewport& viewport,
                                   VkExtent2D targetExtent,
                                   VkFormat colorFormat,
                                   VkFormat depthFormat)
{
    const float vpWidth  = viewport.RegionMax.x - viewport.RegionMin.x;
    const float vpHeight = viewport.RegionMax.y - viewport.RegionMin.y;
    if (vpWidth <= 1.0f || vpHeight <= 1.0f)
        return;
    if (PipelineLayout == VK_NULL_HANDLE)
        return;

    // Lazy pipeline creation — invalidated if formats change.
    if (Pipeline == VK_NULL_HANDLE || CachedColor != colorFormat || CachedDepth != depthFormat)
    {
        ColorBlendAttachmentDesc blend{};
        blend.BlendEnable = true;
        blend.SrcColor    = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.DstColor    = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.ColorOp     = VK_BLEND_OP_ADD;
        blend.SrcAlpha    = VK_BLEND_FACTOR_ONE;
        blend.DstAlpha    = VK_BLEND_FACTOR_ZERO;
        blend.AlphaOp     = VK_BLEND_OP_ADD;

        GraphicsPipelineDesc desc{};
        desc.VertexShader   = VertexShader;
        desc.FragmentShader = FragmentShader;
        desc.Layout         = PipelineLayout;
        desc.CullMode       = VK_CULL_MODE_NONE;
        desc.FrontFace      = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        desc.DepthTest      = true;
        desc.DepthWrite     = false;
        desc.DepthCompare   = VK_COMPARE_OP_LESS_OR_EQUAL;
        desc.ColorBlend     = { blend };
        desc.ColorFormats   = { colorFormat };
        desc.DepthFormat    = depthFormat;

        Pipeline      = Pipelines->GetGraphicsPipeline(desc);
        CachedColor   = colorFormat;
        CachedDepth   = depthFormat;
    }

    if (Pipeline == VK_NULL_HANDLE)
        return;

    // --- viewport-region-local scissor + viewport transform -----------------

    const int32_t  offX = static_cast<int32_t>(viewport.RegionMin.x);
    const int32_t  offY = static_cast<int32_t>(viewport.RegionMin.y);
    const uint32_t endX = std::min(targetExtent.width,
                                   static_cast<uint32_t>(viewport.RegionMax.x));
    const uint32_t endY = std::min(targetExtent.height,
                                   static_cast<uint32_t>(viewport.RegionMax.y));

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

    // --- camera + grid push constants ----------------------------------------

    const CameraRenderData renderData = viewport.BuildRenderData();

    const GridPlane grid = viewport.GetGrid();

    const Vec3d gridCenter =
        viewport.Camera.ActiveMode == EditorCamera::Mode::Perspective
        ? grid.Project(viewport.Camera.Position)
        : viewport.Camera.OrthoCenter;

    const float subdivSpacing = grid.Spacing
        / static_cast<float>(std::max(grid.Subdivisions, 1u));

    const float fadeEnd =
        viewport.Camera.ActiveMode == EditorCamera::Mode::Perspective
        ? std::max(50.0f, grid.Spacing * 200.0f)
        : viewport.Camera.Far;

    GridPushConstants push{};
    push.ViewProj      = renderData.ViewProjection.Transposed();
    push.GridCenter    = gridCenter;
    push.HalfExtent    = viewport.Camera.Far;
    push.AxisU         = grid.AxisU;
    push.Spacing       = grid.Spacing;
    push.AxisV         = grid.AxisV;
    push.SubdivSpacing = subdivSpacing;
    push.CameraPos     = renderData.Position;
    push.FadeEnd       = fadeEnd;

    // --- record draw ----------------------------------------------------------

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
    vkCmdSetViewport(cmd, 0, 1, &vkViewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, PipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(cmd, 6, 1, 0, 0);
}

void GpuGridRenderer::Teardown()
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

    Pipeline     = VK_NULL_HANDLE;
    CachedColor  = VK_FORMAT_UNDEFINED;
    CachedDepth  = VK_FORMAT_UNDEFINED;
    Shaders      = nullptr;
    Pipelines    = nullptr;
    Device       = VK_NULL_HANDLE;
}
