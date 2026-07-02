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
        if (shortcut.Key != keyEvent->Key)
            continue;
        if (shortcut.Modifiers.Ctrl != keyEvent->Modifiers.Ctrl)
            continue;
        if (shortcut.Modifiers.Shift != keyEvent->Modifiers.Shift)
            continue;
        if (shortcut.Modifiers.Alt != keyEvent->Modifiers.Alt)
            continue;

        shortcut.Callback();
        return InputConsumed::Yes;
    }

    return InputConsumed::No;
}
