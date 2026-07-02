#pragma once

#include "input/UiInputCapture.h"
#include "ThemePreferences.h"

#include <graphics/vulkan/Renderer.h>

#include <functional>
#include <memory>
#include <vector>

union SDL_Event;

class Engine;
class SdlWindow;
class VulkanFrameService;
class VulkanInstanceService;
class EditorSkin;
struct IEditorPanel;

class EditorUiFeature : public IRenderFeature
{
public:
    EditorUiFeature(Engine& engine,
                    SdlWindow& window,
                    VulkanInstanceService& instance,
                    VulkanFrameService& frames);
    ~EditorUiFeature() override;

    EditorUiFeature(const EditorUiFeature&) = delete;
    EditorUiFeature& operator=(const EditorUiFeature&) = delete;
    EditorUiFeature(EditorUiFeature&&) = delete;
    EditorUiFeature& operator=(EditorUiFeature&&) = delete;

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
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

private:
    bool InitImGui(const RendererServices& services);
    void ShutdownImGui();
    void DrawMainMenuBar();

    Engine& EngineInstance;
    SdlWindow& Window;
    VulkanInstanceService& Instance;
    VulkanFrameService& Frames;

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

    std::vector<std::unique_ptr<IEditorPanel>> Panels;
    std::vector<std::function<void()>> ChromeBars;
    // View > Preferences > Theme: theme selection plus the palette override window.
    ThemePreferences ThemePrefs;
    // Forces a default-layout rebuild on the next frame (first run / View>Reset).
    bool LayoutDirty = false;
    // 9-slice texture skin (owned here; released before the ImGui backend shuts
    // down since it holds ImGui descriptor sets). Null if textures didn't load.
    std::unique_ptr<EditorSkin> Skin;
};
