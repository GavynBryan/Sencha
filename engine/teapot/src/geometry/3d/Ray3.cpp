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
	return std::abs(Origin.Data[0] - other.Origin.Data[0]) <= epsilon
		&& std::abs(Origin.Data[1] - other.Origin.Data[1]) <= epsilon
		&& std::abs(Origin.Data[2] - other.Origin.Data[2]) <= epsilon
		&& std::abs(Direction.Data[0] - other.Direction.Data[0]) <= epsilon
		&& std::abs(Direction.Data[1] - other.Direction.Data[1]) <= epsilon
		&& std::abs(Direction.Data[2] - other.Direction.Data[2]) <= epsilon;
}

std::ostream& operator<<(std::ostream& os, const Ray3& ray)
{
	os << "{Origin: " << ray.Origin << ", Direction: " << ray.Direction << "}";
	return os;
}
