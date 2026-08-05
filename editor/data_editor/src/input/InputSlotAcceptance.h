#pragma once

#include <input/InputAction.h>
#include <input/InputBindings.h>
#include <input/InputControl.h>

// Which controls may fill one slot of one binding.
//
// Composed from the engine's own two rules rather than restated: what a binding
// kind can be built out of, and what the assembled binding can drive. Every
// authoring surface that offers or accepts a control asks this, so the set the
// editor offers cannot drift from the set the binder accepts -- and when the
// binder learns a new pairing, the offers follow with no edit here.
struct InputSlotAcceptance
{
    InputBindingKind Kind = InputBindingKind::Direct;
    InputActionType ActionType = InputActionType::Digital;

    // False while the binding names no action the set declares. The slot then
    // accepts whatever its kind allows: refusing everything before the author
    // has chosen an action would make the binding unauthorable in the order
    // people actually build one.
    bool ActionKnown = false;

    [[nodiscard]] bool Accepts(InputControlSource source) const
    {
        if (!BindingKindAcceptsControl(Kind, source))
            return false;
        if (!ActionKnown)
            return true;

        InputBinding probe;
        probe.Kind = Kind;
        probe.Controls[kBindingNegativeX] = InputControl{ source, 0 };
        return BindingProducesType(probe, ActionType);
    }
};
