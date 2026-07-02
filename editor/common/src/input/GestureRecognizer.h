#pragma once

#include "InputEvent.h"

#include <chrono>

// Turns the raw press/move/release stream into semantic gestures for the active
// tool. The click-vs-drag deadzone and double-click timing live here and nowhere
// else, so no tool reimplements them. Pure: depends only on InputEvent types. The
// dispatcher drives it and acts on the result (click -> tool, drag -> interaction).
class GestureRecognizer
{
public:
    enum class Result
    {
        None,
        DragBegan,
        Clicked,
        DoubleClicked,
    };

    void Press(const PointerEvent& pointer);
    [[nodiscard]] Result Move(const PointerEvent& pointer);
    [[nodiscard]] Result Release(const PointerEvent& pointer);
    void Reset();

    [[nodiscard]] bool Active() const { return Current != Phase::Idle; }
    [[nodiscard]] const PointerEvent& PressPointer() const { return Pressed; }

private:
    enum class Phase
    {
        Idle,
        Pressed,
        Dragging,
    };

    Phase Current = Phase::Idle;
    PointerEvent Pressed = {};
    std::chrono::steady_clock::time_point LastClickTime{};
    ImVec2 LastClickPosition = {};
};
