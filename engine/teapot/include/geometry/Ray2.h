#pragma once

#include <cmath>
#include <concepts>
#include <ostream>
#include <type_traits>

#include <math/Vec.h>

//=============================================================================
// Ray2<T>
//
// 2D origin + direction ray.
//
// Convention:
//   Direction is expected to be unit-length. The type does not enforce this
//   automatically; call Normalized() if needed.
//
// Common aliases:
//   Ray2f, Ray2d
//=============================================================================
template <typename T = float>
struct Ray2
{
	static_assert(std::is_arithmetic_v<T>, "Ray2 component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	Vec<2, T> Origin;
	Vec<2, T> Direction = Vec<2, T>::Right();

	// -- Construction -------------------------------------------------------

	constexpr Ray2() = default;

	constexpr Ray2(const Vec<2, T>& origin, const Vec<2, T>& direction)
		: Origin(origin), Direction(direction) {}

	// -- Operations ---------------------------------------------------------

	constexpr Vec<2, T> PointAt(T distance) const
	{
		return Origin + Direction * distance;
	}

	Ray2 Normalized() const
		requires std::floating_point<T>
	{
		return Ray2(Origin, Direction.Normalized());
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Ray2& other) const
	{
		return Origin == other.Origin && Direction == other.Direction;
	}

	bool NearlyEquals(const Ray2& other, T epsilon = T{1e-6}) const
		requires std::floating_point<T>
	{
		for (int i = 0; i < 2; ++i)
		{
			if (std::abs(Origin.Data[i] - other.Origin.Data[i]) > epsilon)
				return false;
			if (std::abs(Direction.Data[i] - other.Direction.Data[i]) > epsilon)
				return false;
		}
		return true;
	}
};

// -- Stream output ----------------------------------------------------------

template <typename T>
std::ostream& operator<<(std::ostream& os, const Ray2<T>& ray)
{
	os << "{Origin: " << ray.Origin << ", Direction: " << ray.Direction << "}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Ray2f = Ray2<float>;
using Ray2d = Ray2<double>;
