#pragma once

#include "ITool.h"
#include "ToolWheelMath.h"

#include "input/InputEvent.h"

#include <cstdint>
#include <functional>

class PointerCapture;
class ToolRegistry;

// Wheel lifecycle. Closed: the key is up. Open: the key is down and the wheel
// is showing; the layout and the hot sector are meaningful, and this handler
// holds the pointer exclusively. Dismissed: the key is still down but the
// wheel is gone (Escape); it cannot reopen until the key has been released,
// whatever presses the boundary delivers meanwhile.
enum class ToolWheelPhase : std::uint8_t
{
    Closed,
    Open,
    Dismissed,
};

// The hold-to-choose tool menu: press the key and a wheel of every registered
// tool opens at the pointer; moving picks a sector by direction; releasing
// the key activates that sector's tool. An input-router handler, placed after
// the UI guard (a focused text field keeps its letters) and ahead of every
// viewport handler, so the wheel is modal while it is open.
//
// The hot sector starts empty at open and is set only by pointer motion: a
// wheel shifted in from a window edge may open with the pointer already
// inside a sector, and an immediate release must not select by accident.
// Releasing over the tool that is already active does nothing, because
// re-activating a tool reverts whatever it had pending.
class ToolWheelSession
{
public:
    // `frame` is read once per open: the window area and UI scale the wheel
    // is placed against, which is what keeps hit-testing and painting on one
    // set of numbers.
    ToolWheelSession(ToolRegistry& tools, ITool::Shortcut key, std::function<ToolWheel::Frame()> frame);

    InputConsumed OnInput(const InputEvent& event, PointerCapture& capture);

    [[nodiscard]] ToolWheelPhase GetPhase() const { return Phase; }
    // Meaningful while Open.
    [[nodiscard]] const ToolWheel::Layout& GetLayout() const { return Layout; }
    // The sector under the pointer, or -1 (the hub, or no motion yet).
    [[nodiscard]] int GetHot() const { return Hot; }

private:
    InputConsumed OnKeyDown(const KeyDownEvent& event, PointerCapture& capture);
    InputConsumed OnKeyUp(const KeyUpEvent& event, PointerCapture& capture);
    void Open(ImVec2 pointer, PointerCapture& capture);
    void Leave(ToolWheelPhase next, PointerCapture& capture);
    void Select(int index);
    [[nodiscard]] bool IsWheelKey(SDL_Keycode key) const { return key == Key.Key; }

    ToolRegistry& Tools;
    ITool::Shortcut Key;
    std::function<ToolWheel::Frame()> FrameProvider;
    ToolWheelPhase Phase = ToolWheelPhase::Closed;
    ToolWheel::Layout Layout{};
    int Hot = -1;
};
