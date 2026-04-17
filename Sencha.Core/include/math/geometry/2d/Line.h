#pragma once

#include <iosfwd>

#include <math/Vec.h>

// Infinite 2D float line: Dot(Normal, P) + D = 0.
struct Line
{
	Vec2d Normal = Vec2d::Up();
	float D = 0.0f;

	Line() = default;
	Line(const Vec2d& normal, float d);

	static Line FromNormalAndDistance(const Vec2d& normal, float d);
	static Line FromNormalAndPoint(const Vec2d& normal, const Vec2d& point);

	float SignedDistanceTo(const Vec2d& point) const;
	Line Normalized() const;
	Vec2d ClosestPoint(const Vec2d& point) const;

	bool operator==(const Line& other) const;
	bool NearlyEquals(const Line& other, float epsilon = 1e-6f) const;
};

std::ostream& operator<<(std::ostream& os, const Line& line);

using Linef = Line;
