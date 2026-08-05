#pragma once

#include <core/identity/StrongId.h>

#include <cstdint>
#include <string>

//=============================================================================
// Input actions
//
// An action is what the game asked for, not what the device did: Move, Jump,
// PrimaryFire. Gameplay reads actions and never learns which control produced
// one, which is what lets bindings, devices, and contexts change underneath it.
//
// Identity follows the AbilityId/AttributeId pattern: authored data names an
// action by string, an InputActionRegistry mints dense registration-order ids
// at load, and consumers resolve name-to-id once and index by id afterwards.
// Ids are registration-order runtime values and must never be serialized or
// sent on a wire -- names are the stable identity.
//=============================================================================

using InputActionId = StrongId<struct InputActionIdTag, std::uint32_t>;

// The value shape an action carries. Deliberately three: a button, a scalar,
// and a plane. Anything richer (view angles, gestures) is a consumer's job
// built out of these, not a new value kind here.
enum class InputActionType : std::uint8_t
{
    Digital,
    Axis1D,
    Axis2D,
};

// Whether an action feeds the simulation or only the presentation layer.
//
// Simulation actions are the ones a deterministic player command may carry;
// Presentation actions (menu navigation, debug toggles) are local to this
// client by construction. Both resolve every pass -- this declares intent for
// the command projection and for diagnostics, it does not gate resolution.
enum class InputActionScope : std::uint8_t
{
    Simulation,
    Presentation,
};

// Which edge or level makes a digital action fire.
//
// Authored per action rather than decided at the call site, so a designer
// asking why holding jump jumps repeatedly finds the answer in the input
// mapping instead of in C++. Gameplay that wants one moment reads Fired();
// gameplay built out of several phases -- charge on press, loose on release --
// still reads the raw edges.
enum class InputActionFireMode : std::uint8_t
{
    Pressed,
    Released,
    Held,
};

enum class InputActionFlags : std::uint8_t
{
    None = 0,
    Held = 1 << 0,
    Pressed = 1 << 1,
    Released = 1 << 2,
    // The action's own fire mode is satisfied this pass. A mode-selected copy
    // of one of the three above, stamped where the mode is known so a recorded
    // tick carries the decision made under the mode in force when it ran.
    Fired = 1 << 3,
};

[[nodiscard]] constexpr InputActionFlags operator|(InputActionFlags a, InputActionFlags b)
{
    return static_cast<InputActionFlags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

constexpr InputActionFlags& operator|=(InputActionFlags& a, InputActionFlags b)
{
    a = a | b;
    return a;
}

[[nodiscard]] constexpr bool HasFlag(InputActionFlags value, InputActionFlags flag)
{
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0;
}

// The flag a fire mode selects over.
[[nodiscard]] constexpr InputActionFlags FireFlagOf(InputActionFireMode mode)
{
    switch (mode)
    {
    case InputActionFireMode::Released: return InputActionFlags::Released;
    case InputActionFireMode::Held:     return InputActionFlags::Held;
    case InputActionFireMode::Pressed:  break;
    }
    return InputActionFlags::Pressed;
}

// One action as declared by an authored action set. The registry mints ids for
// these; the data layer produces them without needing a registry to exist yet.
struct InputActionDefinition
{
    std::string Name;
    InputActionType Type = InputActionType::Digital;
    InputActionScope Scope = InputActionScope::Simulation;
    // Meaningful for Digital only. An axis action carries a magnitude, not a
    // moment, and never fires.
    InputActionFireMode Fire = InputActionFireMode::Pressed;
};

// One action's resolved state for one clock.
//
// Pressed and Released may both be set: a tap that begins and ends inside a
// frame that ran no fixed tick still owes the simulation both edges, and
// dropping either would lose the impulse.
struct InputActionValue
{
    float X = 0.0f;
    float Y = 0.0f;
    InputActionFlags Flags = InputActionFlags::None;

    [[nodiscard]] bool IsHeld() const { return HasFlag(Flags, InputActionFlags::Held); }
    [[nodiscard]] bool WasPressed() const { return HasFlag(Flags, InputActionFlags::Pressed); }
    [[nodiscard]] bool WasReleased() const { return HasFlag(Flags, InputActionFlags::Released); }
    [[nodiscard]] bool WasFired() const { return HasFlag(Flags, InputActionFlags::Fired); }
};
