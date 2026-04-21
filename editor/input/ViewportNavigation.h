#pragma once

#include "InputEvent.h"

#include <functional>

class FourWayViewportLayout;
struct EditorViewport;

class ViewportNavigation
{
public:
    ViewportNavigation(FourWayViewportLayout& layout,
                       std::function<void(bool)> onRelativeModeChange);

    InputConsumed OnInput(const InputEvent& event);

private:
    InputConsumed HandlePointerDown(const PointerDownEvent& e);
    InputConsumed HandlePointerUp(const PointerUpEvent& e);
    InputConsumed HandlePointerMove(const PointerMoveEvent& e);
    InputConsumed HandleWheel(const WheelEvent& e);
    InputConsumed HandleFocusLost(const FocusLostEvent& e);

    void ClearCapture();

    FourWayViewportLayout& Layout;
    std::function<void(bool)> OnRelativeModeChange;
};
