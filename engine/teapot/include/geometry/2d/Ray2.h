#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 2D float origin + direction ray.
struct Ray2
{
	Vec2 Origin;
	Vec2 Direction = Vec2::Right();

	Ray2() = default;
	Ray2(const Vec2& origin, const Vec2& direction);

	Vec2 PointAt(float distance) const;
	Ray2 Normalized() const;

	bool operator==(const Ray2& other) const;
	bool NearlyEquals(const Ray2& other, float epsilon = 1e-6f) const;
};

std::ostream& operator<<(std::ostream& os, const Ray2& ray);

using Ray2f = Ray2;
