#include "presenters/StageOrbit.h"

namespace
{
// Pixels: a smaller stage has no picture worth drawing.
constexpr float kMinimumStageSize = 8.0f;
// How far a press may move and still be a click, in pixels.
constexpr float kClickSlop = 3.0f;
// Radians per pixel dragged.
constexpr float kOrbitPerPixel = 0.01f;
} // namespace

bool CanShowStage(const Rect2d& content)
{
    return content.Width() >= kMinimumStageSize && content.Height() >= kMinimumStageSize;
}

void StageOrbitDrag::Press(Vec2d at)
{
    State = Phase::Pressed;
    PressedAt = at;
}

std::optional<StageOrbitDrag::Turn> StageOrbitDrag::Move(Vec2d at, Vec2d moved)
{
    if (State == Phase::Pressed)
    {
        const Vec2d travel = at - PressedAt;
        if (travel.X * travel.X + travel.Y * travel.Y > kClickSlop * kClickSlop)
            State = Phase::Orbiting;
    }
    if (State != Phase::Orbiting)
        return std::nullopt;
    return Turn{ .Yaw = moved.X * kOrbitPerPixel, .Pitch = -moved.Y * kOrbitPerPixel };
}

bool StageOrbitDrag::Release()
{
    const bool click = State == Phase::Pressed;
    State = Phase::Idle;
    return click;
}
