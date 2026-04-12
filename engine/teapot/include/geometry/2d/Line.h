#pragma once

#include <iosfwd>

#include <math/Vec.h>

// Infinite 2D float line: Dot(Normal, P) + D = 0.
struct Line
{
	Vec2 Normal = Vec2::Up();
	float D = 0.0f;

	Line() = default;
	Line(const Vec2& normal, float d);

	static Line FromNormalAndDistance(const Vec2& normal, float d);
	static Line FromNormalAndPoint(const Vec2& normal, const Vec2& point);

	float SignedDistanceTo(const Vec2& point) const;
	Line Normalized() const;
	Vec2 ClosestPoint(const Vec2& point) const;

	bool operator==(const Line& other) const;
	bool NearlyEquals(const Line& other, float epsilon = 1e-6f) const;
};

std::ostream& operator<<(std::ostream& os, const Line& line);

using Linef = Line;
