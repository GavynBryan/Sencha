#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 2D float axis-aligned bounding box.
struct Aabb2d
{
	static constexpr int DimensionCount = 2;

	Vec2d Min;
	Vec2d Max;

	Aabb2d() = default;
	Aabb2d(const Vec2d& min, const Vec2d& max);

	bool IsValid() const;
	Vec2d Center() const;
	Vec2d Size() const;
	Vec2d HalfExtent() const;
	Vec2d Extent() const;
	bool Contains(const Vec2d& point) const;
	bool Intersects(const Aabb2d& other) const;
	void ExpandToInclude(const Vec2d& point);
	void ExpandToInclude(const Aabb2d& other);
	Aabb2d ExpandedToInclude(const Vec2d& point) const;
	Aabb2d ExpandedToInclude(const Aabb2d& other) const;

	bool operator==(const Aabb2d& other) const;

	static Aabb2d FromMinMax(const Vec2d& min, const Vec2d& max);
	static Aabb2d FromCenterHalfExtent(const Vec2d& center, const Vec2d& halfExtent);
	static Aabb2d Empty();
};

std::ostream& operator<<(std::ostream& os, const Aabb2d& box);

using Aabb2f = Aabb2d;
