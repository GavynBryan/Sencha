#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 2D float rectangular region defined by position and size.
struct Rect2
{
	Vec2 Position;
	Vec2 Size;

	Rect2() = default;
	Rect2(const Vec2& position, const Vec2& size);
	Rect2(float x, float y, float width, float height);

	bool IsValid() const;
	Vec2 Min() const;
	Vec2 Max() const;
	Vec2 Center() const;
	float Width() const;
	float Height() const;
	float Area() const;
	bool Contains(const Vec2& point) const;
	bool Intersects(const Rect2& other) const;

	bool operator==(const Rect2& other) const;

	static Rect2 FromMinMax(const Vec2& min, const Vec2& max);
	static Rect2 FromCenterSize(const Vec2& center, const Vec2& size);
};

std::ostream& operator<<(std::ostream& os, const Rect2& rect);

using Rect2f = Rect2;
