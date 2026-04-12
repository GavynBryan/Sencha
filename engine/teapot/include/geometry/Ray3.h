#pragma once

#include <cmath>
#include <concepts>
#include <ostream>
#include <type_traits>

#include <math/Vec.h>

//=============================================================================
// Ray3<T>
//
// 3D origin + direction ray.
//
// Convention:
//   Direction is expected to be unit-length. The type does not enforce this
//   automatically; call Normalized() if needed.
//
// Common aliases:
//   Ray3f, Ray3d
//=============================================================================
template <typename T = float>
struct Ray3
{
	static_assert(std::is_arithmetic_v<T>, "Ray3 component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	Vec<3, T> Origin;
	Vec<3, T> Direction = Vec<3, T>::Forward();

	// -- Construction -------------------------------------------------------

	constexpr Ray3() = default;

	constexpr Ray3(const Vec<3, T>& origin, const Vec<3, T>& direction)
		: Origin(origin), Direction(direction) {}

	// -- Operations ---------------------------------------------------------

	constexpr Vec<3, T> PointAt(T distance) const
	{
		return Origin + Direction * distance;
	}

	Ray3 Normalized() const
		requires std::floating_point<T>
	{
		return Ray3(Origin, Direction.Normalized());
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Ray3& other) const
	{
		return Origin == other.Origin && Direction == other.Direction;
	}

	bool NearlyEquals(const Ray3& other, T epsilon = T{1e-6}) const
		requires std::floating_point<T>
	{
		for (int i = 0; i < 3; ++i)
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
std::ostream& operator<<(std::ostream& os, const Ray3<T>& ray)
{
	os << "{Origin: " << ray.Origin << ", Direction: " << ray.Direction << "}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Ray3f = Ray3<float>;
using Ray3d = Ray3<double>;
