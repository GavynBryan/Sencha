#pragma once

#include <core/config/EngineConfig.h>
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

#include <cstdint>

class LoggingProvider;
class SdlWindow;
class SdlWindowService;

//=============================================================================
// GraphicsServices
//
// The Vulkan backend, owned as a group. Members are declared in dependency
// (construction) order, so reverse-declaration destruction tears them down
// safely -- Renderer first, instance last. This struct IS the Vulkan
// dependency graph: read it top to bottom.
//
// Vulkan-only (lives under graphics/vulkan/, excluded from non-Vulkan builds)
// and non-movable (owns Vulkan handles), so the Engine holds it behind a
// unique_ptr and builds it in place.
//=============================================================================
struct GraphicsServices
{
    VulkanInstanceService       Instance;
    VulkanSurfaceService        Surface;
    VulkanPhysicalDeviceService PhysicalDevice;
    VulkanDeviceService         Device;
    VulkanQueueService          Queues;
    VulkanAllocatorService      Allocator;
    VulkanUploadContextService  Upload;
    VulkanDeletionQueueService  DeletionQueue;
    VulkanBufferService         Buffers;
    VulkanImageService          Images;
    VulkanSamplerCache          Samplers;
    VulkanShaderCache           Shaders;
    VulkanPipelineCache         Pipelines;
    VulkanDescriptorCache       Descriptors;
    VulkanFrameScratch          Scratch;
    VulkanSwapchainService      Swapchain;
    VulkanFrameService          Frames;
    // Named MainRenderer, not Renderer: a member may not share its type's name.
    Renderer                    MainRenderer;

    // Builds the whole chain. Derives the bootstrap policy and frames-in-flight
    // from config + the window service, then constructs every service in order.
    GraphicsServices(LoggingProvider& logging,
                     const EngineConfig& config,
                     SdlWindow& window,
                     SdlWindowService& windows);

    GraphicsServices(const GraphicsServices&) = delete;
    GraphicsServices& operator=(const GraphicsServices&) = delete;

    // True when every service in the chain initialized successfully.
    [[nodiscard]] bool IsValid() const;

private:
    GraphicsServices(LoggingProvider& logging,
                     const VulkanBootstrapPolicy& policy,
                     std::uint32_t framesInFlight,
                     SdlWindow& window);

    static VulkanBootstrapPolicy BuildPolicy(const EngineConfig& config,
                                             SdlWindowService& windows);
    static std::uint32_t ResolveFramesInFlight(const EngineConfig& config);
};
