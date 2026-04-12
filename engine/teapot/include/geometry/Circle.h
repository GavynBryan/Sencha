#pragma once

#include <cmath>
#include <concepts>
#include <ostream>
#include <type_traits>

#include <math/Vec.h>

//=============================================================================
// Circle<T>
//
// 2D center + radius bounded region.
//
// A circle is valid when Radius >= 0.
//
// Common aliases:
//   Circlef, Circled
//=============================================================================
template <typename T = float>
struct Circle
{
	static_assert(std::is_arithmetic_v<T>, "Circle component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	Vec<2, T> Center;
	T Radius = T{0};

	// -- Construction -------------------------------------------------------

	constexpr Circle() = default;

	constexpr Circle(const Vec<2, T>& center, T radius)
		: Center(center), Radius(radius) {}

	// -- Validation ---------------------------------------------------------

	constexpr bool IsValid() const
	{
		return Radius >= T{0};
	}

	// -- Queries ------------------------------------------------------------

	constexpr bool Contains(const Vec<2, T>& point) const
	{
		return Vec<2, T>::SqrDistance(Center, point) <= Radius * Radius;
	}

	constexpr bool Intersects(const Circle& other) const
	{
		T combinedRadius = Radius + other.Radius;
		return Vec<2, T>::SqrDistance(Center, other.Center) <= combinedRadius * combinedRadius;
	}

	// -- Expansion ----------------------------------------------------------

	constexpr void ExpandToInclude(const Vec<2, T>& point)
		requires std::floating_point<T>
	{
		T dist = Vec<2, T>::Distance(Center, point);
		if (dist > Radius)
		{
			Radius = dist;
		}
	}

	constexpr void ExpandToInclude(const Circle& other)
		requires std::floating_point<T>
	{
		T dist = Vec<2, T>::Distance(Center, other.Center) + other.Radius;
		if (dist > Radius)
		{
			Radius = dist;
		}
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Circle& other) const
	{
		return Center == other.Center && Radius == other.Radius;
	}

	bool NearlyEquals(const Circle& other, T epsilon = T{1e-6}) const
		requires std::floating_point<T>
	{
		for (int i = 0; i < 2; ++i)
		{
			if (std::abs(Center.Data[i] - other.Center.Data[i]) > epsilon)
				return false;
		}
		return std::abs(Radius - other.Radius) <= epsilon;
	}
};

// -- Stream output ----------------------------------------------------------

template <typename T>
std::ostream& operator<<(std::ostream& os, const Circle<T>& circle)
{
	os << "{Center: " << circle.Center << ", Radius: " << circle.Radius << "}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Circlef = Circle<float>;
using Circled = Circle<double>;
