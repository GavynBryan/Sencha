#include "SdlEventTranslation.h"

#include <platform/SdlWindow.h>

#include <SDL3/SDL.h>

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
// Where the last pointer event said the pointer is, once one has. A key event
// carries no position of its own; for a real pointer this is what SDL's mouse
// state says, and for a pointer a script moved, SDL's state never heard of it.
struct PointerSeen
{
    bool Heard = false;
    Vec2d At = {};
};
PointerSeen LastPointer;

void Hear(float x, float y)
{
    LastPointer = { true, { x, y } };
}

Vec2d PointerNow()
{
    if (LastPointer.Heard)
        return LastPointer.At;
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

        Hear(event.button.x, event.button.y);
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

        return PointerUpEvent{
            .Position = { event.button.x, event.button.y },
            .Button = button,
            .Modifiers = ReadModifiers(SDL_GetModState()),
        };
    }

    case SDL_EVENT_MOUSE_MOTION:
        Hear(event.motion.x, event.motion.y);
        return PointerMoveEvent{
            .Position = { event.motion.x, event.motion.y },
            .Delta = { event.motion.xrel, event.motion.yrel },
            .Modifiers = ReadModifiers(SDL_GetModState()),
        };

    case SDL_EVENT_MOUSE_WHEEL:
        return WheelEvent{
            .Position = {},
            .Delta = event.wheel.y,
            .Modifiers = ReadModifiers(SDL_GetModState()),
        };

    case SDL_EVENT_KEY_DOWN:
        if (event.key.repeat)
            return std::nullopt;
        return KeyDownEvent{
            .Key = event.key.key,
            .Modifiers = ReadModifiers(event.key.mod),
            .Pointer = PointerNow(),
        };

    case SDL_EVENT_KEY_UP:
        return KeyUpEvent{
            .Key = event.key.key,
            .Modifiers = ReadModifiers(event.key.mod),
            .Pointer = PointerNow(),
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
