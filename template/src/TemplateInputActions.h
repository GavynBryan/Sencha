#pragma once

#include <input/InputAction.h>

// The actions this game reads, resolved from the profile's action set once at
// startup. Systems index by id from here; adding an action is an edit to
// input_actions.sdata, a binding in the profile, and one field here.
struct TemplateInputActions
{
    InputActionId Move;
    InputActionId Look;
    InputActionId Jump;
};
