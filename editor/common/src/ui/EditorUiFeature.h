#pragma once

#include <input/UiInputCapture.h>
#include "PanelVisibilitySettings.h"
#include "ThemePreferences.h"
#include "ThemeTextureCache.h"
#include "WorkspaceView.h"
#include "chrome/ChromeBars.h"
#include "chrome/IconDraw.h"

#include <graphics/vulkan/Renderer.h>
#include <platform/WindowFrameHit.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

union SDL_Event;

class ConsoleRegistry;
class EditorConsolePanel;
class Engine;
class IWorkspace;
class WorkspaceBar;
class WorkspaceHost;
struct WorkspaceKind;
class SdlWindow;
class VulkanFrameService;
class VulkanInstanceService;
struct IEditorPanel;

// What the shell says it is, drawn at the head of the menu bar: the product
// name and what kind of editor this is. Data the application supplies;
// the shell never names a product itself.
struct ShellIdentity
{
    std::string Product;
    // The product's mark, a path to a PNG. Shell branding, not theme art: it
    // is fixed by the application and rides the font atlas with the icons.
    std::string LogoPath;
    // The window's title while the shell hosts workspaces; the active one's
    // name and its status follow it.
    std::string WindowTitle;
};

// Which themed bar a surface belongs to.
enum class BarRole
{
    Caption,
    Toolbar,
};

// The ImGui shell of one window. Given a WorkspaceHost it draws the active
// workspace under a tab strip; without one, the single view an application
// fills through AddPanel and its neighbours.
class EditorUiFeature : public IRenderFeature
{
public:
    // iniFileName is the application's ImGui layout file (e.g. "kyusu.imgui.ini");
    // each editor application names its own so their layouts never collide.
    EditorUiFeature(Engine& engine,
                    SdlWindow& window,
                    VulkanInstanceService& instance,
                    VulkanFrameService& frames,
                    std::string iniFileName,
                    DockLayoutRatios layoutRatios = {});
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

    // Draws the host's workspaces from now on: their panels are adopted as they
    // open (named, given a console, their remembered visibility restored) and
    // let go before they close. The host outlives this feature's drawing.
    void SetWorkspaceHost(WorkspaceHost& host);
    // Shows or hides the active workspace's console.
    void ToggleConsole();

    void AddPanel(std::unique_ptr<IEditorPanel> panel);

    // Fixed app chrome (toolbar, status bar) drawn after the main menu bar and
    // before the panels, so any viewport-side-bar space they reserve is subtracted
    // from the work area the full-bleed viewport panel reads. Insertion order =
    // draw order. Kept as opaque draw callbacks so this feature stays decoupled
    // from the editor's domain types.
    void AddChrome(std::function<void()> draw);
    // Transient surfaces drawn after every panel: something that floats over
    // the whole window for a moment (a held-key menu) and reserves no layout
    // space, as opposed to a chrome bar, which does. Insertion order = draw
    // order.
    void AddOverlay(std::function<void()> draw);
    // Drawn over whichever view is showing, or none: the window's own prompts.
    void AddShellOverlay(std::function<void()> draw);
    void SetUndoActions(std::function<void()> undoAction,
                        std::function<void()> redoAction,
                        std::function<bool()> canUndoAction,
                        std::function<bool()> canRedoAction);
    void SetFileActions(std::function<void()> newAction,
                        std::function<void()> openAction,
                        std::function<void()> saveAction,
                        std::function<void()> saveAsAction);
    void SetSaveAllAction(std::function<void()> saveAllAction);
    // Shown only when set (applications without world documents never see it).
    void SetNewWorldAction(std::function<void()> newWorldAction);

    void SetIdentity(ShellIdentity identity);

    // Whether the shell writes the editor theme into the authored UI layer as
    // `theme.rcss`. On for an editor whose own documents are themed with its
    // chrome; off for a host that shows documents the way a game would, and
    // decides for itself when a theme belongs on one.
    void SetAuthoredThemePublishing(bool enabled);

    // The resolved surface for a bar, as prepared at this frame's boundary.
    // A pure lookup: a bar painting itself never reaches a loader.
    [[nodiscard]] EditorChrome::BarSurface SurfaceFor(BarRole role) const;
    // What the shell is working on, read each frame and shown at the tail of
    // the menu bar (the open document and whether it has unsaved edits).
    void SetStatusProvider(std::function<std::string()> statusProvider);

private:
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
    bool AuthoredThemeEnabled = true;
    void PrepareThemeTextures();
    void BuildShellAtlasIfStale();
    [[nodiscard]] EditorChrome::BarSurface ResolveSurface(EditorUi::BarFinish finish, const std::string& path,
                                                          EditorUi::SurfaceModulation modulation) const;
    // The view drawn this frame: the host's active workspace, or the single
    // view without a host. Null when the host has none open.
    [[nodiscard]] WorkspaceView* ActiveView() const;
    void AdoptWorkspace(const WorkspaceKind& kind, IWorkspace& workspace);
    void ReleaseWorkspace(IWorkspace& workspace);
    void DrawDockHost(WorkspaceView* view);
    void UpdateWindowTitle(const WorkspaceView* view);
    void DrawMainMenuBar(WorkspaceView* view);
    void RegisterPointerCommands(ConsoleRegistry& registry);
    void FeedPointerActions();

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

    std::function<void()> UndoAction;
    std::function<void()> RedoAction;
    std::function<bool()> CanUndoAction;
    std::function<bool()> CanRedoAction;
    std::function<void()> SaveAllAction;
    ShellIdentity Identity;
    // The caption's frame snapshot for the window, rewritten every frame the
    // window draws its own frame.
    WindowFrameRegions FrameRegions;

    // The one view an application without a WorkspaceHost fills.
    WorkspaceView OwnView;
    WorkspaceHost* Workspaces = nullptr;
    std::unique_ptr<WorkspaceBar> TabStrip;
    std::vector<std::function<void()>> ShellOverlays;
    // Each open workspace's console, which this shell adds to its view.
    std::vector<std::pair<IWorkspace*, EditorConsolePanel*>> Consoles;
    std::string LastWindowTitle;
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
};
