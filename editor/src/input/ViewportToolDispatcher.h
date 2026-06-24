#pragma once

#include "GestureRecognizer.h"
#include "InputEvent.h"

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

    // Resolves the element under the cursor for the active element mode and writes
    // it (plus an edge's length) to the overlay hover state, for the renderer glow.
    void UpdateHover(EditorViewport& viewport, ImVec2 pos);

    // Reverts any in-flight interaction and drops any tool gesture. (W4.)
    void Abort();

    ViewportLayout& Layout;
    ToolContext& Context;
    InteractionHost& Interactions;
    EditSessionHost& Sessions;
    ToolRegistry& Tools;
    GestureRecognizer Recognizer;
};
