#pragma once

#include <iosfwd>

#include <math/Vec.h>

// Infinite 3D float plane: Dot(Normal, P) + D = 0.
struct Plane
{
	Vec3d Normal = Vec3d::Up();
	float D = 0.0f;

	Plane() = default;
	Plane(const Vec3d& normal, float d);

	static Plane FromNormalAndDistance(const Vec3d& normal, float d);
	static Plane FromNormalAndPoint(const Vec3d& normal, const Vec3d& point);

	float SignedDistanceTo(const Vec3d& point) const;
	Plane Normalized() const;
	Vec3d ClosestPoint(const Vec3d& point) const;

	bool operator==(const Plane& other) const;
	bool NearlyEquals(const Plane& other, float epsilon = 1e-6f) const;
};

std::ostream& operator<<(std::ostream& os, const Plane& plane);

using Planef = Plane;
