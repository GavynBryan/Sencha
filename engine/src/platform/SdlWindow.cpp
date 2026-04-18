#include <platform/SdlWindow.h>
#include <platform/SdlVideoService.h>

#include <SDL3/SDL.h>

namespace
{
    SDL_WindowFlags TranslateFlags(const WindowCreateInfo& info)
    {
        SDL_WindowFlags flags = 0;

        switch (info.GraphicsApi)
        {
        case WindowGraphicsApi::Vulkan:
            flags |= SDL_WINDOW_VULKAN;
            break;
        case WindowGraphicsApi::None:
            break;
        }

        if (info.Resizable)
            flags |= SDL_WINDOW_RESIZABLE;

        if (!info.Visible)
            flags |= SDL_WINDOW_HIDDEN;

        switch (info.Mode)
        {
        case WindowMode::Fullscreen:
            flags |= SDL_WINDOW_FULLSCREEN;
            break;
        case WindowMode::BorderlessFullscreen:
            flags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_FULLSCREEN;
            break;
        case WindowMode::Windowed:
            break;
        }

        return flags;
    }
}

SdlWindow::SdlWindow(LoggingProvider& logging, SdlVideoService& video, const WindowCreateInfo& createInfo)
    : Log(logging.GetLogger<SdlWindow>())
{
    if (!video.IsValid())
    {
        Log.Error("SDL window creation failed: video subsystem is not initialized");
        return;
    }

    Window = SDL_CreateWindow(
        createInfo.Title.c_str(),
        static_cast<int>(createInfo.Width),
        static_cast<int>(createInfo.Height),
        TranslateFlags(createInfo));

    if (!Window)
    {
        Log.Error("SDL window creation failed: {}", SDL_GetError());
        return;
    }

    Log.Info("Window created: {}x{} \"{}\"", createInfo.Width, createInfo.Height, createInfo.Title);
}

SdlWindow::~SdlWindow()
{
    Close();
}

bool SdlWindow::IsValid() const
{
    return Window != nullptr;
}

void SdlWindow::Close()
{
    if (Window)
    {
        SDL_DestroyWindow(Window);
        Window = nullptr;
        Log.Info("Window closed");
    }
}

std::string SdlWindow::GetTitle() const
{
    if (!Window) return {};
    return SDL_GetWindowTitle(Window);
}

void SdlWindow::SetTitle(std::string_view title)
{
    if (Window)
        SDL_SetWindowTitle(Window, std::string(title).c_str());
}

WindowExtent SdlWindow::GetExtent() const
{
    if (!Window) return {};
    int w = 0, h = 0;
    SDL_GetWindowSize(Window, &w, &h);
    return { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
}

void SdlWindow::SetSize(uint32_t width, uint32_t height)
{
    if (Window)
        SDL_SetWindowSize(Window, static_cast<int>(width), static_cast<int>(height));
}

bool SdlWindow::IsResizable() const
{
    if (!Window) return false;
    return (SDL_GetWindowFlags(Window) & SDL_WINDOW_RESIZABLE) != 0;
}

void SdlWindow::SetResizable(bool resizable)
{
    if (Window)
        SDL_SetWindowResizable(Window, resizable);
}

WindowMode SdlWindow::GetMode() const
{
    if (!Window) return WindowMode::Windowed;
    auto flags = SDL_GetWindowFlags(Window);
    if (flags & SDL_WINDOW_FULLSCREEN)
    {
        if (flags & SDL_WINDOW_BORDERLESS)
            return WindowMode::BorderlessFullscreen;
        return WindowMode::Fullscreen;
    }
    return WindowMode::Windowed;
}

void SdlWindow::SetMode(WindowMode mode)
{
    if (!Window) return;

    switch (mode)
    {
    case WindowMode::Windowed:
        SDL_SetWindowFullscreen(Window, false);
        SDL_SetWindowBordered(Window, true);
        break;
    case WindowMode::Fullscreen:
        SDL_SetWindowBordered(Window, true);
        SDL_SetWindowFullscreen(Window, true);
        break;
    case WindowMode::BorderlessFullscreen:
        SDL_SetWindowBordered(Window, false);
        SDL_SetWindowFullscreen(Window, true);
        break;
    }
}

void SdlWindow::Show()
{
    if (Window)
        SDL_ShowWindow(Window);
}

void SdlWindow::Hide()
{
    if (Window)
        SDL_HideWindow(Window);
}

uint32_t SdlWindow::GetId() const
{
    if (!Window) return 0;
    return SDL_GetWindowID(Window);
}
