#pragma once

#include <graphics/PresentationId.h>
#include <ui/UiSurface.h>

class SdlWindow;

// The window a workspace shows in, as much of it as a workspace needs: the
// platform window (relative mouse, dialogs, capture), its presentation, and two
// display-only surfaces -- one under everything else in the window, for the
// workspace's content, and one over everything, for what a held key brings up.
// Nothing about chrome.
struct WorkspaceWindow
{
    SdlWindow* Window = nullptr;
    PresentationId Presentation;
    UiSurfaceId Content;
    UiSurfaceId Overlay;
};
