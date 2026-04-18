#pragma once

#include <debug/DebugService.h>
#include <debug/IDebugPanel.h>
#include <graphics/Renderer.h>
#include <memory>
#include <utility>
#include <vector>

union SDL_Event;

class SdlWindow;
class VulkanFrameService;
class VulkanInstanceService;

//=============================================================================
// ImGuiDebugOverlay
//
// Optional debug frontend. Owns Dear ImGui and plugs into the renderer as a
// normal render feature. The backend DebugService remains renderer-agnostic.
//=============================================================================
class ImGuiDebugOverlay : public IRenderFeature
{
public:
	ImGuiDebugOverlay(DebugService& debug,
	                  SdlWindow& window,
	                  VulkanInstanceService& instance,
	                  VulkanFrameService& frames);
	~ImGuiDebugOverlay() override;

	ImGuiDebugOverlay(const ImGuiDebugOverlay&) = delete;
	ImGuiDebugOverlay& operator=(const ImGuiDebugOverlay&) = delete;
	ImGuiDebugOverlay(ImGuiDebugOverlay&&) = delete;
	ImGuiDebugOverlay& operator=(ImGuiDebugOverlay&&) = delete;

	[[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
	void Setup(const RendererServices& services) override;
	void OnDraw(const FrameContext& frame) override;
	void Teardown() override;

	bool ProcessSdlEvent(const SDL_Event& event);

	template <typename T, typename... Args>
	T& AddPanel(Args&&... args)
	{
		auto panel = std::make_unique<T>(std::forward<Args>(args)...);
		auto* raw = panel.get();
		Panels.push_back(std::move(panel));
		return *raw;
	}

	[[nodiscard]] bool IsValid() const { return Valid; }

private:
	bool InitImGui(const RendererServices& services);
	void ShutdownImGui();

	DebugService& Debug;
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

	std::vector<std::unique_ptr<IDebugPanel>> Panels;
};
