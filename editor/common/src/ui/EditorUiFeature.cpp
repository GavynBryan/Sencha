#include "EditorUiFeature.h"

#include "EditorSkin.h"
#include "EditorUiSkin.h"
#include "EditorUiStyle.h"
#include "IEditorPanel.h"

#include <app/Engine.h>
#include <core/console/ConsoleService.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <platform/SdlWindow.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h> // DockBuilder* for the default layout
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <string>
#include <vector>

#ifndef SENCHA_EDITOR_THEME_DIR
#define SENCHA_EDITOR_THEME_DIR "."
#endif

namespace
{
// Docks a region node's panels along one axis, each taking its dock weight's
// share of the region. remainderDir is where the not-yet-docked panels go
// (Down stacks top-to-bottom, Right packs left-to-right). Single-panel regions
// just dock the one window.
void DockPacked(ImGuiID region, const std::vector<IEditorPanel*>& panels, ImGuiDir remainderDir)
{
    float totalWeight = 0.0f;
    for (const IEditorPanel* panel : panels)
        totalWeight += panel->GetDockWeight();

    const int count = static_cast<int>(panels.size());
    for (int i = 0; i < count; ++i)
    {
        if (i < count - 1)
        {
            // Slice this panel's weighted share off; recurse into the remainder.
            ImGuiID slice = 0;
            const float weight = panels[i]->GetDockWeight();
            const float remainderRatio = (totalWeight - weight) / totalWeight;
            const ImGuiID remainder =
                ImGui::DockBuilderSplitNode(region, remainderDir, remainderRatio, nullptr, &slice);
            ImGui::DockBuilderDockWindow(panels[i]->GetTitle().data(), slice);
            region = remainder;
            totalWeight -= weight;
        }
        else
        {
            ImGui::DockBuilderDockWindow(panels[i]->GetTitle().data(), region);
        }
    }
}

// Builds the designed default layout from each panel's DockSlot hint: a
// full-width bottom strip, a left column, a right column (an upper row over a
// lower stack), and the viewport in the center (no tab bar) with its own strip
// beneath. Regions with no panels are never split, so applications that only
// populate a few slots get a correspondingly simpler tree. No panel is named
// here — placement is declarative on the panels.
void BuildDefaultDockLayout(ImGuiID dockId,
                            const std::vector<std::unique_ptr<IEditorPanel>>& panels,
                            const DockLayoutRatios& ratios)
{
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);

    std::vector<IEditorPanel*> leftPanels, rightPanels, rightBottomPanels,
        bottomPanels, centerPanels, centerBottomPanels;
    for (const std::unique_ptr<IEditorPanel>& panel : panels)
    {
        if (panel == nullptr)
            continue;
        switch (panel->GetDockSlot())
        {
        case DockSlot::Left:         leftPanels.push_back(panel.get());         break;
        case DockSlot::Right:        rightPanels.push_back(panel.get());        break;
        case DockSlot::RightBottom:  rightBottomPanels.push_back(panel.get());  break;
        case DockSlot::Bottom:       bottomPanels.push_back(panel.get());       break;
        case DockSlot::Center:       centerPanels.push_back(panel.get());       break;
        case DockSlot::CenterBottom: centerBottomPanels.push_back(panel.get()); break;
        case DockSlot::Floating: break; // left undocked
        }
    }

    // Split order shapes the tree: the bottom strip spans the full width, the
    // left and right columns span the remaining height, and the center strips
    // only the center column.
    ImGuiID center = dockId;
    if (!bottomPanels.empty())
    {
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, ratios.Bottom, nullptr, &center);
        DockPacked(bottom, bottomPanels, ImGuiDir_Right);
    }
    if (!leftPanels.empty())
    {
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, ratios.Left, nullptr, &center);
        DockPacked(left, leftPanels, ImGuiDir_Down);
    }
    if (!rightPanels.empty() || !rightBottomPanels.empty())
    {
        ImGuiID rightColumn = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, ratios.Right, nullptr, &center);
        if (!rightPanels.empty() && !rightBottomPanels.empty())
        {
            ImGuiID upper = 0;
            const ImGuiID lower =
                ImGui::DockBuilderSplitNode(rightColumn, ImGuiDir_Down, ratios.RightBottom, nullptr, &upper);
            DockPacked(upper, rightPanels, ImGuiDir_Right);
            DockPacked(lower, rightBottomPanels, ImGuiDir_Down);
        }
        else
        {
            // Only one of the two right regions is populated; it takes the column.
            DockPacked(rightColumn, rightPanels.empty() ? rightBottomPanels : rightPanels,
                       rightPanels.empty() ? ImGuiDir_Down : ImGuiDir_Right);
        }
    }
    if (!centerBottomPanels.empty())
    {
        const ImGuiID lower =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, ratios.CenterBottom, nullptr, &center);
        DockPacked(lower, centerBottomPanels, ImGuiDir_Right);
    }

    // The viewport owns the central node; drop its tab bar so it reads as the scene.
    if (ImGuiDockNode* centerNode = ImGui::DockBuilderGetNode(center))
        centerNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    DockPacked(center, centerPanels, ImGuiDir_Down);

    ImGui::DockBuilderFinish(dockId);
}

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
                                 VulkanFrameService& frames,
                                 std::string iniFileName,
                                 DockLayoutRatios layoutRatios)
    : EngineInstance(engine)
    , Window(window)
    , Instance(instance)
    , Frames(frames)
    , IniFileName(std::move(iniFileName))
    , LayoutRatios(layoutRatios)
    , ThemePrefs(SENCHA_EDITOR_THEME_DIR)
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
    for (const std::function<void()>& chrome : ChromeBars)
    {
        if (chrome)
            chrome();
    }

    // Host dockspace filling the work area the chrome bars left. We build the host
    // window ourselves with NoBackground + a plain DockSpace (NOT PassthruCentralNode):
    // PassthruCentralNode fills WindowBg over the whole root and only leaves a
    // transparent hole when the central node is *empty* — but the viewport docks
    // *into* the central node, so that bg would paint over the 3D. With no dockspace
    // bg, the viewport window's own NoBackground keeps the central node clear so the
    // scene (rendered behind ImGui and scissored to the viewport rect) shows through;
    // the side panels carry their own opaque backgrounds.
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);

        const ImGuiWindowFlags hostFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("##EditorDockHost", nullptr, hostFlags);
        ImGui::PopStyleVar(3);

        const ImGuiID dockId = ImGui::GetID("EditorDockSpace");
        if (LayoutDirty || ImGui::DockBuilderGetNode(dockId) == nullptr)
        {
            BuildDefaultDockLayout(dockId, Panels, LayoutRatios);
            LayoutDirty = false;
        }
        ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        ImGui::End();
    }

    for (const std::unique_ptr<IEditorPanel>& panel : Panels)
    {
        if (panel != nullptr && panel->IsVisible())
            panel->OnDraw();
    }

    ThemePrefs.DrawWindow(EngineInstance.Console().Registry());

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

UiInputCapture EditorUiFeature::GetInputCapture() const
{
    if (!Valid)
        return {};

    const ImGuiIO& io = ImGui::GetIO();
    return UiInputCapture{
        .Mouse = io.WantCaptureMouse,
        // Text input (an active text field) also implies keyboard ownership.
        .Keyboard = io.WantCaptureKeyboard || io.WantTextInput,
    };
}

void EditorUiFeature::SetMouseInputEnabled(bool enabled)
{
    if (!Valid)
        return;

    // NoMouse parks io.MousePos off-screen and drops button state on the next
    // NewFrame, so hover/highlight/click all stop until the flag is cleared.
    ImGuiIO& io = ImGui::GetIO();
    if (enabled)
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    else
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
}

void EditorUiFeature::SetKeyboardInputEnabled(bool enabled)
{
    if (!Valid)
        return;

    // NoKeyboard ignores keyboard events and clears existing key state, so a
    // focused widget (e.g. the console input) stops receiving keystrokes while a
    // viewport gesture owns input and the keys belong to the fly camera.
    ImGuiIO& io = ImGui::GetIO();
    if (enabled)
        io.ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;
    else
        io.ConfigFlags |= ImGuiConfigFlags_NoKeyboard;
}

void EditorUiFeature::AddPanel(std::unique_ptr<IEditorPanel> panel)
{
    if (panel != nullptr)
        Panels.push_back(std::move(panel));
}

void EditorUiFeature::AddChrome(std::function<void()> draw)
{
    if (draw)
        ChromeBars.push_back(std::move(draw));
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

void EditorUiFeature::SetFileActions(std::function<void()> newAction,
                                     std::function<void()> openAction,
                                     std::function<void()> saveAction,
                                     std::function<void()> saveAsAction)
{
    NewAction = std::move(newAction);
    OpenAction = std::move(openAction);
    SaveAction = std::move(saveAction);
    SaveAsAction = std::move(saveAsAction);
}

void EditorUiFeature::SetSaveAllAction(std::function<void()> saveAllAction)
{
    SaveAllAction = std::move(saveAllAction);
}

void EditorUiFeature::SetNewWorldAction(std::function<void()> newWorldAction)
{
    NewWorldAction = std::move(newWorldAction);
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
    // No keyboard nav: it reserves Tab/arrows/Enter for widget focus cycling, which
    // steals viewport hotkeys (the edge-cut Tab toggle most visibly). Text fields
    // still gate keys via WantTextInput, so typing capture is unaffected.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Per-application layout file: the editor family would otherwise fight
    // over one ./imgui.ini. Points at the member so it outlives the context.
    if (!IniFileName.empty())
        io.IniFilename = IniFileName.c_str();
    EditorUi::Apply(ImGui::GetStyle());
    EditorUi::LoadFonts(io);

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

    // Load the 9-slice texture skin (soft dependency: if it fails, EditorUiSkin
    // keeps its gradient rendering). Needs the Vulkan backend up (AddTexture).
    if (services.Images != nullptr && services.Samplers != nullptr)
    {
        Skin = std::make_unique<EditorSkin>(*services.Images, *services.Samplers, SENCHA_EDITOR_SKIN_DIR);
        if (Skin->Loaded())
            EditorUiSkin::SetActiveSkin(Skin.get());
        else if (Log)
            Log->Warn("EditorUiFeature: skin textures not loaded; using gradient fallback");
    }

    return true;
}

void EditorUiFeature::ShutdownImGui()
{
    if (DeviceHandle != VK_NULL_HANDLE)
        vkDeviceWaitIdle(DeviceHandle);

    // Release the skin (its ImGui descriptor sets + images) while the backend and
    // image service are still alive.
    EditorUiSkin::SetActiveSkin(nullptr);
    Skin.reset();

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

    EditorUiSkin::Band(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
                       ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                              ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                       EditorUi::HeaderBg);

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New", "Ctrl+N", false, NewAction != nullptr) && NewAction)
            NewAction();
        if (NewWorldAction && ImGui::MenuItem("New World"))
            NewWorldAction();
        if (ImGui::MenuItem("Open", "Ctrl+O", false, OpenAction != nullptr) && OpenAction)
            OpenAction();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, SaveAction != nullptr) && SaveAction)
            SaveAction();
        if (ImGui::MenuItem("Save All", "Ctrl+Shift+S", false, SaveAllAction != nullptr) && SaveAllAction)
            SaveAllAction();
        if (ImGui::MenuItem("Save As", nullptr, false, SaveAsAction != nullptr) && SaveAsAction)
            SaveAsAction();
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

    if (ImGui::BeginMenu("View"))
    {
        // Per-panel visibility (the viewport is the central node — not hideable).
        for (const std::unique_ptr<IEditorPanel>& panel : Panels)
        {
            if (panel == nullptr || panel->GetDockSlot() == DockSlot::Center)
                continue;
            const std::string title(panel->GetTitle());
            if (ImGui::MenuItem(title.c_str(), nullptr, panel->IsVisible()))
                panel->ToggleVisible();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
            LayoutDirty = true;
        ImGui::Separator();
        if (ImGui::BeginMenu("Preferences"))
        {
            if (ImGui::BeginMenu("Theme"))
            {
                ThemePrefs.DrawMenu(EngineInstance.Console().Registry());
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}
