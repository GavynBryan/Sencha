#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 2D float origin + direction ray.
struct Ray2d
{
	Vec2d Origin;
	Vec2d Direction = Vec2d::Right();

	Ray2d() = default;
	Ray2d(const Vec2d& origin, const Vec2d& direction);

	Vec2d PointAt(float distance) const;
	Ray2d Normalized() const;

	bool operator==(const Ray2d& other) const;
	bool NearlyEquals(const Ray2d& other, float epsilon = 1e-6f) const;
};

std::ostream& operator<<(std::ostream& os, const Ray2d& ray);

using Ray2f = Ray2d;
