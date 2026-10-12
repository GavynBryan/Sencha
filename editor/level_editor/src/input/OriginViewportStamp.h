#pragma once

#include "input/InputEvent.h"

class InputRouter;
class ViewportLayout;

// Resolves and stamps a translated pointer event's viewport before it is
// routed: the pinned viewport while a pinned gesture holds the pointer,
// otherwise the viewport under the cursor (see PointerCapture::Acquire).
void StampOriginViewport(InputRouter& router, ViewportLayout& layout, InputEvent& event);
