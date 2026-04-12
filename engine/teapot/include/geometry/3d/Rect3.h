#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 3D float rectangular region defined by position and size.
struct Rect3
{
	Vec3 Position;
	Vec3 Size;

	Rect3() = default;
	Rect3(const Vec3& position, const Vec3& size);
	Rect3(float x, float y, float z, float width, float height, float depth);

	bool IsValid() const;
	Vec3 Min() const;
	Vec3 Max() const;
	Vec3 Center() const;
	float Width() const;
	float Height() const;
	float Depth() const;
	float Volume() const;
	bool Contains(const Vec3& point) const;
	bool Intersects(const Rect3& other) const;

	bool operator==(const Rect3& other) const;

	static Rect3 FromMinMax(const Vec3& min, const Vec3& max);
	static Rect3 FromCenterSize(const Vec3& center, const Vec3& size);
};

std::ostream& operator<<(std::ostream& os, const Rect3& rect);

using Rect3f = Rect3;
