#include "GestureRecognizer.h"

#include <cmath>

namespace
{
// Drag past this many pixels turns a press into a drag (was SelectTool's threshold).
constexpr float kDragDeadzonePixels = 4.0f;
// A second click within this time + distance of the first is a double-click.
constexpr float kDoubleClickPixels = 5.0f;
constexpr std::chrono::milliseconds kDoubleClickTime{400};

float Distance(ImVec2 a, ImVec2 b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}
}

void GestureRecognizer::Press(const PointerEvent& pointer)
{
    Current = Phase::Pressed;
    Pressed = pointer;
}

GestureRecognizer::Result GestureRecognizer::Move(const PointerEvent& pointer)
{
    if (Current != Phase::Pressed)
        return Result::None; // idle, or already dragging
    if (Distance(pointer.Position, Pressed.Position) <= kDragDeadzonePixels)
        return Result::None;
    Current = Phase::Dragging;
    return Result::DragBegan;
}

GestureRecognizer::Result GestureRecognizer::Release(const PointerEvent& pointer)
{
    const Phase phase = Current;
    Current = Phase::Idle;
    if (phase != Phase::Pressed)
        return Result::None; // a drag's release is owned by its interaction

    const auto now = std::chrono::steady_clock::now();
    const bool quick = (now - LastClickTime) <= kDoubleClickTime;
    const bool nearby = Distance(pointer.Position, LastClickPosition) <= kDoubleClickPixels;
    LastClickPosition = pointer.Position;
    if (quick && nearby)
    {
        LastClickTime = {}; // consume, so a triple click isn't two double-clicks
        return Result::DoubleClicked;
    }
    LastClickTime = now;
    return Result::Clicked;
}

void GestureRecognizer::Reset()
{
    Current = Phase::Idle;
}
