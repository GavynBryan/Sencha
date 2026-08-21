#include <graphics/vulkan/ProjectedShadowProjectPass.h>

#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>

#include <shaders/kProjectedShadowProjectVertSpv.h>
#include <shaders/kProjectedShadowProjectFragSpv.h>

#include <cstring>

static_assert(offsetof(ProjectedShadowProjectUniform, CameraViewProjection) == 0);
static_assert(offsetof(ProjectedShadowProjectUniform, ShadowViewProjection) == 64);
static_assert(offsetof(ProjectedShadowProjectUniform, TileScaleBias) == 128);
static_assert(offsetof(ProjectedShadowProjectUniform, Params) == 144);
static_assert(sizeof(ProjectedShadowProjectUniform) == 160);

void ProjectedShadowProjectPass::Setup(const RendererServices& services)
{
    Services = services;

    VertexShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowProjectVertSpv, kProjectedShadowProjectVertSpvWordCount,
        "Projected shadow project vertex");
    FragmentShader = services.Shaders->CreateModuleFromSpirv(
        kProjectedShadowProjectFragSpv, kProjectedShadowProjectFragSpvWordCount,
        "Projected shadow project fragment");

    const std::vector<VkPushConstantRange> push{
        VkPushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4) },
    };
    PipelineLayout = services.Descriptors->GetPipelineLayout(push);
    // Declare the block this pass reads through the shared dynamic-offset
    // binding; the cache keeps the largest declaration.
    services.Descriptors->RequireFrameUniformRange(
        services.Scratch->GetBuffer(), sizeof(ProjectedShadowProjectUniform));
}

void ProjectedShadowProjectPass::Teardown()
{
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

bool ProjectedShadowProjectPass::EnsurePipeline(const FrameContext& frame)
{
    if (PipelineLayout == VK_NULL_HANDLE || VertexStride == 0)
        return false;

    return Pipeline.Ensure(
        AttachmentFormatKey{ frame.TargetFormat, frame.DepthFormat },
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
            // Land exactly on the opaque surface, never behind it, and leave
            // depth alone: the receiver is already in the depth buffer.
            desc.DepthTest = true;
            desc.DepthWrite = false;
            desc.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
            // Multiplicative darkening: dst * srcColor. Fragments that
            // resolve to 1.0 are no-op writes, which is what makes the
            // outside-the-volume case branchless in the shader.
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

void ProjectedShadowProjectPass::Draw(const FrameContext& frame,
                                      const ProjectedShadowProjectionInput& input)
{
    if (input.Casters.empty())
        return;
    VertexStride = input.VertexStride;
    if (!EnsurePipeline(frame))
        return;
    const VkPipeline pipeline = Pipeline.Get(0);
    if (pipeline == VK_NULL_HANDLE)
        return;

    vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkViewport viewport{};
    viewport.width = static_cast<float>(frame.TargetExtent.width);
    viewport.height = static_cast<float>(frame.TargetExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);

    const VkDescriptorSet bindlessSet = Services.Descriptors->GetBindlessSet();
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
                            1, 1, &bindlessSet, 0, nullptr);

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
