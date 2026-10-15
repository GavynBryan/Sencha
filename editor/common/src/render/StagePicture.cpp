#include "render/StagePicture.h"

#include "render/TargetImagePublish.h"

#include <graphics/vulkan/RenderTargetSession.h>
#include <graphics/vulkan/VulkanSamplerCache.h>

void StagePicture::Setup(const RendererServices& services, const char* debugName)
{
    Services = services;
    Targets.Setup(services);
    RenderTargetDesc desc{};
    desc.ColorFormat = kColorFormat;
    desc.DepthFormat = services.DepthFormat;
    desc.Read = RenderTargetRead::Bindless;
    desc.Sampler = services.Samplers != nullptr ? services.Samplers->GetLinearClamp() : VK_NULL_HANDLE;
    desc.DebugName = debugName;
    Target = Targets.Create(desc);
}

void StagePicture::Teardown()
{
    Targets.Teardown();
    Target = {};
}

void StagePicture::Show(VkExtent2D extent, UiHostImageId image)
{
    Image = image;
    Targets.Show(Target, extent);
}

std::optional<RenderTargetView> StagePicture::Begin(const FrameContext& frame)
{
    Targets.BeginFrame(frame.FrameInFlightIndex);
    std::optional<RenderTargetView> target = Targets.Acquire(Target);
    if (!target && Services.UiTextures != nullptr && Image.has_value())
        Services.UiTextures->WithdrawHostImage(*Image);
    return target;
}

void StagePicture::Draw(const FrameContext& frame, const RenderTargetView& target, VkClearColorValue clear,
                        const std::function<void(const RenderScope&)>& draw)
{
    RenderTargetSession session(frame.Cmd, target.ColorImage, target.ColorLayout, target.DepthImage,
                                Services.DepthFormat);
    {
        RenderScopeDesc desc{};
        desc.Area.extent = target.Extent;
        desc.Color.View = target.ColorView;
        desc.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc.Color.Clear.color = clear;
        desc.ColorFormat = kColorFormat;
        desc.Depth.View = target.DepthView;
        desc.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        desc.Depth.Clear.depthStencil = { 1.0f, 0 };
        desc.DepthFormat = Services.DepthFormat;
        desc.Phase = RenderPhase::Offscreen;
        const RenderScope rendering(frame, desc);
        draw(rendering);
    }
    session.End();
    if (Services.UiTextures != nullptr && Image.has_value())
        PublishTargetImage(*Services.UiTextures, *Image, target.ColorLayout, target.BindlessIndex);
}
