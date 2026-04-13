#include <geometry/2d/Ray2d.h>

#include <cmath>
#include <ostream>

Ray2d::Ray2d(const Vec2d& origin, const Vec2d& direction)
	: Origin(origin), Direction(direction)
{
}

Vec2d Ray2d::PointAt(float distance) const
{
	return Origin + Direction * distance;
}

Ray2d Ray2d::Normalized() const
{
	return Ray2d(Origin, Direction.Normalized());
}

bool Ray2d::operator==(const Ray2d& other) const
{
	return Origin == other.Origin && Direction == other.Direction;
}

bool Ray2d::NearlyEquals(const Ray2d& other, float epsilon) const
{
	return std::abs(Origin.X - other.Origin.X) <= epsilon
		&& std::abs(Origin.Y - other.Origin.Y) <= epsilon
		&& std::abs(Direction.X - other.Direction.X) <= epsilon
		&& std::abs(Direction.Y - other.Direction.Y) <= epsilon;
}

std::ostream& operator<<(std::ostream& os, const Ray2d& ray)
{
	os << "{Origin: " << ray.Origin << ", Direction: " << ray.Direction << "}";
	return os;
}
