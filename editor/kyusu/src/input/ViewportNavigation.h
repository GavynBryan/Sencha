#pragma once

#include "input/InputEvent.h"

#include <functional>

class ViewportLayout;
class PointerCapture;
struct EditorViewport;

class ViewportNavigation
{
public:
    ViewportNavigation(ViewportLayout& layout,
                       std::function<void(bool)> onRelativeModeChange);

    InputConsumed OnInput(const InputEvent& event, PointerCapture& capture);

private:
    InputConsumed HandlePointerDown(const PointerDownEvent& e, PointerCapture& capture);
    InputConsumed HandlePointerUp(const PointerUpEvent& e, PointerCapture& capture);
    InputConsumed HandlePointerMove(const PointerMoveEvent& e);
    InputConsumed HandleWheel(const WheelEvent& e);
    InputConsumed HandleFocusLost(const FocusLostEvent& e, PointerCapture& capture);

    void ClearCapture();

    ViewportLayout& Layout;
    std::function<void(bool)> OnRelativeModeChange;
};
