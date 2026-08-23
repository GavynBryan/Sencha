#include <graphics/vulkan/ProjectedShadowSilhouettePass.h>

#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanSamplerCache.h>

#include <shaders/kProjectedShadowSilhouetteVertSpv.h>
#include <shaders/kProjectedShadowSilhouetteFragSpv.h>
#include <shaders/kProjectedShadowBlurVertSpv.h>
#include <shaders/kProjectedShadowBlurFragSpv.h>
#include <shaders/kProjectedShadowOccluderVertSpv.h>
#include <shaders/kProjectedShadowOccluderFragSpv.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>

namespace
{
// The silhouette is coverage, not depth or colour: one channel is the whole
// story, and R8 keeps the atlas at 1 MB per frame in flight at the default
// 4x4 grid of 256px tiles (the blur scratch doubles that).
constexpr VkFormat kSilhouetteFormat = VK_FORMAT_R8_UNORM;

// Nearest-depth-per-texel via MIN blending: R16_SFLOAT because Vulkan
// mandates colour-attachment blending for it (unlike R16_UNORM). Half
// precision over the fit's [0,1] depth is ~0.5mm-8mm across typical ranges,
// inside the occlusion bias.
constexpr VkFormat kOccluderFormat = VK_FORMAT_R16_SFLOAT;

// The blur's fragment push block: one axis step and the source's bindless
// slot. Layout mirrors projected_shadow_blur.frag.glsl.
struct BlurPush
{
    float StepU = 0.0f;
    float StepV = 0.0f;
    std::uint32_t SourceIndex = 0;
    float Pad = 0.0f;
};
static_assert(sizeof(BlurPush) == 16);
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

    BlurVertexShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowBlurVertSpv, kProjectedShadowBlurVertSpvWordCount,
        "Projected shadow blur vertex");
    BlurFragmentShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowBlurFragSpv, kProjectedShadowBlurFragSpvWordCount,
        "Projected shadow blur fragment");

    const std::vector<VkPushConstantRange> blurPush{
        VkPushConstantRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BlurPush) },
    };
    BlurPipelineLayout = services.Descriptors->GetPipelineLayout(blurPush);

    OccluderVertexShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowOccluderVertSpv, kProjectedShadowOccluderVertSpvWordCount,
        "Projected shadow occluder vertex");
    OccluderFragmentShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowOccluderFragSpv, kProjectedShadowOccluderFragSpvWordCount,
        "Projected shadow occluder fragment");
    const std::vector<VkPushConstantRange> occluderPush{
        VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4) },
    };
    OccluderPipelineLayout = services.Descriptors->GetPipelineLayout(occluderPush);

    // MIN blending on the occluder format is spec-mandatory for R16_SFLOAT;
    // degrade loudly to no-occlusion rather than blend undefined.
    OccluderBlendCapable = false;
    if (services.PhysicalDevice != nullptr)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(
            services.PhysicalDevice->GetPhysicalDevice(), kOccluderFormat, &props);
        OccluderBlendCapable = (props.optimalTilingFeatures
                                & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0;
    }
    if (!OccluderBlendCapable && services.Logging != nullptr)
    {
        services.Logging->GetLogger<ProjectedShadowSilhouettePass>().Error(
            "projected shadow occlusion disabled: R16_SFLOAT lacks "
            "colour-attachment blending on this device (non-conformant driver?)");
    }

    RenderTargetDesc desc{};
    desc.Extent = { 512, 512 }; // resized to the live grid on first Draw
    desc.ColorFormat = kSilhouetteFormat;
    desc.Read = RenderTargetRead::Bindless;
    desc.Sampler = services.Samplers->GetLinearClamp();
    desc.DebugName = "projected_shadow_silhouettes";
    Atlas = Store.Create(desc);
    desc.DebugName = "projected_shadow_blur_scratch";
    BlurScratch = Store.Create(desc);
    desc.ColorFormat = kOccluderFormat;
    desc.DebugName = "projected_shadow_occluders";
    OccluderAtlas = Store.Create(desc);

    (void)EnsurePipeline(kSilhouetteFormat);
    (void)EnsureBlurPipeline(kSilhouetteFormat);
    (void)EnsureOccluderPipeline();
}

void ProjectedShadowSilhouettePass::Teardown()
{
    Store.Teardown();
    Atlas = {};
    BlurScratch = {};
    OccluderAtlas = {};
    BindlessIndex = UINT32_MAX;
    OccluderIndex = UINT32_MAX;
    if (Services.Shaders != nullptr)
    {
        Services.Shaders->Destroy(VertexShader);
        Services.Shaders->Destroy(FragmentShader);
        Services.Shaders->Destroy(BlurVertexShader);
        Services.Shaders->Destroy(BlurFragmentShader);
        Services.Shaders->Destroy(OccluderVertexShader);
        Services.Shaders->Destroy(OccluderFragmentShader);
    }
    VertexShader = {};
    FragmentShader = {};
    BlurVertexShader = {};
    BlurFragmentShader = {};
    OccluderVertexShader = {};
    OccluderFragmentShader = {};
    Pipeline.Reset();
    BlurPipeline.Reset();
    OccluderPipeline.Reset();
    PipelineLayout = VK_NULL_HANDLE;
    BlurPipelineLayout = VK_NULL_HANDLE;
    OccluderPipelineLayout = VK_NULL_HANDLE;
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

bool ProjectedShadowSilhouettePass::EnsureBlurPipeline(VkFormat colorFormat)
{
    if (BlurPipelineLayout == VK_NULL_HANDLE)
        return false;

    return BlurPipeline.Ensure(
        AttachmentFormatKey{ colorFormat, VK_FORMAT_UNDEFINED },
        [&](std::size_t) {
            GraphicsPipelineDesc desc{};
            desc.VertexShader = BlurVertexShader;
            desc.FragmentShader = BlurFragmentShader;
            desc.Layout = BlurPipelineLayout;
            desc.CullMode = VK_CULL_MODE_NONE;
            desc.DepthTest = false;
            desc.DepthWrite = false;
            desc.ColorBlend = { ColorBlendAttachmentDesc{} };
            desc.ColorFormats = { colorFormat };
            desc.DepthFormat = VK_FORMAT_UNDEFINED;
            return Services.Pipelines->GetGraphicsPipeline(desc);
        });
}

bool ProjectedShadowSilhouettePass::BlurAtlas(const FrameContext& frame,
                                              const RenderTargetView& atlas,
                                              VkExtent2D extent,
                                              float softnessTexels)
{
    if (!EnsureBlurPipeline(kSilhouetteFormat))
        return false;
    const VkPipeline pipeline = BlurPipeline.Get(0);
    if (pipeline == VK_NULL_HANDLE)
        return false;

    Store.SetExtent(BlurScratch, extent);
    const std::optional<RenderTargetView> scratch = Store.Acquire(BlurScratch);
    if (!scratch.has_value() || scratch->ColorView == VK_NULL_HANDLE
        || scratch->BindlessIndex == UINT32_MAX || atlas.BindlessIndex == UINT32_MAX)
        return false;

    const float texelStep = softnessTexels / (3.0f * static_cast<float>(extent.width));

    const VkDescriptorSet bindlessSet = Services.Descriptors->GetBindlessSet();
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;
    VkRect2D fullScissor{};
    fullScissor.extent = extent;

    const auto blurInto = [&](const RenderTargetView& target,
                              std::uint32_t sourceIndex, float stepU, float stepV)
    {
        RenderScopeDesc scope{};
        scope.Area.extent = extent;
        scope.Color.View = target.ColorView;
        // Every texel is overwritten by the full-screen triangle.
        scope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        scope.ColorFormat = kSilhouetteFormat;
        scope.Phase = RenderPhase::Offscreen;

        const RenderScope rendering(frame, scope);
        vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);
        vkCmdSetScissor(frame.Cmd, 0, 1, &fullScissor);
        vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                BlurPipelineLayout, 1, 1, &bindlessSet, 0, nullptr);
        const BlurPush push{ stepU, stepV, sourceIndex, 0.0f };
        vkCmdPushConstants(frame.Cmd, BlurPipelineLayout,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(frame.Cmd, 3, 1, 0, 0);
    };

    const auto transition = [&](const RenderTargetView& target,
                                VkImageLayout oldLayout, VkImageLayout newLayout,
                                bool fromShaderRead)
    {
        VulkanBarriers::ImageTransition barrier{};
        barrier.Image = target.ColorImage;
        barrier.OldLayout = oldLayout;
        barrier.NewLayout = newLayout;
        barrier.SrcStage = fromShaderRead ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                                          : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.DstStage = fromShaderRead ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                                          : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.SrcAccess = fromShaderRead ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                                           : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.DstAccess = fromShaderRead ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                                           : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        VulkanBarriers::TransitionImage(frame.Cmd, barrier);
        *target.ColorLayout = newLayout;
    };

    // Horizontal: atlas (already SHADER_READ) -> scratch.
    transition(*scratch,
               *scratch->ColorLayout == VK_IMAGE_LAYOUT_UNDEFINED
                   ? VK_IMAGE_LAYOUT_UNDEFINED
                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    blurInto(*scratch, atlas.BindlessIndex, texelStep, 0.0f);
    transition(*scratch, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);

    // Vertical: scratch -> atlas, which ends SHADER_READ for projection.
    transition(atlas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);
    blurInto(atlas, scratch->BindlessIndex, 0.0f, texelStep);
    transition(atlas, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);
    return true;
}

bool ProjectedShadowSilhouettePass::EnsureOccluderPipeline()
{
    if (OccluderPipelineLayout == VK_NULL_HANDLE || VertexStride == 0
        || !OccluderBlendCapable)
        return false;

    return OccluderPipeline.Ensure(
        AttachmentFormatKey{ kOccluderFormat, VK_FORMAT_UNDEFINED },
        [&](std::size_t) {
            GraphicsPipelineDesc desc{};
            desc.VertexShader = OccluderVertexShader;
            desc.FragmentShader = OccluderFragmentShader;
            desc.Layout = OccluderPipelineLayout;
            desc.VertexBindings = {
                { 0, VertexStride, VK_VERTEX_INPUT_RATE_VERTEX },
            };
            desc.VertexAttributes = {
                { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
            };
            // Both faces block: a shadow must not pass through a wall seen
            // from its far side.
            desc.CullMode = VK_CULL_MODE_NONE;
            desc.DepthTest = false;
            desc.DepthWrite = false;
            // MIN over a tile cleared to 1.0: the blend IS the depth test.
            ColorBlendAttachmentDesc blend{};
            blend.BlendEnable = true;
            blend.SrcColor = VK_BLEND_FACTOR_ONE;
            blend.DstColor = VK_BLEND_FACTOR_ONE;
            blend.ColorOp = VK_BLEND_OP_MIN;
            blend.SrcAlpha = VK_BLEND_FACTOR_ONE;
            blend.DstAlpha = VK_BLEND_FACTOR_ONE;
            blend.AlphaOp = VK_BLEND_OP_MIN;
            desc.ColorBlend = { blend };
            desc.ColorFormats = { kOccluderFormat };
            desc.DepthFormat = VK_FORMAT_UNDEFINED;
            return Services.Pipelines->GetGraphicsPipeline(desc);
        });
}

bool ProjectedShadowSilhouettePass::DrawOccluders(const FrameContext& frame,
                                                  const ProjectedSilhouetteInput& input,
                                                  VkExtent2D extent)
{
    if (input.Occluders.empty() || !EnsureOccluderPipeline())
        return false;
    const VkPipeline pipeline = OccluderPipeline.Get(0);
    if (pipeline == VK_NULL_HANDLE)
        return false;

    Store.SetExtent(OccluderAtlas, extent);
    const std::optional<RenderTargetView> view = Store.Acquire(OccluderAtlas);
    if (!view.has_value() || view->ColorView == VK_NULL_HANDLE
        || view->BindlessIndex == UINT32_MAX)
        return false;

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
    scope.Area.extent = extent;
    scope.Color.View = view->ColorView;
    scope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Color.Clear.color = { { 1.0f, 1.0f, 1.0f, 1.0f } }; // 1.0 = unblocked
    scope.ColorFormat = kOccluderFormat;
    scope.Phase = RenderPhase::Offscreen;

    {
        const RenderScope rendering(frame, scope);
        vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        for (std::uint32_t casterIndex = 0; casterIndex < input.Casters.size();
             ++casterIndex)
        {
            const ProjectedSilhouetteCasterDraw& caster = input.Casters[casterIndex];
            if (caster.OccluderCount == 0)
                continue;

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

            for (std::uint32_t o = 0; o < caster.OccluderCount; ++o)
            {
                const ProjectedSilhouetteOccluderDraw& occluder =
                    input.Occluders[caster.FirstOccluder + o];
                const VkBuffer vertexBuffer =
                    Services.Buffers->GetBuffer(occluder.Vertex);
                const VkBuffer indexBuffer =
                    Services.Buffers->GetBuffer(occluder.Index);
                if (vertexBuffer == VK_NULL_HANDLE || indexBuffer == VK_NULL_HANDLE)
                    continue;
                const Mat4 mvp = occluder.Mvp.Transposed();
                vkCmdPushConstants(frame.Cmd, OccluderPipelineLayout,
                                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);
                const VkDeviceSize zero = 0;
                vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &zero);
                vkCmdBindIndexBuffer(frame.Cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(frame.Cmd, occluder.IndexCount, 1,
                                 occluder.IndexOffset, 0, 0);
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

    OccluderIndex = view->BindlessIndex;
    return true;
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

    // Soften the coverage in atlas space. A failed blur leaves the sharp
    // atlas, which is a valid frame, not a skipped one.
    if (input.SoftnessTexels > 0.0f)
        (void)BlurAtlas(frame, *view, { extent, extent }, input.SoftnessTexels);

    // Nearest receiver depth per tile texel. Failure (or nothing to draw)
    // leaves OccluderIndex invalid and projection skips the occlusion term.
    OccluderIndex = UINT32_MAX;
    (void)DrawOccluders(frame, input, { extent, extent });

    BindlessIndex = view->BindlessIndex;
    return BindlessIndex != UINT32_MAX;
}
