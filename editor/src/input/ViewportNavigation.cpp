#include "ViewportNavigation.h"

#include "InputRouter.h"

#include "../viewport/EditorCamera.h"
#include "../viewport/EditorViewport.h"
#include "../viewport/ViewportLayout.h"

ViewportNavigation::ViewportNavigation(ViewportLayout& layout,
                                       std::function<void(bool)> onRelativeModeChange)
    : Layout(layout)
    , OnRelativeModeChange(std::move(onRelativeModeChange))
{
}

InputConsumed ViewportNavigation::OnInput(const InputEvent& event, PointerCapture& capture)
{
    if (const auto* e = std::get_if<PointerDownEvent>(&event))
        return HandlePointerDown(*e, capture);
    if (const auto* e = std::get_if<PointerUpEvent>(&event))
        return HandlePointerUp(*e, capture);
    if (const auto* e = std::get_if<PointerMoveEvent>(&event))
        return HandlePointerMove(*e);
    if (const auto* e = std::get_if<WheelEvent>(&event))
        return HandleWheel(*e);
    if (const auto* e = std::get_if<FocusLostEvent>(&event))
        return HandleFocusLost(*e, capture);
    return InputConsumed::No;
}

InputConsumed ViewportNavigation::HandlePointerDown(const PointerDownEvent& e, PointerCapture& capture)
{
    if (e.Button != MouseButton::Right && e.Button != MouseButton::Middle)
        return InputConsumed::No;

    EditorViewport* hit = Layout.Find(e.Viewport);
    if (hit == nullptr)
        return InputConsumed::No;

    ClearCapture(); // focus is set by the input boundary, not here

    if (e.Button == MouseButton::Right
        && hit->Camera.ActiveMode == EditorCamera::Mode::Perspective)
    {
        hit->WantsFlyCameraInput = true;
        OnRelativeModeChange(true);
        capture.Acquire(PointerCaptureKind::Viewport, hit->Id);
    }
    else if (e.Button == MouseButton::Middle
             && hit->Camera.ActiveMode == EditorCamera::Mode::Orthographic)
    {
        hit->WantsOrthoPanInput = true;
        capture.Acquire(PointerCaptureKind::Viewport, hit->Id);
    }

    return InputConsumed::Yes;
}

InputConsumed ViewportNavigation::HandlePointerUp(const PointerUpEvent& e, PointerCapture& capture)
{
    if (e.Button != MouseButton::Right && e.Button != MouseButton::Middle)
        return InputConsumed::No;

    ClearCapture();
    OnRelativeModeChange(false);
    if (capture.HeldBySelf())
        capture.Release();
    return InputConsumed::Yes;
}

InputConsumed ViewportNavigation::HandlePointerMove(const PointerMoveEvent& e)
{
    // Reached for camera motion only while this nav holds capture (fly/pan); the
    // event then carries the captured viewport. Focus when nothing is captured is
    // owned by the input boundary, so there is no hover-activate here.
    EditorViewport* vp = Layout.Find(e.Viewport);
    if (vp == nullptr)
        return InputConsumed::No;

    if (vp->WantsFlyCameraInput
        && vp->Camera.ActiveMode == EditorCamera::Mode::Perspective)
    {
        vp->Camera.ApplyPerspectiveLook(e.Delta.x, e.Delta.y);
        return InputConsumed::Yes;
    }

    if (vp->WantsOrthoPanInput
        && vp->Camera.ActiveMode == EditorCamera::Mode::Orthographic)
    {
        const float viewportHeight = vp->RegionMax.y - vp->RegionMin.y;
        vp->Camera.ApplyOrthoPan(e.Delta.x, e.Delta.y, viewportHeight);
        return InputConsumed::Yes;
    }

    return InputConsumed::No;
}

InputConsumed ViewportNavigation::HandleWheel(const WheelEvent& e)
{
    EditorViewport* active = Layout.Active();
    if (active == nullptr)
        return InputConsumed::No;

    if (active->Camera.ActiveMode == EditorCamera::Mode::Orthographic)
        active->Camera.ApplyOrthoZoom(e.Delta);
    else
        active->Camera.ApplyPerspectiveDolly(e.Delta);

    return InputConsumed::Yes;
}

InputConsumed ViewportNavigation::HandleFocusLost(const FocusLostEvent&, PointerCapture& capture)
{
    ClearCapture();
    OnRelativeModeChange(false);
    if (capture.HeldBySelf())
        capture.Release();
    return InputConsumed::No;
}

void ViewportNavigation::ClearCapture()
{
    for (const auto& viewport : Layout.All())
    {
        if (viewport == nullptr)
            continue;
        viewport->WantsFlyCameraInput = false;
        viewport->WantsOrthoPanInput = false;
    }
}
