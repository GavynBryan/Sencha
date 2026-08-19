#include <graphics/vulkan/SkyGradientPass.h>

#include <graphics/vulkan/VulkanDebugLabels.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <shaders/kSkyGradientFragSpv.h>
#include <shaders/kSkyGradientVertSpv.h>

#include <cstddef>

static_assert(offsetof(SkyPushConstants, InverseViewProjection) == 0);
static_assert(offsetof(SkyPushConstants, Top) == 64);
static_assert(offsetof(SkyPushConstants, Bottom) == 80);
static_assert(offsetof(SkyPushConstants, Output) == 96);
// The guaranteed minimum push-constant range is 128 bytes, so this fits on any
// conformant device without a capability query.
static_assert(sizeof(SkyPushConstants) == 112);

void SkyGradientPass::Setup(const RendererServices& services)
{
    Pipelines = services.Pipelines;
    Shaders = services.Shaders;
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;

    VertexShader = Shaders->CreateModuleFromSpirv(
        kSkyGradientVertSpv, kSkyGradientVertSpvWordCount, "Sky gradient vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kSkyGradientFragSpv, kSkyGradientFragSpvWordCount, "Sky gradient fragment");

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = sizeof(SkyPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &PipelineLayout) != VK_SUCCESS)
        PipelineLayout = VK_NULL_HANDLE;

    // Compile against the swapchain's formats at load. A host rendering into
    // some other target rebuilds once through the variant set.
    if (PipelineLayout != VK_NULL_HANDLE && services.Swapchain != nullptr)
    {
        FrameContext prewarm{};
        prewarm.TargetFormat = services.Swapchain->GetFormat();
        prewarm.DepthFormat = services.DepthFormat;
        (void)EnsurePipeline(prewarm);
    }
}

bool SkyGradientPass::EnsurePipeline(const FrameContext& frame)
{
    if (PipelineLayout == VK_NULL_HANDLE || frame.TargetFormat == VK_FORMAT_UNDEFINED)
        return false;

    return Pipeline.Ensure(
        AttachmentFormatKey{ frame.TargetFormat, frame.DepthFormat },
        [&](std::size_t) {
            GraphicsPipelineDesc desc{};
            desc.VertexShader = VertexShader;
            desc.FragmentShader = FragmentShader;
            desc.Layout = PipelineLayout;
            desc.CullMode = VK_CULL_MODE_NONE;
            // Depth state off entirely rather than "test against far": the pass
            // runs before anything writes depth, and leaving it off means a
            // host with no depth attachment needs no second variant.
            desc.DepthTest = false;
            desc.DepthWrite = false;
            desc.ColorBlend = { ColorBlendAttachmentDesc{} };
            desc.ColorFormats = { frame.TargetFormat };
            desc.DepthFormat = frame.DepthFormat;

            const VkPipeline pipeline = Pipelines->GetGraphicsPipeline(desc);
            if (pipeline != VK_NULL_HANDLE)
            {
                VulkanDebugLabels::NameObject(Device, VK_OBJECT_TYPE_PIPELINE,
                                              reinterpret_cast<std::uint64_t>(pipeline),
                                              "Sky/Gradient");
            }
            return pipeline;
        });
}

void SkyGradientPass::Draw(const FrameContext& frame,
                           const Mat4& inverseViewProjection,
                           const SkyGradientParams& sky)
{
    if (!EnsurePipeline(frame))
        return;

    VkViewport viewport{};
    viewport.width = static_cast<float>(frame.TargetExtent.width);
    viewport.height = static_cast<float>(frame.TargetExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = frame.TargetExtent;

    SkyPushConstants push{};
    // Row-major here, column-major in GLSL, same as every other matrix the
    // renderer uploads.
    push.InverseViewProjection = inverseViewProjection.Transposed();
    push.Top = Vec4(sky.Top.X, sky.Top.Y, sky.Top.Z, 1.0f);
    push.Bottom = Vec4(sky.Bottom.X, sky.Bottom.Y, sky.Bottom.Z, 1.0f);
    push.Output = Vec4(sky.Exposure, sky.TonemapKnee, sky.TonemapEnabled ? 1.0f : 0.0f, 0.0f);

    vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline.Get(0));
    vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);
    vkCmdPushConstants(frame.Cmd, PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(push), &push);
    vkCmdDraw(frame.Cmd, 3, 1, 0, 0);
}

void SkyGradientPass::Teardown()
{
    if (Shaders != nullptr)
    {
        Shaders->Destroy(VertexShader);
        Shaders->Destroy(FragmentShader);
    }
    VertexShader = {};
    FragmentShader = {};
    Pipeline.Reset();
    if (PipelineLayout != VK_NULL_HANDLE && Device != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
    PipelineLayout = VK_NULL_HANDLE;
    Device = VK_NULL_HANDLE;
}
