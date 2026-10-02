#include "shell/SurfacePlacement.h"

#include <algorithm>

namespace
{
bool Overlaps(const Rect2d& a, const Rect2d& b)
{
    return a.Min().X < b.Max().X && b.Min().X < a.Max().X && a.Min().Y < b.Max().Y && b.Min().Y < a.Max().Y;
}

float AxisInside(float offset, float min, float max, float boundsMin, float boundsMax)
{
    if (max - min > boundsMax - boundsMin)
        return boundsMin - min;
    return std::clamp(offset, boundsMin - min, boundsMax - max);
}
} // namespace

SurfaceClearance ClearOf(const Rect2d& natural, std::span<const SurfaceObstacle> obstacles, float gap,
                         SurfaceMinimum minimum)
{
    SurfaceClearance clearance;
    for (const SurfaceObstacle& obstacle : obstacles)
    {
        if (!obstacle.Area.IsValid() || !Overlaps(natural, obstacle.Area))
            continue;
        switch (obstacle.Yield)
        {
        case SurfaceYield::ShrinkLeft:
            clearance.Left = std::max(clearance.Left, obstacle.Area.Max().X + gap - natural.Min().X);
            break;
        case SurfaceYield::ShrinkRight:
            clearance.Right = std::max(clearance.Right, natural.Max().X - (obstacle.Area.Min().X - gap));
            break;
        case SurfaceYield::ShrinkBottom:
            clearance.Bottom = std::max(clearance.Bottom, natural.Max().Y - (obstacle.Area.Min().Y - gap));
            break;
        case SurfaceYield::ShiftRight:
            clearance.ShiftX = std::max(clearance.ShiftX, obstacle.Area.Max().X + gap - natural.Min().X);
            break;
        }
    }
    // Both sides give way in proportion until the surface is at its minimum.
    const float sides = clearance.Left + clearance.Right;
    const float widthToGive = std::max(0.0f, natural.Width() - minimum.Width);
    if (sides > widthToGive)
    {
        clearance.Left *= widthToGive / sides;
        clearance.Right *= widthToGive / sides;
    }
    clearance.Bottom = std::min(clearance.Bottom, std::max(0.0f, natural.Height() - minimum.Height));
    return clearance;
}

Rect2d NaturalPlace(const Rect2d& measured, const SurfaceClearance& applied)
{
    return Rect2d::FromMinMax(Vec2d(measured.Min().X - applied.Left, measured.Min().Y),
                              Vec2d(measured.Max().X + applied.Right, measured.Max().Y + applied.Bottom));
}

Vec2d KeepInside(Vec2d offset, const Rect2d& placed, const Rect2d& bounds)
{
    return Vec2d(AxisInside(offset.X, placed.Min().X, placed.Max().X, bounds.Min().X, bounds.Max().X),
                 AxisInside(offset.Y, placed.Min().Y, placed.Max().Y, bounds.Min().Y, bounds.Max().Y));
}

void SurfaceMoves::Press(std::string key, Vec2d pointer, const SurfaceClearance& clearance)
{
    Now = Phase::Pressed;
    PressedKey = std::move(key);
    PressedClearance = clearance;
    PressedAt = pointer;
}

void SurfaceMoves::Drag(Vec2d pointer, const Rect2d& placed, const Rect2d& bounds)
{
    if (Now == Phase::Resting)
        return;
    if (Now == Phase::Pressed)
    {
        const auto [move, added] =
            Moves.try_emplace(PressedKey, SurfaceMove{ .Offset = Vec2d(), .Clearance = PressedClearance });
        (void)added;
        OffsetAtPress = move->second.Offset;
        Now = Phase::Dragging;
    }
    if (const auto held = Moves.find(PressedKey); held != Moves.end())
        held->second.Offset = KeepInside(OffsetAtPress + (pointer - PressedAt), placed, bounds);
}

void SurfaceMoves::Drop()
{
    Now = Phase::Resting;
}

void SurfaceMoves::Reset(std::string_view key)
{
    if (const auto found = Moves.find(key); found != Moves.end())
        Moves.erase(found);
    if (PressedKey == key)
        Now = Phase::Resting;
}

const SurfaceMove* SurfaceMoves::Find(std::string_view key) const
{
    const auto found = Moves.find(key);
    return found != Moves.end() ? &found->second : nullptr;
}
