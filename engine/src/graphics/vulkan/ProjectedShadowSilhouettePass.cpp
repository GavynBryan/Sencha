#include <graphics/vulkan/ProjectedShadowSilhouettePass.h>

#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanSamplerCache.h>

#include <shaders/kProjectedShadowSilhouetteVertSpv.h>
#include <shaders/kProjectedShadowSilhouetteFragSpv.h>

namespace
{
// The silhouette is coverage, not depth or colour: one channel is the whole
// story, and R8 keeps a 512^2 atlas at 256 KB per frame in flight.
constexpr VkFormat kSilhouetteFormat = VK_FORMAT_R8_UNORM;
}

void ProjectedShadowSilhouettePass::Setup(const RendererServices& services,
                                          std::uint32_t vertexStride)
{
    Services = services;
    VertexStride = vertexStride;
    Store.Setup(services);

    VertexShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowSilhouetteVertSpv, kProjectedShadowSilhouetteVertSpvWordCount,
        "Projected shadow silhouette vertex");
    FragmentShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowSilhouetteFragSpv, kProjectedShadowSilhouetteFragSpvWordCount,
        "Projected shadow silhouette fragment");

    const std::vector<VkPushConstantRange> push{
        VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4) },
    };
    PipelineLayout = services.Descriptors->GetPipelineLayout(push);

    RenderTargetDesc desc{};
    desc.Extent = { 512, 512 }; // resized to the live grid on first Draw
    desc.ColorFormat = kSilhouetteFormat;
    desc.Read = RenderTargetRead::Bindless;
    desc.Sampler = services.Samplers->GetLinearClamp();
    desc.DebugName = "projected_shadow_silhouettes";
    Atlas = Store.Create(desc);

    (void)EnsurePipeline(kSilhouetteFormat);
}

void ProjectedShadowSilhouettePass::Teardown()
{
    Store.Teardown();
    Atlas = {};
    BindlessIndex = UINT32_MAX;
    if (Services.Shaders != nullptr)
    {
        Services.Shaders->Destroy(VertexShader);
        Services.Shaders->Destroy(FragmentShader);
    }
    VertexShader = {};
    FragmentShader = {};
    Pipeline.Reset();
    PipelineLayout = VK_NULL_HANDLE;
}

bool ProjectedShadowSilhouettePass::EnsurePipeline(VkFormat colorFormat)
{
    if (PipelineLayout == VK_NULL_HANDLE)
        return false;

    return Pipeline.Ensure(
        AttachmentFormatKey{ colorFormat, VK_FORMAT_UNDEFINED },
        [&](std::size_t) {
            GraphicsPipelineDesc desc{};
            desc.VertexShader = VertexShader;
            desc.FragmentShader = FragmentShader;
            desc.Layout = PipelineLayout;
            desc.VertexBindings = {
                { 0, VertexStride, VK_VERTEX_INPUT_RATE_VERTEX },
            };
            desc.VertexAttributes = {
                { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
            };
            // No culling: a silhouette wants every triangle's coverage, and
            // rest-pose meshes may face either way about the light.
            desc.CullMode = VK_CULL_MODE_NONE;
            desc.DepthTest = false;
            desc.DepthWrite = false;
            desc.ColorBlend = { ColorBlendAttachmentDesc{} };
            desc.ColorFormats = { colorFormat };
            desc.DepthFormat = VK_FORMAT_UNDEFINED;
            return Services.Pipelines->GetGraphicsPipeline(desc);
        });
}

bool ProjectedShadowSilhouettePass::Draw(const FrameContext& frame,
                                         const ProjectedSilhouetteInput& input)
{
    BindlessIndex = UINT32_MAX;
    if (input.Casters.empty() || VertexStride == 0)
        return false;
    if (!EnsurePipeline(kSilhouetteFormat))
        return false;

    Store.BeginFrame(frame.FrameInFlightIndex, frame.Retirement);
    const std::uint32_t extent = input.TilesPerRow * input.TilePixels;
    Store.SetExtent(Atlas, { extent, extent });
    const std::optional<RenderTargetView> view = Store.Acquire(Atlas);
    if (!view.has_value() || view->ColorView == VK_NULL_HANDLE)
        return false;

    // Whatever the atlas held (UNDEFINED on first use, SHADER_READ after) is
    // discarded: every tile is re-rendered every frame.
    VulkanBarriers::ImageTransition toColor{};
    toColor.Image = view->ColorImage;
    toColor.OldLayout = *view->ColorLayout == VK_IMAGE_LAYOUT_UNDEFINED
        ? VK_IMAGE_LAYOUT_UNDEFINED
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toColor.NewLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.SrcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toColor.DstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.SrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toColor.DstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColor.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VulkanBarriers::TransitionImage(frame.Cmd, toColor);

    RenderScopeDesc scope{};
    scope.Area.offset = { 0, 0 };
    scope.Area.extent = view->Extent;
    scope.Color.View = view->ColorView;
    scope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Color.Clear.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    scope.ColorFormat = kSilhouetteFormat;
    scope.Phase = RenderPhase::Offscreen;

    {
        const RenderScope rendering(frame, scope);
        const VkPipeline pipeline = Pipeline.Get(0);
        if (pipeline == VK_NULL_HANDLE)
            return false;
        vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        for (std::uint32_t casterIndex = 0; casterIndex < input.Casters.size();
             ++casterIndex)
        {
            const ProjectedSilhouetteCasterDraw& caster = input.Casters[casterIndex];

            const std::int32_t tileX = static_cast<std::int32_t>(
                (casterIndex % input.TilesPerRow) * input.TilePixels);
            const std::int32_t tileY = static_cast<std::int32_t>(
                (casterIndex / input.TilesPerRow) * input.TilePixels);

            VkViewport viewport{};
            viewport.x = static_cast<float>(tileX);
            viewport.y = static_cast<float>(tileY);
            viewport.width = static_cast<float>(input.TilePixels);
            viewport.height = static_cast<float>(input.TilePixels);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            VkRect2D scissor{};
            scissor.offset = { tileX, tileY };
            scissor.extent = { input.TilePixels, input.TilePixels };
            vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);
            vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);

            const Mat4 mvp = caster.Mvp.Transposed();
            vkCmdPushConstants(frame.Cmd, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(mvp), &mvp);

            for (std::uint32_t s = 0; s < caster.SectionCount; ++s)
            {
                const ProjectedSilhouetteSectionDraw& section =
                    input.Sections[caster.FirstSection + s];
                const VkBuffer vertexBuffer =
                    Services.Buffers->GetBuffer(section.Vertex);
                const VkBuffer indexBuffer =
                    Services.Buffers->GetBuffer(section.Index);
                if (vertexBuffer == VK_NULL_HANDLE || indexBuffer == VK_NULL_HANDLE)
                    continue;
                const VkDeviceSize zero = 0;
                vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &zero);
                vkCmdBindIndexBuffer(frame.Cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(frame.Cmd, section.IndexCount, 1,
                                 section.IndexOffset, 0, 0);
            }
        }
    }

    VulkanBarriers::ImageTransition toRead{};
    toRead.Image = view->ColorImage;
    toRead.OldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toRead.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.SrcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toRead.DstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toRead.SrcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toRead.DstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toRead.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VulkanBarriers::TransitionImage(frame.Cmd, toRead);
    *view->ColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    BindlessIndex = view->BindlessIndex;
    return BindlessIndex != UINT32_MAX;
}
