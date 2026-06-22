#include "ViewportToolDispatcher.h"

#include "InputRouter.h"

#include "../editmodes/EditSessionHost.h"
#include "../interaction/InteractionHost.h"
#include "../tools/ToolContext.h"
#include "../tools/ToolRegistry.h"
#include "../viewport/EditorViewport.h"
#include "../viewport/ViewportLayout.h"

#include <SDL3/SDL_keycode.h>

#include <utility>

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
        // Losing focus mid-gesture (e.g. alt-tab) aborts rather than stranding live
        // preview state; not consumed, so others still observe it.
        Abort();
        if (capture.HeldBySelf())
            capture.Release();
        return InputConsumed::No;
    }
    return InputConsumed::No;
}

void ViewportToolDispatcher::Abort()
{
    Interactions.Cancel(Context); // revert any in-flight interaction (gizmo/marquee/brush)
    Tools.Cancel();               // drop any tool gesture state
    Recognizer.Reset();
}

InputConsumed ViewportToolDispatcher::HandlePointerDown(const PointerDownEvent& e, PointerCapture& capture)
{
    if (e.Button != MouseButton::Left)
        return InputConsumed::No;

    EditorViewport* vp = Layout.Find(e.Viewport);
    if (vp == nullptr)
        return InputConsumed::No;

    // Own the pointer for the gesture: while held, the router delivers every move/up
    // here exclusively (re-stamped with this viewport), so the gesture stays on its
    // origin viewport and the camera/UI never see these events.
    capture.Acquire(PointerCaptureKind::Viewport, vp->Id);

    const PointerEvent pointer{ .Position = e.Position, .Button = e.Button, .Modifiers = e.Modifiers };

    // A manipulator grabs on the press (unchanged); the recognizer only classifies
    // the tool-level click-vs-drag when no gizmo took the press.
    if (Sessions.OnPointerDown(Context, *vp, pointer) == InputConsumed::Yes)
    {
        Recognizer.Reset();
        return InputConsumed::Yes;
    }

    Recognizer.Press(pointer);
    return InputConsumed::Yes;
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

    // A live drag (gizmo, marquee, or brush-create) owns the stream.
    if (Interactions.IsActive())
        return Interactions.OnPointerMove(Context, *vp, pointer);

    // Otherwise a press becomes a drag once it crosses the deadzone: ask the active
    // tool for the interaction that runs it, then feed it this first move.
    if (Recognizer.Move(pointer) == GestureRecognizer::Result::DragBegan)
    {
        const PointerEvent press{
            .Position = Recognizer.PressPointer().Position,
            .Modifiers = Recognizer.PressPointer().Modifiers,
        };
        if (auto interaction = Tools.BeginDrag(*vp, press))
        {
            Interactions.Begin(Context, std::move(interaction));
            return Interactions.OnPointerMove(Context, *vp, pointer);
        }
    }
    return InputConsumed::No;
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
        Recognizer.Reset();
        return InputConsumed::No;
    }

    const PointerEvent pointer{ .Position = e.Position, .Button = e.Button, .Modifiers = e.Modifiers };

    // A live drag commits on release.
    if (Interactions.IsActive())
    {
        const InputConsumed result = Interactions.OnPointerUp(Context, *vp, pointer);
        Recognizer.Reset();
        return result;
    }

    // Otherwise the release is a click (or double-click), routed to the active tool
    // with the modifiers held at press (gesture intent).
    const GestureRecognizer::Result gesture = Recognizer.Release(pointer);
    if (gesture == GestureRecognizer::Result::Clicked
        || gesture == GestureRecognizer::Result::DoubleClicked)
    {
        const PointerEvent click{
            .Position = e.Position,
            .Modifiers = Recognizer.PressPointer().Modifiers,
        };
        return gesture == GestureRecognizer::Result::DoubleClicked ? Tools.DoubleClick(*vp, click)
                                                                   : Tools.Click(*vp, click);
    }
    return InputConsumed::No;
}

InputConsumed ViewportToolDispatcher::HandleKeyDown(const KeyDownEvent& e, PointerCapture& capture)
{
    if (e.Key == SDLK_ESCAPE && (Interactions.IsActive() || Recognizer.Active()))
    {
        Abort();
        if (capture.HeldBySelf())
            capture.Release();
        return InputConsumed::Yes;
    }

    return Tools.OnInput(InputEvent{ e });
}
