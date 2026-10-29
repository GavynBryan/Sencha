#pragma once

#include <cstdint>

#include <math/Vec.h>

// Scatters a unit `direction` uniformly over the cone of `halfAngleDegrees`
// around it, from `seed` alone, so every machine given the seed scatters it the
// same way. The cone stops at a right angle; zero or less is the direction
// itself. What a projectile's spread and a hitscan's both mean.
[[nodiscard]] Vec3d ScatterInCone(const Vec3d& direction, float halfAngleDegrees, std::uint64_t seed);
