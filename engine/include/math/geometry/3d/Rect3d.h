#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 3D float rectangular region defined by position and size.
struct Rect3d
{
	Vec3d Position;
	Vec3d Size;

	Rect3d() = default;
	Rect3d(const Vec3d& position, const Vec3d& size);
	Rect3d(float x, float y, float z, float width, float height, float depth);

	bool IsValid() const;
	Vec3d Min() const;
	Vec3d Max() const;
	Vec3d Center() const;
	float Width() const;
	float Height() const;
	float Depth() const;
	float Volume() const;
	bool Contains(const Vec3d& point) const;
	bool Intersects(const Rect3d& other) const;

	bool operator==(const Rect3d& other) const;

	static Rect3d FromMinMax(const Vec3d& min, const Vec3d& max);
	static Rect3d FromCenterSize(const Vec3d& center, const Vec3d& size);
};

std::ostream& operator<<(std::ostream& os, const Rect3d& rect);

using Rect3f = Rect3d;
