#pragma once

#include <graphics/PresentationId.h>
#include <ui/UiSurface.h>

class ActionContextStack;
class SdlWindow;

// The window a workspace shows in, as much of it as a workspace needs: the
// platform window (relative mouse, dialogs, capture), its presentation, two
// display-only surfaces -- one under everything else in the window, for the
// workspace's content, and one over everything, for what a held key brings up
// -- and the window's interactive surface, for a document a person works in.
// Nothing about chrome.
struct WorkspaceWindow
{
    SdlWindow* Window = nullptr;
    PresentationId Presentation;
    UiSurfaceId Content;
    UiSurfaceId Overlay;
    UiSurfaceId Interactive;
    // Everything the window can perform, closest first: what an action the
    // workspace names resolves through, whoever claims it.
    const ActionContextStack* Actions = nullptr;
};
