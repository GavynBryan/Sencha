#include "EditorBloomPass.h"

#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>

#include <math/Vec.h>

#include <shaders/kBloomBrightpassFragSpv.h>
#include <shaders/kBloomBlurFragSpv.h>
#include <shaders/kBloomCompositeFragSpv.h>
#include <shaders/kFullscreenVertSpv.h>

#include <algorithm>
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

void TransitionColor(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage,
                     VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess)
{
    VulkanBarriers::ImageTransition t{};
    t.Image = image;
    t.OldLayout = oldLayout;
    t.NewLayout = newLayout;
    t.SrcStage = srcStage;
    t.DstStage = dstStage;
    t.SrcAccess = srcAccess;
    t.DstAccess = dstAccess;
    t.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VulkanBarriers::TransitionImage(cmd, t);
}
}

void EditorBloomPass::Setup(const RendererServices& services)
{
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;
    Shaders = services.Shaders;
    Pipelines = services.Pipelines;
    Descriptors = services.Descriptors;
    if (Device == VK_NULL_HANDLE || Shaders == nullptr || Pipelines == nullptr || Descriptors == nullptr)
        return; // bloom unavailable; Record() early-outs while Layout stays null

    VertShader = Shaders->CreateModuleFromSpirv(kFullscreenVertSpv, kFullscreenVertSpvWordCount, "Fullscreen vertex");
    BrightFrag = Shaders->CreateModuleFromSpirv(kBloomBrightpassFragSpv, kBloomBrightpassFragSpvWordCount, "Bloom bright");
    BlurFrag = Shaders->CreateModuleFromSpirv(kBloomBlurFragSpv, kBloomBlurFragSpvWordCount, "Bloom blur");
    CompositeFrag = Shaders->CreateModuleFromSpirv(kBloomCompositeFragSpv, kBloomCompositeFragSpvWordCount, "Bloom composite");

    // Layout: just the engine's bindless image set (bound at set 0 here) + the push
    // constant. No frame UBO, so no descriptor the editor would have to fill.
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(BloomPush);

    VkDescriptorSetLayout setLayout = Descriptors->GetBindlessSetLayout();
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &Layout);
}

VkPipeline EditorBloomPass::EnsurePipeline(Pass pass, VkFormat colorFormat)
{
    if (CachedFormat != colorFormat)
    {
        PassPipelines[0] = PassPipelines[1] = PassPipelines[2] = VK_NULL_HANDLE;
        CachedFormat = colorFormat;
    }
    if (PassPipelines[pass] != VK_NULL_HANDLE)
        return PassPipelines[pass];

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

    GraphicsPipelineDesc desc{};
    desc.VertexShader = VertShader;
    desc.FragmentShader = pass == Bright ? BrightFrag : (pass == Blur ? BlurFrag : CompositeFrag);
    desc.Layout = Layout;
    desc.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.CullMode = VK_CULL_MODE_NONE;
    desc.DepthTest = false;
    desc.DepthWrite = false;
    desc.ColorBlend = { blend };
    desc.ColorFormats = { colorFormat };
    desc.DepthFormat = VK_FORMAT_UNDEFINED;
    PassPipelines[pass] = Pipelines->GetGraphicsPipeline(desc);
    return PassPipelines[pass];
}

void EditorBloomPass::Record(const FrameContext& frame,
                             const ViewportTargetCache::RenderView& t,
                             const BloomParams& params)
{
    if (Descriptors == nullptr || Layout == VK_NULL_HANDLE
        || t.BloomImage[0] == VK_NULL_HANDLE || t.BloomImage[1] == VK_NULL_HANDLE)
        return;

    const VkCommandBuffer cmd = frame.Cmd;
    const VkPipeline bright = EnsurePipeline(Bright, kHdrFormat);
    const VkPipeline blur = EnsurePipeline(Blur, kHdrFormat);
    const VkPipeline composite = EnsurePipeline(Composite, kHdrFormat);
    if (bright == VK_NULL_HANDLE || blur == VK_NULL_HANDLE || composite == VK_NULL_HANDLE)
        return;

    const VkDescriptorSet bindless = Descriptors->GetBindlessSet();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Layout, 0, 1, &bindless, 0, nullptr);

    // One full-screen pass: dst -> COLOR_ATTACHMENT, draw, dst -> SHADER_READ.
    const auto runPass = [&](VkImage dstImage, VkImageView dstView, VkImageLayout* dstLayout,
                             VkExtent2D extent, VkPipeline pipeline, const BloomPush& push, bool loadExisting)
    {
        TransitionColor(cmd, dstImage, *dstLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

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

        VkViewport vp{ 0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f };
        VkRect2D sc{ { 0, 0 }, extent };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdPushConstants(cmd, Layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);

        TransitionColor(cmd, dstImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        *dstLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
    runPass(t.BloomImage[1], t.BloomView[1], t.BloomLayout[1], t.BloomExtent, bright, push, false);

    // 2. blur horizontal: bloom[1] -> bloom[0].
    push.SrcIndex = t.BloomBindless[1];
    push.Texel = Vec2d{ invW, 0.0f };
    runPass(t.BloomImage[0], t.BloomView[0], t.BloomLayout[0], t.BloomExtent, blur, push, false);

    // 3. blur vertical: bloom[0] -> bloom[1].
    push.SrcIndex = t.BloomBindless[0];
    push.Texel = Vec2d{ 0.0f, invH };
    runPass(t.BloomImage[1], t.BloomView[1], t.BloomLayout[1], t.BloomExtent, blur, push, false);

    // 4. composite: bloom[1] -> scene color, additive (load existing scene).
    push.SrcIndex = t.BloomBindless[1];
    push.Texel = Vec2d{ 0.0f, 0.0f };
    runPass(t.ColorImage, t.ColorView, t.ColorLayout, t.Extent, composite, push, true);
}

void EditorBloomPass::Teardown()
{
    if (Shaders != nullptr)
    {
        Shaders->Destroy(VertShader);
        Shaders->Destroy(BrightFrag);
        Shaders->Destroy(BlurFrag);
        Shaders->Destroy(CompositeFrag);
        VertShader = {};
        BrightFrag = {};
        BlurFrag = {};
        CompositeFrag = {};
    }
    if (Device != VK_NULL_HANDLE && Layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Device, Layout, nullptr);
        Layout = VK_NULL_HANDLE;
    }
    PassPipelines[0] = PassPipelines[1] = PassPipelines[2] = VK_NULL_HANDLE;
    CachedFormat = VK_FORMAT_UNDEFINED;
    Descriptors = nullptr;
    Pipelines = nullptr;
    Shaders = nullptr;
    Device = VK_NULL_HANDLE;
}
