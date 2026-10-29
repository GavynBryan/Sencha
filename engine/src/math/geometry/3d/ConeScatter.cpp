#include <math/geometry/3d/ConeScatter.h>

#include <core/hash/SplitMix64.h>

#include <algorithm>
#include <cmath>
#include <numbers>

Vec3d ScatterInCone(const Vec3d& direction, float halfAngleDegrees, std::uint64_t seed)
{
	if (halfAngleDegrees <= 0.0f)
		return direction;
	constexpr float pi = std::numbers::pi_v<float>;
	// Two well-mixed draws from one seed.
	auto next = [&seed]() { return static_cast<float>(SplitMix64(seed) >> 40) * (1.0f / 16777216.0f); };
	const float maxAngle = std::min(halfAngleDegrees, 90.0f) * (pi / 180.0f);
	// Uniform over the cap: cos(theta) uniform between cos(max) and one.
	const float cosTheta = 1.0f - next() * (1.0f - std::cos(maxAngle));
	const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
	const float phi = next() * 2.0f * pi;
	Vec3d right = direction.Cross(Vec3d::Up());
	right = right.SqrMagnitude() < 1.0e-6f ? Vec3d::Right() : right.Normalized();
	const Vec3d up = right.Cross(direction).Normalized();
	return (direction * cosTheta + right * (std::cos(phi) * sinTheta) + up * (std::sin(phi) * sinTheta)).Normalized();
}
