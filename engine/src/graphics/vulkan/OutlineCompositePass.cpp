#include <graphics/vulkan/OutlineCompositePass.h>

#include <graphics/vulkan/VulkanFormat.h>
#include <shaders/kOutlineCompositeFragSpv.h>

#include <array>
#include <cstdint>

namespace
{
// Mirrored by the push block in outline_composite.frag.glsl.
struct OutlineCompositePush
{
    std::uint32_t MaskIndex = UINT32_MAX;
    std::uint32_t StyleIndex = UINT32_MAX;
    float MaxWidthPixels = 0.0f;
};
}

bool OutlineCompositePass::Setup(const RendererServices& services)
{
    Shaders = services.Shaders;
    if (Shaders == nullptr || !Fullscreen.Setup(services, sizeof(OutlineCompositePush)))
        return false;
    FragmentShader = Shaders->CreateModuleFromSpirv(kOutlineCompositeFragSpv, kOutlineCompositeFragSpvWordCount,
                                                    "Outline composite fragment");
    return true;
}

void OutlineCompositePass::Teardown()
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

bool OutlineCompositePass::EnsurePipeline(const FrameContext& frame)
{
    return Pipeline.Ensure(frame.AttachmentFormats(), [&](std::size_t) {
        const std::array encode{ ShaderSpecializationConstant{
            .Id = 0, .Value = FormatNeedsShaderEncode(frame.TargetFormat) ? 1u : 0u } };
        return Fullscreen.BuildPipeline(FragmentShader, frame.AttachmentFormats(), {}, encode);
    });
}

void OutlineCompositePass::Draw(const FrameContext& frame)
{
    const ScenePostState* post = frame.ScenePost;
    if (post == nullptr || post->OutlineMaskBindless == UINT32_MAX || !EnsurePipeline(frame))
        return;
    const OutlineCompositePush push{ .MaskIndex = post->OutlineMaskBindless,
                                     .StyleIndex = post->OutlineStyleBindless,
                                     .MaxWidthPixels = post->OutlineMaxWidthPixels };
    Fullscreen.Draw(frame.Cmd, frame.TargetExtent, post->OutlineScissor, Pipeline.Get(0), &push);
}
