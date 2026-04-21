#pragma once

#include "InputEvent.h"

class EditSessionHost;
class InteractionHost;
class ToolRegistry;
struct EditorViewport;
struct ToolContext;
struct ViewportId;
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

    EditorViewport* FindViewport(ImVec2 pos);
    void SetActiveViewport(ViewportId id);

    ViewportLayout& Layout;
    ToolContext& Context;
    InteractionHost& Interactions;
    EditSessionHost& Sessions;
    ToolRegistry& Tools;
};
