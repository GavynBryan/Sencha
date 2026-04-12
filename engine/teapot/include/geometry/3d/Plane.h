#pragma once

#include <iosfwd>

#include <math/Vec.h>

// Infinite 3D float plane: Dot(Normal, P) + D = 0.
struct Plane
{
	Vec3 Normal = Vec3::Up();
	float D = 0.0f;

	Plane() = default;
	Plane(const Vec3& normal, float d);

	static Plane FromNormalAndDistance(const Vec3& normal, float d);
	static Plane FromNormalAndPoint(const Vec3& normal, const Vec3& point);

	float SignedDistanceTo(const Vec3& point) const;
	Plane Normalized() const;
	Vec3 ClosestPoint(const Vec3& point) const;

	bool operator==(const Plane& other) const;
	bool NearlyEquals(const Plane& other, float epsilon = 1e-6f) const;
};

std::ostream& operator<<(std::ostream& os, const Plane& plane);

using Planef = Plane;
