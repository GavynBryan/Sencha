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
    float Exposure = 1.0f;
    float TonemapKnee = 0.8f;
    std::uint32_t TonemapEnabled = 1;
};
}

bool OutputTransformPass::Setup(const RendererServices& services)
{
    Shaders = services.Shaders;
    if (Shaders == nullptr || !Fullscreen.Setup(services, sizeof(OutputPresentPush)) || !Composite.Setup(services))
        return false;

    FragmentShader = Shaders->CreateModuleFromSpirv(kOutputPresentFragSpv, kOutputPresentFragSpvWordCount,
                                                    "Output present fragment");
    if (services.Swapchain != nullptr)
        (void)EnsurePipeline(SwapchainScopeFormats(services));
    return true;
}

void OutputTransformPass::Teardown()
{
    Composite.Teardown();
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
            .Id = 0, .Value = FormatNeedsShaderEncode(frame.TargetFormat) ? 1u : 0u } };
        return Fullscreen.BuildPipeline(FragmentShader, frame.AttachmentFormats(), {}, encode);
    });
}

void OutputTransformPass::Draw(const FrameContext& frame, const OutputTransformParams& params)
{
    const ScenePostState* post = frame.ScenePost;
    const std::uint32_t source = post != nullptr && post->PresentColorBindless != UINT32_MAX
        ? post->PresentColorBindless
        : frame.SceneColorBindless;
    if (source == UINT32_MAX || !EnsurePipeline(frame))
        return;

    // Once is the rule: an effect chain that already ran the transform leaves only
    // the encode, which a display-referred value in [0,1] passes through unchanged.
    const bool applied = post != nullptr && post->TransformApplied;
    const OutputPresentPush push{ .SceneIndex = source,
                                  .Exposure = applied ? 1.0f : params.Exposure,
                                  .TonemapKnee = params.TonemapKnee,
                                  .TonemapEnabled = !applied && params.TonemapEnabled ? 1u : 0u };
    Fullscreen.Draw(frame.Cmd, frame.TargetExtent, Pipeline.Get(0), &push);
    Composite.Draw(frame);
}
