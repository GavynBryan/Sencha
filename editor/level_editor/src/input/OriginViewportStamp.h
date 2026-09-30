#pragma once

#include "input/InputEvent.h"

class InputRouter;
class ViewportLayout;

// Resolves and stamps a translated pointer event's origin viewport before it is
// routed: the captured viewport while a gesture holds the pointer, otherwise the
// viewport under the cursor (which also becomes the focused viewport).
void StampOriginViewport(InputRouter& router, ViewportLayout& layout, InputEvent& event);
