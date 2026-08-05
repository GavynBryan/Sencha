#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

//=============================================================================
// Input controls
//
// A control is one physical thing a device reports: a key, a mouse button, the
// mouse's motion, the wheel. Bindings translate controls into action values, so
// this is the last layer that speaks about hardware.
//
// Authored data names controls as "key.w", "mouse.left", "mouse.delta". Key
// names resolve through the platform's own scancode table, so the vocabulary is
// exactly the one InputFrame is keyed by, with underscores standing in for
// spaces ("key.left_shift"). Scancodes rather than keycodes: a binding names a
// physical key position, and the rebinding UI is what shows the player the
// layout-dependent label.
//=============================================================================

enum class InputControlSource : std::uint8_t
{
    None,
    Key,            // Index is a platform scancode
    MouseButton,    // Index is a platform button number
    MouseMotion,    // pointer displacement for the pass, in pixels
    MouseWheel,     // wheel displacement for the pass, in notches
    GamepadButton,  // Index is a GamepadButton
    GamepadTrigger, // Index is a GamepadStick-independent trigger, [0, 1]
    GamepadStick,   // Index is a GamepadStick; reports both of its axes
};

// The abstract pad every controller is normalized onto. Named for position on
// the device rather than for the glyph printed there, so one binding covers pads
// whose face buttons carry different letters.
enum class GamepadButton : std::uint16_t
{
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStickClick,
    RightStickClick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count,
};

enum class GamepadTrigger : std::uint16_t
{
    Left,
    Right,
    Count,
};

enum class GamepadStick : std::uint16_t
{
    Left,
    Right,
    Count,
};

// What shape a control reports. A binding is only valid when its controls can
// produce the action's type, which is what makes a mistyped binding a load
// error instead of a control that silently never fires.
enum class InputControlValueKind : std::uint8_t
{
    Button,
    Axis1D,
    Axis2D,
};

struct InputControl
{
    InputControlSource Source = InputControlSource::None;
    std::uint16_t Index = 0;

    [[nodiscard]] bool IsValid() const { return Source != InputControlSource::None; }

    friend bool operator==(InputControl, InputControl) = default;
};

[[nodiscard]] constexpr InputControlValueKind ValueKindOf(InputControlSource source)
{
    switch (source)
    {
    case InputControlSource::MouseMotion:
    case InputControlSource::GamepadStick:
        return InputControlValueKind::Axis2D;
    case InputControlSource::MouseWheel:
    case InputControlSource::GamepadTrigger:
        return InputControlValueKind::Axis1D;
    default:
        return InputControlValueKind::Button;
    }
}

// Parses an authored control name. Returns nothing for an unknown device
// prefix or an unknown control within a known device, so a typo surfaces as a
// validation error rather than a binding that never fires.
[[nodiscard]] std::optional<InputControl> ParseInputControl(std::string_view name);

// The authored name for a control, for diagnostics and round-tripping.
[[nodiscard]] std::string FormatInputControl(InputControl control);
