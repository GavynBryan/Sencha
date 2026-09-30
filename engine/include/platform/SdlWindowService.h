#pragma once

#include <core/logging/LoggingProvider.h>
#include <platform/WindowTypes.h>

#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <vector>

class SdlVideoService;
class SdlWindow;
struct WindowCreateInfo;

// The window an SDL event belongs to, or 0 for one that names none (a
// device connection, a quit request).
[[nodiscard]] std::uint32_t SdlEventWindowId(const SDL_Event& event);

class SdlWindowService
{
public:
    using WindowId = uint32_t;

    struct WindowState
    {
        bool CloseRequested = false;
        bool Minimized = false;
        bool Focused = false;
        bool ResizePending = false;
        WindowExtent Extent;
    };

    SdlWindowService(LoggingProvider& logging, SdlVideoService& video);
    ~SdlWindowService();

    SdlWindowService(const SdlWindowService&) = delete;
    SdlWindowService& operator=(const SdlWindowService&) = delete;
    SdlWindowService(SdlWindowService&&) = delete;
    SdlWindowService& operator=(SdlWindowService&&) = delete;

    SdlWindow* CreateWindow(const WindowCreateInfo& createInfo);
    [[nodiscard]] SdlWindow* GetWindow(WindowId id) const;
    [[nodiscard]] SdlWindow* GetPrimaryWindow() const;
    [[nodiscard]] WindowId GetPrimaryWindowId() const { return PrimaryWindowId; }
    [[nodiscard]] SDL_Window* GetNativeHandle(WindowId id) const;
    [[nodiscard]] std::vector<const char*> GetRequiredVulkanInstanceExtensions() const;

    [[nodiscard]] std::vector<WindowId> GetWindowIds() const;
    [[nodiscard]] size_t GetWindowCount() const { return Windows.size(); }
    [[nodiscard]] bool HasLiveWindows() const;

    [[nodiscard]] const WindowState* GetState(WindowId id) const;
    [[nodiscard]] WindowExtent GetExtent(WindowId id) const;
    [[nodiscard]] bool IsCloseRequested(WindowId id) const;
    [[nodiscard]] bool IsAlive(WindowId id) const;

    void RequestClose(WindowId id);
    // Destroys a window other than the primary. The caller has already let go
    // of anything presenting to it; false for the primary or an unknown id.
    bool DestroyWindow(WindowId id);
    void HandleEvent(const SDL_Event& event);
    bool ConsumeResize(WindowId id, WindowExtent* extent = nullptr);

private:
    struct WindowRecord
    {
        WindowId Id = 0;
        std::unique_ptr<SdlWindow> Window;
        WindowState State;
    };

    WindowRecord* FindRecord(WindowId id);
    const WindowRecord* FindRecord(WindowId id) const;
    void RefreshState(WindowRecord& record);

    Logger& Log;
    LoggingProvider& Logging;
    SdlVideoService& Video;
    std::vector<WindowRecord> Windows;
    WindowId PrimaryWindowId = 0;
};
