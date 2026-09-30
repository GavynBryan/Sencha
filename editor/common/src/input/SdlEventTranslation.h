#pragma once

#include "input/InputEvent.h"

#include <SDL3/SDL_events.h>

#include <optional>

class SdlWindow;

// The editor's SDL input boundary: SDL events in, editor InputEvents out, and
// the relative-mouse control.

[[nodiscard]] ModifierFlags ReadModifiers(SDL_Keymod mod);
[[nodiscard]] std::optional<InputEvent> TranslateSdlEvent(const SDL_Event& event);

// Fly-look: hide the cursor and switch to relative mouse, or restore it.
void SetRelativeMouseMode(SdlWindow& window, bool enabled);
