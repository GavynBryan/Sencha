#include "SdlEventTranslation.h"

#include "InputRouter.h"
#include "../viewport/ViewportLayout.h"

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

void StampOriginViewport(InputRouter& router, ViewportLayout& layout, InputEvent& event)
{
    // While a gesture holds the pointer, every event belongs to the viewport it began
    // in; otherwise a positioned event belongs to the viewport under the cursor, which
    // also becomes the focused viewport. (Wheel carries no position and keeps
    // targeting the focused viewport downstream.)
    const auto resolve = [&](ImVec2 position) -> ViewportId
    {
        if (router.PointerCaptured())
            return router.CaptureViewport();
        const ViewportId hovered = layout.ResolveAt(position);
        if (hovered.IsValid())
            layout.SetActive(hovered);
        return hovered;
    };

    if (auto* e = std::get_if<PointerDownEvent>(&event))
        e->Viewport = resolve(e->Position);
    else if (auto* e = std::get_if<PointerUpEvent>(&event))
        e->Viewport = resolve(e->Position);
    else if (auto* e = std::get_if<PointerMoveEvent>(&event))
        e->Viewport = resolve(e->Position);
}
