#include <math/geometry/3d/Sphere.h>

#include <math/geometry/3d/Aabb3d.h>

#include <algorithm>
#include <cmath>
#include <ostream>

Sphere::Sphere(const Vec3d& center, float radius)
	: Center(center), Radius(radius)
{
}

bool Sphere::IsValid() const { return Radius >= 0.0f; }

bool Sphere::Contains(const Vec3d& point) const
{
	return Vec3d::SqrDistance(Center, point) <= Radius * Radius;
}

bool Sphere::Intersects(const Sphere& other) const
{
	const float combinedRadius = Radius + other.Radius;
	return Vec3d::SqrDistance(Center, other.Center) <= combinedRadius * combinedRadius;
}

bool Sphere::Intersects(const Aabb3d& box) const
{
	const Vec3d closest(
		std::clamp(Center.X, box.Min.X, box.Max.X),
		std::clamp(Center.Y, box.Min.Y, box.Max.Y),
		std::clamp(Center.Z, box.Min.Z, box.Max.Z));
	return Vec3d::SqrDistance(Center, closest) <= Radius * Radius;
}

void Sphere::ExpandToInclude(const Vec3d& point)
{
	const float dist = Vec3d::Distance(Center, point);
	if (dist > Radius)
		Radius = dist;
}

void Sphere::ExpandToInclude(const Sphere& other)
{
	const float dist = Vec3d::Distance(Center, other.Center) + other.Radius;
	if (dist > Radius)
		Radius = dist;
}

bool Sphere::operator==(const Sphere& other) const
{
	return Center == other.Center && Radius == other.Radius;
}

bool Sphere::NearlyEquals(const Sphere& other, float epsilon) const
{
	return std::abs(Center.X - other.Center.X) <= epsilon
		&& std::abs(Center.Y - other.Center.Y) <= epsilon
		&& std::abs(Center.Z - other.Center.Z) <= epsilon
		&& std::abs(Radius - other.Radius) <= epsilon;
}

std::ostream& operator<<(std::ostream& os, const Sphere& sphere)
{
	os << "{Center: " << sphere.Center << ", Radius: " << sphere.Radius << "}";
	return os;
}
