#include "ShortcutRegistry.h"

void ShortcutRegistry::Register(std::string_view action, SDL_Keycode key, ModifierFlags modifiers,
                                std::function<void()> callback)
{
    Shortcuts.push_back({ action, key, modifiers, std::move(callback) });
}

InputConsumed ShortcutRegistry::OnInput(const InputEvent& event)
{
    const auto* keyEvent = std::get_if<KeyDownEvent>(&event);
    if (keyEvent == nullptr)
        return InputConsumed::No;

    for (const Shortcut& shortcut : Shortcuts)
    {
        if (shortcut.Key != keyEvent->Key || !(shortcut.Modifiers == keyEvent->Modifiers))
            continue;

        shortcut.Callback();
        return InputConsumed::Yes;
    }

    return InputConsumed::No;
}
