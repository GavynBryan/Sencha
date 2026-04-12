#include <geometry/2d/Line.h>

#include <cassert>
#include <cmath>
#include <ostream>

Line::Line(const Vec2& normal, float d)
	: Normal(normal), D(d)
{
}

Line Line::FromNormalAndDistance(const Vec2& normal, float d)
{
	return Line(normal, d);
}

Line Line::FromNormalAndPoint(const Vec2& normal, const Vec2& point)
{
	return Line(normal, -normal.Dot(point));
}

float Line::SignedDistanceTo(const Vec2& point) const
{
	return Normal.Dot(point) + D;
}

Line Line::Normalized() const
{
	const float mag = Normal.Magnitude();
	assert(mag > 0.0f && "Cannot normalize a line with zero-length normal.");
	return Line(Normal / mag, D / mag);
}

Vec2 Line::ClosestPoint(const Vec2& point) const
{
	return point - Normal * SignedDistanceTo(point);
}

bool Line::operator==(const Line& other) const
{
	return Normal == other.Normal && D == other.D;
}

bool Line::NearlyEquals(const Line& other, float epsilon) const
{
	return std::abs(Normal.Data[0] - other.Normal.Data[0]) <= epsilon
		&& std::abs(Normal.Data[1] - other.Normal.Data[1]) <= epsilon
		&& std::abs(D - other.D) <= epsilon;
}

std::ostream& operator<<(std::ostream& os, const Line& line)
{
	os << "{Normal: " << line.Normal << ", D: " << line.D << "}";
	return os;
}
