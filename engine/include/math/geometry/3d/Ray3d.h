#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 3D float origin + direction ray.
struct Ray3d
{
	Vec3d Origin;
	Vec3d Direction = Vec3d::Forward();

	Ray3d() = default;
	Ray3d(const Vec3d& origin, const Vec3d& direction);

	Vec3d PointAt(float distance) const;
	Ray3d Normalized() const;

	bool operator==(const Ray3d& other) const;
	bool NearlyEquals(const Ray3d& other, float epsilon = 1e-6f) const;
};

std::ostream& operator<<(std::ostream& os, const Ray3d& ray);

using Ray3f = Ray3d;
