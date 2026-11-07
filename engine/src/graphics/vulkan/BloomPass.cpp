#include <graphics/vulkan/BloomPass.h>

#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <shaders/kBloomBlurFragSpv.h>
#include <shaders/kBloomBrightpassFragSpv.h>
#include <shaders/kBloomCompositeFragSpv.h>

#include <algorithm>

namespace
{
struct BloomPush
{
    std::uint32_t SourceIndex = UINT32_MAX;
    std::uint32_t SceneIndex = UINT32_MAX;
    float Threshold = 1.0f;
    float Intensity = 1.0f;
    float Radius = 1.0f;
    std::uint32_t Gather = 1;
    float Texel[2]{};
};
static_assert(sizeof(BloomPush) == 32);

ColorBlendAttachmentDesc Additive()
{
    ColorBlendAttachmentDesc blend{};
    blend.BlendEnable = true;
    blend.SrcColor = VK_BLEND_FACTOR_ONE;
    blend.DstColor = VK_BLEND_FACTOR_ONE;
    blend.SrcAlpha = VK_BLEND_FACTOR_ONE;
    blend.DstAlpha = VK_BLEND_FACTOR_ONE;
    return blend;
}
} // namespace

bool BloomPass::Setup(const RendererServices& services)
{
    Shaders = services.Shaders;
    if (Shaders == nullptr || !Fullscreen.Setup(services, sizeof(BloomPush)))
        return false;
    BrightFrag = Shaders->CreateModuleFromSpirv(kBloomBrightpassFragSpv, kBloomBrightpassFragSpvWordCount, "Bloom bright");
    BlurFrag = Shaders->CreateModuleFromSpirv(kBloomBlurFragSpv, kBloomBlurFragSpvWordCount, "Bloom blur");
    CompositeFrag =
        Shaders->CreateModuleFromSpirv(kBloomCompositeFragSpv, kBloomCompositeFragSpvWordCount, "Bloom composite");
    return true;
}

void BloomPass::Teardown()
{
    Pipelines.Reset();
    Fullscreen.Teardown();
    if (Shaders != nullptr)
        for (ShaderHandle* shader : { &BrightFrag, &BlurFrag, &CompositeFrag })
        {
            Shaders->Destroy(*shader);
            *shader = {};
        }
    Shaders = nullptr;
}

bool BloomPass::EnsurePipelines(VkFormat format)
{
    return Pipelines.Ensure(format, [&](std::size_t step) {
        const ShaderHandle fragment = step == Bright ? BrightFrag : step == Spread ? BlurFrag : CompositeFrag;
        return Fullscreen.BuildPipeline(fragment, AttachmentFormatKey{ .Color = format },
                                        step == Over ? Additive() : ColorBlendAttachmentDesc{});
    });
}

void BloomPass::Blur(const FrameContext& frame, std::uint32_t source, VkExtent2D sourceExtent,
                     const BloomPlane& glow, const BloomPlane& scratch, const BloomParams& params)
{
    if (!EnsurePipelines(kSceneColorFormat))
        return;

    BloomPush push{ .SourceIndex = source, .Threshold = params.Threshold, .Radius = params.Radius };
    const auto into = [&](const BloomPlane& plane, Step step) {
        RenderTargetSession session(frame.Cmd, plane.Image, *plane.Access);
        RenderScopeDesc desc{};
        desc.Area.extent = plane.Extent;
        desc.Color.View = plane.View;
        desc.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        desc.ColorFormat = kSceneColorFormat;
        const RenderScope scope(frame, desc);
        Fullscreen.Draw(frame.Cmd, plane.Extent, Pipelines.Get(step), &push);
        push.SourceIndex = plane.Bindless;
    };
    push.Gather = std::max(1u, sourceExtent.width / std::max(1u, glow.Extent.width));
    into(glow, Bright);
    push.Texel[0] = 1.0f / static_cast<float>(std::max(1u, glow.Extent.width));
    into(scratch, Spread);
    push.Texel[0] = 0.0f;
    push.Texel[1] = 1.0f / static_cast<float>(std::max(1u, glow.Extent.height));
    into(glow, Spread);
}

void BloomPass::Composite(const FrameContext& frame, std::uint32_t glow, std::uint32_t scene, float intensity)
{
    if (!EnsurePipelines(kSceneColorFormat))
        return;
    const BloomPush push{ .SourceIndex = glow, .SceneIndex = scene, .Intensity = intensity };
    Fullscreen.Draw(frame.Cmd, frame.TargetExtent, Pipelines.Get(scene == UINT32_MAX ? Over : Replace), &push);
}
