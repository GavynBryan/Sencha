#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

class ConsoleRegistry;

//=============================================================================
// SynthesizedInput
//
// Platform events a script asks for, pushed into the platform's queue at the
// frame it names: a held key, the pointer over a view, a click. A startup
// script drives the editor the way a person would, through every layer a real
// event passes, so an unattended capture can show what a hand would bring up.
//=============================================================================
class SynthesizedInput
{
public:
    // `window` answers the id of the window the events are for.
    void Register(ConsoleRegistry& registry, std::function<std::uint32_t()> window);

    // Once per frame, before the platform's events are read: pushes what is due.
    void Advance();

private:
    struct Pending
    {
        std::uint64_t AtFrame = 0;
        SDL_Event Event{};
    };

    std::vector<Pending> Queue;
    // What dropped files' events point at, for as long as the platform may read them.
    std::deque<std::string> DroppedPaths;
    std::uint64_t Frame = 0;
    float PointerX = 0.0f;
    float PointerY = 0.0f;
    SDL_Keymod Modifiers = SDL_KMOD_NONE;
    SDL_MouseButtonFlags Buttons = 0;
};
