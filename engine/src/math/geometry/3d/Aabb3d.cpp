#include <math/geometry/3d/Aabb3d.h>

#include <algorithm>
#include <limits>
#include <ostream>

Aabb3d::Aabb3d(const Vec3d& min, const Vec3d& max)
	: Min(min), Max(max)
{
}

bool Aabb3d::IsValid() const
{
	return Min[0] <= Max[0] && Min[1] <= Max[1] && Min[2] <= Max[2];
}

Vec3d Aabb3d::Center() const { return (Min + Max) / 2.0f; }
Vec3d Aabb3d::Size() const { return Max - Min; }
Vec3d Aabb3d::HalfExtent() const { return Size() / 2.0f; }
Vec3d Aabb3d::Extent() const { return HalfExtent(); }

bool Aabb3d::Contains(const Vec3d& point) const
{
	return point[0] >= Min[0] && point[0] <= Max[0]
		&& point[1] >= Min[1] && point[1] <= Max[1]
		&& point[2] >= Min[2] && point[2] <= Max[2];
}

bool Aabb3d::Intersects(const Aabb3d& other) const
{
	return Max[0] >= other.Min[0] && Min[0] <= other.Max[0]
		&& Max[1] >= other.Min[1] && Min[1] <= other.Max[1]
		&& Max[2] >= other.Min[2] && Min[2] <= other.Max[2];
}

void Aabb3d::ExpandToInclude(const Vec3d& point)
{
	Min[0] = std::min(Min[0], point[0]);
	Min[1] = std::min(Min[1], point[1]);
	Min[2] = std::min(Min[2], point[2]);
	Max[0] = std::max(Max[0], point[0]);
	Max[1] = std::max(Max[1], point[1]);
	Max[2] = std::max(Max[2], point[2]);
}

void Aabb3d::ExpandToInclude(const Aabb3d& other)
{
	Min[0] = std::min(Min[0], other.Min[0]);
	Min[1] = std::min(Min[1], other.Min[1]);
	Min[2] = std::min(Min[2], other.Min[2]);
	Max[0] = std::max(Max[0], other.Max[0]);
	Max[1] = std::max(Max[1], other.Max[1]);
	Max[2] = std::max(Max[2], other.Max[2]);
}

Aabb3d Aabb3d::ExpandedToInclude(const Vec3d& point) const
{
	Aabb3d result = *this;
	result.ExpandToInclude(point);
	return result;
}

Aabb3d Aabb3d::ExpandedToInclude(const Aabb3d& other) const
{
	Aabb3d result = *this;
	result.ExpandToInclude(other);
	return result;
}

bool Aabb3d::operator==(const Aabb3d& other) const
{
	return Min == other.Min && Max == other.Max;
}

Aabb3d Aabb3d::FromMinMax(const Vec3d& min, const Vec3d& max)
{
	return Aabb3d(min, max);
}

Aabb3d Aabb3d::FromCenterHalfExtent(const Vec3d& center, const Vec3d& halfExtent)
{
	return Aabb3d(center - halfExtent, center + halfExtent);
}

Aabb3d Aabb3d::Empty()
{
	return Aabb3d(
		Vec3d(
			std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max()),
		Vec3d(
			std::numeric_limits<float>::lowest(),
			std::numeric_limits<float>::lowest(),
			std::numeric_limits<float>::lowest()));
}

std::ostream& operator<<(std::ostream& os, const Aabb3d& box)
{
	os << "{Min: " << box.Min << ", Max: " << box.Max << "}";
	return os;
}

std::vector<Vec3d> ClipConvexPolygon(std::span<const Vec3d> polygon, const Aabb3d& box)
{
	std::vector<Vec3d> kept(polygon.begin(), polygon.end());
	std::vector<Vec3d> next;
	for (int axis = 0; axis < Aabb3d::DimensionCount && !kept.empty(); ++axis)
		for (const float sign : { -1.0f, 1.0f })
		{
			// Inside is sign * (p[axis] - bound) <= 0 for this face of the box.
			const float bound = sign < 0.0f ? box.Min[axis] : box.Max[axis];
			const auto outside = [&](const Vec3d& point) { return sign * (point[axis] - bound); };
			next.clear();
			for (std::size_t i = 0; i < kept.size(); ++i)
			{
				const Vec3d& from = kept[i];
				const Vec3d& to = kept[(i + 1) % kept.size()];
				const float a = outside(from);
				const float b = outside(to);
				if (a <= 0.0f)
					next.push_back(from);
				if ((a < 0.0f && b > 0.0f) || (a > 0.0f && b < 0.0f))
					next.push_back(from + (to - from) * (a / (a - b)));
			}
			kept.swap(next);
			if (kept.empty())
				break;
		}
	return kept;
}
