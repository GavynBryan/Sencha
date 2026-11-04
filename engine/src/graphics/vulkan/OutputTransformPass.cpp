#include <graphics/vulkan/OutputTransformPass.h>

#include <graphics/vulkan/VulkanFormat.h>
#include <shaders/kOutputPresentFragSpv.h>

#include <array>
#include <cstdint>

namespace
{
// Mirrored by the push block in output_present.frag.glsl.
struct OutputPresentPush
{
    std::uint32_t SceneIndex = 0;
};
}

bool OutputTransformPass::Setup(const RendererServices& services)
{
    Shaders = services.Shaders;
    if (Shaders == nullptr || !Fullscreen.Setup(services, sizeof(OutputPresentPush)))
        return false;

    FragmentShader = Shaders->CreateModuleFromSpirv(kOutputPresentFragSpv, kOutputPresentFragSpvWordCount,
                                                    "Output present fragment");
    if (services.Swapchain != nullptr)
        (void)EnsurePipeline(SwapchainScopeFormats(services));
    return true;
}

void OutputTransformPass::Teardown()
{
    Pipeline.Reset();
    Fullscreen.Teardown();
    if (Shaders != nullptr)
    {
        Shaders->Destroy(FragmentShader);
        FragmentShader = {};
    }
    Shaders = nullptr;
}

bool OutputTransformPass::EnsurePipeline(const FrameContext& frame)
{
    return Pipeline.Ensure(frame.AttachmentFormats(), [&](std::size_t) {
        const std::array encode{ ShaderSpecializationConstant{
            .Id = 0, .Value = FormatEncodesSrgbOnWrite(frame.TargetFormat) ? 0u : 1u } };
        return Fullscreen.BuildPipeline(FragmentShader, frame.AttachmentFormats(), {}, encode);
    });
}

void OutputTransformPass::Draw(const FrameContext& frame)
{
    if (frame.SceneColorBindless == UINT32_MAX || !EnsurePipeline(frame))
        return;

    const OutputPresentPush push{ .SceneIndex = frame.SceneColorBindless };
    Fullscreen.Draw(frame.Cmd, frame.TargetExtent, Pipeline.Get(0), &push);
}
