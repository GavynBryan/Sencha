#include <debug/ImGuiDebugOverlay.h>

#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <window/SdlWindow.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <array>

namespace
{
bool IsDebugUiInputEvent(const SDL_Event& event)
{
	switch (event.type)
	{
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_TEXT_INPUT:
	case SDL_EVENT_TEXT_EDITING:
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
	case SDL_EVENT_MOUSE_MOTION:
	case SDL_EVENT_MOUSE_WHEEL:
		return true;
	default:
		return false;
	}
}
}

ImGuiDebugOverlay::ImGuiDebugOverlay(DebugService& debug,
                                     SdlWindow& window,
                                     VulkanInstanceService& instance,
                                     VulkanFrameService& frames)
	: Debug(debug)
	, Window(window)
	, Instance(instance)
	, Frames(frames)
{
}

ImGuiDebugOverlay::~ImGuiDebugOverlay()
{
	Teardown();
}

void ImGuiDebugOverlay::Setup(const RendererServices& services)
{
	Log = services.Logging ? &services.Logging->GetLogger<ImGuiDebugOverlay>() : nullptr;
	Valid = InitImGui(services);
	if (Valid && Log)
		Log->Info("ImGui debug overlay ready - press ` to toggle");
}

void ImGuiDebugOverlay::OnDraw(const FrameContext& frame)
{
	if (!Valid)
		return;

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	if (Debug.IsOpen())
	{
		for (auto& panel : Panels)
			panel->Draw();
	}

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.Cmd);
}

void ImGuiDebugOverlay::Teardown()
{
	if (!Valid && DescriptorPool == VK_NULL_HANDLE)
		return;

	ShutdownImGui();
	Valid = false;
}

bool ImGuiDebugOverlay::ProcessSdlEvent(const SDL_Event& event)
{
	if (event.type == SDL_EVENT_KEY_DOWN
		&& !event.key.repeat
		&& event.key.scancode == SDL_SCANCODE_GRAVE)
	{
		Debug.Toggle();
	}

	if (Valid)
		ImGui_ImplSDL3_ProcessEvent(&event);

	return Debug.IsOpen() && IsDebugUiInputEvent(event);
}

bool ImGuiDebugOverlay::InitImGui(const RendererServices& services)
{
	if (!services.Device || !services.PhysicalDevice || !services.Queues || !services.Swapchain)
	{
		if (Log) Log->Error("ImGuiDebugOverlay: missing renderer services");
		return false;
	}

	const auto& queueFamilies = services.Queues->GetQueueFamilies();
	if (!queueFamilies.Graphics)
	{
		if (Log) Log->Error("ImGuiDebugOverlay: graphics queue family unavailable");
		return false;
	}

	DeviceHandle = services.Device->GetDevice();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiContextReady = true;

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();

	const std::array<VkDescriptorPoolSize, 11> poolSizes{{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 32 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 32 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 32 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 32 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 32 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 32 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 32 },
	}};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 32 * static_cast<uint32_t>(poolSizes.size());
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();

	if (vkCreateDescriptorPool(DeviceHandle, &poolInfo, nullptr, &DescriptorPool) != VK_SUCCESS)
	{
		if (Log) Log->Error("ImGuiDebugOverlay: failed to create descriptor pool");
		ShutdownImGui();
		return false;
	}

	if (!ImGui_ImplSDL3_InitForVulkan(Window.GetHandle()))
	{
		if (Log) Log->Error("ImGuiDebugOverlay: SDL backend init failed");
		ShutdownImGui();
		return false;
	}
	SdlBackendReady = true;

	ImGui_ImplVulkan_InitInfo vulkanInfo{};
	vulkanInfo.Instance = Instance.GetInstance();
	vulkanInfo.PhysicalDevice = services.PhysicalDevice->GetPhysicalDevice();
	vulkanInfo.Device = DeviceHandle;
	vulkanInfo.QueueFamily = *queueFamilies.Graphics;
	vulkanInfo.Queue = services.Queues->GetGraphicsQueue();
	vulkanInfo.DescriptorPool = DescriptorPool;
	vulkanInfo.MinImageCount = Frames.GetFramesInFlight();
	vulkanInfo.ImageCount = Frames.GetFramesInFlight();
	vulkanInfo.UseDynamicRendering = true;
	vulkanInfo.PipelineRenderingCreateInfo.sType =
		VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	vulkanInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;

	ColorFormat = services.Swapchain->GetFormat();
	vulkanInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &ColorFormat;

	if (!ImGui_ImplVulkan_Init(&vulkanInfo))
	{
		if (Log) Log->Error("ImGuiDebugOverlay: Vulkan backend init failed");
		ShutdownImGui();
		return false;
	}
	VulkanBackendReady = true;

	return true;
}

void ImGuiDebugOverlay::ShutdownImGui()
{
	if (DeviceHandle != VK_NULL_HANDLE)
		vkDeviceWaitIdle(DeviceHandle);

	if (VulkanBackendReady)
		ImGui_ImplVulkan_Shutdown();
	if (SdlBackendReady)
		ImGui_ImplSDL3_Shutdown();

	if (ImGuiContextReady && ImGui::GetCurrentContext())
		ImGui::DestroyContext();

	if (DescriptorPool != VK_NULL_HANDLE && DeviceHandle != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(DeviceHandle, DescriptorPool, nullptr);
		DescriptorPool = VK_NULL_HANDLE;
	}

	DeviceHandle = VK_NULL_HANDLE;
	ColorFormat = VK_FORMAT_UNDEFINED;
	ImGuiContextReady = false;
	SdlBackendReady = false;
	VulkanBackendReady = false;
}
