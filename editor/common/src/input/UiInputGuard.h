#pragma once

#include "InputRouter.h"
#include <input/UiInputCapture.h>

#include <functional>

// Builds the input-router handler that makes the UI the top layer of the input
// stack. Register it FIRST in the InputRouter: it consumes an event when the UI
// owns the device that event belongs to (pointer/wheel ↔ mouse, keys ↔ keyboard),
// stopping it before viewport navigation, tools, or shortcuts ever see it.
//
// `capture` is queried per event so the decision always reflects the current UI
// state. This is the one place input ownership is decided — adding panels or
// tools needs no new capture checks.
InputRouter::Handler MakeUiInputGuard(std::function<UiInputCapture()> capture);

// What the guard should be told, given an editor that hosts two UI layers.
//
// The shell's own capture is first narrowed by the viewport hole: input over
// the 3D region belongs to the scene even though an ImGui window is technically
// there. The authored layer's claim is folded in AFTER that, because it has no
// such hole -- a modal scrim covering the viewport is covering it on purpose,
// and a focused text field owns its letters wherever the cursor is. Applying
// the hole to the combined value instead would let clicks fall through a dialog
// into the tools it was blocking.
[[nodiscard]] UiInputCapture CombineUiCapture(UiInputCapture shell,
                                              bool overViewport,
                                              UiInputCapture authored);
