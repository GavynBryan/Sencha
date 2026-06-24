#include "ViewportToolDispatcher.h"

#include "InputRouter.h"

#include "../document/EditorScene.h"
#include "../editmodes/EditSessionHost.h"
#include "../interaction/InteractionHost.h"
#include "../meshedit/MeshEditService.h"
#include "../meshedit/MeshElements.h"
#include "../overlay/EditorOverlayState.h"
#include "../overlay/SelectionLabels.h"
#include "../selection/SelectionService.h"
#include "../tools/ToolContext.h"
#include "../tools/ToolRegistry.h"
#include "../viewport/EditorViewport.h"
#include "../viewport/Picking.h"
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
        Context.Overlay.Hover = {}; // cursor left the viewports: drop the hover glow
        Tools.HoverEnd();           // and any tool's hover preview (e.g. the edge cut)
        // The gesture's viewport vanished mid-drag (e.g. a layout change): abandon it.
        if (capture.HeldBySelf())
        {
            Abort();
            capture.Release();
        }
        return InputConsumed::No;
    }

    const PointerEvent pointer{ .Position = e.Position, .Delta = e.Delta, .Modifiers = e.Modifiers };

    // A live drag (gizmo, marquee, or brush-create) owns the stream; no hover feedback.
    if (Interactions.IsActive())
    {
        Context.Overlay.Hover = {};
        Tools.HoverEnd();
        return Interactions.OnPointerMove(Context, *vp, pointer);
    }

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

    // The active tool gets first crack at hover (the edge cut drives a live preview);
    // if it doesn't handle it, fall back to the element-mode selection glow.
    if (Tools.Hover(*vp, pointer.Position) == InputConsumed::No)
        UpdateHover(*vp, pointer.Position);
    return InputConsumed::No;
}

void ViewportToolDispatcher::UpdateHover(EditorViewport& viewport, ImVec2 pos)
{
    const MeshElementKind mode = Context.MeshEdit.GetElementKind();
    // Match the select tool's lock: in an element mode, only the brush being edited
    // is hoverable.
    const SelectionSnapshot selection = Context.Selection.GetSnapshot();
    const EntityId activeBody = (mode != MeshElementKind::Object && selection.Primary.IsValid())
        ? selection.Primary.Entity : EntityId{};
    const SelectableRef hovered = Context.Picking.Pick(
        viewport, pos, Context.Scene, BrushPickRequest{ .Mode = PickModeForElementKind(mode), .RestrictTo = activeBody });

    ElementHoverState& hover = Context.Overlay.Hover;
    hover.Element = hovered;
    hover.Measure.clear();

    // An edge carries its length, anchored at its midpoint.
    if (hovered.IsEdge())
    {
        const BrushMesh* mesh = Context.Scene.TryGetBrushMesh(hovered.Entity);
        const Transform3f* transform = Context.Scene.TryGetTransform(hovered.Entity);
        if (mesh != nullptr && transform != nullptr)
            if (const std::optional<EdgeElement> edge =
                    MeshElements::TryGetEdge(*mesh, *transform, hovered.ElementId))
            {
                hover.Measure = FormatUnits((edge->A - edge->B).Magnitude());
                hover.MeasureAnchor = edge->Mid;
            }
    }
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
        // Pick at the press position, not the release: a click stays within the drag
        // deadzone of its press, and the press is where the hover glow was last
        // resolved, so "if it highlights, the click selects it" holds even for an
        // edge grabbed at a glancing angle.
        const PointerEvent click{
            .Position = Recognizer.PressPointer().Position,
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
