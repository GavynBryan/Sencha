#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 2D float axis-aligned bounding box.
struct Aabb2
{
	static constexpr int DimensionCount = 2;

	Vec2 Min;
	Vec2 Max;

	Aabb2() = default;
	Aabb2(const Vec2& min, const Vec2& max);

	bool IsValid() const;
	Vec2 Center() const;
	Vec2 Size() const;
	Vec2 HalfExtent() const;
	Vec2 Extent() const;
	bool Contains(const Vec2& point) const;
	bool Intersects(const Aabb2& other) const;
	void ExpandToInclude(const Vec2& point);
	void ExpandToInclude(const Aabb2& other);
	Aabb2 ExpandedToInclude(const Vec2& point) const;
	Aabb2 ExpandedToInclude(const Aabb2& other) const;

	bool operator==(const Aabb2& other) const;

	static Aabb2 FromMinMax(const Vec2& min, const Vec2& max);
	static Aabb2 FromCenterHalfExtent(const Vec2& center, const Vec2& halfExtent);
	static Aabb2 Empty();
};

std::ostream& operator<<(std::ostream& os, const Aabb2& box);

using Aabb2f = Aabb2;
