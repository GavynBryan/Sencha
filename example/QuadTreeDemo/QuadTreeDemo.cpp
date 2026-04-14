#include <core/json/JsonParser.h>
#include <core/logging/ConsoleLogSink.h>
#include <core/service/ServiceHost.h>
#include <core/system/SystemHost.h>
#include <input/InputActionRegistry.h>
#include <input/InputBindingCompiler.h>
#include <input/InputBindingService.h>
#include <input/InputTypes.h>
#include <input/SdlInputControlResolver.h>
#include <input/SdlInputSystem.h>
#include <math/spatial/QuadTree.h>
#include <render/Renderer.h>
#include <render/backend/vulkan/VulkanAllocatorService.h>
#include <render/backend/vulkan/VulkanBootstrapPolicy.h>
#include <render/backend/vulkan/VulkanBufferService.h>
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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class QuadTreeDemo
{
};

namespace
{
namespace DemoActions
{
	static constexpr std::string_view Names[] = {
		"MoveLeft",
		"MoveRight",
		"MoveUp",
		"MoveDown",
		"Quit",
	};

	enum Action : uint16_t
	{
		MoveLeft,
		MoveRight,
		MoveUp,
		MoveDown,
		Quit,
		Count,
	};
}

struct Player
{
	Vec2d Position{240.0f, 140.0f};
	Vec2d Size{32.0f, 32.0f};
	QuadTree<uint32_t>::EntryId TreeEntry = QuadTree<uint32_t>::InvalidEntry;
};

std::optional<std::string> ReadTextFile(const char* path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) return std::nullopt;

	std::ostringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

bool IsRenderableExtent(WindowExtent extent)
{
	return extent.Width > 0 && extent.Height > 0;
}

ImageHandle CreateWhiteImage(VulkanImageService& images)
{
	ImageCreateInfo info{};
	info.Format = VK_FORMAT_R8G8B8A8_SRGB;
	info.Extent = {1, 1};
	info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.DebugName = "QuadTreeDemo.WhitePixel";

	ImageHandle handle = images.Create(info);
	if (!handle.IsValid()) return {};

	const uint32_t pixel = 0xFFFFFFFFu;
	if (!images.Upload(handle, &pixel, sizeof(pixel)))
	{
		images.Destroy(handle);
		return {};
	}

	return handle;
}

Aabb2d MakeCenteredAabb(const Vec2d& center, const Vec2d& size)
{
	const Vec2d half = size * 0.5f;
	return Aabb2d(center - half, center + half);
}

void SubmitBox(SpriteFeature& sprites, BindlessImageIndex texture, const Aabb2d& bounds,
	uint32_t color, int32_t sortKey)
{
	SpriteFeature::Sprite sprite{};
	sprite.CenterX = bounds.Center().X;
	sprite.CenterY = bounds.Center().Y;
	sprite.Width = bounds.Size().X;
	sprite.Height = bounds.Size().Y;
	sprite.Texture = texture;
	sprite.Color = color;
	sprite.SortKey = sortKey;
	sprites.Submit(sprite);
}

void SubmitLine(SpriteFeature& sprites, BindlessImageIndex texture,
	float centerX, float centerY, float width, float height, uint32_t color)
{
	SpriteFeature::Sprite sprite{};
	sprite.CenterX = centerX;
	sprite.CenterY = centerY;
	sprite.Width = width;
	sprite.Height = height;
	sprite.Texture = texture;
	sprite.Color = color;
	sprite.SortKey = 0;
	sprites.Submit(sprite);
}

void SubmitOutline(SpriteFeature& sprites, BindlessImageIndex texture,
	const Aabb2d& bounds, uint32_t color)
{
	constexpr float thickness = 1.0f;
	const float width = std::max(1.0f, bounds.Size().X);
	const float height = std::max(1.0f, bounds.Size().Y);
	const float centerX = bounds.Center().X;
	const float centerY = bounds.Center().Y;

	SubmitLine(sprites, texture, centerX, bounds.Min.Y, width, thickness, color);
	SubmitLine(sprites, texture, centerX, bounds.Max.Y, width, thickness, color);
	SubmitLine(sprites, texture, bounds.Min.X, centerY, thickness, height, color);
	SubmitLine(sprites, texture, bounds.Max.X, centerY, thickness, height, color);
}

std::optional<InputBindingTable> LoadInputBindings(Logger& logger)
{
	auto configText = ReadTextFile(SENCHA_QUADTREE_INPUT_CONFIG_PATH);
	if (!configText)
	{
		logger.Error("Failed to read input config: {}", SENCHA_QUADTREE_INPUT_CONFIG_PATH);
		return std::nullopt;
	}

	JsonParseError parseError;
	auto json = JsonParse(*configText, &parseError);
	if (!json)
	{
		logger.Error(
			"Failed to parse input config at byte {}: {}",
			parseError.Position,
			parseError.Message);
		return std::nullopt;
	}

	InputCompileError compileError;
	auto config = DeserializeInputConfig(*json, &compileError);
	if (!config)
	{
		logger.Error("Failed to deserialize input config: {}", compileError.Message);
		return std::nullopt;
	}

	SdlInputControlResolver controlResolver;
	InputActionRegistry actionRegistry{DemoActions::Names};
	auto table = CompileInputBindings(*config, actionRegistry, controlResolver, &compileError);
	if (!table)
	{
		logger.Error("Failed to compile input bindings: {}", compileError.Message);
		return std::nullopt;
	}

	return std::move(*table);
}

void InjectInputEvent(SdlInputSystem& input, const SDL_Event& event)
{
	switch (event.type)
	{
	case SDL_EVENT_KEY_DOWN:
		if (!event.key.repeat)
		{
			input.GetRawInput().Emplace(
				InputDeviceType::Keyboard, true,
				static_cast<uint16_t>(event.key.scancode), 1.0f, InputUserId{});
		}
		break;

	case SDL_EVENT_KEY_UP:
		input.GetRawInput().Emplace(
			InputDeviceType::Keyboard, false,
			static_cast<uint16_t>(event.key.scancode), 0.0f, InputUserId{});
		break;

	default:
		break;
	}
}

} // anonymous namespace

int main()
{
	ServiceHost services;
	auto& logging = services.GetLoggingProvider();
	logging.AddSink<ConsoleLogSink>();

	auto& logger = logging.GetLogger<QuadTreeDemo>();

	auto inputTable = LoadInputBindings(logger);
	if (!inputTable) return 1;

	WindowCreateInfo windowInfo;
	windowInfo.Title = "Sencha QuadTree Demo";
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
	vulkanPolicy.AppName = "QuadTreeDemo";
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

	VulkanBufferService buffers(logging, device, allocator, upload);
	if (!buffers.IsValid()) return 1;

	VulkanImageService imageService(logging, device, allocator, upload);
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

	VulkanFrameService frames(logging, device, queues, swapchain);
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

	const ImageHandle white = CreateWhiteImage(imageService);
	if (!white.IsValid()) return 1;

	const VkSampler sampler = samplers.GetNearestRepeat();
	const BindlessImageIndex whiteSlot = descriptors.RegisterSampledImage(white, sampler);
	if (!whiteSlot.IsValid()) return 1;

	InputBindingService bindings;
	bindings.SetBindings(std::move(*inputTable));

	SystemHost systems;
	auto& input = systems.AddSystem<SdlInputSystem>(SystemPhase::Input, logging, bindings);
	systems.Init();

	QuadTree<uint32_t>::Config treeConfig;
	treeConfig.RootBounds = Aabb2d(Vec2d(0.0f, 0.0f), Vec2d(1280.0f, 720.0f));
	treeConfig.MaxDepth = 6;
	// Demo-only setting: force a single moving entry to descend so the current
	// owning node is visible without hidden static objects shaping the tree.
	treeConfig.MaxEntriesPerLeaf = 0;

	QuadTree<uint32_t> tree(treeConfig);

	Player player;

	bool running = true;
	auto previousTime = std::chrono::steady_clock::now();

	logger.Info("QuadTree demo running. Arrow keys move the block. Escape quits.");

	while (running && windows.IsAlive(windowId))
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			windows.HandleEvent(event);
			InjectInputEvent(input, event);

			if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
			{
				running = false;
			}
		}

		systems.Update();

		Vec2d direction = Vec2d::Zero();
		for (const auto& event : input.GetEvents().Items())
		{
			if (event.Action == InputActionId{DemoActions::Quit}
				&& event.Phase == InputPhase::Started)
			{
				running = false;
			}

			if (event.Phase != InputPhase::Started && event.Phase != InputPhase::Performed)
			{
				continue;
			}

			if (event.Action == InputActionId{DemoActions::MoveLeft}) direction.X -= 1.0f;
			if (event.Action == InputActionId{DemoActions::MoveRight}) direction.X += 1.0f;
			if (event.Action == InputActionId{DemoActions::MoveUp}) direction.Y += 1.0f;
			if (event.Action == InputActionId{DemoActions::MoveDown}) direction.Y -= 1.0f;
		}

		const auto now = std::chrono::steady_clock::now();
		const std::chrono::duration<float> elapsed = now - previousTime;
		previousTime = now;
		const float dt = std::min(elapsed.count(), 1.0f / 15.0f);

		constexpr float speed = 300.0f;
		if (direction.SqrMagnitude() > 0.0f)
		{
			direction = direction.Normalized();
			player.Position += direction * (speed * dt);
		}

		WindowExtent resizeExtent{};
		const bool resizePending = windows.ConsumeResize(windowId, &resizeExtent);
		const auto* state = windows.GetState(windowId);
		const bool minimized = state && state->Minimized;
		WindowExtent currentExtent = resizePending ? resizeExtent : windows.GetExtent(windowId);

		const Vec2d half = player.Size * 0.5f;
		const Aabb2d& worldBounds = tree.GetConfig().RootBounds;
		const float maxPlayerX = std::min(
			worldBounds.Max.X - half.X,
			static_cast<float>(currentExtent.Width) - half.X);
		const float maxPlayerY = std::min(
			worldBounds.Max.Y - half.Y,
			static_cast<float>(currentExtent.Height) - half.Y);
		player.Position.X = std::clamp(
			player.Position.X, worldBounds.Min.X + half.X, maxPlayerX);
		player.Position.Y = std::clamp(
			player.Position.Y, worldBounds.Min.Y + half.Y, maxPlayerY);

		const Aabb2d playerBounds = MakeCenteredAabb(player.Position, player.Size);
		tree.Clear();
		player.TreeEntry = tree.Insert(0, playerBounds);

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

		tree.ForEachNode([&](const Aabb2d& bounds, int depth)
		{
			const uint32_t alpha = static_cast<uint32_t>(std::max(34, 96 - depth * 10));
			const uint32_t color = 0x00FFFFFFu | (alpha << 24);
			SubmitOutline(*sprites, whiteSlot, bounds, color);
		});
		SubmitOutline(*sprites, whiteSlot, tree.GetEntryNodeBounds(player.TreeEntry), 0xBFFFFFFFu);
		SubmitBox(*sprites, whiteSlot, playerBounds, 0xFFFFFFFFu, 10);

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
	}

	systems.Shutdown();

	return 0;
}
