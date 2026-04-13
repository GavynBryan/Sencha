#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 3D float axis-aligned bounding box.
struct Aabb3d
{
	static constexpr int DimensionCount = 3;

	Vec3d Min;
	Vec3d Max;

	Aabb3d() = default;
	Aabb3d(const Vec3d& min, const Vec3d& max);

	bool IsValid() const;
	Vec3d Center() const;
	Vec3d Size() const;
	Vec3d HalfExtent() const;
	Vec3d Extent() const;
	bool Contains(const Vec3d& point) const;
	bool Intersects(const Aabb3d& other) const;
	void ExpandToInclude(const Vec3d& point);
	void ExpandToInclude(const Aabb3d& other);
	Aabb3d ExpandedToInclude(const Vec3d& point) const;
	Aabb3d ExpandedToInclude(const Aabb3d& other) const;

	bool operator==(const Aabb3d& other) const;

	static Aabb3d FromMinMax(const Vec3d& min, const Vec3d& max);
	static Aabb3d FromCenterHalfExtent(const Vec3d& center, const Vec3d& halfExtent);
	static Aabb3d Empty();
};

std::ostream& operator<<(std::ostream& os, const Aabb3d& box);

using Aabb3f = Aabb3d;
