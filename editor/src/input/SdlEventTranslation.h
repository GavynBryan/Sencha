#pragma once

#include "InputEvent.h"

#include <SDL3/SDL_events.h>

#include <optional>

class SdlWindow;
class InputRouter;
class ViewportLayout;

// The editor's SDL input boundary: SDL events in, engine InputEvents out, plus
// the relative-mouse control and the origin-viewport stamp that the boundary
// applies before routing.

[[nodiscard]] ModifierFlags ReadModifiers(SDL_Keymod mod);
[[nodiscard]] std::optional<InputEvent> TranslateSdlEvent(const SDL_Event& event);

// Fly-look: hide the cursor and switch to relative mouse, or restore it.
void SetRelativeMouseMode(SdlWindow& window, bool enabled);

// Resolves and stamps a translated pointer event's origin viewport before it is
// routed: the captured viewport while a gesture holds the pointer, otherwise the
// viewport under the cursor (which also becomes the focused viewport).
void StampOriginViewport(InputRouter& router, ViewportLayout& layout, InputEvent& event);
