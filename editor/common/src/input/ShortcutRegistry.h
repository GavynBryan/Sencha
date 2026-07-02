#pragma once

#include "InputEvent.h"

#include <functional>
#include <string_view>
#include <vector>

class ShortcutRegistry
{
public:
    // The action name identifies the binding (for a future user keymap override
    // and for diagnostics); dispatch matches key + exact modifiers.
    void Register(std::string_view action, SDL_Keycode key, ModifierFlags modifiers,
                  std::function<void()> callback);
    InputConsumed OnInput(const InputEvent& event);

private:
    struct Shortcut
    {
        std::string_view Action;
        SDL_Keycode Key;
        ModifierFlags Modifiers;
        std::function<void()> Callback;
    };

    std::vector<Shortcut> Shortcuts;
};
