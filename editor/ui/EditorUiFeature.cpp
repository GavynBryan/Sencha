#include "EditorUiFeature.h"

#include "IEditorPanel.h"

#include <app/Engine.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <platform/SdlWindow.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <array>

namespace
{
bool IsEditorUiInputEvent(const SDL_Event& event)
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

EditorUiFeature::EditorUiFeature(Engine& engine,
                                 SdlWindow& window,
                                 VulkanInstanceService& instance,
                                 VulkanFrameService& frames)
    : EngineInstance(engine)
    , Window(window)
    , Instance(instance)
    , Frames(frames)
{
}

EditorUiFeature::~EditorUiFeature()
{
    Teardown();
}

void EditorUiFeature::Setup(const RendererServices& services)
{
    Log = services.Logging ? &services.Logging->GetLogger<EditorUiFeature>() : nullptr;
    Valid = InitImGui(services);
    if (Log != nullptr)
        Log->Info("EditorUiFeature setup {}", Valid ? "succeeded" : "failed");
}

void EditorUiFeature::OnDraw(const FrameContext& frame)
{
    if (!Valid)
        return;

    if (!LoggedFirstDraw && Log != nullptr)
    {
        Log->Info("EditorUiFeature drawing {} panel(s)", Panels.size());
        LoggedFirstDraw = true;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    DrawMainMenuBar();
    for (const std::unique_ptr<IEditorPanel>& panel : Panels)
    {
        if (panel != nullptr && panel->IsVisible())
            panel->OnDraw();
    }

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.Cmd);
}

void EditorUiFeature::Teardown()
{
    if (!Valid && DescriptorPool == VK_NULL_HANDLE)
        return;

    ShutdownImGui();
    Valid = false;
}

bool EditorUiFeature::ProcessSdlEvent(const SDL_Event& event)
{
    if (!Valid)
        return false;

    ImGui_ImplSDL3_ProcessEvent(&event);
    if (!IsEditorUiInputEvent(event))
        return false;

    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput;
}

void EditorUiFeature::AddPanel(std::unique_ptr<IEditorPanel> panel)
{
    if (panel != nullptr)
        Panels.push_back(std::move(panel));
}

void EditorUiFeature::SetUndoActions(std::function<void()> undoAction,
                                     std::function<void()> redoAction,
                                     std::function<bool()> canUndoAction,
                                     std::function<bool()> canRedoAction)
{
    UndoAction = std::move(undoAction);
    RedoAction = std::move(redoAction);
    CanUndoAction = std::move(canUndoAction);
    CanRedoAction = std::move(canRedoAction);
}

bool EditorUiFeature::InitImGui(const RendererServices& services)
{
    if (!services.Device || !services.PhysicalDevice || !services.Queues || !services.Swapchain)
    {
        if (Log) Log->Error("EditorUiFeature: missing renderer services");
        return false;
    }

    const auto& queueFamilies = services.Queues->GetQueueFamilies();
    if (!queueFamilies.Graphics)
    {
        if (Log) Log->Error("EditorUiFeature: graphics queue family unavailable");
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
        if (Log) Log->Error("EditorUiFeature: failed to create descriptor pool");
        ShutdownImGui();
        return false;
    }

    if (!ImGui_ImplSDL3_InitForVulkan(Window.GetHandle()))
    {
        if (Log) Log->Error("EditorUiFeature: SDL backend init failed");
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
    vulkanInfo.PipelineRenderingCreateInfo.depthAttachmentFormat = services.DepthFormat;

    if (!ImGui_ImplVulkan_Init(&vulkanInfo))
    {
        if (Log) Log->Error("EditorUiFeature: Vulkan backend init failed");
        ShutdownImGui();
        return false;
    }
    VulkanBackendReady = true;

    return true;
}

void EditorUiFeature::ShutdownImGui()
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

void EditorUiFeature::DrawMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        ImGui::MenuItem("New", nullptr, false, false);
        ImGui::MenuItem("Open", nullptr, false, false);
        ImGui::MenuItem("Save", nullptr, false, false);
        ImGui::MenuItem("Save As", nullptr, false, false);
        if (ImGui::MenuItem("Exit"))
            EngineInstance.RequestExit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        const bool canUndo = CanUndoAction ? CanUndoAction() : false;
        const bool canRedo = CanRedoAction ? CanRedoAction() : false;

        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo) && UndoAction)
            UndoAction();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo) && RedoAction)
            RedoAction();

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}
