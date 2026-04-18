#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 2D float center + radius bounded region.
struct Circle
{
	Vec2d Center;
	float Radius = 0.0f;

	Circle() = default;
	Circle(const Vec2d& center, float radius);

	bool IsValid() const;
	bool Contains(const Vec2d& point) const;
	bool Intersects(const Circle& other) const;
	void ExpandToInclude(const Vec2d& point);
	void ExpandToInclude(const Circle& other);

	bool operator==(const Circle& other) const;
	bool NearlyEquals(const Circle& other, float epsilon = 1e-6f) const;
};

std::ostream& operator<<(std::ostream& os, const Circle& circle);

using Circlef = Circle;
