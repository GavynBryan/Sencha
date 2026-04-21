#pragma once

#include "InputEvent.h"

#include <functional>

class ViewportLayout;
struct EditorViewport;

class ViewportNavigation
{
public:
    ViewportNavigation(ViewportLayout& layout,
                       std::function<void(bool)> onRelativeModeChange);

    InputConsumed OnInput(const InputEvent& event);

private:
    InputConsumed HandlePointerDown(const PointerDownEvent& e);
    InputConsumed HandlePointerUp(const PointerUpEvent& e);
    InputConsumed HandlePointerMove(const PointerMoveEvent& e);
    InputConsumed HandleWheel(const WheelEvent& e);
    InputConsumed HandleFocusLost(const FocusLostEvent& e);

    void ClearCapture();

    ViewportLayout& Layout;
    std::function<void(bool)> OnRelativeModeChange;
};
