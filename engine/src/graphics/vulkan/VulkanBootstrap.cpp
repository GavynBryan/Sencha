#include <graphics/vulkan/VulkanBootstrap.h>

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanAllocatorService.h>
#include <graphics/vulkan/VulkanBootstrapPolicy.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeletionQueueService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <graphics/vulkan/VulkanSurfaceService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <graphics/vulkan/VulkanUploadContextService.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>

#include <vulkan/vulkan.h>

bool VulkanBootstrap::Install(ServiceHost& services,
                              const EngineConfig& config,
                              SdlWindow& window,
                              SdlWindowService& windows)
{
    LoggingProvider& logging = services.GetLoggingProvider();

    VulkanBootstrapPolicy policy;
    policy.AppName = config.App.Name;
    policy.EnableValidation = config.Graphics.EnableValidation;
    policy.RequiredQueues.Present = true;
    policy.RequiredInstanceExtensions = windows.GetRequiredVulkanInstanceExtensions();
    policy.RequiredDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // present_id + present_wait let us block the CPU on a specific vsync.
    // Optional because macOS/MoltenVK and older drivers lack it; VulkanFrameService
    // falls back to acquire-based pacing.
    policy.OptionalDeviceExtensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
    policy.OptionalDeviceExtensions.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);

    auto& instance = services.AddService<VulkanInstanceService>(logging, policy);
    auto& surface = services.AddService<VulkanSurfaceService>(logging, instance, window);
    auto& physicalDevice =
        services.AddService<VulkanPhysicalDeviceService>(logging, instance, policy, &surface);
    auto& device = services.AddService<VulkanDeviceService>(logging, physicalDevice, policy);
    auto& queues = services.AddService<VulkanQueueService>(logging, device, physicalDevice, policy);
    auto& allocator =
        services.AddService<VulkanAllocatorService>(logging, instance, physicalDevice, device);
    auto& upload = services.AddService<VulkanUploadContextService>(logging, device, queues);

    const uint32_t framesInFlight = config.Graphics.FramesInFlight == 0
        ? 1u
        : config.Graphics.FramesInFlight;
    auto& deletionQueue = services.AddService<VulkanDeletionQueueService>(logging, framesInFlight);
    auto& buffers = services.AddService<VulkanBufferService>(logging, device, allocator, upload);
    auto& images =
        services.AddService<VulkanImageService>(logging, device, allocator, upload, deletionQueue);
    auto& samplers = services.AddService<VulkanSamplerCache>(logging, device);
    auto& shaders = services.AddService<VulkanShaderCache>(logging, device);
    auto& pipelines = services.AddService<VulkanPipelineCache>(logging, device, shaders);
    auto& descriptors =
        services.AddService<VulkanDescriptorCache>(logging, device, buffers, images);
    auto& scratch = services.AddService<VulkanFrameScratch>(
        logging, device, physicalDevice, buffers,
        VulkanFrameScratch::Config{ .FramesInFlight = framesInFlight });
    auto& swapchain = services.AddService<VulkanSwapchainService>(
        logging, device, physicalDevice, surface, queues, window.GetExtent());
    auto& frames = services.AddService<VulkanFrameService>(
        logging, device, queues, swapchain, deletionQueue, framesInFlight);

    services.AddService<Renderer>(
        logging, device, physicalDevice, queues, swapchain, frames, allocator, buffers,
        images, samplers, shaders, pipelines, descriptors, scratch, upload);

    return IsServiceChainValid(services);
}

bool VulkanBootstrap::IsServiceChainValid(ServiceHost& services)
{
    return services.Get<VulkanInstanceService>().IsValid()
        && services.Get<VulkanSurfaceService>().IsValid()
        && services.Get<VulkanPhysicalDeviceService>().IsValid()
        && services.Get<VulkanDeviceService>().IsValid()
        && services.Get<VulkanQueueService>().IsValid()
        && services.Get<VulkanAllocatorService>().IsValid()
        && services.Get<VulkanUploadContextService>().IsValid()
        && services.Get<VulkanBufferService>().IsValid()
        && services.Get<VulkanImageService>().IsValid()
        && services.Get<VulkanSamplerCache>().IsValid()
        && services.Get<VulkanShaderCache>().IsValid()
        && services.Get<VulkanPipelineCache>().IsValid()
        && services.Get<VulkanDescriptorCache>().IsValid()
        && services.Get<VulkanFrameScratch>().IsValid()
        && services.Get<VulkanSwapchainService>().IsValid()
        && services.Get<VulkanFrameService>().IsValid()
        && services.Get<Renderer>().IsValid();
}
