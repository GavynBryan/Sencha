#include <input/SdlInputCapture.h>

#include <SDL3/SDL.h>

void SdlInputCapture::BeginFrame(InputFrame& frame)
{
    frame.MouseDeltaX = 0.0f;
    frame.MouseDeltaY = 0.0f;
    frame.MouseWheelY = 0.0f;
    // Republished after this frame's events are routed, by whoever owns the
    // surfaces. Cleared here so a host that registers none, or one whose overlay
    // was torn down, cannot leave a stale claim standing.
    frame.UiCapture = {};
}

bool SdlInputCapture::Accept(InputFrame& frame, const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_QUIT:
        frame.QuitRequested = true;
        return true;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // An unfocused window gets no key-up events, so whatever was down when
        // focus left would stay held until the player pressed and released it
        // again.
        frame.ReleaseAllHeld();
        return true;

    case SDL_EVENT_KEY_DOWN:
        if (!event.key.repeat)
            frame.SetButton({ InputControlSource::Key, static_cast<std::uint16_t>(event.key.scancode) }, true);
        return true;

    case SDL_EVENT_KEY_UP:
        frame.SetButton({ InputControlSource::Key, static_cast<std::uint16_t>(event.key.scancode) }, false);
        return true;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        frame.SetButton({ InputControlSource::MouseButton, static_cast<std::uint16_t>(event.button.button) }, true);
        return true;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        frame.SetButton({ InputControlSource::MouseButton, static_cast<std::uint16_t>(event.button.button) }, false);
        return true;

    case SDL_EVENT_MOUSE_MOTION:
        frame.MouseDeltaX += event.motion.xrel;
        frame.MouseDeltaY += event.motion.yrel;
        return true;

    case SDL_EVENT_MOUSE_WHEEL:
        frame.MouseWheelY += event.wheel.y;
        return true;

    default:
        return false;
    }
}
