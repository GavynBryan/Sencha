#pragma once

#include <input/InputAction.h>
#include <input/InputControl.h>

#include <cstdint>
#include <string>
#include <vector>

//=============================================================================
// Compiled input bindings
//
// The runtime form of an authored profile: flat tables of controls, transforms
// and dense action indices, with every name already resolved. A resolve pass
// walks these and does no string work, no lookup, and no allocation.
//
// Contexts are slices of one binding array rather than separate objects so a
// pass over the active set stays a linear walk.
//=============================================================================

// How a binding turns its controls into a value.
enum class InputBindingKind : std::uint8_t
{
    // One control drives the action directly. A button gives 0 or 1, the mouse
    // gives its displacement for the pass.
    Direct,
    // Two buttons drive one axis: Controls[0] is the negative end,
    // Controls[1] the positive one.
    AxisPair,
    // Four buttons drive a plane, in the order -X, +X, -Y, +Y. WASD is
    // A, D, S, W: strafe on X, forward on Y.
    Cardinal,
};

// Slot meanings for InputBinding::Controls. Direct uses NegativeX only.
inline constexpr std::size_t kInputBindingControlCount = 4;
inline constexpr std::size_t kBindingNegativeX = 0;
inline constexpr std::size_t kBindingPositiveX = 1;
inline constexpr std::size_t kBindingNegativeY = 2;
inline constexpr std::size_t kBindingPositiveY = 3;

struct InputBinding
{
    std::uint32_t ActionIndex = 0;
    InputBindingKind Kind = InputBindingKind::Direct;
    InputControl Controls[kInputBindingControlCount]{};

    float Scale = 1.0f;
    // Below this magnitude the control reads as rest. Applied radially for a
    // plane and axially for a scalar, rescaling what is left so the live range
    // still starts at zero instead of stepping.
    float DeadZone = 0.0f;
    bool InvertX = false;
    bool InvertY = false;
    // Clamp a composite to the unit circle, so holding two directions is not
    // faster than holding one. Meaningless for Direct, which passes device
    // displacement through unscaled.
    bool Normalize = true;
};

// Whether a binding can produce the value an action carries. A button cannot
// describe a plane and the mouse cannot be a button, so a mismatch is an
// authoring error worth failing the load over rather than a control that
// quietly never fires.
[[nodiscard]] constexpr bool BindingProducesType(const InputBinding& binding, InputActionType type)
{
    switch (binding.Kind)
    {
    case InputBindingKind::Direct:
    {
        const InputControlValueKind kind = ValueKindOf(binding.Controls[kBindingNegativeX].Source);
        switch (type)
        {
        case InputActionType::Digital:
            return kind == InputControlValueKind::Button;
        case InputActionType::Axis1D:
            return kind == InputControlValueKind::Button || kind == InputControlValueKind::Axis1D;
        case InputActionType::Axis2D:
            return kind == InputControlValueKind::Axis2D;
        }
        return false;
    }
    case InputBindingKind::AxisPair:
        return type == InputActionType::Axis1D;
    case InputBindingKind::Cardinal:
        return type == InputActionType::Axis2D;
    }
    return false;
}

// A runtime-activatable group of bindings.
//
// Priority orders claim, not evaluation: within one frame the highest-priority
// active context that binds a control gets it, and lower contexts see that
// control as absent. Priorities are unique within a profile so the order never
// depends on authoring accident.
struct InputContextDefinition
{
    std::string Name;
    std::int32_t Priority = 0;
    std::uint32_t FirstBinding = 0;
    std::uint32_t BindingCount = 0;
};

struct BoundInputProfile
{
    std::string Name;
    // Sorted by descending priority: a resolve pass walks them in claim order.
    std::vector<InputContextDefinition> Contexts;
    std::vector<InputBinding> Bindings;
    // Action type by dense index, so a resolve pass knows whether to sum
    // contributions or or-them-together without consulting the registry.
    std::vector<InputActionType> ActionTypes;

    [[nodiscard]] std::uint32_t ActionCount() const
    {
        return static_cast<std::uint32_t>(ActionTypes.size());
    }

    [[nodiscard]] std::uint32_t FindContext(std::string_view name) const;
};

inline constexpr std::uint32_t kInvalidInputContext = 0xFFFFFFFFu;
