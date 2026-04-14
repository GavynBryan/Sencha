#include <core/logging/ConsoleLogSink.h>
#include <core/service/ServiceHost.h>
#include <render/Renderer.h>
#include <render/backend/vulkan/VulkanAllocatorService.h>
#include <render/backend/vulkan/VulkanBootstrapPolicy.h>
#include <render/backend/vulkan/VulkanBufferService.h>
#include <render/backend/vulkan/VulkanDeletionQueueService.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/backend/vulkan/VulkanDeviceService.h>
#include <render/backend/vulkan/VulkanFrameScratch.h>
#include <render/backend/vulkan/VulkanFrameService.h>
#include <render/backend/vulkan/VulkanImageService.h>
#include <render/backend/vulkan/VulkanInstanceService.h>
#include <render/backend/vulkan/VulkanPhysicalDeviceService.h>
#include <render/backend/vulkan/VulkanPipelineCache.h>
#include <render/backend/vulkan/VulkanQueueService.h>
#include <render/backend/vulkan/VulkanSamplerCache.h>
#include <render/backend/vulkan/VulkanShaderCache.h>
#include <render/backend/vulkan/VulkanSurfaceService.h>
#include <render/backend/vulkan/VulkanSwapchainService.h>
#include <render/backend/vulkan/VulkanUploadContextService.h>
#include <render/features/SpriteFeature.h>
#include <window/SdlVideoService.h>
#include <window/SdlWindow.h>
#include <window/SdlWindowService.h>
#include <window/WindowCreateInfo.h>

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>

class SdlVulkanWindowExample
{
};

namespace
{
    bool IsRenderableExtent(WindowExtent extent)
    {
        return extent.Width > 0 && extent.Height > 0;
    }

    // Build a tiny 2x2 checkerboard in R8G8B8A8_SRGB so we have something
    // visible to sample without dragging asset loading into this example.
    ImageHandle CreateCheckerImage(VulkanImageService& images)
    {
        ImageCreateInfo info{};
        info.Format = VK_FORMAT_R8G8B8A8_SRGB;
        info.Extent = { 2, 2 };
        info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.DebugName = "SdlVulkanWindow.Checker";

        ImageHandle handle = images.Create(info);
        if (!handle.IsValid()) return {};

        const std::array<uint32_t, 4> pixels = {
            0xFFFFFFFFu, 0xFF202020u,
            0xFF202020u, 0xFFFFFFFFu,
        };
        if (!images.Upload(handle, pixels.data(), pixels.size() * sizeof(uint32_t)))
        {
            images.Destroy(handle);
            return {};
        }
        return handle;
    }
}

int main()
{
    ServiceHost services;
    auto& logging = services.GetLoggingProvider();
    logging.AddSink<ConsoleLogSink>();

    auto& logger = logging.GetLogger<SdlVulkanWindowExample>();

    WindowCreateInfo windowInfo;
    windowInfo.Title = "Sencha SDL3 Vulkan Window";
    windowInfo.Width = 1280;
    windowInfo.Height = 720;
    windowInfo.GraphicsApi = WindowGraphicsApi::Vulkan;
    windowInfo.Resizable = true;
    windowInfo.Visible = true;

    SdlVideoService video(logging);
    if (!video.IsValid()) return 1;

    SdlWindowService windows(logging, video);
    SdlWindow* window = windows.CreateWindow(windowInfo);
    if (!window) return 1;

    const auto windowId = window->GetId();

    VulkanBootstrapPolicy vulkanPolicy;
    vulkanPolicy.AppName = "SdlVulkanWindow";
    vulkanPolicy.RequiredQueues.Present = true;
    vulkanPolicy.RequiredDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    auto platformExtensions = windows.GetRequiredVulkanInstanceExtensions();
    vulkanPolicy.RequiredInstanceExtensions.insert(
        vulkanPolicy.RequiredInstanceExtensions.end(),
        platformExtensions.begin(),
        platformExtensions.end());

    VulkanInstanceService instance(logging, vulkanPolicy);
    if (!instance.IsValid()) return 1;

    VulkanSurfaceService surface(logging, instance, *window);
    if (!surface.IsValid()) return 1;

    VulkanPhysicalDeviceService physicalDevice(logging, instance, vulkanPolicy, &surface);
    if (!physicalDevice.IsValid()) return 1;

    VulkanDeviceService device(logging, physicalDevice, vulkanPolicy);
    if (!device.IsValid()) return 1;

    VulkanQueueService queues(logging, device, physicalDevice, vulkanPolicy);
    if (!queues.IsValid()) return 1;

    VulkanAllocatorService allocator(logging, instance, physicalDevice, device);
    if (!allocator.IsValid()) return 1;

    VulkanUploadContextService upload(logging, device, queues);
    if (!upload.IsValid()) return 1;

    VulkanDeletionQueueService deletionQueue(logging, 2);

    VulkanBufferService buffers(logging, device, allocator, upload);
    if (!buffers.IsValid()) return 1;

    VulkanImageService imageService(logging, device, allocator, upload, deletionQueue);
    if (!imageService.IsValid()) return 1;

    VulkanSamplerCache samplers(logging, device);

    VulkanShaderCache shaders(logging, device);
    if (!shaders.IsValid()) return 1;

    VulkanPipelineCache pipelines(logging, device, shaders);
    if (!pipelines.IsValid()) return 1;

    VulkanDescriptorCache descriptors(logging, device, buffers, imageService);
    if (!descriptors.IsValid()) return 1;

    VulkanFrameScratch::Config scratchConfig{};
    scratchConfig.FramesInFlight = 2;
    scratchConfig.BytesPerFrame = 256 * 1024;
    VulkanFrameScratch scratch(logging, device, physicalDevice, buffers, scratchConfig);
    if (!scratch.IsValid()) return 1;

    VulkanSwapchainService swapchain(
        logging, device, physicalDevice, surface, queues, window->GetExtent());
    if (!swapchain.IsValid()) return 1;

    VulkanFrameService frames(logging, device, queues, swapchain, deletionQueue);
    if (!frames.IsValid()) return 1;

    Renderer renderer(
        logging, device, physicalDevice, queues, swapchain, frames,
        allocator, buffers, imageService, samplers, shaders, pipelines,
        descriptors, scratch, upload);
    if (!renderer.IsValid()) return 1;

    SpriteFeature* sprites = renderer.AddFeature(std::make_unique<SpriteFeature>());
    if (sprites == nullptr || !sprites->IsValid())
    {
        logger.Error("Failed to initialize SpriteFeature");
        return 1;
    }

    const ImageHandle checker = CreateCheckerImage(imageService);
    if (!checker.IsValid()) return 1;

    const VkSampler sampler = samplers.GetNearestRepeat();
    const BindlessImageIndex checkerSlot =
        descriptors.RegisterSampledImage(checker, sampler);
    if (!checkerSlot.IsValid()) return 1;

    logger.Info("SDL3 Vulkan Sencha renderer demo running. Close the window to exit.");

    uint64_t frameIndex = 0;
    while (windows.IsAlive(windowId))
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            windows.HandleEvent(event);
        }

        WindowExtent resizeExtent{};
        const bool resizePending = windows.ConsumeResize(windowId, &resizeExtent);
        const auto* state = windows.GetState(windowId);
        const bool minimized = state && state->Minimized;
        WindowExtent currentExtent = resizePending ? resizeExtent : windows.GetExtent(windowId);

        if (minimized || !IsRenderableExtent(currentExtent))
        {
            SDL_Delay(16);
            continue;
        }

        if (resizePending)
        {
            if (!swapchain.Recreate(currentExtent))
            {
                logger.Error("Failed to recreate swapchain");
                return 1;
            }
            frames.ResetAfterSwapchainRecreate();
            renderer.NotifySwapchainRecreated();
            continue;
        }

        // Submit a few animated sprites so we can confirm the full stack
        // rounds-trips: scratch -> pipeline -> bindless -> pixels.
        const float t = static_cast<float>(frameIndex) * 0.016f;
        const float cx = static_cast<float>(currentExtent.Width) * 0.5f;
        const float cy = static_cast<float>(currentExtent.Height) * 0.5f;

        for (int i = 0; i < 5; ++i)
        {
            SpriteFeature::Sprite s{};
            const float phase = t + static_cast<float>(i) * 1.2566f;
            s.CenterX = cx + std::cos(phase) * 200.0f;
            s.CenterY = cy + std::sin(phase) * 120.0f;
            s.Width = 128.0f;
            s.Height = 128.0f;
            s.Rotation = phase;
            s.Texture = checkerSlot;
            s.Color = 0xFFFFFFFFu;
            s.SortKey = i;
            sprites->Submit(s);
        }

        const auto status = renderer.DrawFrame();
        if (status == Renderer::DrawStatus::SwapchainOutOfDate)
        {
            if (!swapchain.Recreate(currentExtent))
            {
                logger.Error("Failed to recreate swapchain");
                return 1;
            }
            frames.ResetAfterSwapchainRecreate();
            renderer.NotifySwapchainRecreated();
            continue;
        }
        if (status == Renderer::DrawStatus::Error)
        {
            return 1;
        }

        ++frameIndex;
    }

    return 0;
}
