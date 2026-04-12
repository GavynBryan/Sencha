#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 3D float origin + direction ray.
struct Ray3
{
	Vec3 Origin;
	Vec3 Direction = Vec3::Forward();

	Ray3() = default;
	Ray3(const Vec3& origin, const Vec3& direction);

	Vec3 PointAt(float distance) const;
	Ray3 Normalized() const;

	bool operator==(const Ray3& other) const;
	bool NearlyEquals(const Ray3& other, float epsilon = 1e-6f) const;
};

std::ostream& operator<<(std::ostream& os, const Ray3& ray);

using Ray3f = Ray3;
