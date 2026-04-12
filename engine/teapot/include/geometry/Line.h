#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <ostream>
#include <type_traits>

#include <math/Vec.h>

//=============================================================================
// Line<T>
//
// Infinite 2D line.
//
// Equation convention:
//   Dot(Normal, P) + D = 0
//
// Normal points into the positive half-space. D is the negated signed
// distance from the origin along the normal.
//
// A point P is:
//   - in the positive half-space when Dot(Normal, P) + D > 0
//   - on the line when Dot(Normal, P) + D == 0
//   - in the negative half-space when Dot(Normal, P) + D < 0
//
// Common aliases:
//   Linef, Lined
//=============================================================================
template <typename T = float>
struct Line
{
	static_assert(std::is_arithmetic_v<T>, "Line component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	Vec<2, T> Normal = Vec<2, T>::Up();
	T D = T{0};

	// -- Construction -------------------------------------------------------

	constexpr Line() = default;

	constexpr Line(const Vec<2, T>& normal, T d)
		: Normal(normal), D(d) {}

	// -- Factories ----------------------------------------------------------

	static constexpr Line FromNormalAndDistance(const Vec<2, T>& normal, T d)
	{
		return Line(normal, d);
	}

	static constexpr Line FromNormalAndPoint(const Vec<2, T>& normal, const Vec<2, T>& point)
	{
		return Line(normal, -normal.Dot(point));
	}

	// -- Operations ---------------------------------------------------------

	T SignedDistanceTo(const Vec<2, T>& point) const
	{
		return Normal.Dot(point) + D;
	}

	Line Normalized() const
		requires std::floating_point<T>
	{
		T mag = Normal.Magnitude();
		assert(mag > T{0} && "Cannot normalize a line with zero-length normal.");
		return Line(Normal / mag, D / mag);
	}

	Vec<2, T> ClosestPoint(const Vec<2, T>& point) const
		requires std::floating_point<T>
	{
		return point - Normal * SignedDistanceTo(point);
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Line& other) const
	{
		return Normal == other.Normal && D == other.D;
	}

	bool NearlyEquals(const Line& other, T epsilon = T{1e-6}) const
		requires std::floating_point<T>
	{
		for (int i = 0; i < 2; ++i)
		{
			if (std::abs(Normal.Data[i] - other.Normal.Data[i]) > epsilon)
				return false;
		}
		return std::abs(D - other.D) <= epsilon;
	}
};

// -- Stream output ----------------------------------------------------------

template <typename T>
std::ostream& operator<<(std::ostream& os, const Line<T>& line)
{
	os << "{Normal: " << line.Normal << ", D: " << line.D << "}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Linef = Line<float>;
using Lined = Line<double>;
