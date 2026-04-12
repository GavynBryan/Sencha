#pragma once

#include <algorithm>
#include <limits>
#include <ostream>
#include <type_traits>

#include <math/Vec.h>

//=============================================================================
// Aabb2<T>
//
// 2D axis-aligned bounding box in an unspecified coordinate space.
//
// Min and Max are inclusive bounds. A box is valid when Min <= Max on every
// axis. The type does not imply world space, local space, rendering, collision,
// or ownership of any transform.
//
// Common aliases:
//   Aabb2f, Aabb2d, Aabb2i
//=============================================================================
template <typename T = float>
struct Aabb2
{
	static_assert(std::is_arithmetic_v<T>, "Aabb2 component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	static constexpr int DimensionCount = 2;
	Vec<2, T> Min;
	Vec<2, T> Max;

	// -- Construction -------------------------------------------------------

	constexpr Aabb2() = default;

	constexpr Aabb2(const Vec<2, T>& min, const Vec<2, T>& max)
		: Min(min), Max(max) {}

	// -- Validation ---------------------------------------------------------

	constexpr bool IsValid() const
	{
		return Min[0] <= Max[0] && Min[1] <= Max[1];
	}

	// -- Geometry -----------------------------------------------------------

	constexpr Vec<2, T> Center() const
	{
		return (Min + Max) / T{2};
	}

	constexpr Vec<2, T> Size() const
	{
		return Max - Min;
	}

	constexpr Vec<2, T> HalfExtent() const
	{
		return Size() / T{2};
	}

	constexpr Vec<2, T> Extent() const
	{
		return HalfExtent();
	}

	// -- Queries ------------------------------------------------------------

	constexpr bool Contains(const Vec<2, T>& point) const
	{
		return point[0] >= Min[0] && point[0] <= Max[0]
			&& point[1] >= Min[1] && point[1] <= Max[1];
	}

	constexpr bool Intersects(const Aabb2& other) const
	{
		return Max[0] >= other.Min[0] && Min[0] <= other.Max[0]
			&& Max[1] >= other.Min[1] && Min[1] <= other.Max[1];
	}

	// -- Expansion ----------------------------------------------------------

	constexpr void ExpandToInclude(const Vec<2, T>& point)
	{
		Min[0] = std::min(Min[0], point[0]);
		Min[1] = std::min(Min[1], point[1]);
		Max[0] = std::max(Max[0], point[0]);
		Max[1] = std::max(Max[1], point[1]);
	}

	constexpr void ExpandToInclude(const Aabb2& other)
	{
		Min[0] = std::min(Min[0], other.Min[0]);
		Min[1] = std::min(Min[1], other.Min[1]);
		Max[0] = std::max(Max[0], other.Max[0]);
		Max[1] = std::max(Max[1], other.Max[1]);
	}

	constexpr Aabb2 ExpandedToInclude(const Vec<2, T>& point) const
	{
		Aabb2 result = *this;
		result.ExpandToInclude(point);
		return result;
	}

	constexpr Aabb2 ExpandedToInclude(const Aabb2& other) const
	{
		Aabb2 result = *this;
		result.ExpandToInclude(other);
		return result;
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Aabb2& other) const
	{
		return Min == other.Min && Max == other.Max;
	}

	// -- Static factories ---------------------------------------------------

	static constexpr Aabb2 FromMinMax(const Vec<2, T>& min, const Vec<2, T>& max)
	{
		return Aabb2(min, max);
	}

	static constexpr Aabb2 FromCenterHalfExtent(const Vec<2, T>& center, const Vec<2, T>& halfExtent)
	{
		return Aabb2(center - halfExtent, center + halfExtent);
	}

	static constexpr Aabb2 Empty()
	{
		return Aabb2(
			Vec<2, T>(std::numeric_limits<T>::max(), std::numeric_limits<T>::max()),
			Vec<2, T>(std::numeric_limits<T>::lowest(), std::numeric_limits<T>::lowest())
		);
	}
};

// -- Stream output ----------------------------------------------------------

template <typename T>
std::ostream& operator<<(std::ostream& os, const Aabb2<T>& box)
{
	os << "{Min: " << box.Min << ", Max: " << box.Max << "}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Aabb2f = Aabb2<float>;
using Aabb2d = Aabb2<double>;
using Aabb2i = Aabb2<int>;
