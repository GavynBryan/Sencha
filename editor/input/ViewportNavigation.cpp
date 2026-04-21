#include "ViewportNavigation.h"

#include "../viewport/EditorCamera.h"
#include "../viewport/EditorViewport.h"
#include "../viewport/ViewportLayout.h"

ViewportNavigation::ViewportNavigation(ViewportLayout& layout,
                                       std::function<void(bool)> onRelativeModeChange)
    : Layout(layout)
    , OnRelativeModeChange(std::move(onRelativeModeChange))
{
}

InputConsumed ViewportNavigation::OnInput(const InputEvent& event)
{
    if (const auto* e = std::get_if<PointerDownEvent>(&event))
        return HandlePointerDown(*e);
    if (const auto* e = std::get_if<PointerUpEvent>(&event))
        return HandlePointerUp(*e);
    if (const auto* e = std::get_if<PointerMoveEvent>(&event))
        return HandlePointerMove(*e);
    if (const auto* e = std::get_if<WheelEvent>(&event))
        return HandleWheel(*e);
    if (const auto* e = std::get_if<FocusLostEvent>(&event))
        return HandleFocusLost(*e);
    return InputConsumed::No;
}

InputConsumed ViewportNavigation::HandlePointerDown(const PointerDownEvent& e)
{
    if (e.Button != MouseButton::Right && e.Button != MouseButton::Middle)
        return InputConsumed::No;

    EditorViewport* hit = nullptr;
    for (const auto& viewport : Layout.All())
    {
        if (viewport != nullptr && viewport->Contains(e.Position))
        {
            hit = viewport.get();
            break;
        }
    }

    if (hit == nullptr)
        return InputConsumed::No;

    Layout.SetActive(hit->Id);
    ClearCapture();

    if (e.Button == MouseButton::Right
        && hit->Camera.ActiveMode == EditorCamera::Mode::Perspective)
    {
        hit->WantsFlyCameraInput = true;
        OnRelativeModeChange(true);
    }
    else if (e.Button == MouseButton::Middle
             && hit->Camera.ActiveMode == EditorCamera::Mode::Orthographic)
    {
        hit->WantsOrthoPanInput = true;
    }

    return InputConsumed::Yes;
}

InputConsumed ViewportNavigation::HandlePointerUp(const PointerUpEvent& e)
{
    if (e.Button != MouseButton::Right && e.Button != MouseButton::Middle)
        return InputConsumed::No;

    ClearCapture();
    OnRelativeModeChange(false);
    return InputConsumed::Yes;
}

InputConsumed ViewportNavigation::HandlePointerMove(const PointerMoveEvent& e)
{
    EditorViewport* active = Layout.Active();
    if (active == nullptr)
        return InputConsumed::No;

    if (active->WantsFlyCameraInput
        && active->Camera.ActiveMode == EditorCamera::Mode::Perspective)
    {
        active->Camera.ApplyPerspectiveLook(e.Delta.x, e.Delta.y);
        return InputConsumed::Yes;
    }

    if (active->WantsOrthoPanInput
        && active->Camera.ActiveMode == EditorCamera::Mode::Orthographic)
    {
        const float viewportHeight = active->RegionMax.y - active->RegionMin.y;
        active->Camera.ApplyOrthoPan(e.Delta.x, e.Delta.y, viewportHeight);
        return InputConsumed::Yes;
    }

    for (const auto& viewport : Layout.All())
    {
        if (viewport != nullptr && viewport->Contains(e.Position))
        {
            Layout.SetActive(viewport->Id);
            break;
        }
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

InputConsumed ViewportNavigation::HandleFocusLost(const FocusLostEvent&)
{
    ClearCapture();
    OnRelativeModeChange(false);
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
