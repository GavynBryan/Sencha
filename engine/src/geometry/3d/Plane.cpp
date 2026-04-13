#include <geometry/3d/Plane.h>

#include <cassert>
#include <cmath>
#include <ostream>

Plane::Plane(const Vec3d& normal, float d)
	: Normal(normal), D(d)
{
}

Plane Plane::FromNormalAndDistance(const Vec3d& normal, float d)
{
	return Plane(normal, d);
}

Plane Plane::FromNormalAndPoint(const Vec3d& normal, const Vec3d& point)
{
	return Plane(normal, -normal.Dot(point));
}

float Plane::SignedDistanceTo(const Vec3d& point) const
{
	return Normal.Dot(point) + D;
}

Plane Plane::Normalized() const
{
	const float mag = Normal.Magnitude();
	assert(mag > 0.0f && "Cannot normalize a plane with zero-length normal.");
	return Plane(Normal / mag, D / mag);
}

Vec3d Plane::ClosestPoint(const Vec3d& point) const
{
	return point - Normal * SignedDistanceTo(point);
}

bool Plane::operator==(const Plane& other) const
{
	return Normal == other.Normal && D == other.D;
}

bool Plane::NearlyEquals(const Plane& other, float epsilon) const
{
	return std::abs(Normal.X - other.Normal.X) <= epsilon
		&& std::abs(Normal.Y - other.Normal.Y) <= epsilon
		&& std::abs(Normal.Z - other.Normal.Z) <= epsilon
		&& std::abs(D - other.D) <= epsilon;
}

std::ostream& operator<<(std::ostream& os, const Plane& plane)
{
	os << "{Normal: " << plane.Normal << ", D: " << plane.D << "}";
	return os;
}
