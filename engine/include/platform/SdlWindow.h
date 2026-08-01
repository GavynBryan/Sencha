#pragma once

#include <core/logging/LoggingProvider.h>
#include <platform/WindowCreateInfo.h>
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

    [[nodiscard]] SDL_Window* GetHandle() const { return Window; }
    [[nodiscard]] uint32_t GetId() const;

private:
    Logger& Log;
    SDL_Window* Window = nullptr;
};
