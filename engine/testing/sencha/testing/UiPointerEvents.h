#pragma once

#include <ui/UiService.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>

#include <cstdint>

[[nodiscard]] inline SDL_Event UiPointerEvent(std::uint32_t type, float x, float y)
{
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION)
    {
        event.motion.x = x;
        event.motion.y = y;
        return event;
    }
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    return event;
}

inline void UiClickAt(UiService& ui, float x, float y)
{
    for (const std::uint32_t type : { SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP })
        (void)ui.ProcessPlatformEvent(UiPointerEvent(type, x, y));
}
