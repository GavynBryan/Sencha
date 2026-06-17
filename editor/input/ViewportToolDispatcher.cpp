#include "ViewportToolDispatcher.h"

#include "../editmodes/EditSessionHost.h"
#include "../interaction/InteractionHost.h"
#include "../tools/ToolContext.h"
#include "../tools/ToolRegistry.h"
#include "../viewport/EditorViewport.h"
#include "../viewport/ViewportLayout.h"

#include <SDL3/SDL_keycode.h>

ViewportToolDispatcher::ViewportToolDispatcher(ViewportLayout& layout,
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
    if (std::get_if<FocusLostEvent>(&event))
    {
        // Losing focus mid-drag (e.g. alt-tab) aborts the gesture rather than
        // stranding live preview state; not consumed, so others still observe it.
        Abort();
        return InputConsumed::No;
    }
    return InputConsumed::No;
}

void ViewportToolDispatcher::Abort()
{
    Interactions.Cancel(Context); // revert any in-flight interaction (gizmo/brush)
    Tools.Cancel();               // drop any tool gesture (marquee)
}

InputConsumed ViewportToolDispatcher::HandlePointerDown(const PointerDownEvent& e)
{
    if (e.Button != MouseButton::Left)
        return InputConsumed::No;

    EditorViewport* vp = FindViewport(e.Position);
    if (vp == nullptr)
        return InputConsumed::No;

    SetActiveViewport(vp->Id);

    const PointerEvent pointer{ .Position = e.Position, .Button = e.Button, .Modifiers = e.Modifiers };
    if (Sessions.OnPointerDown(Context, *vp, pointer) == InputConsumed::Yes)
        return InputConsumed::Yes;

    return Tools.HandlePointerDown(*vp, pointer);
}

InputConsumed ViewportToolDispatcher::HandlePointerMove(const PointerMoveEvent& e)
{
    EditorViewport* vp = Layout.Active();
    if (vp == nullptr)
        return InputConsumed::No;

    const PointerEvent pointer{ .Position = e.Position, .Delta = e.Delta, .Modifiers = e.Modifiers };
    if (Interactions.OnPointerMove(Context, *vp, pointer) == InputConsumed::Yes)
        return InputConsumed::Yes;

    return Tools.HandlePointerMove(*vp, pointer);
}

InputConsumed ViewportToolDispatcher::HandlePointerUp(const PointerUpEvent& e)
{
    if (e.Button != MouseButton::Left)
        return InputConsumed::No;

    EditorViewport* vp = Layout.Active();
    if (vp == nullptr)
        return InputConsumed::No;

    const PointerEvent pointer{ .Position = e.Position, .Button = e.Button, .Modifiers = e.Modifiers };
    if (Interactions.OnPointerUp(Context, *vp, pointer) == InputConsumed::Yes)
        return InputConsumed::Yes;

    return Tools.HandlePointerUp(*vp, pointer);
}

InputConsumed ViewportToolDispatcher::HandleKeyDown(const KeyDownEvent& e)
{
    if (e.Key == SDLK_ESCAPE && Interactions.IsActive())
    {
        Abort();
        return InputConsumed::Yes;
    }

    return Tools.OnInput(InputEvent{ e });
}

EditorViewport* ViewportToolDispatcher::FindViewport(ImVec2 pos)
{
    for (const auto& viewport : Layout.All())
    {
        if (viewport != nullptr && viewport->Contains(pos))
            return viewport.get();
    }
    return nullptr;
}

void ViewportToolDispatcher::SetActiveViewport(ViewportId id)
{
    Layout.SetActive(id);
}
