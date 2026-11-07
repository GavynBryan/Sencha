#include "EditorBloomPass.h"

#include <graphics/vulkan/RenderTargetSession.h>

#include <math/Vec.h>

#include <shaders/kBloomBrightpassFragSpv.h>
#include <shaders/kBloomBlurFragSpv.h>
#include <shaders/kBloomCompositeFragSpv.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace
{
constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// Matches the push_constant block in the bloom fragment shaders (24 bytes).
struct BloomPush
{
    std::uint32_t SrcIndex = 0; // bindless index of the sampled image
    float         Threshold = 1.0f;
    float         Intensity = 1.0f;
    float         Radius = 1.0f;
    Vec2d         Texel{}; // blur step direction * texel size; (0,0) when unused
};

}

void EditorBloomPass::Setup(const RendererServices& services)
{
    Shaders = services.Shaders;
    if (Shaders == nullptr || !Fullscreen.Setup(services, sizeof(BloomPush)))
        return; // bloom unavailable; Record() early-outs while no pipeline builds

    BrightFrag = Shaders->CreateModuleFromSpirv(kBloomBrightpassFragSpv, kBloomBrightpassFragSpvWordCount, "Bloom bright");
    BlurFrag = Shaders->CreateModuleFromSpirv(kBloomBlurFragSpv, kBloomBlurFragSpvWordCount, "Bloom blur");
    CompositeFrag = Shaders->CreateModuleFromSpirv(kBloomCompositeFragSpv, kBloomCompositeFragSpvWordCount, "Bloom composite");
}

bool EditorBloomPass::EnsurePipelines(VkFormat colorFormat)
{
    return Pipelines.Ensure(colorFormat, [&](std::size_t pass) {
        ColorBlendAttachmentDesc blend{};
        if (pass == Composite) // additive: add the glow over the scene
        {
            blend.BlendEnable = true;
            blend.SrcColor = VK_BLEND_FACTOR_ONE;
            blend.DstColor = VK_BLEND_FACTOR_ONE;
            blend.ColorOp = VK_BLEND_OP_ADD;
            blend.SrcAlpha = VK_BLEND_FACTOR_ONE;
            blend.DstAlpha = VK_BLEND_FACTOR_ONE;
            blend.AlphaOp = VK_BLEND_OP_ADD;
        }
        const ShaderHandle fragment = pass == Bright ? BrightFrag : (pass == Blur ? BlurFrag : CompositeFrag);
        return Fullscreen.BuildPipeline(fragment, AttachmentFormatKey{ .Color = colorFormat }, blend);
    });
}

void EditorBloomPass::Record(const FrameContext& frame,
                             const ViewportTargetCache::RenderView& t,
                             const BloomParams& params)
{
    if (t.BloomImage[0] == VK_NULL_HANDLE || t.BloomImage[1] == VK_NULL_HANDLE || !EnsurePipelines(kHdrFormat))
        return;

    const VkCommandBuffer cmd = frame.Cmd;
    const VkPipeline bright = Pipelines.Get(Bright);
    const VkPipeline blur = Pipelines.Get(Blur);
    const VkPipeline composite = Pipelines.Get(Composite);

    // One full-screen pass into `dstView`, inside the session its caller opened: dst
    // an attachment now, back to sampled when that session ends, which is what makes
    // the next pass's source readable.
    const auto renderInto = [&](VkImageView dstView, VkExtent2D extent, VkPipeline pipeline, const BloomPush& push,
                                bool loadExisting)
    {

        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = dstView;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = loadExisting ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        info.renderArea.extent = extent;
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &color;
        vkCmdBeginRendering(cmd, &info);
        Fullscreen.Draw(cmd, extent, pipeline, &push);
        vkCmdEndRendering(cmd);
    };

    const float invW = 1.0f / static_cast<float>(std::max(1u, t.BloomExtent.width));
    const float invH = 1.0f / static_cast<float>(std::max(1u, t.BloomExtent.height));

    BloomPush push{};
    push.Threshold = params.Threshold;
    push.Intensity = params.Intensity;
    push.Radius = params.Radius;

    // The glow source (active wireframe, on top) is already rendered into bloom[0].
    // 1. bright pass: bloom[0] -> bloom[1] (keep only the > threshold part).
    push.SrcIndex = t.BloomBindless[0];
    push.Texel = Vec2d{ 0.0f, 0.0f };
    // The planes are within-frame: each session is ordered after what last touched it.
    const auto intoPlane = [&](std::size_t plane, VkPipeline pipeline) {
        RenderTargetSession session(cmd, t.BloomImage[plane], *t.BloomAccess[plane]);
        renderInto(t.BloomView[plane], t.BloomExtent, pipeline, push, false);
    };
    intoPlane(1, bright);

    // 2. blur horizontal: bloom[1] -> bloom[0].
    push.SrcIndex = t.BloomBindless[1];
    push.Texel = Vec2d{ invW, 0.0f };
    intoPlane(0, blur);

    // 3. blur vertical: bloom[0] -> bloom[1].
    push.SrcIndex = t.BloomBindless[0];
    push.Texel = Vec2d{ 0.0f, invH };
    intoPlane(1, blur);

    // 4. composite: bloom[1] -> scene color, additive (load existing scene).
    push.SrcIndex = t.BloomBindless[1];
    push.Texel = Vec2d{ 0.0f, 0.0f };
    RenderTargetSession display(cmd, t.ColorImage, t.ColorLayout);
    renderInto(t.ColorView, t.Extent, composite, push, true);
}

void EditorBloomPass::Teardown()
{
    Pipelines.Reset();
    Fullscreen.Teardown();
    if (Shaders != nullptr)
    {
        Shaders->Destroy(BrightFrag);
        Shaders->Destroy(BlurFrag);
        Shaders->Destroy(CompositeFrag);
        BrightFrag = {};
        BlurFrag = {};
        CompositeFrag = {};
    }
    Shaders = nullptr;
}
