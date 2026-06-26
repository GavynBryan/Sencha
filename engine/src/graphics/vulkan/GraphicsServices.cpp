#include <graphics/vulkan/GraphicsServices.h>

#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>

#include <vulkan/vulkan.h>

VulkanBootstrapPolicy GraphicsServices::BuildPolicy(const EngineConfig& config,
                                                    SdlWindowService& windows)
{
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
    return policy;
}

std::uint32_t GraphicsServices::ResolveFramesInFlight(const EngineConfig& config)
{
    return config.Graphics.FramesInFlight == 0 ? 1u : config.Graphics.FramesInFlight;
}

GraphicsServices::GraphicsServices(LoggingProvider& logging,
                                   const EngineConfig& config,
                                   SdlWindow& window,
                                   SdlWindowService& windows)
    : GraphicsServices(logging, BuildPolicy(config, windows), ResolveFramesInFlight(config),
                       config.Graphics.FrameScratchBytesPerFrame, window)
{
}

GraphicsServices::GraphicsServices(LoggingProvider& logging,
                                   const VulkanBootstrapPolicy& policy,
                                   std::uint32_t framesInFlight,
                                   std::uint64_t scratchBytesPerFrame,
                                   SdlWindow& window)
    : Instance(logging, policy)
    , Surface(logging, Instance, window)
    , PhysicalDevice(logging, Instance, policy, &Surface)
    , Device(logging, PhysicalDevice, policy)
    , Queues(logging, Device, PhysicalDevice, policy)
    , Allocator(logging, Instance, PhysicalDevice, Device)
    , Upload(logging, Device, Queues)
    , DeletionQueue(logging, framesInFlight)
    , Buffers(logging, Device, Allocator, Upload, DeletionQueue)
    , Images(logging, Device, Allocator, Upload, DeletionQueue)
    , Samplers(logging, Device)
    , Shaders(logging, Device)
    , Pipelines(logging, Device, Shaders)
    , Descriptors(logging, Device, Buffers, Images)
    , Scratch(logging, Device, PhysicalDevice, Buffers,
              VulkanFrameScratch::Config{ .FramesInFlight = framesInFlight,
                                          .BytesPerFrame = scratchBytesPerFrame })
    , Swapchain(logging, Device, PhysicalDevice, Surface, Queues, window.GetExtent())
    , Frames(logging, Device, Queues, Swapchain, DeletionQueue, framesInFlight)
    , MainRenderer(logging, Device, PhysicalDevice, Queues, Swapchain, Frames, Allocator,
                   Buffers, Images, Samplers, Shaders, Pipelines, Descriptors, Scratch, Upload)
{
}

bool GraphicsServices::IsValid() const
{
    return Instance.IsValid()
        && Surface.IsValid()
        && PhysicalDevice.IsValid()
        && Device.IsValid()
        && Queues.IsValid()
        && Allocator.IsValid()
        && Upload.IsValid()
        && Buffers.IsValid()
        && Images.IsValid()
        && Samplers.IsValid()
        && Shaders.IsValid()
        && Pipelines.IsValid()
        && Descriptors.IsValid()
        && Scratch.IsValid()
        && Swapchain.IsValid()
        && Frames.IsValid()
        && MainRenderer.IsValid();
}
