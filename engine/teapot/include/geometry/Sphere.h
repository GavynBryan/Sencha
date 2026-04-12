#pragma once

#include <cmath>
#include <concepts>
#include <ostream>
#include <type_traits>

#include <math/Vec.h>

//=============================================================================
// Sphere<T>
//
// 3D center + radius bounded region.
//
// A sphere is valid when Radius >= 0.
//
// Common aliases:
//   Spheref, Sphered
//=============================================================================
template <typename T = float>
struct Sphere
{
	static_assert(std::is_arithmetic_v<T>, "Sphere component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	Vec<3, T> Center;
	T Radius = T{0};

	// -- Construction -------------------------------------------------------

	constexpr Sphere() = default;

	constexpr Sphere(const Vec<3, T>& center, T radius)
		: Center(center), Radius(radius) {}

	// -- Validation ---------------------------------------------------------

	constexpr bool IsValid() const
	{
		return Radius >= T{0};
	}

	// -- Queries ------------------------------------------------------------

	constexpr bool Contains(const Vec<3, T>& point) const
	{
		return Vec<3, T>::SqrDistance(Center, point) <= Radius * Radius;
	}

	constexpr bool Intersects(const Sphere& other) const
	{
		T combinedRadius = Radius + other.Radius;
		return Vec<3, T>::SqrDistance(Center, other.Center) <= combinedRadius * combinedRadius;
	}

	// -- Expansion ----------------------------------------------------------

	constexpr void ExpandToInclude(const Vec<3, T>& point)
		requires std::floating_point<T>
	{
		T dist = Vec<3, T>::Distance(Center, point);
		if (dist > Radius)
		{
			Radius = dist;
		}
	}

	constexpr void ExpandToInclude(const Sphere& other)
		requires std::floating_point<T>
	{
		T dist = Vec<3, T>::Distance(Center, other.Center) + other.Radius;
		if (dist > Radius)
		{
			Radius = dist;
		}
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Sphere& other) const
	{
		return Center == other.Center && Radius == other.Radius;
	}

	bool NearlyEquals(const Sphere& other, T epsilon = T{1e-6}) const
		requires std::floating_point<T>
	{
		for (int i = 0; i < 3; ++i)
		{
			if (std::abs(Center.Data[i] - other.Center.Data[i]) > epsilon)
				return false;
		}
		return std::abs(Radius - other.Radius) <= epsilon;
	}
};

// -- Stream output ----------------------------------------------------------

template <typename T>
std::ostream& operator<<(std::ostream& os, const Sphere<T>& sphere)
{
	os << "{Center: " << sphere.Center << ", Radius: " << sphere.Radius << "}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Spheref = Sphere<float>;
using Sphered = Sphere<double>;
