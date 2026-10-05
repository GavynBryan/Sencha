#include "shell/SurfacePlacement.h"

#include <algorithm>
#include <limits>

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

bool Free(const Rect2d& rect, std::span<const Rect2d> taken)
{
    return std::ranges::none_of(taken, [&](const Rect2d& other) { return Overlaps(rect, other); });
}

Rect2d Inside(const Rect2d& rect, const Rect2d& bounds)
{
    return Rect2d(rect.Position + KeepInside(Vec2d(), rect, bounds), rect.Size);
}
} // namespace

Rect2d FindSpace(const Rect2d& home, std::span<const Rect2d> taken, const Rect2d& bounds, float step)
{
    const Rect2d start = Inside(home, bounds);
    if (Free(start, taken))
        return start;
    Rect2d best = start;
    float bestDistance = std::numeric_limits<float>::max();
    for (float y = bounds.Min().Y; y + home.Height() <= bounds.Max().Y; y += step)
        for (float x = bounds.Min().X; x + home.Width() <= bounds.Max().X; x += step)
        {
            const Rect2d candidate(Vec2d(x, y), home.Size);
            const float distance = (candidate.Position - start.Position).Magnitude();
            if (distance < bestDistance && Free(candidate, taken))
            {
                best = candidate;
                bestDistance = distance;
            }
        }
    return best;
}

Vec2d KeepInside(Vec2d offset, const Rect2d& placed, const Rect2d& bounds)
{
    return Vec2d(AxisInside(offset.X, placed.Min().X, placed.Max().X, bounds.Min().X, bounds.Max().X),
                 AxisInside(offset.Y, placed.Min().Y, placed.Max().Y, bounds.Min().Y, bounds.Max().Y));
}

Rect2d SurfaceLayout::Place(std::string_view key, SurfaceAnchor anchor, const Rect2d& home,
                            std::span<const Rect2d> taken, const Rect2d& bounds, float step)
{
    auto found = Entries.find(key);
    if (found == Entries.end())
        found = Entries.emplace(std::string(key), Entry{}).first;
    Entry& entry = found->second;
    if (!entry.Moved && anchor == SurfaceAnchor::Home)
        entry.Rect.Position = home.Position;
    else if (!entry.Moved && !entry.Showing)
        entry.Rect = FindSpace(home, taken, bounds, step);
    entry.Showing = true;
    if (!entry.Resized)
        entry.Rect.Size = home.Size;
    entry.Rect = Inside(entry.Rect, bounds);
    return entry.Rect;
}

void SurfaceLayout::Hide(std::string_view key)
{
    const auto found = Entries.find(key);
    if (found == Entries.end())
        return;
    if (found->second.Moved || found->second.Resized)
        found->second.Showing = false;
    else
        Entries.erase(found);
    if (PressedKey == key)
        Now = Phase::Resting;
}

void SurfaceLayout::PressHeader(std::string key, Vec2d pointer)
{
    if (!Entries.contains(key))
        return;
    Now = Phase::PressedHeader;
    PressedKey = std::move(key);
    PressedAt = pointer;
}

void SurfaceLayout::PressCorner(std::string key, Vec2d pointer)
{
    if (!Entries.contains(key))
        return;
    Now = Phase::PressedCorner;
    PressedKey = std::move(key);
    PressedAt = pointer;
}

void SurfaceLayout::Drag(Vec2d pointer, const Rect2d& bounds, Vec2d minimum)
{
    const auto found = Entries.find(PressedKey);
    if (Now == Phase::Resting || found == Entries.end())
        return;
    Entry& entry = found->second;
    if (Now == Phase::PressedHeader || Now == Phase::PressedCorner)
    {
        RectAtPress = entry.Rect;
        Now = Now == Phase::PressedHeader ? Phase::Moving : Phase::Sizing;
    }
    const Vec2d travel = pointer - PressedAt;
    if (Now == Phase::Moving)
    {
        entry.Rect = Rect2d(RectAtPress.Position + KeepInside(travel, RectAtPress, bounds), RectAtPress.Size);
        entry.Moved = true;
        return;
    }
    const Vec2d room = bounds.Max() - RectAtPress.Position;
    entry.Rect.Size = Vec2d(std::clamp(RectAtPress.Width() + travel.X, minimum.X, std::max(minimum.X, room.X)),
                            std::clamp(RectAtPress.Height() + travel.Y, minimum.Y, std::max(minimum.Y, room.Y)));
    entry.Resized = true;
}

void SurfaceLayout::Drop()
{
    Now = Phase::Resting;
}

void SurfaceLayout::Reset(std::string_view key)
{
    const auto found = Entries.find(key);
    if (found == Entries.end())
        return;
    found->second.Moved = false;
    found->second.Resized = false;
    found->second.Showing = false;
    if (PressedKey == key)
        Now = Phase::Resting;
}

bool SurfaceLayout::Sized(std::string_view key) const
{
    const auto found = Entries.find(key);
    return found != Entries.end() && found->second.Resized;
}
