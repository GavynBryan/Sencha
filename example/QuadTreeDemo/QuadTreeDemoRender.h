#pragma once

#include <render/Renderer.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/backend/vulkan/VulkanImageService.h>
#include <render/features/SpriteFeature.h>
#include <window/SdlWindowService.h>
#include <window/WindowTypes.h>

#include <memory>

class LoggingProvider;
class SdlVideoService;
class SdlWindow;
class VulkanAllocatorService;
class VulkanBufferService;
class VulkanDeletionQueueService;
class VulkanDeviceService;
class VulkanFrameScratch;
class VulkanFrameService;
class VulkanInstanceService;
class VulkanPhysicalDeviceService;
class VulkanPipelineCache;
class VulkanQueueService;
class VulkanSamplerCache;
class VulkanShaderCache;
class VulkanSurfaceService;
class VulkanSwapchainService;
class VulkanUploadContextService;

class QuadTreeDemoRenderBootstrap
{
public:
	explicit QuadTreeDemoRenderBootstrap(LoggingProvider& logging);
	~QuadTreeDemoRenderBootstrap();

	QuadTreeDemoRenderBootstrap(const QuadTreeDemoRenderBootstrap&) = delete;
	QuadTreeDemoRenderBootstrap& operator=(const QuadTreeDemoRenderBootstrap&) = delete;

	bool IsValid() const { return Valid; }

	SdlWindowService& Windows() { return *WindowService; }
	SdlWindowService::WindowId WindowId() const { return WindowIdValue; }
	SpriteFeature& Sprites() { return *SpritesPtr; }
	BindlessImageIndex WhiteTexture() const { return WhiteSlot; }

	bool RecreateSwapchain(WindowExtent extent);
	Renderer::DrawStatus DrawFrame();

private:
	bool CreateWhitePixelTexture();

	LoggingProvider& Logging;
	bool Valid = false;

	std::unique_ptr<SdlVideoService> Video;
	std::unique_ptr<SdlWindowService> WindowService;
	SdlWindow* WindowPtr = nullptr;
	SdlWindowService::WindowId WindowIdValue = 0;

	std::unique_ptr<VulkanInstanceService> Instance;
	std::unique_ptr<VulkanSurfaceService> Surface;
	std::unique_ptr<VulkanPhysicalDeviceService> PhysicalDevice;
	std::unique_ptr<VulkanDeviceService> Device;
	std::unique_ptr<VulkanQueueService> Queues;
	std::unique_ptr<VulkanAllocatorService> Allocator;
	std::unique_ptr<VulkanUploadContextService> Upload;
	std::unique_ptr<VulkanDeletionQueueService> DeletionQueue;
	std::unique_ptr<VulkanBufferService> Buffers;
	std::unique_ptr<VulkanImageService> Images;
	std::unique_ptr<VulkanSamplerCache> Samplers;
	std::unique_ptr<VulkanShaderCache> Shaders;
	std::unique_ptr<VulkanPipelineCache> Pipelines;
	std::unique_ptr<VulkanDescriptorCache> Descriptors;
	std::unique_ptr<VulkanFrameScratch> Scratch;
	std::unique_ptr<VulkanSwapchainService> Swapchain;
	std::unique_ptr<VulkanFrameService> Frames;
	std::unique_ptr<Renderer> RendererPtr;

	FeatureRef<SpriteFeature> SpritesPtr;
	ImageHandle WhiteImage;
	BindlessImageIndex WhiteSlot;
};
