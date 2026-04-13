#include <math/geometry/2d/Aabb2d.h>

#include <algorithm>
#include <limits>
#include <ostream>

Aabb2d::Aabb2d(const Vec2d& min, const Vec2d& max)
	: Min(min), Max(max)
{
}

bool Aabb2d::IsValid() const
{
	return Min[0] <= Max[0] && Min[1] <= Max[1];
}

Vec2d Aabb2d::Center() const { return (Min + Max) / 2.0f; }
Vec2d Aabb2d::Size() const { return Max - Min; }
Vec2d Aabb2d::HalfExtent() const { return Size() / 2.0f; }
Vec2d Aabb2d::Extent() const { return HalfExtent(); }

bool Aabb2d::Contains(const Vec2d& point) const
{
	return point[0] >= Min[0] && point[0] <= Max[0]
		&& point[1] >= Min[1] && point[1] <= Max[1];
}

bool Aabb2d::Intersects(const Aabb2d& other) const
{
	return Max[0] >= other.Min[0] && Min[0] <= other.Max[0]
		&& Max[1] >= other.Min[1] && Min[1] <= other.Max[1];
}

void Aabb2d::ExpandToInclude(const Vec2d& point)
{
	Min[0] = std::min(Min[0], point[0]);
	Min[1] = std::min(Min[1], point[1]);
	Max[0] = std::max(Max[0], point[0]);
	Max[1] = std::max(Max[1], point[1]);
}

void Aabb2d::ExpandToInclude(const Aabb2d& other)
{
	Min[0] = std::min(Min[0], other.Min[0]);
	Min[1] = std::min(Min[1], other.Min[1]);
	Max[0] = std::max(Max[0], other.Max[0]);
	Max[1] = std::max(Max[1], other.Max[1]);
}

Aabb2d Aabb2d::ExpandedToInclude(const Vec2d& point) const
{
	Aabb2d result = *this;
	result.ExpandToInclude(point);
	return result;
}

Aabb2d Aabb2d::ExpandedToInclude(const Aabb2d& other) const
{
	Aabb2d result = *this;
	result.ExpandToInclude(other);
	return result;
}

bool Aabb2d::operator==(const Aabb2d& other) const
{
	return Min == other.Min && Max == other.Max;
}

Aabb2d Aabb2d::FromMinMax(const Vec2d& min, const Vec2d& max)
{
	return Aabb2d(min, max);
}

Aabb2d Aabb2d::FromCenterHalfExtent(const Vec2d& center, const Vec2d& halfExtent)
{
	return Aabb2d(center - halfExtent, center + halfExtent);
}

Aabb2d Aabb2d::Empty()
{
	return Aabb2d(
		Vec2d(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
		Vec2d(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()));
}

std::ostream& operator<<(std::ostream& os, const Aabb2d& box)
{
	os << "{Min: " << box.Min << ", Max: " << box.Max << "}";
	return os;
}
