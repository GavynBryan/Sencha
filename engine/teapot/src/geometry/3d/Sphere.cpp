#include <geometry/3d/Sphere.h>

#include <cmath>
#include <ostream>

Sphere::Sphere(const Vec3& center, float radius)
	: Center(center), Radius(radius)
{
}

bool Sphere::IsValid() const { return Radius >= 0.0f; }

bool Sphere::Contains(const Vec3& point) const
{
	return Vec3::SqrDistance(Center, point) <= Radius * Radius;
}

bool Sphere::Intersects(const Sphere& other) const
{
	const float combinedRadius = Radius + other.Radius;
	return Vec3::SqrDistance(Center, other.Center) <= combinedRadius * combinedRadius;
}

void Sphere::ExpandToInclude(const Vec3& point)
{
	const float dist = Vec3::Distance(Center, point);
	if (dist > Radius)
		Radius = dist;
}

void Sphere::ExpandToInclude(const Sphere& other)
{
	const float dist = Vec3::Distance(Center, other.Center) + other.Radius;
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
