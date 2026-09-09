#include "EditorUiFeature.h"

#include "EditorSkin.h"
#include "EditorUiSkin.h"
#include "EditorUiStyle.h"
#include "IEditorPanel.h"
#include "chrome/ChromeBars.h"
#include "chrome/ChromeChassis.h"

#include <app/Engine.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/console/ConsoleTypes.h>
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

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <string>
#include <variant>
#include <vector>

#ifndef SENCHA_EDITOR_THEME_DIR
#define SENCHA_EDITOR_THEME_DIR "."
#endif

namespace
{
// Docks a region node's panels along one axis, each unit taking its dock
// weight's share of the region. remainderDir is where the not-yet-docked units
// go (Down stacks top-to-bottom, Right packs left-to-right). Panels sharing a
// non-negative tab group form ONE unit: every member docks into the same node
// and ImGui tabs them (unit weight = the largest member weight). The first
// visible member of a multi-panel unit is recorded in outFrontTabs so the host
// can raise it once the windows exist (a rebuild would otherwise leave an
// arbitrary tab selected).
void DockPacked(ImGuiID region, const std::vector<IEditorPanel*>& panels, ImGuiDir remainderDir,
                std::vector<std::string>& outFrontTabs)
{
    struct DockUnit
    {
        std::vector<IEditorPanel*> Members{};
        float Weight = 0.0f;
        int TabGroup = -1;
    };
    std::vector<DockUnit> units;
    for (IEditorPanel* panel : panels)
    {
        DockUnit* unit = nullptr;
        const int group = panel->GetDockTabGroup();
        if (group >= 0)
            for (DockUnit& candidate : units)
                if (candidate.TabGroup == group)
                {
                    unit = &candidate;
                    break;
                }
        if (unit == nullptr)
        {
            units.push_back(DockUnit{ .TabGroup = group });
            unit = &units.back();
        }
        unit->Members.push_back(panel);
        unit->Weight = std::max(unit->Weight, panel->GetDockWeight());
    }

    float totalWeight = 0.0f;
    for (const DockUnit& unit : units)
        totalWeight += unit.Weight;

    const int count = static_cast<int>(units.size());
    for (int i = 0; i < count; ++i)
    {
        ImGuiID node = region;
        if (i < count - 1)
        {
            // Slice this unit's weighted share off; recurse into the remainder.
            ImGuiID slice = 0;
            const float weight = units[i].Weight;
            const float remainderRatio = (totalWeight - weight) / totalWeight;
            region = ImGui::DockBuilderSplitNode(region, remainderDir, remainderRatio, nullptr, &slice);
            node = slice;
            totalWeight -= weight;
        }
        for (IEditorPanel* member : units[i].Members)
            ImGui::DockBuilderDockWindow(member->GetTitle().data(), node);
        if (units[i].Members.size() > 1)
        {
            for (IEditorPanel* member : units[i].Members)
                if (member->IsVisible())
                {
                    outFrontTabs.emplace_back(member->GetTitle());
                    break;
                }
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
                            const DockLayoutRatios& ratios,
                            std::vector<std::string>& outFrontTabs)
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
        DockPacked(bottom, bottomPanels, ImGuiDir_Right, outFrontTabs);
    }
    if (!leftPanels.empty())
    {
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, ratios.Left, nullptr, &center);
        DockPacked(left, leftPanels, ImGuiDir_Down, outFrontTabs);
    }
    if (!rightPanels.empty() || !rightBottomPanels.empty())
    {
        ImGuiID rightColumn = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, ratios.Right, nullptr, &center);
        if (!rightPanels.empty() && !rightBottomPanels.empty())
        {
            ImGuiID upper = 0;
            const ImGuiID lower =
                ImGui::DockBuilderSplitNode(rightColumn, ImGuiDir_Down, ratios.RightBottom, nullptr, &upper);
            DockPacked(upper, rightPanels, ImGuiDir_Right, outFrontTabs);
            DockPacked(lower, rightBottomPanels, ImGuiDir_Down, outFrontTabs);
        }
        else
        {
            // Only one of the two right regions is populated; it takes the column.
            DockPacked(rightColumn, rightPanels.empty() ? rightBottomPanels : rightPanels,
                       rightPanels.empty() ? ImGuiDir_Down : ImGuiDir_Right, outFrontTabs);
        }
    }
    if (!centerBottomPanels.empty())
    {
        const ImGuiID lower =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, ratios.CenterBottom, nullptr, &center);
        DockPacked(lower, centerBottomPanels, ImGuiDir_Right, outFrontTabs);
    }

    // The viewport owns the central node; drop its tab bar so it reads as the scene.
    if (ImGuiDockNode* centerNode = ImGui::DockBuilderGetNode(center))
        centerNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    DockPacked(center, centerPanels, ImGuiDir_Down, outFrontTabs);

    ImGui::DockBuilderFinish(dockId);
}

// The one place a display scale becomes the UI scale. Archived so a chosen value
// survives restarts; 0 (the default) defers to what the window's display
// reports, which is 1 on a plain desktop. Read once, before the style and the
// font atlas are built at that size.
float ResolveUiScale(ConsoleRegistry& registry, SDL_Window* window, Logger* log)
{
    registry.RegisterCVar({
        .Name = "editor.ui.scale",
        .Owner = "editor",
        .Type = CVarType::Double,
        .DefaultValue = 0.0,
        .CurrentValue = 0.0,
        .Flags = CVarFlags::Archive,
        .Help = "UI scale for the editor chrome and fonts. 0 = follow the window's display scale. Applied at startup.",
        .Source = { "editor" },
        .Min = 0.0,
        .Max = 4.0,
    });

    double requested = 0.0;
    if (const CVarMetadata* var = registry.FindCVar("editor.ui.scale"))
        if (const double* value = std::get_if<double>(&var->CurrentValue))
            requested = *value;

    float scale = static_cast<float>(requested);
    const char* origin = "editor.ui.scale";
    if (scale <= 0.0f)
    {
        scale = window != nullptr ? SDL_GetWindowDisplayScale(window) : 0.0f;
        origin = "display";
        if (scale <= 0.0f)
        {
            scale = 1.0f;
            origin = "fallback";
        }
    }
    scale = std::clamp(scale, 0.5f, 4.0f);
    if (log != nullptr)
        log->Info("EditorUiFeature: UI scale {} ({})", scale, origin);
    return scale;
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

bool EditorUiFeature::Setup(const RenderFeatureServices& featureServices)
{
    const RendererServices& services = *featureServices.Backend;
    Log = services.Logging ? &services.Logging->GetLogger<EditorUiFeature>() : nullptr;
    Valid = InitImGui(services);
    if (Valid)
        RegisterClickCommand(EngineInstance.Console().Registry());
    if (Log != nullptr)
        Log->Info("EditorUiFeature setup {}", Valid ? "succeeded" : "failed");
    // The editor shell is its panels: without an ImGui context there is
    // nothing for this feature to draw.
    return Valid;
}

void EditorUiFeature::OnDraw(const RenderFrame& renderFrame)
{
    const FrameContext& frame = *renderFrame.Backend;
    if (!Valid)
        return;

    if (!LoggedFirstDraw && Log != nullptr)
    {
        Log->Info("EditorUiFeature drawing {} panel(s)", Panels.size());
        LoggedFirstDraw = true;
    }

    // The look is built on the first frame, not in Setup: the startup script
    // (argv +set, config cvars) runs after every feature's Setup, so this is the
    // earliest point editor.ui.scale holds its final value. The backend uploads
    // the font atlas lazily in its NewFrame, so the atlas is still open here.
    if (!LookBuilt)
    {
        EditorUi::UiScale = ResolveUiScale(EngineInstance.Console().Registry(), Window.GetHandle(), Log);
        EditorUi::Apply(ImGui::GetStyle());
        EditorUi::LoadFonts(ImGui::GetIO());
        LookBuilt = true;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    FeedQueuedClicks();
    ImGui::NewFrame();

    // One frame after a layout rebuild: raise the intended front tab of each
    // tab-group node. Deferred because SetWindowFocus is by-name and the freshly
    // docked windows only exist once they have been submitted.
    if (!PendingTabFocus.empty())
    {
        for (const std::string& title : PendingTabFocus)
            ImGui::SetWindowFocus(title.c_str());
        PendingTabFocus.clear();
    }

    DrawMainMenuBar();
    for (const std::function<void()>& chrome : ChromeBars)
    {
        if (chrome)
            chrome();
    }

    // The chassis fills the work area the chrome bars left, and the dock host
    // sits inside its ring: our own window with no background and a plain
    // DockSpace, so the chassis ground shows through the seams between nodes.
    // Every docked panel, the viewports included, paints its own opaque body.
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 workMin = vp->WorkPos;
        const ImVec2 workMax(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
        ImDrawList* background = ImGui::GetBackgroundDrawList();
        EditorChrome::DrawChassisBase(background, workMin, workMax);
        EditorChrome::DrawChassisEdges(background, workMin, workMax);
        EditorChrome::DrawChassisOrnaments(background, workMin, workMax);

        const float inset = EditorChrome::ChassisInset();
        ImGui::SetNextWindowPos(ImVec2(workMin.x + inset, workMin.y + inset));
        ImGui::SetNextWindowSize(ImVec2(std::max(0.0f, vp->WorkSize.x - inset * 2.0f),
                                        std::max(0.0f, vp->WorkSize.y - inset * 2.0f)));
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
            PendingTabFocus.clear();
            BuildDefaultDockLayout(dockId, Panels, LayoutRatios, PendingTabFocus);
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

void EditorUiFeature::RegisterClickCommand(ConsoleRegistry& registry)
{
    registry.RegisterCommand({
        .Name = "editor.ui.click",
        .Owner = "editor",
        .Usage = "editor.ui.click <x> <y> [frame]",
        .Help = "Left-click the UI at window pixel (x, y). With a frame number, "
                "waits until that UI frame so the layout has settled. For "
                "unattended verification alongside render.screenshot.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            if (args.size() < 2 || args.size() > 3)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("expected <x> <y> [frame]");
                return result;
            }
            QueuedClick click;
            try
            {
                click.Pos = ImVec2(std::stof(args[0]), std::stof(args[1]));
                click.AtFrame = args.size() == 3 ? std::stoi(args[2]) : 0;
            }
            catch (const std::exception&)
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("x, y, and frame must be numbers");
                return result;
            }
            QueuedClicks.push_back(click);
            result.Info(std::format("click queued at ({}, {}) for frame {}", click.Pos.x, click.Pos.y, click.AtFrame));
            return result;
        },
    });
}

void EditorUiFeature::FeedQueuedClicks()
{
    if (QueuedClicks.empty())
        return;
    ImGuiIO& io = ImGui::GetIO();
    const int frame = ImGui::GetFrameCount();
    for (std::size_t i = 0; i < QueuedClicks.size();)
    {
        QueuedClick& click = QueuedClicks[i];
        if (frame < click.AtFrame)
        {
            ++i;
            continue;
        }
        io.AddMousePosEvent(click.Pos.x, click.Pos.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, !click.Pressed);
        if (!click.Pressed)
        {
            click.Pressed = true;
            ++i;
            continue;
        }
        QueuedClicks.erase(QueuedClicks.begin() + static_cast<std::ptrdiff_t>(i));
    }
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

void EditorUiFeature::SetIdentity(ShellIdentity identity)
{
    Identity = std::move(identity);
}

void EditorUiFeature::SetStatusProvider(std::function<std::string()> statusProvider)
{
    StatusProvider = std::move(statusProvider);
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

    // Every ImGuiTextureBinding costs one combined-image-sampler set: the skin,
    // the viewport targets, and up to editor.materials.thumbnail_budget resident
    // material thumbnails all draw from this pool, so it is sized well past that
    // budget's default (128).
    const std::array<VkDescriptorPoolSize, 11> poolSizes{{
        { VK_DESCRIPTOR_TYPE_SAMPLER, 32 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 },
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
    poolInfo.maxSets = 1024;
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

    EditorChrome::BarBackdrop(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(),
                              ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                     ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                              EditorChrome::BarEdge::Bottom);

    // The identity plate at the head of the bar, before the menus.
    if (!Identity.Product.empty())
    {
        EditorUi::RoleLabel(EditorUi::TextRole::ApplicationTitle, Identity.Product,
                            ImGui::GetColorU32(EditorUi::AccentHover));
        if (!Identity.Subtitle.empty())
        {
            ImGui::SameLine(0.0f, EditorUi::Px(6.0f));
            EditorUi::RoleLabel(EditorUi::TextRole::Status, Identity.Subtitle);
        }
        ImGui::SameLine(0.0f, EditorUi::Px(14.0f));
    }

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

    // What is open, at the tail of the bar.
    if (StatusProvider)
    {
        const std::string status = StatusProvider();
        if (!status.empty())
        {
            // A path or a name is data, so it takes the quiet secondary role
            // rather than the uppercase label roles.
            const float width = EditorUi::MeasureRoleText(EditorUi::TextRole::SecondaryText, status).x;
            const float avail = ImGui::GetContentRegionAvail().x - EditorUi::Px(10.0f);
            if (avail > width)
                ImGui::SameLine(ImGui::GetCursorPosX() + avail - width);
            EditorUi::RoleLabel(EditorUi::TextRole::SecondaryText, status);
        }
    }

    ImGui::EndMainMenuBar();
}
