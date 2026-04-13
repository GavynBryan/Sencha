#include <geometry/3d/Ray3.h>

#include <cmath>
#include <ostream>

Ray3::Ray3(const Vec3& origin, const Vec3& direction)
	: Origin(origin), Direction(direction)
{
}

Vec3 Ray3::PointAt(float distance) const
{
	return Origin + Direction * distance;
}

Ray3 Ray3::Normalized() const
{
	return Ray3(Origin, Direction.Normalized());
}

bool Ray3::operator==(const Ray3& other) const
{
	return Origin == other.Origin && Direction == other.Direction;
}

bool Ray3::NearlyEquals(const Ray3& other, float epsilon) const
{
	return std::abs(Origin.X - other.Origin.X) <= epsilon
		&& std::abs(Origin.Y - other.Origin.Y) <= epsilon
		&& std::abs(Origin.Z - other.Origin.Z) <= epsilon
		&& std::abs(Direction.X - other.Direction.X) <= epsilon
		&& std::abs(Direction.Y - other.Direction.Y) <= epsilon
		&& std::abs(Direction.Z - other.Direction.Z) <= epsilon;
}

std::ostream& operator<<(std::ostream& os, const Ray3& ray)
{
	os << "{Origin: " << ray.Origin << ", Direction: " << ray.Direction << "}";
	return os;
}
