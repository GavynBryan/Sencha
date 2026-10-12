#include "SdlEventTranslation.h"

#include <platform/SdlWindow.h>

#include <SDL3/SDL.h>

#include <unordered_map>

ModifierFlags ReadModifiers(SDL_Keymod mod)
{
    return {
        .Ctrl = (mod & SDL_KMOD_CTRL) != 0,
        .Shift = (mod & SDL_KMOD_SHIFT) != 0,
        .Alt = (mod & SDL_KMOD_ALT) != 0,
    };
}

namespace
{
// Where the last pointer event in each window said the pointer is. A key event
// carries no position of its own; for a real pointer this is what SDL's mouse
// state says, and for a pointer a script moved, SDL's state never heard of it.
std::unordered_map<SDL_WindowID, Vec2d>& LastPointers()
{
    static std::unordered_map<SDL_WindowID, Vec2d> seen;
    return seen;
}

void Hear(SDL_WindowID window, float x, float y)
{
    LastPointers()[window] = { x, y };
}

Vec2d PointerNow(SDL_WindowID window)
{
    if (const auto seen = LastPointers().find(window); seen != LastPointers().end())
        return seen->second;
    float x = 0.0f;
    float y = 0.0f;
    SDL_GetMouseState(&x, &y);
    return { x, y };
}
}

std::optional<InputEvent> TranslateSdlEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    {
        MouseButton button;
        if (event.button.button == SDL_BUTTON_LEFT)
            button = MouseButton::Left;
        else if (event.button.button == SDL_BUTTON_RIGHT)
            button = MouseButton::Right;
        else if (event.button.button == SDL_BUTTON_MIDDLE)
            button = MouseButton::Middle;
        else
            return std::nullopt;

        Hear(event.button.windowID, event.button.x, event.button.y);
        return PointerDownEvent{
            .Position = { event.button.x, event.button.y },
            .Button = button,
            .Modifiers = ReadModifiers(SDL_GetModState()),
        };
    }

    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        MouseButton button;
        if (event.button.button == SDL_BUTTON_LEFT)
            button = MouseButton::Left;
        else if (event.button.button == SDL_BUTTON_RIGHT)
            button = MouseButton::Right;
        else if (event.button.button == SDL_BUTTON_MIDDLE)
            button = MouseButton::Middle;
        else
            return std::nullopt;

        Hear(event.button.windowID, event.button.x, event.button.y);
        return PointerUpEvent{
            .Position = { event.button.x, event.button.y },
            .Button = button,
            .Modifiers = ReadModifiers(SDL_GetModState()),
        };
    }

    case SDL_EVENT_MOUSE_MOTION:
        Hear(event.motion.windowID, event.motion.x, event.motion.y);
        return PointerMoveEvent{
            .Position = { event.motion.x, event.motion.y },
            .Delta = { event.motion.xrel, event.motion.yrel },
            .Modifiers = ReadModifiers(SDL_GetModState()),
        };

    case SDL_EVENT_MOUSE_WHEEL:
        Hear(event.wheel.windowID, event.wheel.mouse_x, event.wheel.mouse_y);
        return WheelEvent{
            .Position = { event.wheel.mouse_x, event.wheel.mouse_y },
            .Delta = event.wheel.y,
            .Modifiers = ReadModifiers(SDL_GetModState()),
        };

    case SDL_EVENT_KEY_DOWN:
        if (event.key.repeat)
            return std::nullopt;
        return KeyDownEvent{
            .Key = event.key.key,
            .Modifiers = ReadModifiers(event.key.mod),
            .Pointer = PointerNow(event.key.windowID),
        };

    case SDL_EVENT_KEY_UP:
        return KeyUpEvent{
            .Key = event.key.key,
            .Modifiers = ReadModifiers(event.key.mod),
            .Pointer = PointerNow(event.key.windowID),
        };

    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return FocusLostEvent{};

    default:
        return std::nullopt;
    }
}

void SetRelativeMouseMode(SdlWindow& window, bool enabled)
{
    SDL_Window* handle = window.GetHandle();
    if (handle == nullptr)
        return;

    if (SDL_GetWindowRelativeMouseMode(handle) != enabled)
        SDL_SetWindowRelativeMouseMode(handle, enabled);

    SDL_CaptureMouse(enabled);
    if (enabled)
        SDL_HideCursor();
    else
        SDL_ShowCursor();
}
