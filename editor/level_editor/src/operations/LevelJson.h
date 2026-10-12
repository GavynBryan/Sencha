#pragma once

#include <core/json/JsonValue.h>
#include <math/Vec.h>

// How the level's operations spell numbers and points in their answers.

// Millimetres: finer than any authored position, and free of the float noise
// (0.10000000149) a client would pay to read.
[[nodiscard]] JsonValue MetresJson(double value);

// [x, y, z] in world metres.
[[nodiscard]] JsonValue PointJson(const Vec3d& point);

// [x, z] on the ground plane.
[[nodiscard]] JsonValue GroundJson(const Vec2d& point);

// From a value already checked as a three-number vector.
[[nodiscard]] Vec3d PointFromJson(const JsonValue& value);
