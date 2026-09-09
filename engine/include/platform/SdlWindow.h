#pragma once

#include <core/logging/LoggingProvider.h>
#include <platform/WindowCreateInfo.h>
#include <platform/WindowFrameHit.h>
#include <platform/WindowTypes.h>

#include <cstdint>
#include <string>
#include <string_view>

struct SDL_Window;
class SdlVideoService;

// The platform window. SDL types stay in the .cpp -- the handle is opaque
// here -- so consumers of this header do not pull in SDL.
class SdlWindow
{
public:
    SdlWindow(LoggingProvider& logging, SdlVideoService& video, const WindowCreateInfo& createInfo);
    ~SdlWindow();

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;
    SdlWindow(SdlWindow&&) = delete;
    SdlWindow& operator=(SdlWindow&&) = delete;

    [[nodiscard]] bool IsValid() const;
    void Close();

    [[nodiscard]] std::string GetTitle() const;
    void SetTitle(std::string_view title);

    [[nodiscard]] WindowExtent GetExtent() const;
    void SetSize(uint32_t width, uint32_t height);

    [[nodiscard]] bool IsResizable() const;
    void SetResizable(bool resizable);

    [[nodiscard]] WindowMode GetMode() const;
    void SetMode(WindowMode mode);

    void Show();
    void Hide();

    // Client decorations: true when the window draws its own frame, which
    // it does only when it was asked to and the platform accepted the hit
    // test. A request the platform cannot honor leaves the platform frame on.
    [[nodiscard]] bool HasClientDecorations() const { return ClientDecorationsActive; }

    // The application's frame snapshot: where its caption is and whether
    // dragging from it is permitted right now. Copied, read from the platform's
    // hit test on this thread.
    void SetFrameRegions(const WindowFrameRegions& regions);

    // What a press at window point (x, y) means, from the snapshot and the
    // window's own state. Client while the pointer belongs to the scene
    // (relative mouse mode).
    [[nodiscard]] WindowFrameHit ClassifyFrameHit(int32_t x, int32_t y) const;

    // Window-state verbs; false (and a logged platform error) on failure.
    bool Minimize();
    bool Maximize();
    bool Restore();
    [[nodiscard]] bool IsMaximized() const;

    [[nodiscard]] SDL_Window* GetHandle() const { return Window; }
    [[nodiscard]] uint32_t GetId() const;

private:
    Logger& Log;
    SDL_Window* Window = nullptr;
    bool ClientDecorationsActive = false;
    WindowFrameRegions FrameRegions;
};
