#pragma once

#include "InputEvent.h"

#include "../viewport/ViewportId.h"

class EditSessionHost;
class InteractionHost;
class PointerCapture;
class ToolRegistry;
struct EditorViewport;
struct ToolContext;
class ViewportLayout;

class ViewportToolDispatcher
{
public:
    ViewportToolDispatcher(ViewportLayout& layout,
                           ToolContext& context,
                           InteractionHost& interactions,
                           EditSessionHost& sessions,
                           ToolRegistry& tools);

    InputConsumed OnInput(const InputEvent& event, PointerCapture& capture);

private:
    InputConsumed HandlePointerDown(const PointerDownEvent& e, PointerCapture& capture);
    InputConsumed HandlePointerMove(const PointerMoveEvent& e, PointerCapture& capture);
    InputConsumed HandlePointerUp(const PointerUpEvent& e, PointerCapture& capture);
    InputConsumed HandleKeyDown(const KeyDownEvent& e, PointerCapture& capture);

    // Reverts any in-flight interaction and drops any tool gesture. (W4.)
    void Abort();

    EditorViewport* FindViewport(ImVec2 pos);
    void SetActiveViewport(ViewportId id);

    // Resolves the viewport the gesture belongs to: while this dispatcher holds the
    // pointer capture, the viewport it began in (pinned at press), so the cursor
    // crossing into another viewport mid-drag can't switch the projection out from
    // under it. Falls back to the active viewport when no gesture is in progress.
    EditorViewport* ResolveGestureViewport(PointerCapture& capture);

    ViewportLayout& Layout;
    ToolContext& Context;
    InteractionHost& Interactions;
    EditSessionHost& Sessions;
    ToolRegistry& Tools;

    // The viewport a pointer gesture began in, pinned at press for the gesture's
    // lifetime. Exclusivity itself comes from the router's pointer capture.
    ViewportId GestureViewport = {};
};
