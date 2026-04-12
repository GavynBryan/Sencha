#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 3D float axis-aligned bounding box.
struct Aabb3
{
	static constexpr int DimensionCount = 3;

	Vec3 Min;
	Vec3 Max;

	Aabb3() = default;
	Aabb3(const Vec3& min, const Vec3& max);

	bool IsValid() const;
	Vec3 Center() const;
	Vec3 Size() const;
	Vec3 HalfExtent() const;
	Vec3 Extent() const;
	bool Contains(const Vec3& point) const;
	bool Intersects(const Aabb3& other) const;
	void ExpandToInclude(const Vec3& point);
	void ExpandToInclude(const Aabb3& other);
	Aabb3 ExpandedToInclude(const Vec3& point) const;
	Aabb3 ExpandedToInclude(const Aabb3& other) const;

	bool operator==(const Aabb3& other) const;

	static Aabb3 FromMinMax(const Vec3& min, const Vec3& max);
	static Aabb3 FromCenterHalfExtent(const Vec3& center, const Vec3& halfExtent);
	static Aabb3 Empty();
};

std::ostream& operator<<(std::ostream& os, const Aabb3& box);

using Aabb3f = Aabb3;
