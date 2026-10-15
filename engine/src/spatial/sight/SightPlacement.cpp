#include <spatial/sight/SightPlacement.h>

#include <cmath>
#include <numbers>

namespace
{
    bool IsFinite(const Vec3d& value)
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }
}

bool IsUsableSightSensor(const SightSensor& sensor)
{
    return std::isfinite(sensor.Range) && sensor.Range > 0.0f
        && std::isfinite(sensor.HalfAngle) && sensor.HalfAngle > 0.0f
        && sensor.HalfAngle <= std::numbers::pi_v<float>
        && IsFinite(sensor.EyeOffset);
}

std::optional<SightObserver> PlaceSightObserver(const SightSensor& sensor, const Transform3f& world)
{
    if (!IsUsableSightSensor(sensor))
        return std::nullopt;
    return SightObserver{ .Eye = world.TransformPoint(sensor.EyeOffset),
                          .Forward = world.Forward(),
                          .HalfAngle = sensor.HalfAngle,
                          .Range = sensor.Range };
}

Vec3d PlaceSightTarget(const SightTarget& target, const Transform3f& world)
{
    return world.TransformPoint(target.SampleOffset);
}
