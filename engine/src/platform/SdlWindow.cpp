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

        // The application's own frame; the fullscreen modes set their own
        // border state below.
        if (info.ClientDecorations && info.Mode == WindowMode::Windowed)
            flags |= SDL_WINDOW_BORDERLESS;

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

    // SDL asks what a point means on every press and, for the cursor shape,
    // on motion, inside its event dispatch on the main thread. The answer
    // comes from the window's snapshot and state, nothing else.
    SDL_HitTestResult FrameHitTest(SDL_Window*, const SDL_Point* point, void* data)
    {
        const auto* window = static_cast<const SdlWindow*>(data);
        if (window == nullptr || point == nullptr)
            return SDL_HITTEST_NORMAL;
        switch (window->ClassifyFrameHit(point->x, point->y))
        {
        case WindowFrameHit::Client:            return SDL_HITTEST_NORMAL;
        case WindowFrameHit::Caption:           return SDL_HITTEST_DRAGGABLE;
        case WindowFrameHit::ResizeLeft:        return SDL_HITTEST_RESIZE_LEFT;
        case WindowFrameHit::ResizeRight:       return SDL_HITTEST_RESIZE_RIGHT;
        case WindowFrameHit::ResizeTop:         return SDL_HITTEST_RESIZE_TOP;
        case WindowFrameHit::ResizeBottom:      return SDL_HITTEST_RESIZE_BOTTOM;
        case WindowFrameHit::ResizeTopLeft:     return SDL_HITTEST_RESIZE_TOPLEFT;
        case WindowFrameHit::ResizeTopRight:    return SDL_HITTEST_RESIZE_TOPRIGHT;
        case WindowFrameHit::ResizeBottomLeft:  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        case WindowFrameHit::ResizeBottomRight: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        }
        return SDL_HITTEST_NORMAL;
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

    // Client decorations are honored only once the platform accepts the hit
    // test; without it a borderless window could be neither moved nor
    // resized, so the platform frame comes back instead.
    if (createInfo.ClientDecorations && createInfo.Mode == WindowMode::Windowed)
    {
        if (SDL_SetWindowHitTest(Window, &FrameHitTest, this))
        {
            ClientDecorationsActive = true;
            Log.Info("Window frame: client decorations");
        }
        else
        {
            Log.Warn("Window frame: client decorations unavailable ({}), keeping the platform frame", SDL_GetError());
            SDL_SetWindowBordered(Window, true);
        }
    }
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
        if (ClientDecorationsActive)
        {
            SDL_SetWindowHitTest(Window, nullptr, nullptr);
            ClientDecorationsActive = false;
        }
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
        SDL_SetWindowBordered(Window, !ClientDecorationsActive);
        break;
    case WindowMode::Fullscreen:
        SDL_SetWindowBordered(Window, !ClientDecorationsActive);
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

void SdlWindow::SetFrameRegions(const WindowFrameRegions& regions)
{
    FrameRegions = regions;
}

WindowFrameHit SdlWindow::ClassifyFrameHit(int32_t x, int32_t y) const
{
    if (!Window || SDL_GetWindowRelativeMouseMode(Window))
        return WindowFrameHit::Client;
    const auto flags = SDL_GetWindowFlags(Window);
    const WindowFrameProbe probe{
        .X = x,
        .Y = y,
        .Extent = GetExtent(),
        .Resizable = (flags & SDL_WINDOW_RESIZABLE) != 0,
        .Maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0,
    };
    return ClassifyWindowFrameHit(FrameRegions, probe);
}

bool SdlWindow::Minimize()
{
    if (!Window) return false;
    if (SDL_MinimizeWindow(Window)) return true;
    Log.Warn("Window minimize failed: {}", SDL_GetError());
    return false;
}

bool SdlWindow::Maximize()
{
    if (!Window) return false;
    if (SDL_MaximizeWindow(Window)) return true;
    Log.Warn("Window maximize failed: {}", SDL_GetError());
    return false;
}

bool SdlWindow::Restore()
{
    if (!Window) return false;
    if (SDL_RestoreWindow(Window)) return true;
    Log.Warn("Window restore failed: {}", SDL_GetError());
    return false;
}

bool SdlWindow::IsMaximized() const
{
    if (!Window) return false;
    return (SDL_GetWindowFlags(Window) & SDL_WINDOW_MAXIMIZED) != 0;
}

uint32_t SdlWindow::GetId() const
{
    if (!Window) return 0;
    return SDL_GetWindowID(Window);
}
