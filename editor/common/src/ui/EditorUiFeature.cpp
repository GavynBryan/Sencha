#include "EditorUiFeature.h"

#include "actions/ActionCatalog.h"

#include "AuthoredThemeStyleSheet.h"
#include "ui/UiScaleChoice.h"

#include <ui/UiService.h>

#include "EditorConsolePanel.h"
#include "EditorUiStyle.h"
#include "IEditorPanel.h"
#include "ImGuiTextureOwner.h"
#include "workspaces/WorkspaceHost.h"
#include "chrome/ChromeBars.h"
#include "chrome/ChromeChassis.h"
#include "chrome/ChromeControls.h"
#include "chrome/ChromeHeader.h"
#include "chrome/IconDraw.h"

#include <app/Engine.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/config/EngineConfig.h>
#include <core/console/ConsoleTypes.h>
#include <debug/DebugService.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameService.h>
#include <graphics/vulkan/VulkanInstanceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <platform/SdlWindow.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h> // DockBuilder* for the default layout, FindWindowSettingsByID for the placement check
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>
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
            ImGui::DockBuilderDockWindow(member->GetWindowName().data(), node);
        if (units[i].Members.size() > 1)
        {
            for (IEditorPanel* member : units[i].Members)
                if (member->IsVisible())
                {
                    outFrontTabs.emplace_back(member->GetWindowName());
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

    std::vector<IEditorPanel*> leftEdgePanels, leftPanels, rightPanels, rightBottomPanels,
        bottomPanels, centerPanels, centerBottomPanels;
    for (const std::unique_ptr<IEditorPanel>& panel : panels)
    {
        if (panel == nullptr)
            continue;
        switch (panel->GetDockSlot())
        {
        case DockSlot::LeftEdge:     leftEdgePanels.push_back(panel.get());     break;
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
    if (!leftEdgePanels.empty())
    {
        const ImGuiID edge = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, ratios.LeftEdge, nullptr, &center);
        // The narrow edge column needs its tab width for the title; tab context
        // menus remain available by right-clicking the tab.
        if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(edge))
            node->LocalFlags |= ImGuiDockNodeFlags_NoWindowMenuButton;
        DockPacked(edge, leftEdgePanels, ImGuiDir_Down, outFrontTabs);
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

    const float scale = ChooseUiScale(requested, window != nullptr ? SDL_GetWindowDisplayScale(window) : 0.0f);
    const char* origin = requested > 0.0 ? "editor.ui.scale" : "display";
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
                                 std::string iniFileName)
    : EngineInstance(engine)
    , Window(window)
    , Instance(instance)
    , Frames(frames)
    , IniFileName(std::move(iniFileName))
    , ThemePrefs(SENCHA_EDITOR_THEME_DIR)
{
}

EditorUiFeature::EditorUiFeature(EditorUiFeature& primary, SdlWindow& window)
    : Role(ShellRole::Detached)
    , PrimaryShell(&primary)
    , EngineInstance(primary.EngineInstance)
    , Window(window)
    , Instance(primary.Instance)
    , Frames(primary.Frames)
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
    if (Valid && Role == ShellRole::Primary)
        RegisterPointerCommands(EngineInstance.Console().Registry());
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
        Log->Info("EditorUiFeature drawing");
        LoggedFirstDraw = true;
    }

    const ScopedImGuiContext scope(Context);
    if (Role == ShellRole::Primary)
    {
        PrepareFrameChrome();
        ImGui_ImplVulkan_NewFrame();
    }
    else
    {
        // The primary's style and atlas. Its backend made the font texture, so
        // this one never calls ImGui_ImplVulkan_NewFrame, which would make a
        // second; imgui 1.92's managed textures retire this.
        ImGui::GetStyle() = PrimaryShell->Context->Style;
        if (ImGui::GetIO().Fonts->TexID == 0)
            return;
    }
    // The cursor is one per desktop: only the window under the pointer sets it.
    ImGuiIO& io = ImGui::GetIO();
    if (SDL_GetMouseFocus() == Window.GetHandle())
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplSDL3_NewFrame();
    if (Role == ShellRole::Primary)
        FeedPointerActions();
    ImGui::NewFrame();

    WorkspaceView* view = ActiveView();

    // One frame after a layout rebuild: raise the intended front tab of each
    // tab-group node. Deferred because SetWindowFocus is by-name and the freshly
    // docked windows only exist once they have been submitted.
    if (view != nullptr && !view->Dock.PendingTabFocus.empty())
    {
        for (const std::string& name : view->Dock.PendingTabFocus)
            ImGui::SetWindowFocus(name.c_str());
        view->Dock.PendingTabFocus.clear();
    }

    ClaimViewActions(view);
    if (view != nullptr)
        for (const std::function<void()>& chrome : view->Chrome)
            if (chrome)
                chrome();

    DrawDockHost(view);

    if (view != nullptr)
        for (const std::unique_ptr<IEditorPanel>& panel : view->Panels)
            if (panel != nullptr && panel->IsVisible())
                panel->OnDraw();
    if (Role == ShellRole::Primary)
    {
        // After the panels have drawn: a close box acts during a panel's own
        // draw. The primary tracks every window's panels.
        PanelVisibility.Track();
        ThemePrefs.DrawWindow(EngineInstance.Console().Registry());
    }

    if (view != nullptr)
        for (const std::function<void()>& overlay : view->Overlays)
            if (overlay)
                overlay();
    for (const std::function<void()>& overlay : ShellOverlays)
        overlay();

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame.Cmd);
}

void EditorUiFeature::DrawDockHost(WorkspaceView* view)
{
    // The chassis fills the work area the chrome bars left, and the dock host
    // sits inside its ring: our own window with no background and a plain
    // DockSpace, so the chassis ground shows through the seams between nodes.
    // Every docked panel, the viewports included, paints its own opaque body.
    // Under the shell's rail, which is authored UI drawn over this window.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float top = std::min(ContentTopInset, vp->WorkSize.y);
    const ImVec2 workMin(vp->WorkPos.x, vp->WorkPos.y + top);
    const ImVec2 workMax(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    EditorChrome::DrawChassisBase(background, workMin, workMax);
    EditorChrome::DrawChassisEdges(background, workMin, workMax);
    EditorChrome::DrawChassisOrnaments(background, workMin, workMax);

    const float inset = EditorChrome::ChassisInset();
    ImGui::SetNextWindowPos(ImVec2(workMin.x + inset, workMin.y + inset));
    ImGui::SetNextWindowSize(ImVec2(std::max(0.0f, vp->WorkSize.x - inset * 2.0f),
                                    std::max(0.0f, vp->WorkSize.y - top - inset * 2.0f)));
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

    // The file has been read by the first NewFrame; a remembered choice
    // overrides the compiled default exactly once.
    if (Role == ShellRole::Primary && !VisibilityApplied)
    {
        PanelVisibility.Apply();
        VisibilityApplied = true;
    }

    // Each workspace keeps its own dockspace, so its arrangement survives the
    // tab being in the background; a hidden one is only kept alive.
    const auto dockIdOf = [](std::string_view kind) {
        const std::string name = "workspace/" + std::string(kind);
        return ImGui::GetID(name.c_str());
    };
    if (Workspaces != nullptr)
        for (const WorkspaceHost::Entry& entry : Workspaces->OpenWorkspaces())
            if (entry.Window == Presentation && entry.Kind != Workspaces->ActiveKindIn(Presentation))
                ImGui::DockSpace(dockIdOf(entry.Kind->Id), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_KeepAliveOnly);

    if (view != nullptr)
    {
        const ImGuiID dockId = dockIdOf(Workspaces->ActiveKindIn(Presentation)->Id);
        // A dockable panel with no saved placement (a renamed or newly added
        // panel against an older ini) would come up floating; the designed
        // layout is rebuilt instead. Checked once per view; DockBuilder places
        // every panel, so the check is quiet on later launches.
        if (!view->Dock.PlacementChecked)
        {
            view->Dock.LayoutDirty |= std::any_of(view->Panels.begin(), view->Panels.end(),
                                                  [](const std::unique_ptr<IEditorPanel>& panel) {
                if (panel == nullptr || panel->GetDockSlot() == DockSlot::Floating)
                    return false;
                const std::string_view name = panel->GetWindowName();
                return ImGui::FindWindowSettingsByID(ImHashStr(name.data(), name.size())) == nullptr;
            });
            view->Dock.PlacementChecked = true;
        }
        if (view->Dock.LayoutDirty || ImGui::DockBuilderGetNode(dockId) == nullptr)
        {
            view->Dock.PendingTabFocus.clear();
            BuildDefaultDockLayout(dockId, view->Panels, view->Layout, view->Dock.PendingTabFocus);
            view->Dock.LayoutDirty = false;
        }
        // The tab bars are drawn inside DockSpace, so the font pushed here is
        // the one every docked tab label takes.
        ImFont* tabFont = EditorUi::StyleFor(EditorUi::TextRole::Tab).Font;
        if (tabFont != nullptr)
            ImGui::PushFont(tabFont);
        ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        if (tabFont != nullptr)
            ImGui::PopFont();
    }
    ImGui::End();
}

void EditorUiFeature::RegisterPointerCommands(ConsoleRegistry& registry)
{
    // Both commands parse the same "<x> <y> [frame]" arguments and queue one
    // action; only the kind differs.
    const auto queue = [this](PointerAction::Kind kind, std::span<const std::string> args) {
        ConsoleResult result;
        if (args.size() < 2 || args.size() > 3)
        {
            result.Status = ConsoleStatus::InvalidArguments;
            result.Error("expected <x> <y> [frame]");
            return result;
        }
        PointerAction action;
        action.Action = kind;
        try
        {
            action.Pos = ImVec2(std::stof(args[0]), std::stof(args[1]));
            action.AtFrame = args.size() == 3 ? std::stoi(args[2]) : 0;
        }
        catch (const std::exception&)
        {
            result.Status = ConsoleStatus::InvalidArguments;
            result.Error("x, y, and frame must be numbers");
            return result;
        }
        PointerActions.push_back(action);
        result.Info(std::format("{} queued at ({}, {}) for frame {}", kind == PointerAction::Kind::Click ? "click" : "pointer",
                                action.Pos.x, action.Pos.y, action.AtFrame));
        return result;
    };

    // What the chrome's resources have actually done. The point is to be able
    // to show that switching a theme invalidated only what it should: colors
    // and metrics cost nothing, a new texture path costs one load, and neither
    // touches the font atlas.
    registry.RegisterCommand({
        .Name = "editor.ui.chromestats",
        .Owner = "editor",
        .Usage = "editor.ui.chromestats",
        .Help = "Reports shell atlas builds and theme texture loads, replacements, "
                "failures, and resident count.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string>) {
            const ThemeTextureCache::Counters counters =
                ThemeTextures.has_value() ? ThemeTextures->Stats() : ThemeTextureCache::Counters{};
            ConsoleResult result;
            result.Info(std::format("shell atlas builds: {}", AtlasBuilds));
            result.Info(std::format("theme textures: {} loaded, {} replaced, {} failed, {} resident",
                                    counters.Loads, counters.Replacements, counters.Failures, counters.Resident));
            return result;
        },
    });
    registry.RegisterCommand({
        .Name = "editor.ui.rebuild_atlas",
        .Owner = "editor",
        .Usage = "editor.ui.rebuild_atlas",
        .Help = "Rebuilds the shell font atlas (fonts, icons, mark). Diagnostic: the "
                "atlas is otherwise rebuilt only when its own inputs change.",
        .Callback = [this](ConsoleExecutionContext&, std::span<const std::string>) {
            AtlasBuilt = false;
            ConsoleResult result;
            result.Info("shell atlas will rebuild at the next frame boundary");
            return result;
        },
    });
    registry.RegisterCommand({
        .Name = "editor.ui.click",
        .Owner = "editor",
        .Usage = "editor.ui.click <x> <y> [frame]",
        .Help = "Left-click the UI at window pixel (x, y). With a frame number, "
                "waits until that UI frame so the layout has settled. For "
                "unattended verification alongside render.screenshot.",
        .Callback = [queue](ConsoleExecutionContext&, std::span<const std::string> args) {
            return queue(PointerAction::Kind::Click, args);
        },
    });
    registry.RegisterCommand({
        .Name = "editor.ui.pointer",
        .Owner = "editor",
        .Usage = "editor.ui.pointer <x> <y> [frame]",
        .Help = "Move the UI pointer to window pixel (x, y) and hold it there, "
                "so hover states can be captured. With a frame number, waits "
                "until that UI frame.",
        .Callback = [queue](ConsoleExecutionContext&, std::span<const std::string> args) {
            return queue(PointerAction::Kind::Move, args);
        },
    });
}

void EditorUiFeature::FeedPointerActions()
{
    ImGuiIO& io = ImGui::GetIO();
    const int frame = ImGui::GetFrameCount();
    for (std::size_t i = 0; i < PointerActions.size();)
    {
        PointerAction& action = PointerActions[i];
        if (frame < action.AtFrame)
        {
            ++i;
            continue;
        }
        if (action.Action == PointerAction::Kind::Move)
        {
            HeldPointer = action.Pos;
            PointerActions.erase(PointerActions.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        io.AddMousePosEvent(action.Pos.x, action.Pos.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, !action.Pressed);
        if (!action.Pressed)
        {
            action.Pressed = true;
            ++i;
            continue;
        }
        PointerActions.erase(PointerActions.begin() + static_cast<std::ptrdiff_t>(i));
    }
    // The SDL backend re-asserts the real cursor each frame while the window
    // has focus, so a held pointer is re-fed after it every frame.
    if (HeldPointer.has_value())
        io.AddMousePosEvent(HeldPointer->x, HeldPointer->y);
}

void EditorUiFeature::Teardown()
{
    if (!Valid && Context == nullptr)
        return;

    ShutdownImGui();
    Valid = false;
}

bool EditorUiFeature::ProcessSdlEvent(const SDL_Event& event)
{
    if (!Valid)
        return false;
    const ScopedImGuiContext scope(Context);

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

    const ScopedImGuiContext scope(Context);
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
    const ScopedImGuiContext scope(Context);
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
    const ScopedImGuiContext scope(Context);
    ImGuiIO& io = ImGui::GetIO();
    if (enabled)
        io.ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;
    else
        io.ConfigFlags |= ImGuiConfigFlags_NoKeyboard;
}

void EditorUiFeature::AddShellOverlay(std::function<void()> draw)
{
    if (draw)
        ShellOverlays.push_back(std::move(draw));
}

void EditorUiFeature::SetWorkspaceHost(WorkspaceHost& host, PresentationId window)
{
    Workspaces = &host;
    Presentation = window;
    if (Role == ShellRole::Detached)
        return;
    host.SetOpenedListener([this](const WorkspaceKind& kind, IWorkspace& workspace) { AdoptWorkspace(kind, workspace); });
    host.SetClosingListener([this](const WorkspaceKind&, IWorkspace& workspace) { ReleaseWorkspace(workspace); });
    for (const WorkspaceHost::Entry& entry : host.OpenWorkspaces())
        AdoptWorkspace(*entry.Kind, *entry.Instance);
}

void EditorUiFeature::AdoptWorkspace(const WorkspaceKind& kind, IWorkspace& workspace)
{
    WorkspaceView& view = workspace.View();
    // Every workspace gets a console over the one log: which one is showing is
    // a workspace's layout, like any of its panels.
    auto console = std::make_unique<EditorConsolePanel>(EngineInstance.Debug().GetLogSink(), EngineInstance.Console());
    console->SetVisible(EngineInstance.Config().Console.OpenOnStart);
    Consoles.emplace_back(&workspace, console.get());
    view.AddPanel(std::move(console));

    for (const std::unique_ptr<IEditorPanel>& panel : view.Panels)
        if (panel != nullptr)
            panel->SetWindowIdentity(kind.Id + "." + std::string(panel->GetPersistence().Id));
    PanelVisibility.Attach(kind.Id, view.Panels);
    workspace.Place(*this);
}

void EditorUiFeature::ReleaseWorkspace(IWorkspace& workspace)
{
    PanelVisibility.Detach(workspace.View().Panels);
    std::erase_if(Consoles, [&](const auto& entry) { return entry.first == &workspace; });
}

void EditorUiFeature::ClaimViewActions(WorkspaceView* view)
{
    // What the View menu used to offer, as actions the window's shell lists:
    // showing each of the active workspace's panels, and rebuilding its layout.
    // Claimed per view, so the hatch lists only what this window shows.
    if (view == ClaimedView)
        return;
    for (const std::string& id : ClaimedPanelActions)
        ViewActions.Release(id);
    ClaimedPanelActions.clear();
    ViewActions.Release("view.reset_layout");
    ClaimedView = view;
    const WorkspaceKind* kind = Workspaces != nullptr ? Workspaces->ActiveKindIn(Presentation) : nullptr;
    if (view == nullptr || kind == nullptr || Catalog == nullptr)
        return;

    ViewActions.Claim("view.reset_layout", ActionHandler{ .Invoke = [view] { view->Dock.LayoutDirty = true; } });
    for (const std::unique_ptr<IEditorPanel>& owned : view->Panels)
    {
        IEditorPanel* panel = owned.get();
        if (panel == nullptr || panel->GetDockSlot() == DockSlot::Center)
            continue;
        const std::string id = "panel." + kind->Id + "." + std::string(panel->GetPersistence().Id);
        (void)Catalog->Define({ .Id = id, .Label = std::string(panel->GetTitle()), .Category = "Panels" });
        ViewActions.Claim(id, ActionHandler{ .Invoke = [panel] { panel->ToggleVisible(); },
                                             .Checked = [panel] { return panel->IsVisible(); } });
        ClaimedPanelActions.push_back(id);
    }
}

void EditorUiFeature::ShowThemePreferences()
{
    Shell().ThemePrefs.OpenPaletteWindow();
}

void EditorUiFeature::DefineThemeActions()
{
    if (Catalog == nullptr || Role != ShellRole::Primary)
        return;
    ConsoleRegistry& console = EngineInstance.Console().Registry();
    for (const std::string& name : ThemePrefs.ThemeNames(console))
    {
        const std::string id = "theme.use." + (name.empty() ? std::string("builtin") : name);
        (void)Catalog->Define({ .Id = id, .Label = name.empty() ? std::string("Built-in") : name,
                                .Category = "Theme" });
        ViewActions.Claim(id, ActionHandler{
            .Invoke = [this, name] { ThemePrefs.Choose(EngineInstance.Console().Registry(), name); },
            .Checked = [this, name] { return ThemePrefs.ActiveTheme() == name; } });
    }
    (void)Catalog->Define({ .Id = "theme.palette", .Label = "Customize Palette...", .Category = "Theme" });
    ViewActions.Claim("theme.palette", ActionHandler{ .Invoke = [this] { ShowThemePreferences(); } });
    (void)Catalog->Define({ .Id = "view.reset_layout", .Label = "Reset Layout", .Category = "View" });
}

void EditorUiFeature::ToggleConsole()
{
    IWorkspace* active = Workspaces != nullptr ? Workspaces->ActiveIn(Presentation) : nullptr;
    for (const auto& [workspace, console] : Shell().Consoles)
        if (workspace == active)
            console->ToggleVisible();
}

WorkspaceView* EditorUiFeature::ActiveView() const
{
    IWorkspace* active = Workspaces != nullptr ? Workspaces->ActiveIn(Presentation) : nullptr;
    return active != nullptr ? &active->View() : nullptr;
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
    Context = ImGui::CreateContext(Role == ShellRole::Detached ? PrimaryShell->Context->IO.Fonts : nullptr);
    ImGuiContextReady = true;
    const ScopedImGuiContext scope(Context);

    ImGuiIO& io = ImGui::GetIO();
    // No keyboard nav: it reserves Tab/arrows/Enter for widget focus cycling, which
    // steals viewport hotkeys (the edge-cut Tab toggle most visibly). Text fields
    // still gate keys via WantTextInput, so typing capture is unaffected.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Per-application layout file: the editor family would otherwise fight
    // over one ./imgui.ini. Points at the member so it outlives the context.
    if (Role == ShellRole::Primary)
    {
        if (!IniFileName.empty())
            io.IniFilename = IniFileName.c_str();
        // Before the first NewFrame, which is when ImGui reads the file.
        PanelVisibility.Register();
    }
    else
    {
        // A detached window starts from the primary's layout, so a workspace
        // arrives arranged as it was; what changes here is not written back.
        io.IniFilename = nullptr;
        std::string layout;
        {
            const ScopedImGuiContext primary(PrimaryShell->Context);
            layout = ImGui::SaveIniSettingsToMemory();
        }
        ImGui::LoadIniSettingsFromMemory(layout.c_str(), layout.size());
    }

    // Every ImGuiTextureBinding costs one combined-image-sampler set: the
    // viewport targets and up to editor.materials.thumbnail_budget resident
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

    // Every texture set is the primary's (ImGuiTextureOwner.h), so a detached
    // window's backend allocates none and takes the smallest pool it accepts.
    if (Role == ShellRole::Primary
        && vkCreateDescriptorPool(DeviceHandle, &poolInfo, nullptr, &DescriptorPool) != VK_SUCCESS)
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
    if (Role == ShellRole::Detached)
        vulkanInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE + 1;
    vulkanInfo.MinImageCount = Frames.GetFramesInFlight();
    vulkanInfo.ImageCount = Frames.GetFramesInFlight();
    vulkanInfo.UseDynamicRendering = true;
    vulkanInfo.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    vulkanInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;

    ColorFormat = services.Swapchain->GetFormat();
    vulkanInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &ColorFormat;
    // See the same line in ImGuiDebugOverlay: the swapchain scope binds a
    // stencil attachment, so every pipeline in it declares the format.
    vulkanInfo.PipelineRenderingCreateInfo.depthAttachmentFormat = services.DepthFormat;
    vulkanInfo.PipelineRenderingCreateInfo.stencilAttachmentFormat = services.StencilFormat;

    if (!ImGui_ImplVulkan_Init(&vulkanInfo))
    {
        if (Log) Log->Error("EditorUiFeature: Vulkan backend init failed");
        ShutdownImGui();
        return false;
    }
    VulkanBackendReady = true;
    if (Role == ShellRole::Primary)
        SetImGuiTextureOwner(Context);

    return true;
}

void EditorUiFeature::ShutdownImGui()
{
    if (DeviceHandle != VK_NULL_HANDLE)
        vkDeviceWaitIdle(DeviceHandle);

    // Between the wait above and the backend teardown below: the device is
    // idle, so nothing in flight can still sample these, and the backend is
    // still alive to free their descriptor sets. Deferred retirement is no use
    // here -- there are no further frames to retire against.
    if (ThemeTextures.has_value())
    {
        ThemeTextures->Shutdown();
        ThemeTextures.reset();
        // The images went to the deletion queue, which defers even when idle,
        // and nothing will advance its ring again. Run it out here rather than
        // leaving it to the graphics services' own teardown.
        if (GraphicsServices* graphics = EngineInstance.TryGraphics())
            graphics->DeletionQueue.FlushAll();
    }

    if (Context != nullptr)
    {
        const ScopedImGuiContext scope(Context);
        if (VulkanBackendReady)
            ImGui_ImplVulkan_Shutdown();
        if (SdlBackendReady)
            ImGui_ImplSDL3_Shutdown();
    }
    if (Role == ShellRole::Primary)
        SetImGuiTextureOwner(nullptr);
    if (ImGuiContextReady && Context != nullptr)
    {
        ImGui::DestroyContext(Context);
        Context = nullptr;
        // Destroying the current context leaves none; the primary's is the
        // one everything outside a draw expects.
        if (Role == ShellRole::Detached && ImGui::GetCurrentContext() == nullptr)
            ImGui::SetCurrentContext(PrimaryShell->Context);
    }

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

void EditorUiFeature::PrepareFrameChrome()
{
    // The look is built on the first frame, not in Setup: the startup script
    // (argv +set, config cvars) runs after every feature's Setup, so this is
    // the earliest point editor.ui.scale holds its final value.
    if (!LookBuilt)
    {
        EditorUi::UiScale = ResolveUiScale(EngineInstance.Console().Registry(), Window.GetHandle(), Log);
        EditorUi::Apply(ImGui::GetStyle());
        LookBuilt = true;
    }

    // Everything below derives from theme state, so the theme is committed
    // first. A choice made from the menu is recorded rather than applied,
    // because the menu is drawn mid-frame: this is the boundary where a new
    // theme, the assets it names, and the atlas it implies all change together.
    if (!ThemeSynced)
    {
        ThemePrefs.SyncWithCVar(EngineInstance.Console().Registry());
        ThemeSynced = true;
    }
    const bool themeChanged = ThemePrefs.CommitPending();
    PublishAuthoredTheme(themeChanged);
    PrepareThemeTextures();
    BuildShellAtlasIfStale();
}

void EditorUiFeature::PublishAuthoredTheme(bool themeChanged)
{
    // The authored UI layer is the other thing that derives from theme state,
    // so it is refreshed at the same boundary as the chrome textures and the
    // shell atlas: one frame, one theme.
    //
    // Written as a stylesheet rather than published through any document's
    // model. A theme is the host's presentation policy; a presentation model is
    // what a surface presents, and a colour is not that.
    if (!themeChanged && AuthoredThemePublished)
        return;

    UiService* ui = EngineInstance.TryUi();
    if (ui == nullptr || !ui->IsReady())
        return;

    (void)ui->SetHostStyleSheet(kAuthoredThemeStyleSheetName, BuildAuthoredThemeStyleSheet());
    AuthoredThemePublished = true;
}

void EditorUiFeature::PrepareThemeTextures()
{
    GraphicsServices* graphics = EngineInstance.TryGraphics();
    if (graphics == nullptr)
        return;
    if (!ThemeTextures.has_value())
        ThemeTextures.emplace(graphics->Images, graphics->Samplers, Log);

    // Retire first, then resolve: this is the one place in the frame that is
    // allowed to touch the filesystem, allocate, or upload for chrome art.
    ThemeTextures->BeginFrame(graphics->Frames.GetRetirement());
    EditorUi::RequestedSurfaceTextures(EditorUi::Surfaces, RequestedTextures);
    ThemeTextures->Prepare(RequestedTextures);

    const EditorUi::ChromeSurfaces& surfaces = EditorUi::Surfaces;
    CaptionSurface = ResolveSurface(surfaces.Caption, surfaces.CaptionTexture, surfaces.CaptionModulate);
    ToolbarSurface = ResolveSurface(surfaces.Toolbar, surfaces.ToolbarTexture, surfaces.ToolbarModulate);
}

EditorChrome::BarSurface EditorUiFeature::ResolveSurface(EditorUi::BarFinish finish, const std::string& path,
                                                         EditorUi::SurfaceModulation modulation) const
{
    EditorChrome::BarSurface surface;
    surface.Finish = finish;
    if (finish != EditorUi::BarFinish::Texture || !ThemeTextures.has_value())
        return surface;
    const ThemeTextureCache::Texture texture = ThemeTextures->Get(path);
    surface.Texture = texture.Id;
    surface.TextureSize = texture.Size;
    // Authored colour by default. A theme that painted a neutral greyscale
    // surface asks for Metal instead, and gets the palette's metal lifted so a
    // mid-grey texel lands near MetalBase rather than being crushed to black.
    if (modulation == EditorUi::SurfaceModulation::Metal)
        surface.Tint = ImGui::GetColorU32(EditorUi::Lighten(EditorUi::MetalBase, 0.35f));
    return surface;
}

EditorChrome::BarSurface EditorUiFeature::SurfaceFor(BarRole role) const
{
    if (Role == ShellRole::Detached)
        return PrimaryShell->SurfaceFor(role);
    return role == BarRole::Caption ? CaptionSurface : ToolbarSurface;
}

void EditorUiFeature::BuildShellAtlasIfStale()
{
    const EditorChrome::ShellAtlasKey key{ .UiScale = EditorUi::UiScale, .LogoPath = {} };
    if (AtlasBuilt && key == BuiltAtlas)
        return;

    // One operation: clear, reload the fonts, bake every custom rect, and only
    // then hand the result to the backend. Anything that holds atlas-generation
    // state -- the font pointers LoadFonts owns, the raster table the bake
    // owns -- is rewritten inside it, which is the whole reason this is not
    // several independent steps. Between frames, so no draw list is live and
    // no font is pushed.
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    EditorUi::LoadFonts(io);
    const EditorChrome::ShellAtlasResult baked = EditorChrome::BakeAtlasArt(*io.Fonts, key);
    constexpr int kIconCount = static_cast<int>(IconId::Count) - 1;
    if (baked.Icons < kIconCount && Log != nullptr)
        Log->Warn("EditorUiFeature: {} of {} icons baked from " SENCHA_EDITOR_ICON_DIR "; the rest show their glyph",
                  baked.Icons, kIconCount);
    if (!baked.Logo && !key.LogoPath.empty() && Log != nullptr)
        Log->Warn("EditorUiFeature: no shell mark baked from '{}'; the nameplate keeps its plain cap", key.LogoPath);

    // The first build leaves the upload to ImGui_ImplVulkan_NewFrame, which
    // creates the font texture when there is none. A rebuild has to ask, since
    // from the backend's side nothing has changed.
    if (AtlasBuilt && VulkanBackendReady)
        ImGui_ImplVulkan_CreateFontsTexture();
    BuiltAtlas = key;
    AtlasBuilt = true;
    ++AtlasBuilds;
    if (Log != nullptr)
        Log->Info("EditorUiFeature: shell atlas built ({} icons, mark {}) -- build #{}", baked.Icons,
                  baked.Logo ? "yes" : "no", AtlasBuilds);
}
