#include <graphics/vulkan/ProjectedShadowProjectPass.h>

#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanSamplerCache.h>

#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>

#include <shaders/kProjectedShadowProjectVertSpv.h>
#include <shaders/kProjectedShadowProjectFragSpv.h>
#include <shaders/kProjectedShadowCompositeVertSpv.h>
#include <shaders/kProjectedShadowCompositeFragSpv.h>

#include <cstring>

static_assert(offsetof(ProjectedShadowProjectUniform, CameraViewProjection) == 0);
static_assert(offsetof(ProjectedShadowProjectUniform, ShadowViewProjection) == 64);
static_assert(offsetof(ProjectedShadowProjectUniform, TileScaleBias) == 128);
static_assert(offsetof(ProjectedShadowProjectUniform, Params) == 144);
static_assert(sizeof(ProjectedShadowProjectUniform) == 160);

namespace
{
// One channel of shadow amount; R8's 256 steps are ~1/460 of a colour step
// at the default darkness, below quantization visibility.
constexpr VkFormat kMaskFormat = VK_FORMAT_R8_UNORM;

// The composite's fragment push block; mirrors
// projected_shadow_composite.frag.glsl.
struct CompositePush
{
    float UvScaleX = 1.0f;
    float UvScaleY = 1.0f;
    float Darkness = 0.0f;
    std::uint32_t MaskIndex = 0;
};
static_assert(sizeof(CompositePush) == 16);
}

void ProjectedShadowProjectPass::Setup(const RendererServices& services)
{
    Services = services;
    Store.Setup(services);

    // R8 colour-attachment blending is spec-mandatory, so this should never
    // fire on a conformant driver -- but MAX blending on an unsupported
    // format is undefined, and shadows off beats undefined. Degrade loudly.
    BlendCapable = false;
    if (services.PhysicalDevice != nullptr)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(
            services.PhysicalDevice->GetPhysicalDevice(), kMaskFormat, &props);
        BlendCapable = (props.optimalTilingFeatures
                        & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0;
    }
    if (!BlendCapable && services.Logging != nullptr)
    {
        services.Logging->GetLogger<ProjectedShadowProjectPass>().Error(
            "projected shadows disabled: R8_UNORM lacks colour-attachment "
            "blending on this device (non-conformant driver?)");
    }

    VertexShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowProjectVertSpv, kProjectedShadowProjectVertSpvWordCount,
        "Projected shadow project vertex");
    FragmentShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowProjectFragSpv, kProjectedShadowProjectFragSpvWordCount,
        "Projected shadow project fragment");
    CompositeVertexShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowCompositeVertSpv, kProjectedShadowCompositeVertSpvWordCount,
        "Projected shadow composite vertex");
    CompositeFragmentShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowCompositeFragSpv, kProjectedShadowCompositeFragSpvWordCount,
        "Projected shadow composite fragment");

    const std::vector<VkPushConstantRange> push{
        VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4) },
    };
    PipelineLayout = services.Descriptors->GetPipelineLayout(push);
    const std::vector<VkPushConstantRange> compositePush{
        VkPushConstantRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CompositePush) },
    };
    CompositePipelineLayout = services.Descriptors->GetPipelineLayout(compositePush);

    // Declare the block this pass reads through the shared dynamic-offset
    // binding; the cache keeps the largest declaration.
    services.Descriptors->RequireFrameUniformRange(
        services.Scratch->GetBuffer(), sizeof(ProjectedShadowProjectUniform));

    RenderTargetDesc desc{};
    desc.Extent = { 512, 512 }; // resized to the live view on first DrawMask
    desc.ColorFormat = kMaskFormat;
    desc.Read = RenderTargetRead::Bindless;
    desc.Sampler = services.Samplers->GetLinearClamp();
    desc.DebugName = "projected_shadow_mask";
    Mask = Store.Create(desc);
}

void ProjectedShadowProjectPass::Teardown()
{
    Store.Teardown();
    Mask = {};
    MaskBindlessIndex = UINT32_MAX;
    if (Services.Shaders != nullptr)
    {
        Services.Shaders->Destroy(VertexShader);
        Services.Shaders->Destroy(FragmentShader);
        Services.Shaders->Destroy(CompositeVertexShader);
        Services.Shaders->Destroy(CompositeFragmentShader);
    }
    VertexShader = {};
    FragmentShader = {};
    CompositeVertexShader = {};
    CompositeFragmentShader = {};
    Pipeline.Reset();
    CompositePipeline.Reset();
    PipelineLayout = VK_NULL_HANDLE;
    CompositePipelineLayout = VK_NULL_HANDLE;
}

bool ProjectedShadowProjectPass::EnsureMaskPipeline(VkFormat depthFormat)
{
    if (PipelineLayout == VK_NULL_HANDLE || VertexStride == 0 || !BlendCapable)
        return false;

    return Pipeline.Ensure(
        AttachmentFormatKey{ kMaskFormat, depthFormat },
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
            desc.FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            desc.CullMode = VK_CULL_MODE_BACK_BIT;
            // Land exactly on the opaque surface, never behind it; the depth
            // attachment is the host's, read-only in this scope.
            desc.DepthTest = true;
            desc.DepthWrite = false;
            desc.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
            // The union: overlapping casters keep the strongest amount
            // instead of accumulating. This is the whole point of the mask.
            ColorBlendAttachmentDesc blend{};
            blend.BlendEnable = true;
            blend.SrcColor = VK_BLEND_FACTOR_ONE;
            blend.DstColor = VK_BLEND_FACTOR_ONE;
            blend.ColorOp = VK_BLEND_OP_MAX;
            blend.SrcAlpha = VK_BLEND_FACTOR_ONE;
            blend.DstAlpha = VK_BLEND_FACTOR_ONE;
            blend.AlphaOp = VK_BLEND_OP_MAX;
            desc.ColorBlend = { blend };
            desc.ColorFormats = { kMaskFormat };
            desc.DepthFormat = depthFormat;
            return Services.Pipelines->GetGraphicsPipeline(desc);
        });
}

bool ProjectedShadowProjectPass::EnsureCompositePipeline(const FrameContext& frame)
{
    if (CompositePipelineLayout == VK_NULL_HANDLE)
        return false;

    return CompositePipeline.Ensure(
        AttachmentFormatKey{ frame.TargetFormat, frame.DepthFormat },
        [&](std::size_t) {
            GraphicsPipelineDesc desc{};
            desc.VertexShader = CompositeVertexShader;
            desc.FragmentShader = CompositeFragmentShader;
            desc.Layout = CompositePipelineLayout;
            desc.CullMode = VK_CULL_MODE_NONE;
            desc.DepthTest = false;
            desc.DepthWrite = false;
            // Multiplicative darkening, applied once for the whole frame's
            // mask: dst * srcColor, dst alpha preserved for the editor's UI
            // composite.
            ColorBlendAttachmentDesc blend{};
            blend.BlendEnable = true;
            blend.SrcColor = VK_BLEND_FACTOR_ZERO;
            blend.DstColor = VK_BLEND_FACTOR_SRC_COLOR;
            blend.SrcAlpha = VK_BLEND_FACTOR_ZERO;
            blend.DstAlpha = VK_BLEND_FACTOR_ONE;
            desc.ColorBlend = { blend };
            desc.ColorFormats = { frame.TargetFormat };
            desc.DepthFormat = frame.DepthFormat;
            return Services.Pipelines->GetGraphicsPipeline(desc);
        });
}

bool ProjectedShadowProjectPass::DrawMask(const FrameContext& frame,
                                          const ProjectedShadowProjectionInput& input,
                                          VkExtent2D maskExtent)
{
    MaskBindlessIndex = UINT32_MAX;
    if (input.Casters.empty())
        return false;
    VertexStride = input.VertexStride;
    if (!EnsureMaskPipeline(frame.DepthFormat))
        return false;
    const VkPipeline pipeline = Pipeline.Get(0);
    if (pipeline == VK_NULL_HANDLE)
        return false;

    Store.BeginFrame(frame.FrameInFlightIndex, frame.Retirement);
    Store.SetExtent(Mask, maskExtent);
    const std::optional<RenderTargetView> view = Store.Acquire(Mask);
    if (!view.has_value() || view->ColorView == VK_NULL_HANDLE
        || view->BindlessIndex == UINT32_MAX)
        return false;

    // Whatever the mask held is stale; only the region this view uses is
    // cleared (the scope's render area), the rest is never sampled.
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
    scope.Area.extent = frame.TargetExtent; // the view's region, origin-anchored
    scope.Color.View = view->ColorView;
    scope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Color.Clear.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    scope.ColorFormat = kMaskFormat;
    // The host's depth, tested read-only; its contents must survive for the
    // resumed instance, so it is loaded and stored.
    scope.Depth.View = frame.DepthView;
    scope.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    scope.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    scope.DepthFormat = frame.DepthFormat;
    scope.Phase = frame.Phase;

    {
        const RenderScope rendering(frame, scope);
        vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkViewport viewport{};
        viewport.width = static_cast<float>(frame.TargetExtent.width);
        viewport.height = static_cast<float>(frame.TargetExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);

        const VkDescriptorSet bindlessSet = Services.Descriptors->GetBindlessSet();
        vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                PipelineLayout, 1, 1, &bindlessSet, 0, nullptr);

        for (const ProjectedShadowProjection& caster : input.Casters)
        {
            if (caster.ScissorWidth == 0 || caster.ScissorHeight == 0)
                continue;
            ProjectedShadowProjectUniform uniform = caster.Uniform;
            uniform.CameraViewProjection = uniform.CameraViewProjection.Transposed();
            uniform.ShadowViewProjection = uniform.ShadowViewProjection.Transposed();
            const auto allocation = Services.Scratch->AllocateUniform(sizeof(uniform));
            if (!allocation.IsValid())
                continue;
            std::memcpy(allocation.Mapped, &uniform, sizeof(uniform));

            const auto dynamicOffset = static_cast<std::uint32_t>(allocation.Offset);
            const VkDescriptorSet frameSet = Services.Descriptors->GetFrameSet();
            vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    PipelineLayout, 0, 1, &frameSet, 1, &dynamicOffset);

            VkRect2D scissor{};
            scissor.offset = { caster.ScissorX, caster.ScissorY };
            scissor.extent = { caster.ScissorWidth, caster.ScissorHeight };
            vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);

            for (std::uint32_t r = 0; r < caster.ReceiverCount; ++r)
            {
                const ProjectedReceiverDraw& receiver =
                    input.Receivers[caster.FirstReceiver + r];
                const VkBuffer vertexBuffer = Services.Buffers->GetBuffer(receiver.Vertex);
                const VkBuffer indexBuffer = Services.Buffers->GetBuffer(receiver.Index);
                if (vertexBuffer == VK_NULL_HANDLE || indexBuffer == VK_NULL_HANDLE)
                    continue;

                const Mat4 world = receiver.World.Transposed();
                vkCmdPushConstants(frame.Cmd, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(world), &world);
                const VkDeviceSize zero = 0;
                vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &zero);
                vkCmdBindIndexBuffer(frame.Cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(frame.Cmd, receiver.IndexCount, 1,
                                 receiver.IndexOffset, 0, 0);
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

    MaskBindlessIndex = view->BindlessIndex;
    LastMaskExtent = maskExtent;
    LastViewExtent = frame.TargetExtent;
    return true;
}

void ProjectedShadowProjectPass::Composite(const FrameContext& frame,
                                           float darkness,
                                           std::int32_t unionX, std::int32_t unionY,
                                           std::uint32_t unionWidth,
                                           std::uint32_t unionHeight)
{
    if (MaskBindlessIndex == UINT32_MAX || unionWidth == 0 || unionHeight == 0)
        return;
    if (!EnsureCompositePipeline(frame))
        return;
    const VkPipeline pipeline = CompositePipeline.Get(0);
    if (pipeline == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport viewport{};
    viewport.width = static_cast<float>(frame.TargetExtent.width);
    viewport.height = static_cast<float>(frame.TargetExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.offset = { unionX, unionY };
    scissor.extent = { unionWidth, unionHeight };
    vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);

    const VkDescriptorSet bindlessSet = Services.Descriptors->GetBindlessSet();
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            CompositePipelineLayout, 1, 1, &bindlessSet, 0, nullptr);

    const CompositePush push{
        LastMaskExtent.width != 0
            ? static_cast<float>(LastViewExtent.width) / static_cast<float>(LastMaskExtent.width)
            : 1.0f,
        LastMaskExtent.height != 0
            ? static_cast<float>(LastViewExtent.height) / static_cast<float>(LastMaskExtent.height)
            : 1.0f,
        darkness,
        MaskBindlessIndex,
    };
    vkCmdPushConstants(frame.Cmd, CompositePipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(frame.Cmd, 3, 1, 0, 0);
}
