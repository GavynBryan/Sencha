#include <debug/DebugLines.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

void DebugLineList::Clear()
{
    TestedLines.clear();
    OnTopLines.clear();
}

void DebugLineList::AddTested(const Vec3d& from, const Vec3d& to, const Vec4& color)
{
    TestedLines.push_back(DebugLine{ from, to, color });
}

void DebugLineList::AddOnTop(const Vec3d& from, const Vec3d& to, const Vec4& color)
{
    OnTopLines.push_back(DebugLine{ from, to, color });
}

DebugLineChannelId DebugLineChannels::Channel(std::string_view name)
{
    const auto found = std::ranges::find(Names, name);
    if (found != Names.end())
        return DebugLineChannelId{ static_cast<std::uint32_t>(found - Names.begin()) + 1 };
    Names.emplace_back(name);
    Lists.emplace_back();
    return DebugLineChannelId{ static_cast<std::uint32_t>(Names.size()) };
}

DebugLineList& DebugLineChannels::Lines(DebugLineChannelId channel)
{
    assert(channel.IsValid() && channel.Value <= Lists.size());
    return Lists[channel.Value - 1];
}

void AppendCone(DebugLineList& lines, const Vec3d& apex, const Vec3d& axis, float halfAngle,
                float length, const Vec4& color)
{
    const float axisLength = axis.Magnitude();
    if (axisLength <= 0.0f || length <= 0.0f)
        return;
    const Vec3d forward = axis / axisLength;
    const Vec3d helper = std::abs(forward.Y) < 0.99f ? Vec3d::Up() : Vec3d(1.0f, 0.0f, 0.0f);
    const Vec3d right = forward.Cross(helper).Normalized();
    const Vec3d up = right.Cross(forward);

    const Vec3d centre = apex + forward * (length * std::cos(halfAngle));
    const float radius = length * std::sin(halfAngle);
    constexpr int kRimSegments = 24;
    Vec3d previous = centre + right * radius;
    for (int segment = 1; segment <= kRimSegments; ++segment)
    {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(segment) / kRimSegments;
        const Vec3d point = centre + (right * std::cos(angle) + up * std::sin(angle)) * radius;
        lines.AddTested(previous, point, color);
        if (segment % (kRimSegments / 4) == 0)
            lines.AddTested(apex, point, color);
        previous = point;
    }
}

void AppendMarker(DebugLineList& lines, const Vec3d& point, float size, const Vec4& color)
{
    const float half = size * 0.5f;
    lines.AddOnTop(point - Vec3d(half, 0.0f, 0.0f), point + Vec3d(half, 0.0f, 0.0f), color);
    lines.AddOnTop(point - Vec3d(0.0f, half, 0.0f), point + Vec3d(0.0f, half, 0.0f), color);
    lines.AddOnTop(point - Vec3d(0.0f, 0.0f, half), point + Vec3d(0.0f, 0.0f, half), color);
}
