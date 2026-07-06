#pragma once

#include "InputRouter.h"
#include "UiInputCapture.h"

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
