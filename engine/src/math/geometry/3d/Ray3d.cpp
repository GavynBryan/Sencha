#include <math/geometry/3d/Ray3d.h>

#include <cmath>
#include <ostream>

Ray3d::Ray3d(const Vec3d& origin, const Vec3d& direction)
	: Origin(origin), Direction(direction)
{
}

Vec3d Ray3d::PointAt(float distance) const
{
	return Origin + Direction * distance;
}

Ray3d Ray3d::Normalized() const
{
	return Ray3d(Origin, Direction.Normalized());
}

bool Ray3d::operator==(const Ray3d& other) const
{
	return Origin == other.Origin && Direction == other.Direction;
}

bool Ray3d::NearlyEquals(const Ray3d& other, float epsilon) const
{
	return std::abs(Origin.X - other.Origin.X) <= epsilon
		&& std::abs(Origin.Y - other.Origin.Y) <= epsilon
		&& std::abs(Origin.Z - other.Origin.Z) <= epsilon
		&& std::abs(Direction.X - other.Direction.X) <= epsilon
		&& std::abs(Direction.Y - other.Direction.Y) <= epsilon
		&& std::abs(Direction.Z - other.Direction.Z) <= epsilon;
}

std::ostream& operator<<(std::ostream& os, const Ray3d& ray)
{
	os << "{Origin: " << ray.Origin << ", Direction: " << ray.Direction << "}";
	return os;
}
