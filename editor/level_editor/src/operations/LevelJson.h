#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/DataSchema.h>
#include <math/Vec.h>

#include <string>
#include <string_view>

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

// The eight-way compass direction from one point to another on the ground
// plane, north being -Z and east +X: n, ne, e, se, s, sw, w, nw, or none.
[[nodiscard]] std::string_view CompassBearing(const Vec3d& from, const Vec3d& to);
[[nodiscard]] DataFieldSchema CompassBearingField(std::string key, std::string summary);
