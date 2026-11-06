#include "render/StagePicture.h"

#include "render/TargetImagePublish.h"

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

    RenderTargetDesc scene{};
    scene.ColorFormat = kSceneColorFormat;
    scene.Read = RenderTargetRead::Bindless;
    scene.Sampler = services.Samplers != nullptr ? services.Samplers->GetNearestClamp() : VK_NULL_HANDLE;
    scene.DebugName = "stage_scene";
    scene.Lifetime = RenderTargetLifetime::WithinFrame;
    Scene = Targets.Create(scene);

    OutputReady = Output.Setup(services);
}

void StagePicture::Teardown()
{
    Targets.Teardown();
    Output.Teardown();
    Target = {};
    Scene = {};
}

void StagePicture::Show(VkExtent2D extent, UiHostImageId image)
{
    Image = image;
    Targets.Show(Target, extent);
    Targets.Show(Scene, extent);
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
                        const DrawFn& drawScene, const DrawFn& drawOverlay)
{
    const std::optional<RenderTargetView> scene = Targets.Acquire(Scene);
    if (!scene.has_value() || !OutputReady)
    {
        if (Services.UiTextures != nullptr && Image.has_value())
            Services.UiTextures->WithdrawHostImage(*Image);
        return;
    }
    const SceneDisplayTargets targets{ .Extent = target.Extent,
                                       .SceneImage = scene->ColorImage,
                                       .SceneView = scene->ColorView,
                                       .SceneAccess = scene->ColorAccess,
                                       .SceneBindless = scene->BindlessIndex,
                                       .DisplayImage = target.ColorImage,
                                       .DisplayView = target.ColorView,
                                       .DisplayLayout = target.ColorLayout,
                                       .DepthImage = target.DepthImage,
                                       .DepthView = target.DepthView,
                                       .DepthFormat = Services.DepthFormat };
    RecordSceneThenDisplay(frame, targets, clear, Output, OutputTransformParams{}, drawScene, drawOverlay);
    if (Services.UiTextures != nullptr && Image.has_value())
        PublishTargetImage(*Services.UiTextures, *Image, target.ColorLayout, target.BindlessIndex);
}
