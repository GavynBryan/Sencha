#pragma once

#include <iosfwd>

#include <math/Vec.h>

// 3D float center + radius bounded region.
struct Sphere
{
	Vec3d Center;
	float Radius = 0.0f;

	Sphere() = default;
	Sphere(const Vec3d& center, float radius);

	bool IsValid() const;
	bool Contains(const Vec3d& point) const;
	bool Intersects(const Sphere& other) const;
	void ExpandToInclude(const Vec3d& point);
	void ExpandToInclude(const Sphere& other);

	bool operator==(const Sphere& other) const;
	bool NearlyEquals(const Sphere& other, float epsilon = 1e-6f) const;
};

std::ostream& operator<<(std::ostream& os, const Sphere& sphere);

using Spheref = Sphere;
