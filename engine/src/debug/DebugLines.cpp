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
    const float spread = std::clamp(halfAngle, 0.0f, std::numbers::pi_v<float>);

    // A point on the range's sphere, `polar` from straight ahead, `azimuth` around.
    const auto onSphere = [&](float polar, float azimuth) {
        const Vec3d around = right * std::cos(azimuth) + up * std::sin(azimuth);
        return apex + (forward * std::cos(polar) + around * std::sin(polar)) * length;
    };
    const auto circle = [&](float polar) {
        constexpr int kSegments = 24;
        for (int segment = 0; segment < kSegments; ++segment)
        {
            const float step = 2.0f * std::numbers::pi_v<float> / kSegments;
            lines.AddTested(onSphere(polar, step * static_cast<float>(segment)),
                            onSphere(polar, step * static_cast<float>(segment + 1)), color);
        }
    };

    // The rim, unless it has closed to a point behind the apex.
    if (std::sin(spread) > 1e-3f)
        circle(spread);
    // Past a right angle the widest part of what is seen is no longer the rim.
    if (spread > std::numbers::pi_v<float> / 2.0f)
        circle(std::numbers::pi_v<float> / 2.0f);

    constexpr float kArcStep = std::numbers::pi_v<float> / 12.0f;
    const int arcSegments = std::max(1, static_cast<int>(std::ceil(spread / kArcStep)));
    for (int meridian = 0; meridian < 4; ++meridian)
    {
        const float azimuth = std::numbers::pi_v<float> / 2.0f * static_cast<float>(meridian);
        lines.AddTested(apex, onSphere(spread, azimuth), color);
        for (int segment = 0; segment < arcSegments; ++segment)
        {
            const float from = spread * static_cast<float>(segment) / static_cast<float>(arcSegments);
            const float to = spread * static_cast<float>(segment + 1) / static_cast<float>(arcSegments);
            lines.AddTested(onSphere(from, azimuth), onSphere(to, azimuth), color);
        }
    }
}

void AppendMarker(DebugLineList& lines, const Vec3d& point, float size, const Vec4& color)
{
    const float half = size * 0.5f;
    lines.AddOnTop(point - Vec3d(half, 0.0f, 0.0f), point + Vec3d(half, 0.0f, 0.0f), color);
    lines.AddOnTop(point - Vec3d(0.0f, half, 0.0f), point + Vec3d(0.0f, half, 0.0f), color);
    lines.AddOnTop(point - Vec3d(0.0f, 0.0f, half), point + Vec3d(0.0f, 0.0f, half), color);
}
