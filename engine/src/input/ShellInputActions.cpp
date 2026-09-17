#include <input/ShellInputActions.h>

#include <input/InputControl.h>

#include <SDL3/SDL_scancode.h>

#include <array>

namespace
{
    InputActionDefinition Digital(std::string_view name)
    {
        InputActionDefinition definition;
        definition.Name = std::string(name);
        definition.Type = InputActionType::Digital;
        // Local to this client by construction: a menu keystroke is never part
        // of a deterministic player command.
        definition.Scope = InputActionScope::Presentation;
        definition.Fire = InputActionFireMode::Pressed;
        return definition;
    }

    InputBinding Direct(ShellAction action, InputControl control)
    {
        InputBinding binding;
        binding.ActionIndex = static_cast<std::uint32_t>(action);
        binding.Kind = InputBindingKind::Direct;
        binding.Controls[kBindingNegativeX] = control;
        return binding;
    }

    InputControl Key(std::uint16_t scancode)
    {
        return InputControl{ InputControlSource::Key, scancode };
    }

    InputControl Pad(GamepadButton button)
    {
        return InputControl{ InputControlSource::GamepadButton,
                             static_cast<std::uint16_t>(button) };
    }

    const std::array<InputActionDefinition, static_cast<std::size_t>(ShellAction::Count)>&
    Definitions()
    {
        // Function-local so the strings are built once, on first use, rather
        // than in a static initializer that would run before main.
        static const std::array<InputActionDefinition,
                                static_cast<std::size_t>(ShellAction::Count)> definitions{
            Digital(ShellActionName(ShellAction::Back)),
            Digital(ShellActionName(ShellAction::Accept)),
            Digital(ShellActionName(ShellAction::Up)),
            Digital(ShellActionName(ShellAction::Down)),
            Digital(ShellActionName(ShellAction::Left)),
            Digital(ShellActionName(ShellAction::Right)),
        };
        return definitions;
    }

    const std::array<InputBinding, 12>& Defaults()
    {
        // Two controls per action, keyboard and pad, so a player who picks up a
        // controller mid-session finds the menu already answers it.
        static const std::array<InputBinding, 12> bindings{
            Direct(ShellAction::Back,   Key(SDL_SCANCODE_ESCAPE)),
            Direct(ShellAction::Back,   Pad(GamepadButton::Start)),
            Direct(ShellAction::Accept, Key(SDL_SCANCODE_RETURN)),
            Direct(ShellAction::Accept, Pad(GamepadButton::South)),
            Direct(ShellAction::Up,     Key(SDL_SCANCODE_UP)),
            Direct(ShellAction::Up,     Pad(GamepadButton::DpadUp)),
            Direct(ShellAction::Down,   Key(SDL_SCANCODE_DOWN)),
            Direct(ShellAction::Down,   Pad(GamepadButton::DpadDown)),
            Direct(ShellAction::Left,   Key(SDL_SCANCODE_LEFT)),
            Direct(ShellAction::Left,   Pad(GamepadButton::DpadLeft)),
            Direct(ShellAction::Right,  Key(SDL_SCANCODE_RIGHT)),
            Direct(ShellAction::Right,  Pad(GamepadButton::DpadRight)),
        };
        return bindings;
    }
}

std::string_view ShellActionName(ShellAction action)
{
    switch (action)
    {
    case ShellAction::Back:   return "ui.back";
    case ShellAction::Accept: return "ui.accept";
    case ShellAction::Up:     return "ui.up";
    case ShellAction::Down:   return "ui.down";
    case ShellAction::Left:   return "ui.left";
    case ShellAction::Right:  return "ui.right";
    case ShellAction::Count:  break;
    }
    return {};
}

const InputActionDefinition* FindShellAction(std::string_view name)
{
    for (const InputActionDefinition& definition : Definitions())
    {
        if (definition.Name == name)
            return &definition;
    }
    return nullptr;
}

std::span<const InputActionDefinition> ShellActionDefinitions()
{
    return Definitions();
}

std::span<const InputBinding> ShellDefaultBindings()
{
    return Defaults();
}

void BuildShellOnlyProfile(InputActionRegistry& actions, BoundInputProfile& out)
{
    out = BoundInputProfile{};
    out.Name = "shell";

    std::string error;
    // Cannot fail: the definitions are the engine's own and carry no duplicate.
    (void)actions.Rebuild(Definitions(), &error);

    out.ActionTypes.assign(actions.SlotCount(), InputActionType::Digital);
    out.ActionFireModes.assign(actions.SlotCount(), InputActionFireMode::Pressed);
    for (const InputActionDefinition& definition : Definitions())
    {
        const std::size_t index = InputActionRegistry::IndexOf(actions.Find(definition.Name));
        out.ActionTypes[index] = definition.Type;
        out.ActionFireModes[index] = definition.Fire;
    }

    InputContextDefinition shell;
    shell.Name = std::string(kShellContextName);
    shell.Priority = kShellContextPriority;
    shell.IsShell = true;
    shell.FirstBinding = 0;
    shell.BindingCount = static_cast<std::uint32_t>(ShellDefaultBindings().size());
    out.Bindings.assign(ShellDefaultBindings().begin(), ShellDefaultBindings().end());
    out.Contexts.push_back(std::move(shell));
}
