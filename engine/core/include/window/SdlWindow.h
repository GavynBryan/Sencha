#pragma once

#include <core/service/IService.h>
#include <core/logging/LoggingProvider.h>
#include <window/IWindow.h>
#include <window/WindowCreateInfo.h>

struct SDL_Window;
class SdlVideoService;

class SdlWindow : public IWindow, public IService
{
public:
    SdlWindow(LoggingProvider& logging, SdlVideoService& video, const WindowCreateInfo& createInfo);
    ~SdlWindow() override;

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;
    SdlWindow(SdlWindow&&) = delete;
    SdlWindow& operator=(SdlWindow&&) = delete;

    [[nodiscard]] bool IsValid() const override;
    void Close() override;

    [[nodiscard]] std::string GetTitle() const override;
    void SetTitle(std::string_view title) override;

    [[nodiscard]] WindowExtent GetExtent() const override;
    void SetSize(uint32_t width, uint32_t height) override;

    [[nodiscard]] bool IsResizable() const override;
    void SetResizable(bool resizable) override;

    [[nodiscard]] WindowMode GetMode() const override;
    void SetMode(WindowMode mode) override;

    void Show() override;
    void Hide() override;

    [[nodiscard]] SDL_Window* GetHandle() const { return Window; }
    [[nodiscard]] uint32_t GetId() const;

private:
    Logger& Log;
    SDL_Window* Window = nullptr;
};
