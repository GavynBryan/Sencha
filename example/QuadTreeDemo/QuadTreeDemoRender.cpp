#include "QuadTreeDemoRender.h"

#include <core/logging/LoggingProvider.h>
#include <render/backend/vulkan/VulkanAllocatorService.h>
#include <render/backend/vulkan/VulkanBootstrapPolicy.h>
#include <render/backend/vulkan/VulkanBufferService.h>
#include <render/backend/vulkan/VulkanDeletionQueueService.h>
#include <render/backend/vulkan/VulkanDeviceService.h>
#include <render/backend/vulkan/VulkanFrameScratch.h>
#include <render/backend/vulkan/VulkanFrameService.h>
#include <render/backend/vulkan/VulkanInstanceService.h>
#include <render/backend/vulkan/VulkanPhysicalDeviceService.h>
#include <render/backend/vulkan/VulkanPipelineCache.h>
#include <render/backend/vulkan/VulkanQueueService.h>
#include <render/backend/vulkan/VulkanSamplerCache.h>
#include <render/backend/vulkan/VulkanShaderCache.h>
#include <render/backend/vulkan/VulkanSurfaceService.h>
#include <render/backend/vulkan/VulkanSwapchainService.h>
#include <render/backend/vulkan/VulkanUploadContextService.h>
#include <window/SdlVideoService.h>
#include <window/SdlWindow.h>
#include <window/WindowCreateInfo.h>

#include <cstdint>

QuadTreeDemoRenderBootstrap::QuadTreeDemoRenderBootstrap(LoggingProvider& logging)
	: Logging(logging)
{
	auto& logger = Logging.GetLogger<QuadTreeDemoRenderBootstrap>();

	WindowCreateInfo windowInfo;
	windowInfo.Title = "Sencha QuadTree Demo";
	windowInfo.Width = 1280;
	windowInfo.Height = 720;
	windowInfo.GraphicsApi = WindowGraphicsApi::Vulkan;
	windowInfo.Resizable = true;
	windowInfo.Visible = true;

	Video = std::make_unique<SdlVideoService>(Logging);
	if (!Video->IsValid()) return;

	WindowService = std::make_unique<SdlWindowService>(Logging, *Video);
	WindowPtr = WindowService->CreateWindow(windowInfo);
	if (WindowPtr == nullptr) return;
	WindowIdValue = WindowPtr->GetId();

	VulkanBootstrapPolicy vulkanPolicy;
	vulkanPolicy.AppName = "QuadTreeDemo";
	vulkanPolicy.RequiredQueues.Present = true;
	vulkanPolicy.RequiredDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

	auto platformExtensions = WindowService->GetRequiredVulkanInstanceExtensions();
	vulkanPolicy.RequiredInstanceExtensions.insert(
		vulkanPolicy.RequiredInstanceExtensions.end(),
		platformExtensions.begin(),
		platformExtensions.end());

	Instance = std::make_unique<VulkanInstanceService>(Logging, vulkanPolicy);
	if (!Instance->IsValid()) return;

	Surface = std::make_unique<VulkanSurfaceService>(Logging, *Instance, *WindowPtr);
	if (!Surface->IsValid()) return;

	PhysicalDevice = std::make_unique<VulkanPhysicalDeviceService>(
		Logging, *Instance, vulkanPolicy, Surface.get());
	if (!PhysicalDevice->IsValid()) return;

	Device = std::make_unique<VulkanDeviceService>(Logging, *PhysicalDevice, vulkanPolicy);
	if (!Device->IsValid()) return;

	Queues = std::make_unique<VulkanQueueService>(
		Logging, *Device, *PhysicalDevice, vulkanPolicy);
	if (!Queues->IsValid()) return;

	Allocator = std::make_unique<VulkanAllocatorService>(
		Logging, *Instance, *PhysicalDevice, *Device);
	if (!Allocator->IsValid()) return;

	Upload = std::make_unique<VulkanUploadContextService>(Logging, *Device, *Queues);
	if (!Upload->IsValid()) return;

	DeletionQueue = std::make_unique<VulkanDeletionQueueService>(Logging, 2);

	Buffers = std::make_unique<VulkanBufferService>(Logging, *Device, *Allocator, *Upload);
	if (!Buffers->IsValid()) return;

	Images = std::make_unique<VulkanImageService>(Logging, *Device, *Allocator, *Upload, *DeletionQueue);
	if (!Images->IsValid()) return;

	Samplers = std::make_unique<VulkanSamplerCache>(Logging, *Device);

	Shaders = std::make_unique<VulkanShaderCache>(Logging, *Device);
	if (!Shaders->IsValid()) return;

	Pipelines = std::make_unique<VulkanPipelineCache>(Logging, *Device, *Shaders);
	if (!Pipelines->IsValid()) return;

	Descriptors = std::make_unique<VulkanDescriptorCache>(Logging, *Device, *Buffers, *Images);
	if (!Descriptors->IsValid()) return;

	VulkanFrameScratch::Config scratchConfig{};
	scratchConfig.FramesInFlight = 2;
	scratchConfig.BytesPerFrame = 256 * 1024;
	Scratch = std::make_unique<VulkanFrameScratch>(
		Logging, *Device, *PhysicalDevice, *Buffers, scratchConfig);
	if (!Scratch->IsValid()) return;

	Swapchain = std::make_unique<VulkanSwapchainService>(
		Logging, *Device, *PhysicalDevice, *Surface, *Queues, WindowPtr->GetExtent());
	if (!Swapchain->IsValid()) return;

	Frames = std::make_unique<VulkanFrameService>(Logging, *Device, *Queues, *Swapchain, *DeletionQueue);
	if (!Frames->IsValid()) return;

	RendererPtr = std::make_unique<Renderer>(
		Logging, *Device, *PhysicalDevice, *Queues, *Swapchain, *Frames,
		*Allocator, *Buffers, *Images, *Samplers, *Shaders, *Pipelines,
		*Descriptors, *Scratch, *Upload);
	if (!RendererPtr->IsValid()) return;

	SpritesPtr = RendererPtr->AddFeature(std::make_unique<SpriteFeature>());
	if (SpritesPtr == nullptr || !SpritesPtr->IsValid())
	{
		logger.Error("Failed to initialize SpriteFeature");
		return;
	}

	if (!CreateWhitePixelTexture()) return;

	Valid = true;
}

QuadTreeDemoRenderBootstrap::~QuadTreeDemoRenderBootstrap() = default;

bool QuadTreeDemoRenderBootstrap::RecreateSwapchain(WindowExtent extent)
{
	if (!Swapchain->Recreate(extent))
	{
		return false;
	}

	Frames->ResetAfterSwapchainRecreate();
	RendererPtr->NotifySwapchainRecreated();
	return true;
}

Renderer::DrawStatus QuadTreeDemoRenderBootstrap::DrawFrame()
{
	return RendererPtr->DrawFrame();
}

bool QuadTreeDemoRenderBootstrap::CreateWhitePixelTexture()
{
	ImageCreateInfo info{};
	info.Format = VK_FORMAT_R8G8B8A8_SRGB;
	info.Extent = {1, 1};
	info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.DebugName = "QuadTreeDemo.WhitePixel";

	WhiteImage = Images->Create(info);
	if (!WhiteImage.IsValid()) return false;

	const uint32_t pixel = 0xFFFFFFFFu;
	if (!Images->Upload(WhiteImage, &pixel, sizeof(pixel)))
	{
		Images->Destroy(WhiteImage);
		WhiteImage = {};
		return false;
	}

	const VkSampler sampler = Samplers->GetNearestRepeat();
	WhiteSlot = Descriptors->RegisterSampledImage(WhiteImage, sampler);
	return WhiteSlot.IsValid();
}
