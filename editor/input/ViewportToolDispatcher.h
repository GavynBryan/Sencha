#pragma once

#include "InputEvent.h"

#include "../viewport/ViewportId.h"

class EditSessionHost;
class InteractionHost;
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

    InputConsumed OnInput(const InputEvent& event);

private:
    InputConsumed HandlePointerDown(const PointerDownEvent& e);
    InputConsumed HandlePointerMove(const PointerMoveEvent& e);
    InputConsumed HandlePointerUp(const PointerUpEvent& e);
    InputConsumed HandleKeyDown(const KeyDownEvent& e);

    // Reverts any in-flight interaction and drops any tool gesture. (W4.)
    void Abort();

    EditorViewport* FindViewport(ImVec2 pos);
    void SetActiveViewport(ViewportId id);

    // Resolves the viewport a live gesture belongs to. A pointer gesture keeps the
    // viewport it began in for its whole lifetime (down->move->up), so the cursor
    // crossing into another viewport mid-drag can't switch the projection out from
    // under it (ViewportNavigation re-activates the hovered viewport on move).
    // Falls back to the active viewport when no gesture is in progress.
    EditorViewport* ActiveGestureViewport();

    ViewportLayout& Layout;
    ToolContext& Context;
    InteractionHost& Interactions;
    EditSessionHost& Sessions;
    ToolRegistry& Tools;

    ViewportId GestureViewport = {};
    bool GestureActive = false;
};
