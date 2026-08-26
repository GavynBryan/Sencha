#pragma once

#include "input/UiInputCapture.h"
#include "ThemePreferences.h"

#include <graphics/vulkan/Renderer.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

union SDL_Event;

class Engine;
class SdlWindow;
class VulkanFrameService;
class VulkanInstanceService;
class EditorSkin;
struct IEditorPanel;

// Fraction of its parent split each DockSlot region takes when the default
// layout is built. Regions without panels are never split, so the fields for
// slots an application leaves empty are inert.
struct DockLayoutRatios
{
    float Bottom = 0.19f;       // full-width strip, of the whole dockspace height
    float Left = 0.18f;         // left column, of the main row width
    float Right = 0.24f;        // right column, of the width left after the left column
    float CenterBottom = 0.26f; // strip under the central node, of the center column height
    float RightBottom = 0.285f; // lower right, of the right column height
};

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

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
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

    void AddPanel(std::unique_ptr<IEditorPanel> panel);

    // Fixed app chrome (toolbar, status bar) drawn after the main menu bar and
    // before the panels, so any viewport-side-bar space they reserve is subtracted
    // from the work area the full-bleed viewport panel reads. Insertion order =
    // draw order. Kept as opaque draw callbacks so this feature stays decoupled
    // from the editor's domain types.
    void AddChrome(std::function<void()> draw);
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

private:
    bool InitImGui(const RendererServices& services);
    void ShutdownImGui();
    void DrawMainMenuBar();

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

    std::function<void()> UndoAction;
    std::function<void()> RedoAction;
    std::function<bool()> CanUndoAction;
    std::function<bool()> CanRedoAction;

    std::function<void()> NewAction;
    std::function<void()> OpenAction;
    std::function<void()> SaveAction;
    std::function<void()> SaveAsAction;
    std::function<void()> SaveAllAction;
    std::function<void()> NewWorldAction;

    std::vector<std::unique_ptr<IEditorPanel>> Panels;
    std::vector<std::function<void()>> ChromeBars;
    DockLayoutRatios LayoutRatios;
    // View > Preferences > Theme: theme selection plus the palette override window.
    ThemePreferences ThemePrefs;
    // Forces a default-layout rebuild on the next frame (first run / View>Reset).
    bool LayoutDirty = false;
    // Front tabs to raise on the frame after a layout rebuild (window titles of
    // tab-group nodes; SetWindowFocus needs the windows to exist first).
    std::vector<std::string> PendingTabFocus;
    // 9-slice texture skin (owned here; released before the ImGui backend shuts
    // down since it holds ImGui descriptor sets). Null if textures didn't load.
    std::unique_ptr<EditorSkin> Skin;
};
