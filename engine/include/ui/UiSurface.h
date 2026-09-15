#pragma once

#include <core/handle/Handle.h>
#include <graphics/RenderExtent.h>

#include <cstdint>

//=============================================================================
// UiSurface
//
// One place authored UI is presented: a window, an editor viewport region, an
// offscreen target. A surface owns its own layout size and its own stack of
// screens, so two surfaces never share focus or scroll state.
//
// Plural from the start, deliberately. A first implementation that creates
// exactly one is fine; a type that can only ever describe one would bake a
// single-window assumption into a layer whose whole claim is that it does not
// care which application hosts it.
//=============================================================================
using UiSurfaceId = Handle<struct UiSurfaceTag>;

// Abstract navigation, as a host's mapped actions express it. Deliberately not
// keys: a document never names a gamepad button or a scancode, so remapping,
// controller profiles and accessibility settings keep working without any of
// them knowing a document exists.
enum class UiNavigation : std::uint8_t
{
    Up = 0,
    Down,
    Left,
    Right,
    Next,      // tab order forward
    Previous,  // tab order back
    Accept,    // activate what has focus
    Cancel,    // dismiss, back out
};

// The measured layout box of an element, in surface pixels, with the origin at
// the surface's top left. Content area: what the element actually occupies,
// excluding its own padding and border.
struct UiElementBox
{
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;
};
