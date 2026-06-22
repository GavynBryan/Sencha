#pragma once

#include "InputEvent.h"

#include <functional>
#include <vector>

class ShortcutRegistry
{
public:
    void Register(SDL_Keycode key, ModifierFlags modifiers, std::function<void()> action);
    InputConsumed OnInput(const InputEvent& event);

private:
    struct Shortcut
    {
        SDL_Keycode Key;
        ModifierFlags Modifiers;
        std::function<void()> Action;
    };

    std::vector<Shortcut> Shortcuts;
};
