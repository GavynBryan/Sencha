#include <geometry/2d/Ray2.h>

#include <cmath>
#include <ostream>

Ray2::Ray2(const Vec2& origin, const Vec2& direction)
	: Origin(origin), Direction(direction)
{
}

Vec2 Ray2::PointAt(float distance) const
{
	return Origin + Direction * distance;
}

Ray2 Ray2::Normalized() const
{
	return Ray2(Origin, Direction.Normalized());
}

bool Ray2::operator==(const Ray2& other) const
{
	return Origin == other.Origin && Direction == other.Direction;
}

bool Ray2::NearlyEquals(const Ray2& other, float epsilon) const
{
	return std::abs(Origin.X - other.Origin.X) <= epsilon
		&& std::abs(Origin.Y - other.Origin.Y) <= epsilon
		&& std::abs(Direction.X - other.Direction.X) <= epsilon
		&& std::abs(Direction.Y - other.Direction.Y) <= epsilon;
}

std::ostream& operator<<(std::ostream& os, const Ray2& ray)
{
	os << "{Origin: " << ray.Origin << ", Direction: " << ray.Direction << "}";
	return os;
}
