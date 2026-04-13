#include <geometry/2d/Circle.h>

#include <cmath>
#include <ostream>

Circle::Circle(const Vec2& center, float radius)
	: Center(center), Radius(radius)
{
}

bool Circle::IsValid() const { return Radius >= 0.0f; }

bool Circle::Contains(const Vec2& point) const
{
	return Vec2::SqrDistance(Center, point) <= Radius * Radius;
}

bool Circle::Intersects(const Circle& other) const
{
	const float combinedRadius = Radius + other.Radius;
	return Vec2::SqrDistance(Center, other.Center) <= combinedRadius * combinedRadius;
}

void Circle::ExpandToInclude(const Vec2& point)
{
	const float dist = Vec2::Distance(Center, point);
	if (dist > Radius)
		Radius = dist;
}

void Circle::ExpandToInclude(const Circle& other)
{
	const float dist = Vec2::Distance(Center, other.Center) + other.Radius;
	if (dist > Radius)
		Radius = dist;
}

bool Circle::operator==(const Circle& other) const
{
	return Center == other.Center && Radius == other.Radius;
}

bool Circle::NearlyEquals(const Circle& other, float epsilon) const
{
	return std::abs(Center.X - other.Center.X) <= epsilon
		&& std::abs(Center.Y - other.Center.Y) <= epsilon
		&& std::abs(Radius - other.Radius) <= epsilon;
}

std::ostream& operator<<(std::ostream& os, const Circle& circle)
{
	os << "{Center: " << circle.Center << ", Radius: " << circle.Radius << "}";
	return os;
}
