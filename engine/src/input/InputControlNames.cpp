#include <input/InputControl.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <span>

// The only place authored control names meet the platform's control numbering.
// Keeping it in one translation unit is what lets every layer above speak
// InputControl and never a scancode constant.

namespace
{
struct NamedControl
{
    std::string_view Name;
    InputControl Control;
};

// Mouse controls are few and their platform numbering is not self-describing,
// so they are named here rather than looked up.
constexpr std::array<NamedControl, 7> kMouseControls{ {
    { "left",   { InputControlSource::MouseButton, SDL_BUTTON_LEFT } },
    { "right",  { InputControlSource::MouseButton, SDL_BUTTON_RIGHT } },
    { "middle", { InputControlSource::MouseButton, SDL_BUTTON_MIDDLE } },
    { "x1",     { InputControlSource::MouseButton, SDL_BUTTON_X1 } },
    { "x2",     { InputControlSource::MouseButton, SDL_BUTTON_X2 } },
    { "delta",  { InputControlSource::MouseMotion, 0 } },
    { "wheel",  { InputControlSource::MouseWheel, 0 } },
} };

// Named by position on the pad, not by the glyph printed on it: "gamepad.south"
// is the bottom face button whether the device calls it A or Cross.
constexpr std::array<NamedControl, 19> kGamepadControls{ {
    { "south",             { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::South) } },
    { "east",              { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::East) } },
    { "west",              { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::West) } },
    { "north",             { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::North) } },
    { "back",              { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::Back) } },
    { "guide",             { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::Guide) } },
    { "start",             { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::Start) } },
    { "left_stick_click",  { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::LeftStickClick) } },
    { "right_stick_click", { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::RightStickClick) } },
    { "left_shoulder",     { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::LeftShoulder) } },
    { "right_shoulder",    { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::RightShoulder) } },
    { "dpad_up",           { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::DpadUp) } },
    { "dpad_down",         { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::DpadDown) } },
    { "dpad_left",         { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::DpadLeft) } },
    { "dpad_right",        { InputControlSource::GamepadButton, static_cast<std::uint16_t>(GamepadButton::DpadRight) } },
    { "left_trigger",      { InputControlSource::GamepadTrigger, static_cast<std::uint16_t>(GamepadTrigger::Left) } },
    { "right_trigger",     { InputControlSource::GamepadTrigger, static_cast<std::uint16_t>(GamepadTrigger::Right) } },
    { "left_stick",        { InputControlSource::GamepadStick, static_cast<std::uint16_t>(GamepadStick::Left) } },
    { "right_stick",       { InputControlSource::GamepadStick, static_cast<std::uint16_t>(GamepadStick::Right) } },
} };

std::string ToLower(std::string_view text)
{
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

std::optional<InputControl> ParseKey(std::string_view suffix)
{
    if (suffix.empty())
        return std::nullopt;

    // Authored names spell spaces as underscores so a control name is one
    // token; the platform table spells them with spaces.
    std::string platformName(suffix);
    std::replace(platformName.begin(), platformName.end(), '_', ' ');

    const SDL_Scancode scancode = SDL_GetScancodeFromName(platformName.c_str());
    if (scancode == SDL_SCANCODE_UNKNOWN)
        return std::nullopt;
    return InputControl{ InputControlSource::Key, static_cast<std::uint16_t>(scancode) };
}

std::optional<InputControl> ParseNamed(std::span<const NamedControl> table, std::string_view suffix)
{
    for (const NamedControl& entry : table)
    {
        if (entry.Name == suffix)
            return entry.Control;
    }
    return std::nullopt;
}
}

std::optional<InputControl> ParseInputControl(std::string_view name)
{
    const std::size_t dot = name.find('.');
    if (dot == std::string_view::npos)
        return std::nullopt;

    const std::string device = ToLower(name.substr(0, dot));
    const std::string control = ToLower(name.substr(dot + 1));
    if (control.empty())
        return std::nullopt;

    if (device == "key")
        return ParseKey(control);
    if (device == "mouse")
        return ParseNamed(kMouseControls, control);
    if (device == "gamepad")
        return ParseNamed(kGamepadControls, control);
    return std::nullopt;
}

std::span<const NamedInputControl> EnumerateInputControls()
{
    static const std::vector<NamedInputControl> controls = []
    {
        std::vector<NamedInputControl> all;

        // Scancode zero is "unknown"; the rest are offered only if the name
        // this platform gives them parses back to the same control. That drops
        // the unnamed ones and, where two scancodes share a name, keeps the one
        // the parser actually resolves to -- so every entry here is bindable.
        for (std::uint16_t scancode = 1; scancode < SDL_SCANCODE_COUNT; ++scancode)
        {
            const InputControl control{ InputControlSource::Key, scancode };
            std::string name = FormatInputControl(control);
            if (name == "key.unknown")
                continue;
            if (ParseInputControl(name) != control)
                continue;
            all.push_back(NamedInputControl{ control, std::move(name) });
        }

        for (const NamedControl& entry : kMouseControls)
            all.push_back(NamedInputControl{ entry.Control, "mouse." + std::string(entry.Name) });
        for (const NamedControl& entry : kGamepadControls)
            all.push_back(NamedInputControl{ entry.Control, "gamepad." + std::string(entry.Name) });

        return all;
    }();

    return controls;
}

std::string FormatInputControl(InputControl control)
{
    switch (control.Source)
    {
    case InputControlSource::Key:
    {
        const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(control.Index));
        if (name == nullptr || name[0] == '\0')
            return "key.unknown";
        std::string formatted = ToLower(name);
        std::replace(formatted.begin(), formatted.end(), ' ', '_');
        return "key." + formatted;
    }
    case InputControlSource::MouseButton:
    case InputControlSource::MouseMotion:
    case InputControlSource::MouseWheel:
        for (const NamedControl& entry : kMouseControls)
        {
            if (entry.Control == control)
                return "mouse." + std::string(entry.Name);
        }
        return "mouse.unknown";
    case InputControlSource::GamepadButton:
    case InputControlSource::GamepadTrigger:
    case InputControlSource::GamepadStick:
        for (const NamedControl& entry : kGamepadControls)
        {
            if (entry.Control == control)
                return "gamepad." + std::string(entry.Name);
        }
        return "gamepad.unknown";
    case InputControlSource::None:
        break;
    }
    return "none";
}
