#include <geometry/3d/Frustum.h>

#include <ostream>

Frustum Frustum::FromViewProjection(const Mat4& vp)
{
	Frustum f;

	f.Planes[Left] = Plane(
		Vec3(vp[3][0] + vp[0][0], vp[3][1] + vp[0][1], vp[3][2] + vp[0][2]),
		vp[3][3] + vp[0][3]).Normalized();

	f.Planes[Right] = Plane(
		Vec3(vp[3][0] - vp[0][0], vp[3][1] - vp[0][1], vp[3][2] - vp[0][2]),
		vp[3][3] - vp[0][3]).Normalized();

	f.Planes[Bottom] = Plane(
		Vec3(vp[3][0] + vp[1][0], vp[3][1] + vp[1][1], vp[3][2] + vp[1][2]),
		vp[3][3] + vp[1][3]).Normalized();

	f.Planes[Top] = Plane(
		Vec3(vp[3][0] - vp[1][0], vp[3][1] - vp[1][1], vp[3][2] - vp[1][2]),
		vp[3][3] - vp[1][3]).Normalized();

	f.Planes[Near] = Plane(
		Vec3(vp[3][0] + vp[2][0], vp[3][1] + vp[2][1], vp[3][2] + vp[2][2]),
		vp[3][3] + vp[2][3]).Normalized();

	f.Planes[Far] = Plane(
		Vec3(vp[3][0] - vp[2][0], vp[3][1] - vp[2][1], vp[3][2] - vp[2][2]),
		vp[3][3] - vp[2][3]).Normalized();

	return f;
}

bool Frustum::ContainsPoint(const Vec3& point) const
{
	for (int i = 0; i < PlaneCount; ++i)
	{
		if (Planes[i].SignedDistanceTo(point) < 0.0f)
			return false;
	}
	return true;
}

bool Frustum::IntersectsSphere(const Sphere& sphere) const
{
	for (int i = 0; i < PlaneCount; ++i)
	{
		if (Planes[i].SignedDistanceTo(sphere.Center) < -sphere.Radius)
			return false;
	}
	return true;
}

bool Frustum::IntersectsAabb(const Aabb3& box) const
{
	for (int i = 0; i < PlaneCount; ++i)
	{
		Vec3 pVertex;
		for (int axis = 0; axis < 3; ++axis)
		{
			pVertex[axis] = (Planes[i].Normal[axis] >= 0.0f)
				? box.Max[axis]
				: box.Min[axis];
		}

		if (Planes[i].SignedDistanceTo(pVertex) < 0.0f)
			return false;
	}
	return true;
}

std::ostream& operator<<(std::ostream& os, const Frustum& frustum)
{
	os << "{Frustum: [";
	const char* names[] = { "Left", "Right", "Bottom", "Top", "Near", "Far" };
	for (int i = 0; i < Frustum::PlaneCount; ++i)
	{
		if (i > 0) os << ", ";
		os << names[i] << ": " << frustum.Planes[i];
	}
	os << "]}";
	return os;
}
