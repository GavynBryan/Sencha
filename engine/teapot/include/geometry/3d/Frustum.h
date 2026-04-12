#pragma once

#include <iosfwd>

#include <geometry/3d/Aabb3.h>
#include <geometry/3d/Plane.h>
#include <geometry/3d/Sphere.h>
#include <math/Mat.h>

// Six-plane float frustum for culling and containment tests.
struct Frustum
{
	static constexpr int PlaneCount = 6;

	enum Side { Left = 0, Right, Bottom, Top, Near, Far };

	Plane Planes[PlaneCount];

	Frustum() = default;

	static Frustum FromViewProjection(const Mat4& vp);

	bool ContainsPoint(const Vec3& point) const;
	bool IntersectsSphere(const Sphere& sphere) const;
	bool IntersectsAabb(const Aabb3& box) const;
};

std::ostream& operator<<(std::ostream& os, const Frustum& frustum);

using Frustumf = Frustum;
