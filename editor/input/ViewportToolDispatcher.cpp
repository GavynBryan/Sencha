#include "ViewportToolDispatcher.h"

#include "../editmodes/EditSessionHost.h"
#include "../interaction/InteractionHost.h"
#include "../tools/ToolContext.h"
#include "../tools/ToolRegistry.h"
#include "../viewport/EditorViewport.h"
#include "../viewport/FourWayViewportLayout.h"

#include <SDL3/SDL_keycode.h>

ViewportToolDispatcher::ViewportToolDispatcher(FourWayViewportLayout& layout,
                                               ToolContext& context,
                                               InteractionHost& interactions,
                                               EditSessionHost& sessions,
                                               ToolRegistry& tools)
    : Layout(layout)
    , Context(context)
    , Interactions(interactions)
    , Sessions(sessions)
    , Tools(tools)
{
}

InputConsumed ViewportToolDispatcher::OnInput(const InputEvent& event)
{
    if (const auto* e = std::get_if<PointerDownEvent>(&event))
        return HandlePointerDown(*e);
    if (const auto* e = std::get_if<PointerMoveEvent>(&event))
        return HandlePointerMove(*e);
    if (const auto* e = std::get_if<PointerUpEvent>(&event))
        return HandlePointerUp(*e);
    if (const auto* e = std::get_if<KeyDownEvent>(&event))
        return HandleKeyDown(*e);
    return InputConsumed::No;
}

InputConsumed ViewportToolDispatcher::HandlePointerDown(const PointerDownEvent& e)
{
    if (e.Button != MouseButton::Left)
        return InputConsumed::No;

    EditorViewport* vp = FindViewport(e.Position);
    if (vp == nullptr)
        return InputConsumed::No;

    SetActiveViewport(static_cast<int>(vp - Layout.Viewports));

    if (Sessions.OnPointerDown(Context, *vp, e.Position) == InputConsumed::Yes)
        return InputConsumed::Yes;

    return Tools.HandlePointerDown(*vp, e.Position);
}

InputConsumed ViewportToolDispatcher::HandlePointerMove(const PointerMoveEvent& e)
{
    EditorViewport* vp = Layout.GetActiveViewport();
    if (vp == nullptr)
        return InputConsumed::No;

    if (Interactions.OnPointerMove(Context, *vp, e.Position, e.Delta) == InputConsumed::Yes)
        return InputConsumed::Yes;

    return Tools.HandlePointerMove(*vp, e.Position, e.Delta);
}

InputConsumed ViewportToolDispatcher::HandlePointerUp(const PointerUpEvent& e)
{
    if (e.Button != MouseButton::Left)
        return InputConsumed::No;

    EditorViewport* vp = Layout.GetActiveViewport();
    if (vp == nullptr)
        return InputConsumed::No;

    if (Interactions.OnPointerUp(Context, *vp, e.Position) == InputConsumed::Yes)
        return InputConsumed::Yes;

    return Tools.HandlePointerUp(*vp, e.Position);
}

InputConsumed ViewportToolDispatcher::HandleKeyDown(const KeyDownEvent& e)
{
    if (e.Key == SDLK_ESCAPE && Interactions.IsActive())
    {
        Interactions.Cancel(Context);
        return InputConsumed::Yes;
    }

    return Tools.OnInput(InputEvent{ e });
}

EditorViewport* ViewportToolDispatcher::FindViewport(ImVec2 pos)
{
    for (EditorViewport& vp : Layout.Viewports)
    {
        if (vp.Contains(pos))
            return &vp;
    }
    return nullptr;
}

void ViewportToolDispatcher::SetActiveViewport(int index)
{
    Layout.ActiveIndex = index;
    for (int i = 0; i < 4; ++i)
        Layout.Viewports[i].IsActive = (i == index);
}
