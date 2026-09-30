#pragma once

#include "input/InputEvent.h"
#include "viewport/ViewportArrangement.h"

#include <functional>

class PointerCapture;

//=============================================================================
// ViewDividerDrag
//
// Moving the divider between arranged views: a press on it takes the pointer,
// a drag moves it, and releasing lets go. The divider is the workspace's
// setting; this reads how the views are arranged and reports where the
// divider moved.
//=============================================================================
class ViewDividerDrag
{
public:
    struct Arrangement
    {
        ViewportArrangementKind Kind = ViewportArrangementKind::Single;
        // Where the views were last laid out, in window pixels.
        Rect2d Content;
        Vec2d Divider{ 0.5f, 0.5f };
    };

    ViewDividerDrag(std::function<Arrangement()> arrangement, std::function<void(Vec2d)> moved);

    InputConsumed OnInput(const InputEvent& event, PointerCapture& capture);

private:
    std::function<Arrangement()> Current;
    std::function<void(Vec2d)> Moved;
    DividerGrip Held;
};
