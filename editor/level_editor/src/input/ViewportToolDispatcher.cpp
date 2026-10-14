#include "ViewportToolDispatcher.h"

#include "input/InputRouter.h"

#include "brush/BrushMesh.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "editmodes/EditSessionHost.h"
#include "interaction/InteractionHost.h"
#include "meshedit/MeshEditService.h"
#include "meshedit/MeshElements.h"
#include "resources/ActiveResources.h"
#include "overlay/EditorOverlayState.h"
#include "overlay/SelectionLabels.h"
#include "selection/SelectionService.h"
#include "tools/ToolContext.h"
#include "tools/ToolRegistry.h"
#include "viewport/EditorViewport.h"
#include "viewport/Picking.h"
#include "viewport/ViewportLayout.h"

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
    if (const auto* e = std::get_if<PointerHandedEvent>(&event))
        return HandlePointerHanded(*e, capture);
    if (const auto* e = std::get_if<KeyDownEvent>(&event))
        return HandleKeyDown(*e, capture);
    if (std::get_if<FocusLostEvent>(&event))
    {
        // Losing focus mid-gesture (e.g. alt-tab) aborts rather than stranding live
        // preview state; not consumed, so others still observe it.
        AbortAndRelease(capture);
        return InputConsumed::No;
    }
    return InputConsumed::No;
}

bool ViewportToolDispatcher::GestureOutlivedItsTool() const
{
    return Span != GestureSpan::None && Tools.GetActiveTool() != GestureTool;
}

void ViewportToolDispatcher::Abort()
{
    Interactions.Cancel(Context); // revert any in-flight interaction (gizmo/marquee/brush)
    Tools.Cancel();               // drop any tool gesture state
    Recognizer.Reset();
    Span = GestureSpan::None;
    GestureTool = nullptr;
}

void ViewportToolDispatcher::AbortAndRelease(PointerCapture& capture)
{
    Abort();
    if (capture.HeldBySelf())
        capture.Release();
}

void ViewportToolDispatcher::HoverAt(EditorViewport& viewport, Vec2d pos)
{
    // Gizmo part highlight tracks the cursor regardless of the active tool
    // (the session hit-tests only the manipulator that would take the press).
    Sessions.UpdateHover(viewport, pos);

    // The active tool gets first crack at hover (the edge cut drives a live preview);
    // if it doesn't handle it, fall back to the element-mode selection glow.
    if (Tools.Hover(viewport, pos) == InputConsumed::No)
        UpdateHover(viewport, pos);
}

void ViewportToolDispatcher::ClearHover()
{
    Context.Overlay.Hover = {};
    Context.Overlay.HoverBody = {};
    Sessions.ClearHover();
    Tools.HoverEnd();
}

void ViewportToolDispatcher::SampleFaceMaterial(const PointerDownEvent& e)
{
    EditorViewport* vp = Layout.Find(e.Viewport);
    if (vp == nullptr)
        return;

    const SelectableRef picked = Context.Picking.Pick(*vp, e.Position, Context.Scene,
                                                      { .Mode = BrushPickMode::FaceOnly });
    if (!picked.IsFace())
        return;
    const BrushMesh* mesh = Context.Scene.TryGetBrushMesh(picked.Entity);
    if (mesh == nullptr || picked.ElementId >= mesh->Faces.size())
        return;

    // Sample what the face RENDERS: an inherit-default face yields the level
    // default, not an empty ref.
    Context.Resources.SetActive(EffectiveMaterial(mesh->Faces[picked.ElementId].Material,
                                                      Context.Document.GetDefaultMaterial()));
}

InputConsumed ViewportToolDispatcher::HandlePointerDown(const PointerDownEvent& e, PointerCapture& capture)
{
    // Shift+Right samples a face's material into the active material; fly-look
    // explicitly yields this chord (ViewportNavigation). Consumed even on a
    // miss: the gesture was aimed at the scene either way.
    if (e.Button == MouseButton::Right && e.Modifiers.Shift)
    {
        SampleFaceMaterial(e);
        return InputConsumed::Yes;
    }

    if (e.Button != MouseButton::Left || Span != GestureSpan::None)
        return InputConsumed::No;

    EditorViewport* vp = Layout.Find(e.Viewport);
    if (vp == nullptr)
        return InputConsumed::No;

    // Own the pointer for the gesture: while held, the router delivers every move/up
    // here exclusively (re-stamped with this viewport), so the gesture stays on its
    // origin viewport and the camera/UI never see these events.
    capture.Acquire(PointerCaptureKind::Exclusive, vp->Id);
    Span = GestureSpan::PinnedToView;
    GestureTool = Tools.GetActiveTool();

    const PointerEvent pointer{ .Position = e.Position, .Button = e.Button, .Modifiers = e.Modifiers };

    // A manipulator grabs on the press (unchanged); the recognizer only classifies
    // the tool-level click-vs-drag when no gizmo took the press.
    if (Sessions.OnPointerDown(Context, *vp, pointer) == InputConsumed::Yes)
    {
        Recognizer.Reset();
        return InputConsumed::Yes;
    }

    // What the click or drag acts on is what was shown at the press: no hover
    // runs again until the gesture ends.
    HoverAt(*vp, e.Position);
    Recognizer.Press(pointer);
    return InputConsumed::Yes;
}

InputConsumed ViewportToolDispatcher::HandlePointerMove(const PointerMoveEvent& e, PointerCapture& capture)
{
    if (GestureOutlivedItsTool())
        AbortAndRelease(capture);

    EditorViewport* vp = Layout.Find(e.Viewport);
    if (vp == nullptr)
    {
        // A gesture that follows the pointer is merely between views.
        if (Span == GestureSpan::FollowsPointer)
        {
            Interactions.OnPointerLeftViews(Context);
            return InputConsumed::Yes;
        }
        ClearHover(); // cursor left the viewports
        // The gesture's viewport vanished mid-drag (e.g. a layout change): abandon it.
        if (capture.HeldBySelf())
            AbortAndRelease(capture);
        return InputConsumed::No;
    }

    const PointerEvent pointer{ .Position = e.Position, .Delta = e.Delta, .Modifiers = e.Modifiers };

    // A live drag (gizmo, marquee, brush-create, a carry) owns the stream; no hover feedback.
    if (Interactions.IsActive())
    {
        ClearHover();
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

    // A held button is not a hover: the press already settled what is shown.
    if (Span != GestureSpan::None || capture.HeldBySelf())
        return InputConsumed::Yes;

    HoverAt(*vp, pointer.Position);
    return InputConsumed::No;
}

void ViewportToolDispatcher::UpdateHover(EditorViewport& viewport, Vec2d pos)
{
    const MeshElementKind mode = Context.MeshEdit.GetElementKind();
    // Hover any brush, even in an element mode: hovering another mesh's element
    // previews it, and a click switches the active body to it.
    const SelectableRef hovered = Context.Picking.Pick(
        viewport, pos, Context.Scene, BrushPickRequest{ .Mode = PickModeForElementKind(mode) });

    ElementHoverState& hover = Context.Overlay.Hover;
    hover.Element = hovered;
    hover.Measure.clear();

    // In an element mode, mark the hovered element's brush as the preview body so its
    // wireframe reads as "a click would select this". Object mode uses the element glow.
    Context.Overlay.HoverBody = (mode != MeshElementKind::Object && hovered.IsValid())
        ? hovered.Entity : EntityId{};

    // An edge carries its length, anchored at its midpoint.
    if (hovered.IsEdge())
    {
        const SourceWorldElements* elements =
            Context.Scene.PlacementFacts().GetSourceWorldElements(hovered.Entity);
        if (elements != nullptr && hovered.ElementId < elements->Edges.size())
        {
            const EdgeElement& edge = elements->Edges[hovered.ElementId];
            hover.Measure = FormatUnits((edge.A - edge.B).Magnitude());
            hover.MeasureAnchor = edge.Mid;
        }
    }
}

InputConsumed ViewportToolDispatcher::HandlePointerUp(const PointerUpEvent& e, PointerCapture& capture)
{
    if (e.Button != MouseButton::Left)
        return InputConsumed::No;

    if (capture.HeldBySelf())
        capture.Release(); // the LMB gesture ends here, whatever becomes of it

    // A release with no gesture of ours -- one already aborted, or a capture
    // inherited from a rebuilt stack -- ends nothing.
    if (Span == GestureSpan::None)
    {
        Recognizer.Reset();
        return InputConsumed::No;
    }
    if (GestureOutlivedItsTool())
    {
        Abort();
        return InputConsumed::No;
    }

    EditorViewport* vp = Layout.Find(e.Viewport);
    if (vp == nullptr)
    {
        // Let go over no view, or its view is gone: nothing is made.
        Abort();
        return InputConsumed::No;
    }

    const PointerEvent pointer{ .Position = e.Position, .Button = e.Button, .Modifiers = e.Modifiers };
    const GestureRecognizer::Result gesture = Recognizer.Release(pointer);
    const PointerEvent press{
        .Position = Recognizer.PressPointer().Position,
        .Modifiers = Recognizer.PressPointer().Modifiers,
    };
    Span = GestureSpan::None;
    GestureTool = nullptr;

    // A live drag commits on release.
    if (Interactions.IsActive())
    {
        const InputConsumed result = Interactions.OnPointerUp(Context, *vp, pointer);
        Recognizer.Reset();
        return result;
    }

    // Otherwise the release is a click (or double-click), routed to the active tool
    // at the press: a click stays within the drag deadzone of its press, and the
    // press is where the hover was last resolved, so "if it highlights, the click
    // selects it" holds even for an edge grabbed at a glancing angle.
    if (gesture == GestureRecognizer::Result::Clicked)
        return Tools.Click(*vp, press);
    if (gesture == GestureRecognizer::Result::DoubleClicked)
        return Tools.DoubleClick(*vp, press);
    return InputConsumed::No;
}

InputConsumed ViewportToolDispatcher::HandlePointerHanded(const PointerHandedEvent& e, PointerCapture& capture)
{
    if (Span != GestureSpan::None)
        return InputConsumed::No;
    std::unique_ptr<IInteraction> interaction = Tools.TakeHandedGesture();
    if (interaction == nullptr)
        return InputConsumed::No;

    // No origin: the rest of the gesture follows the pointer from view to view.
    capture.Acquire(PointerCaptureKind::Exclusive);
    Span = GestureSpan::FollowsPointer;
    GestureTool = Tools.GetActiveTool();
    Recognizer.Reset();
    ClearHover();
    Interactions.Begin(Context, std::move(interaction));

    if (EditorViewport* vp = Layout.Find(e.Viewport))
        (void)Interactions.OnPointerMove(Context, *vp, PointerEvent{ .Position = e.Position, .Modifiers = e.Modifiers });
    else
        Interactions.OnPointerLeftViews(Context);
    return InputConsumed::Yes;
}

InputConsumed ViewportToolDispatcher::HandleKeyDown(const KeyDownEvent& e, PointerCapture& capture)
{
    if (e.Key == SDLK_ESCAPE && (Span != GestureSpan::None || Interactions.IsActive() || Recognizer.Active()))
    {
        AbortAndRelease(capture);
        return InputConsumed::Yes;
    }

    return Tools.OnInput(InputEvent{ e });
}
