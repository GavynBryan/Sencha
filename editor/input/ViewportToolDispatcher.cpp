#include "ViewportToolDispatcher.h"

#include "InputRouter.h"

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

InputConsumed ViewportToolDispatcher::OnInput(const InputEvent& event, PointerCapture& capture)
{
    if (const auto* e = std::get_if<PointerDownEvent>(&event))
        return HandlePointerDown(*e, capture);
    if (const auto* e = std::get_if<PointerMoveEvent>(&event))
        return HandlePointerMove(*e, capture);
    if (const auto* e = std::get_if<PointerUpEvent>(&event))
        return HandlePointerUp(*e, capture);
    if (const auto* e = std::get_if<KeyDownEvent>(&event))
        return HandleKeyDown(*e, capture);
    if (std::get_if<FocusLostEvent>(&event))
    {
        // Losing focus mid-drag (e.g. alt-tab) aborts the gesture rather than
        // stranding live preview state; not consumed, so others still observe it.
        Abort();
        if (capture.HeldBySelf())
            capture.Release();
        return InputConsumed::No;
    }
    return InputConsumed::No;
}

void ViewportToolDispatcher::Abort()
{
    Interactions.Cancel(Context); // revert any in-flight interaction (gizmo/brush)
    Tools.Cancel();               // drop any tool gesture (marquee)
}

InputConsumed ViewportToolDispatcher::HandlePointerDown(const PointerDownEvent& e, PointerCapture& capture)
{
    if (e.Button != MouseButton::Left)
        return InputConsumed::No;

    EditorViewport* vp = Layout.Find(e.Viewport);
    if (vp == nullptr)
        return InputConsumed::No;

    // Own the pointer for the gesture: while held, the router delivers every move/up
    // here exclusively and re-stamps them with this viewport, so the gesture stays on
    // its origin viewport and the camera/UI never see these events.
    capture.Acquire(PointerCaptureKind::Viewport, vp->Id);

    const PointerEvent pointer{ .Position = e.Position, .Button = e.Button, .Modifiers = e.Modifiers };
    if (Sessions.OnPointerDown(Context, *vp, pointer) == InputConsumed::Yes)
        return InputConsumed::Yes;

    return Tools.HandlePointerDown(*vp, pointer);
}

InputConsumed ViewportToolDispatcher::HandlePointerMove(const PointerMoveEvent& e, PointerCapture& capture)
{
    EditorViewport* vp = Layout.Find(e.Viewport);
    if (vp == nullptr)
    {
        // The gesture's viewport vanished mid-drag (e.g. a layout change): abandon it.
        if (capture.HeldBySelf())
        {
            Abort();
            capture.Release();
        }
        return InputConsumed::No;
    }

    const PointerEvent pointer{ .Position = e.Position, .Delta = e.Delta, .Modifiers = e.Modifiers };
    if (Interactions.OnPointerMove(Context, *vp, pointer) == InputConsumed::Yes)
        return InputConsumed::Yes;

    return Tools.HandlePointerMove(*vp, pointer);
}

InputConsumed ViewportToolDispatcher::HandlePointerUp(const PointerUpEvent& e, PointerCapture& capture)
{
    if (e.Button != MouseButton::Left)
        return InputConsumed::No;

    EditorViewport* vp = Layout.Find(e.Viewport);
    const bool wasHeld = capture.HeldBySelf();
    if (wasHeld)
        capture.Release(); // the LMB gesture ends here

    if (vp == nullptr)
    {
        if (wasHeld)
            Abort(); // origin viewport gone; revert rather than strand preview state
        return InputConsumed::No;
    }

    const PointerEvent pointer{ .Position = e.Position, .Button = e.Button, .Modifiers = e.Modifiers };
    if (Interactions.OnPointerUp(Context, *vp, pointer) == InputConsumed::Yes)
        return InputConsumed::Yes;

    return Tools.HandlePointerUp(*vp, pointer);
}

InputConsumed ViewportToolDispatcher::HandleKeyDown(const KeyDownEvent& e, PointerCapture& capture)
{
    if (e.Key == SDLK_ESCAPE && Interactions.IsActive())
    {
        Abort();
        if (capture.HeldBySelf())
            capture.Release();
        return InputConsumed::Yes;
    }

    return Tools.OnInput(InputEvent{ e });
}
