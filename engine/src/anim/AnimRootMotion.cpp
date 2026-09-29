#include <anim/AnimRootMotion.h>

#include <algorithm>
#include <cmath>

namespace
{
    // `second` applied after `first`, in `first`'s starting frame.
    AnimRootDelta ComposeRootDeltas(const AnimRootDelta& first, const AnimRootDelta& second)
    {
        const float c = std::cos(first.Yaw);
        const float s = std::sin(first.Yaw);
        // Yaw about +Y: a positive turn takes -Z (forward) towards -X.
        return AnimRootDelta{ .X = first.X + c * second.X + s * second.Z,
                              .Z = first.Z - s * second.X + c * second.Z,
                              .Yaw = first.Yaw + second.Yaw };
    }

    // Within one pass over the clip, `from` <= `to`, both in [0, duration].
    AnimRootDelta Segment(const AnimationRootCurve& curve, float from, float to)
    {
        const AnimRootPose a = SampleAnimRootCurve(curve, from);
        const AnimRootPose b = SampleAnimRootCurve(curve, to);
        const float dx = b.X - a.X;
        const float dz = b.Z - a.Z;
        // Into the frame the root faced at `from`: undo its yaw.
        const float c = std::cos(a.Yaw);
        const float s = std::sin(a.Yaw);
        return AnimRootDelta{ .X = c * dx - s * dz, .Z = s * dx + c * dz, .Yaw = b.Yaw - a.Yaw };
    }
}

AnimRootPose SampleAnimRootCurve(const AnimationRootCurve& curve, float seconds)
{
    const std::vector<float>& times = curve.TimesSeconds;
    if (times.empty())
        return {};
    const auto at = [&](std::size_t key) {
        return AnimRootPose{ curve.Values[key * 3], curve.Values[key * 3 + 1], curve.Values[key * 3 + 2] };
    };
    if (seconds <= times.front())
        return at(0);
    if (seconds >= times.back())
        return at(times.size() - 1);
    const std::size_t next =
        static_cast<std::size_t>(std::upper_bound(times.begin(), times.end(), seconds) - times.begin());
    const std::size_t prev = next - 1;
    const float t = (seconds - times[prev]) / (times[next] - times[prev]);
    const AnimRootPose a = at(prev);
    const AnimRootPose b = at(next);
    return AnimRootPose{ a.X + (b.X - a.X) * t, a.Z + (b.Z - a.Z) * t, a.Yaw + (b.Yaw - a.Yaw) * t };
}

AnimRootDelta AnimRootMotionBetween(const AnimationRootCurve& curve, float durationSeconds, double fromSeconds,
                                    double toSeconds, bool cyclic)
{
    if (durationSeconds <= 0.0f || toSeconds <= fromSeconds)
        return {};
    const double length = durationSeconds;
    if (!cyclic)
    {
        const float from = static_cast<float>(std::clamp(fromSeconds, 0.0, length));
        const float to = static_cast<float>(std::clamp(toSeconds, 0.0, length));
        return Segment(curve, from, to);
    }

    const double firstLoop = std::floor(fromSeconds / length);
    const double lastLoop = std::floor(toSeconds / length);
    const float from = static_cast<float>(fromSeconds - firstLoop * length);
    const float to = static_cast<float>(toSeconds - lastLoop * length);
    if (firstLoop == lastLoop)
        return Segment(curve, from, to);

    AnimRootDelta total = Segment(curve, from, durationSeconds);
    const AnimRootDelta whole = Segment(curve, 0.0f, durationSeconds);
    for (double loop = firstLoop + 1.0; loop < lastLoop; loop += 1.0)
        total = ComposeRootDeltas(total, whole);
    return ComposeRootDeltas(total, Segment(curve, 0.0f, to));
}
