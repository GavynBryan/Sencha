#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 2D float rectangular region defined by position and size.
struct Rect2d
{
	Vec2d Position;
	Vec2d Size;

	Rect2d() = default;
	Rect2d(const Vec2d& position, const Vec2d& size);
	Rect2d(float x, float y, float width, float height);

	bool IsValid() const;
	Vec2d Min() const;
	Vec2d Max() const;
	Vec2d Center() const;
	float Width() const;
	float Height() const;
	float Area() const;
	bool Contains(const Vec2d& point) const;
	bool Intersects(const Rect2d& other) const;

	bool operator==(const Rect2d& other) const;

	static Rect2d FromMinMax(const Vec2d& min, const Vec2d& max);
	static Rect2d FromCenterSize(const Vec2d& center, const Vec2d& size);
};

std::ostream& operator<<(std::ostream& os, const Rect2d& rect);

using Rect2f = Rect2d;
