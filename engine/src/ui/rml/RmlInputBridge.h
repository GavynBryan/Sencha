#pragma once

#include <RmlUi/Core/Input.h>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

#include <cstdint>

//=============================================================================
// Platform input, in the terms the document engine uses.
//
// A translation table and nothing else: no policy lives here. Which surface an
// event reaches, and whether a game hears it too, are decided above.
//
// The table is deliberately partial. It covers what a UI is actually driven by
// -- navigation, editing, activation, and the printable keys a shortcut might
// use -- and everything else maps to UNKNOWN. That is not a gap to fill for
// completeness: typed characters arrive as text-input events, not as keys, so a
// key this cannot name is one no document needed named.
//=============================================================================

[[nodiscard]] Rml::Input::KeyIdentifier ToRmlKey(SDL_Scancode scancode);

[[nodiscard]] int ToRmlKeyModifiers(SDL_Keymod modifiers);

// Left 0, right 1, middle 2, matching the document engine's own numbering.
// Negative for a button it has no concept of.
[[nodiscard]] int ToRmlMouseButton(std::uint8_t sdlButton);
