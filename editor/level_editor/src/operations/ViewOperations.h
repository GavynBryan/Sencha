#pragma once

#include "operations/EditorOperations.h"

#include <vector>

class CommandStack;
class SelectionService;
class ViewportLayout;
class WorldDocument;

// The views, the selection and the level the view operations act on.
struct ViewTarget
{
    WorldDocument&    World;
    ViewportLayout&   Layout;
    SelectionService& Selection;
    CommandStack&     Commands;
};

// What the designer is looking at, and the selection and framing a client can
// hand back: view.context, view.select and view.frame. They read the live
// document and the active view, never a draft; without a view, view.context
// and view.frame refuse with view.none.
[[nodiscard]] std::vector<EditorOperationEntry> ViewOperations(ViewTarget target);
