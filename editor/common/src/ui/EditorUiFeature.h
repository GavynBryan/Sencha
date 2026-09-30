#pragma once

#include <input/UiInputCapture.h>
#include "actions/ActionContext.h"
#include "PanelVisibilitySettings.h"
#include "ThemePreferences.h"
#include "ThemeTextureCache.h"
#include "WorkspaceView.h"
#include "chrome/ChromeBars.h"
#include "chrome/IconDraw.h"

#include <graphics/PresentationId.h>
#include <graphics/vulkan/Renderer.h>
#include <platform/WindowFrameHit.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

union SDL_Event;
struct ImGuiContext;

class ConsoleRegistry;
class EditorConsolePanel;
class Engine;
class IWorkspace;
class ActionCatalog;
class WorkspaceHost;
struct WorkspaceKind;
class SdlWindow;
class VulkanFrameService;
class VulkanInstanceService;
struct IEditorPanel;

// Which themed bar a surface belongs to.
enum class BarRole
{
    Caption,
    Toolbar,
};

// The ImGui content of one window while workspaces migrate to authored UI: the
// panels of the workspace its WorkspaceHost shows there, docked under the
// window's shell rail. The primary owns what every window shares -- the font
// atlas, the theme, the layout file, the prompts; a detached one draws with
// them in a window of its own, over an ImGui context of its own. It goes when
// the last workspace presents through the shell (docs/editor/interaction-grammar.md).
class EditorUiFeature : public IRenderFeature
{
public:
    // iniFileName is the ImGui layout file (e.g. "kyusu.imgui.ini").
    EditorUiFeature(Engine& engine,
                    SdlWindow& window,
                    VulkanInstanceService& instance,
                    VulkanFrameService& frames,
                    std::string iniFileName);
    // A detached window's shell. `primary` outlives it.
    EditorUiFeature(EditorUiFeature& primary, SdlWindow& window);
    ~EditorUiFeature() override;

    EditorUiFeature(const EditorUiFeature&) = delete;
    EditorUiFeature& operator=(const EditorUiFeature&) = delete;
    EditorUiFeature(EditorUiFeature&&) = delete;
    EditorUiFeature& operator=(EditorUiFeature&&) = delete;

    // ApplicationUi, not DevelopmentOverlay: this is the editor's own chrome --
    // its application UI, which happens to be implemented in ImGui -- and not a
    // diagnostic. The distinction is load-bearing during the migration to
    // authored UI: retained surfaces record later in this same phase, so an
    // authored panel draws over the ImGui panel it is replacing rather than
    // under it.
    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::ApplicationUi; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

    bool ProcessSdlEvent(const SDL_Event& event);

    // Which input devices the UI currently owns (mouse/keyboard hovered or
    // focused by an ImGui widget). The input router consults this to keep events
    // over the UI from reaching the viewport. Authoritative because this feature
    // owns the ImGui context.
    [[nodiscard]] UiInputCapture GetInputCapture() const;

    // Enables or disables ImGui mouse input wholesale (ImGuiConfigFlags_NoMouse).
    // While a viewport owns the pointer for navigation (fly look / ortho pan) the
    // cursor is hidden and belongs to the camera, so the UI must stop hovering,
    // highlighting, and clicking. Driven by the same seam that toggles SDL relative
    // mouse mode, keeping a single authority for who owns the pointer.
    void SetMouseInputEnabled(bool enabled);

    // Enables or disables ImGui keyboard input wholesale (ImGuiConfigFlags_NoKeyboard).
    // Driven by the same capture seam as the mouse: while a viewport gesture owns
    // input (fly camera) the keys belong to the camera, so ImGui must not route
    // them to a focused widget (otherwise WASD fills the console input box).
    void SetKeyboardInputEnabled(bool enabled);

    // Draws the host's workspaces placed in `window` from now on. The primary
    // shell also adopts their panels as they open (named, given a console,
    // their remembered visibility restored) and lets go before they close.
    // The host outlives this feature's drawing.
    void SetWorkspaceHost(WorkspaceHost& host, PresentationId window);
    [[nodiscard]] PresentationId ShownWindow() const { return Presentation; }
    [[nodiscard]] SdlWindow& GetWindow() const { return Window; }
    // Where this window's view actions are defined: showing each panel,
    // rebuilding the layout.
    void SetActionCatalog(ActionCatalog* catalog) { Catalog = catalog; }
    // The actions this window's docked content performs; the host puts them in
    // play in the window's stack.
    [[nodiscard]] ActionContext& ViewActionContext() { return ViewActions; }
    // Where the docked content starts, below the rail, in window points.
    void SetContentTopInset(float points) { ContentTopInset = points; }
    // Theme choice, as actions: one per theme, and the palette window.
    void DefineThemeActions();
    // Shows or hides the active workspace's console.
    void ToggleConsole();

    // Drawn over whichever view is showing, or none: the window's own prompts.
    void AddShellOverlay(std::function<void()> draw);

    // The resolved surface for a bar, as prepared at this frame's boundary.
    // A pure lookup: a bar painting itself never reaches a loader.
    [[nodiscard]] EditorChrome::BarSurface SurfaceFor(BarRole role) const;

private:
    enum class ShellRole : std::uint8_t
    {
        Primary,
        Detached,
    };

    // The primary shell, which holds what every window shares.
    [[nodiscard]] EditorUiFeature& Shell() { return Role == ShellRole::Primary ? *this : *PrimaryShell; }
    bool InitImGui(const RendererServices& services);
    void ShutdownImGui();
    // The frame boundary: commit a pending theme, then resolve everything
    // derived from theme state, before any of it is drawn.
    void PrepareFrameChrome();
    // Hands the active theme to the authored UI layer as a stylesheet. Called
    // at the theme boundary, and once at startup because there is nothing to
    // have changed yet.
    void PublishAuthoredTheme(bool themeChanged);
    bool AuthoredThemePublished = false;
    void PrepareThemeTextures();
    void BuildShellAtlasIfStale();
    [[nodiscard]] EditorChrome::BarSurface ResolveSurface(EditorUi::BarFinish finish, const std::string& path,
                                                          EditorUi::SurfaceModulation modulation) const;
    // The host's active workspace's view; null when none is open.
    [[nodiscard]] WorkspaceView* ActiveView() const;
    void AdoptWorkspace(const WorkspaceKind& kind, IWorkspace& workspace);
    void ReleaseWorkspace(IWorkspace& workspace);
    void DrawDockHost(WorkspaceView* view);
    void ClaimViewActions(WorkspaceView* view);
    void ShowThemePreferences();
    void RegisterPointerCommands(ConsoleRegistry& registry);
    void FeedPointerActions();

    ShellRole Role = ShellRole::Primary;
    EditorUiFeature* PrimaryShell = nullptr;
    ImGuiContext* Context = nullptr;
    PresentationId Presentation;

    Engine& EngineInstance;
    SdlWindow& Window;
    VulkanInstanceService& Instance;
    VulkanFrameService& Frames;
    std::string IniFileName;

    Logger* Log = nullptr;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDevice DeviceHandle = VK_NULL_HANDLE;
    VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
    bool ImGuiContextReady = false;
    bool SdlBackendReady = false;
    bool VulkanBackendReady = false;
    bool Valid = false;
    bool LoggedFirstDraw = false;
    // Style, scale, and fonts are built on the first OnDraw (see there).
    bool LookBuilt = false;
    bool ThemeSynced = false;

    // Theme artwork, with a lifetime of its own: a theme switch replaces these
    // and leaves the font atlas alone.
    std::optional<ThemeTextureCache> ThemeTextures;
    std::vector<std::string> RequestedTextures;
    EditorChrome::BarSurface CaptionSurface;
    EditorChrome::BarSurface ToolbarSurface;

    // The inputs the shell atlas was last built from. Compared, not signalled:
    // nothing tells this feature "the theme changed", it decides for itself
    // whether its own inputs moved.
    EditorChrome::ShellAtlasKey BuiltAtlas;
    bool AtlasBuilt = false;
    std::uint32_t AtlasBuilds = 0;

    WorkspaceHost* Workspaces = nullptr;
    float ContentTopInset = 0.0f;
    ActionContext ViewActions;
    WorkspaceView* ClaimedView = nullptr;
    std::vector<std::string> ClaimedPanelActions;
    std::vector<std::function<void()>> ShellOverlays;
    // Each open workspace's console, which this shell adds to its view.
    std::vector<std::pair<IWorkspace*, EditorConsolePanel*>> Consoles;
    // Remembers which panels are shown; declared after the views it reads.
    PanelVisibilitySettings PanelVisibility;
    bool VisibilityApplied = false;
    // View > Preferences > Theme: theme selection plus the palette override window.
    ThemePreferences ThemePrefs;
    // Pointer actions queued by the editor.ui.click and editor.ui.pointer
    // commands, so an unattended run can drive or hover a widget before a
    // screenshot. A click is pressed on the named frame and released on the
    // next; a move puts the pointer at the position on the named frame and
    // holds it there. Fed to ImGui after the SDL backend's own mouse update so
    // the injected position wins for that frame.
    struct PointerAction
    {
        enum class Kind : std::uint8_t { Click, Move };
        Kind Action = Kind::Click;
        ImVec2 Pos{};
        int AtFrame = 0;
        bool Pressed = false;
    };
    std::vector<PointerAction> PointerActions;
    // The held pointer position from the latest Move, re-fed every frame.
    std::optional<ImVec2> HeldPointer;
    ActionCatalog* Catalog = nullptr;
};
