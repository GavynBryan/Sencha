#pragma once

#include <math/Vec.h>

#include <cstdint>

// A brush-local axis, the vocabulary architectural modifiers speak: "along X",
// "across Y". Local so a rotated entity keeps its own front, side, and up.
enum class LocalAxis : std::uint8_t
{
    X,
    Y,
    Z,
};

[[nodiscard]] inline Vec3d LocalAxisVector(LocalAxis axis)
{
    switch (axis)
    {
    case LocalAxis::Y: return Vec3d{ 0.0f, 1.0f, 0.0f };
    case LocalAxis::Z: return Vec3d{ 0.0f, 0.0f, 1.0f };
    case LocalAxis::X:
    default:           return Vec3d{ 1.0f, 0.0f, 0.0f };
    }
}

[[nodiscard]] inline float LocalAxisComponent(Vec3d v, LocalAxis axis)
{
    switch (axis)
    {
    case LocalAxis::Y: return v.Y;
    case LocalAxis::Z: return v.Z;
    case LocalAxis::X:
    default:           return v.X;
    }
}
