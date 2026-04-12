#include <geometry/2d/Aabb2.h>

#include <algorithm>
#include <limits>
#include <ostream>

Aabb2::Aabb2(const Vec2& min, const Vec2& max)
	: Min(min), Max(max)
{
}

bool Aabb2::IsValid() const
{
	return Min[0] <= Max[0] && Min[1] <= Max[1];
}

Vec2 Aabb2::Center() const { return (Min + Max) / 2.0f; }
Vec2 Aabb2::Size() const { return Max - Min; }
Vec2 Aabb2::HalfExtent() const { return Size() / 2.0f; }
Vec2 Aabb2::Extent() const { return HalfExtent(); }

bool Aabb2::Contains(const Vec2& point) const
{
	return point[0] >= Min[0] && point[0] <= Max[0]
		&& point[1] >= Min[1] && point[1] <= Max[1];
}

bool Aabb2::Intersects(const Aabb2& other) const
{
	return Max[0] >= other.Min[0] && Min[0] <= other.Max[0]
		&& Max[1] >= other.Min[1] && Min[1] <= other.Max[1];
}

void Aabb2::ExpandToInclude(const Vec2& point)
{
	Min[0] = std::min(Min[0], point[0]);
	Min[1] = std::min(Min[1], point[1]);
	Max[0] = std::max(Max[0], point[0]);
	Max[1] = std::max(Max[1], point[1]);
}

void Aabb2::ExpandToInclude(const Aabb2& other)
{
	Min[0] = std::min(Min[0], other.Min[0]);
	Min[1] = std::min(Min[1], other.Min[1]);
	Max[0] = std::max(Max[0], other.Max[0]);
	Max[1] = std::max(Max[1], other.Max[1]);
}

Aabb2 Aabb2::ExpandedToInclude(const Vec2& point) const
{
	Aabb2 result = *this;
	result.ExpandToInclude(point);
	return result;
}

Aabb2 Aabb2::ExpandedToInclude(const Aabb2& other) const
{
	Aabb2 result = *this;
	result.ExpandToInclude(other);
	return result;
}

bool Aabb2::operator==(const Aabb2& other) const
{
	return Min == other.Min && Max == other.Max;
}

Aabb2 Aabb2::FromMinMax(const Vec2& min, const Vec2& max)
{
	return Aabb2(min, max);
}

Aabb2 Aabb2::FromCenterHalfExtent(const Vec2& center, const Vec2& halfExtent)
{
	return Aabb2(center - halfExtent, center + halfExtent);
}

Aabb2 Aabb2::Empty()
{
	return Aabb2(
		Vec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
		Vec2(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()));
}

std::ostream& operator<<(std::ostream& os, const Aabb2& box)
{
	os << "{Min: " << box.Min << ", Max: " << box.Max << "}";
	return os;
}
