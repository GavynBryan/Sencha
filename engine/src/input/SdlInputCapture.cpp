#include <input/SdlInputCapture.h>

#include <SDL3/SDL.h>

void SdlInputCapture::BeginFrame(InputFrame& frame)
{
    frame.MouseDeltaX = 0.0f;
    frame.MouseDeltaY = 0.0f;
    frame.MouseWheelY = 0.0f;
}

bool SdlInputCapture::Accept(InputFrame& frame, const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_QUIT:
        frame.QuitRequested = true;
        return true;

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        frame.FocusLost = true;
        return true;

    case SDL_EVENT_KEY_DOWN:
    {
        const uint32_t scancode = static_cast<uint32_t>(event.key.scancode);
        if (!event.key.repeat && !frame.IsKeyDown(scancode))
        {
            frame.KeysPressed.push_back(scancode);
        }
        frame.SetKeyHeld(scancode, true);
        return true;
    }

    case SDL_EVENT_KEY_UP:
    {
        const uint32_t scancode = static_cast<uint32_t>(event.key.scancode);
        if (frame.IsKeyDown(scancode))
        {
            frame.KeysReleased.push_back(scancode);
        }
        frame.SetKeyHeld(scancode, false);
        return true;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
        const uint32_t button = static_cast<uint32_t>(event.button.button);
        if (!frame.IsMouseButtonDown(button))
        {
            frame.MouseButtonsPressed.push_back(button);
        }
        frame.SetMouseButtonHeld(button, true);
        return true;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        const uint32_t button = static_cast<uint32_t>(event.button.button);
        if (frame.IsMouseButtonDown(button))
        {
            frame.MouseButtonsReleased.push_back(button);
        }
        frame.SetMouseButtonHeld(button, false);
        return true;
    }

    case SDL_EVENT_MOUSE_MOTION:
        frame.MouseX = event.motion.x;
        frame.MouseY = event.motion.y;
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
