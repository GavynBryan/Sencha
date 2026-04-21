#pragma once

#include <graphics/vulkan/Renderer.h>

#include <functional>
#include <memory>
#include <vector>

union SDL_Event;

class Engine;
class SdlWindow;
class VulkanFrameService;
class VulkanInstanceService;
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
    void AddPanel(std::unique_ptr<IEditorPanel> panel);
    void SetUndoActions(std::function<void()> undoAction,
                        std::function<void()> redoAction,
                        std::function<bool()> canUndoAction,
                        std::function<bool()> canRedoAction);

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

    std::vector<std::unique_ptr<IEditorPanel>> Panels;
};
