#include <geometry/3d/Aabb3.h>

#include <algorithm>
#include <limits>
#include <ostream>

Aabb3::Aabb3(const Vec3& min, const Vec3& max)
	: Min(min), Max(max)
{
}

bool Aabb3::IsValid() const
{
	return Min[0] <= Max[0] && Min[1] <= Max[1] && Min[2] <= Max[2];
}

Vec3 Aabb3::Center() const { return (Min + Max) / 2.0f; }
Vec3 Aabb3::Size() const { return Max - Min; }
Vec3 Aabb3::HalfExtent() const { return Size() / 2.0f; }
Vec3 Aabb3::Extent() const { return HalfExtent(); }

bool Aabb3::Contains(const Vec3& point) const
{
	return point[0] >= Min[0] && point[0] <= Max[0]
		&& point[1] >= Min[1] && point[1] <= Max[1]
		&& point[2] >= Min[2] && point[2] <= Max[2];
}

bool Aabb3::Intersects(const Aabb3& other) const
{
	return Max[0] >= other.Min[0] && Min[0] <= other.Max[0]
		&& Max[1] >= other.Min[1] && Min[1] <= other.Max[1]
		&& Max[2] >= other.Min[2] && Min[2] <= other.Max[2];
}

void Aabb3::ExpandToInclude(const Vec3& point)
{
	Min[0] = std::min(Min[0], point[0]);
	Min[1] = std::min(Min[1], point[1]);
	Min[2] = std::min(Min[2], point[2]);
	Max[0] = std::max(Max[0], point[0]);
	Max[1] = std::max(Max[1], point[1]);
	Max[2] = std::max(Max[2], point[2]);
}

void Aabb3::ExpandToInclude(const Aabb3& other)
{
	Min[0] = std::min(Min[0], other.Min[0]);
	Min[1] = std::min(Min[1], other.Min[1]);
	Min[2] = std::min(Min[2], other.Min[2]);
	Max[0] = std::max(Max[0], other.Max[0]);
	Max[1] = std::max(Max[1], other.Max[1]);
	Max[2] = std::max(Max[2], other.Max[2]);
}

Aabb3 Aabb3::ExpandedToInclude(const Vec3& point) const
{
	Aabb3 result = *this;
	result.ExpandToInclude(point);
	return result;
}

Aabb3 Aabb3::ExpandedToInclude(const Aabb3& other) const
{
	Aabb3 result = *this;
	result.ExpandToInclude(other);
	return result;
}

bool Aabb3::operator==(const Aabb3& other) const
{
	return Min == other.Min && Max == other.Max;
}

Aabb3 Aabb3::FromMinMax(const Vec3& min, const Vec3& max)
{
	return Aabb3(min, max);
}

Aabb3 Aabb3::FromCenterHalfExtent(const Vec3& center, const Vec3& halfExtent)
{
	return Aabb3(center - halfExtent, center + halfExtent);
}

Aabb3 Aabb3::Empty()
{
	return Aabb3(
		Vec3(
			std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max()),
		Vec3(
			std::numeric_limits<float>::lowest(),
			std::numeric_limits<float>::lowest(),
			std::numeric_limits<float>::lowest()));
}

std::ostream& operator<<(std::ostream& os, const Aabb3& box)
{
	os << "{Min: " << box.Min << ", Max: " << box.Max << "}";
	return os;
}
