#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <ostream>
#include <type_traits>

#include <math/Vec.h>

//=============================================================================
// Plane<T>
//
// Infinite 3D plane.
//
// Equation convention:
//   Dot(Normal, P) + D = 0
//
// Normal points into the positive half-space. D is the negated signed
// distance from the origin along the normal.
//
// A point P is:
//   - in the positive half-space when Dot(Normal, P) + D > 0
//   - on the plane when Dot(Normal, P) + D == 0
//   - in the negative half-space when Dot(Normal, P) + D < 0
//
// Common aliases:
//   Planef, Planed
//=============================================================================
template <typename T = float>
struct Plane
{
	static_assert(std::is_arithmetic_v<T>, "Plane component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	Vec<3, T> Normal = Vec<3, T>::Up();
	T D = T{0};

	// -- Construction -------------------------------------------------------

	constexpr Plane() = default;

	constexpr Plane(const Vec<3, T>& normal, T d)
		: Normal(normal), D(d) {}

	// -- Factories ----------------------------------------------------------

	static constexpr Plane FromNormalAndDistance(const Vec<3, T>& normal, T d)
	{
		return Plane(normal, d);
	}

	static constexpr Plane FromNormalAndPoint(const Vec<3, T>& normal, const Vec<3, T>& point)
	{
		return Plane(normal, -normal.Dot(point));
	}

	// -- Operations ---------------------------------------------------------

	T SignedDistanceTo(const Vec<3, T>& point) const
	{
		return Normal.Dot(point) + D;
	}

	Plane Normalized() const
		requires std::floating_point<T>
	{
		T mag = Normal.Magnitude();
		assert(mag > T{0} && "Cannot normalize a plane with zero-length normal.");
		return Plane(Normal / mag, D / mag);
	}

	Vec<3, T> ClosestPoint(const Vec<3, T>& point) const
		requires std::floating_point<T>
	{
		return point - Normal * SignedDistanceTo(point);
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Plane& other) const
	{
		return Normal == other.Normal && D == other.D;
	}

	bool NearlyEquals(const Plane& other, T epsilon = T{1e-6}) const
		requires std::floating_point<T>
	{
		for (int i = 0; i < 3; ++i)
		{
			if (std::abs(Normal.Data[i] - other.Normal.Data[i]) > epsilon)
				return false;
		}
		return std::abs(D - other.D) <= epsilon;
	}
};

// -- Stream output ----------------------------------------------------------

template <typename T>
std::ostream& operator<<(std::ostream& os, const Plane<T>& plane)
{
	os << "{Normal: " << plane.Normal << ", D: " << plane.D << "}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Planef = Plane<float>;
using Planed = Plane<double>;
