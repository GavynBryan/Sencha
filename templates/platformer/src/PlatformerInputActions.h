#pragma once

#include <input/InputAction.h>

// The actions this game reads, resolved from the profile's action set once at
// startup. Look turns the orbit camera; Move steers in its frame.
struct PlatformerInputActions
{
    InputActionId Move;
    InputActionId Look;
    InputActionId Jump;
};
