#pragma once

#include <graphics/PresentationId.h>

struct GraphicsServices;
struct PresentationDesc;
struct WindowCreateInfo;
class SdlWindowService;

// A window of its own and a presentation onto it, for a host showing more than
// one view. Null when either cannot be made. Its events arrive through the
// ordinary platform route, carrying its window id.
[[nodiscard]] PresentationId OpenPresentationWindow(GraphicsServices& graphics, SdlWindowService& windows,
                                                    const WindowCreateInfo& window, const PresentationDesc& desc);

// Refused while a feature records into the presentation, and for the primary.
// The window hides at once and is destroyed after its surface, once the frames
// using it have retired.
bool ClosePresentationWindow(GraphicsServices& graphics, SdlWindowService& windows, PresentationId presentation);
