#pragma once

#include <algorithm>
#include <limits>
#include <ostream>
#include <type_traits>

#include <math/Vec.h>

//=============================================================================
// Aabb3<T>
//
// 3D axis-aligned bounding box in an unspecified coordinate space.
//
// Min and Max are inclusive bounds. A box is valid when Min <= Max on every
// axis. The type does not imply world space, local space, rendering, collision,
// or ownership of any transform.
//
// Common aliases:
//   Aabb3f, Aabb3d, Aabb3i
//=============================================================================
template <typename T = float>
struct Aabb3
{
	static_assert(std::is_arithmetic_v<T>, "Aabb3 component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	static constexpr int DimensionCount = 3;
	Vec<3, T> Min;
	Vec<3, T> Max;

	// -- Construction -------------------------------------------------------

	constexpr Aabb3() = default;

	constexpr Aabb3(const Vec<3, T>& min, const Vec<3, T>& max)
		: Min(min), Max(max) {}

	// -- Validation ---------------------------------------------------------

	constexpr bool IsValid() const
	{
		return Min[0] <= Max[0] && Min[1] <= Max[1] && Min[2] <= Max[2];
	}

	// -- Geometry -----------------------------------------------------------

	constexpr Vec<3, T> Center() const
	{
		return (Min + Max) / T{2};
	}

	constexpr Vec<3, T> Size() const
	{
		return Max - Min;
	}

	constexpr Vec<3, T> HalfExtent() const
	{
		return Size() / T{2};
	}

	constexpr Vec<3, T> Extent() const
	{
		return HalfExtent();
	}

	// -- Queries ------------------------------------------------------------

	constexpr bool Contains(const Vec<3, T>& point) const
	{
		return point[0] >= Min[0] && point[0] <= Max[0]
			&& point[1] >= Min[1] && point[1] <= Max[1]
			&& point[2] >= Min[2] && point[2] <= Max[2];
	}

	constexpr bool Intersects(const Aabb3& other) const
	{
		return Max[0] >= other.Min[0] && Min[0] <= other.Max[0]
			&& Max[1] >= other.Min[1] && Min[1] <= other.Max[1]
			&& Max[2] >= other.Min[2] && Min[2] <= other.Max[2];
	}

	// -- Expansion ----------------------------------------------------------

	constexpr void ExpandToInclude(const Vec<3, T>& point)
	{
		Min[0] = std::min(Min[0], point[0]);
		Min[1] = std::min(Min[1], point[1]);
		Min[2] = std::min(Min[2], point[2]);
		Max[0] = std::max(Max[0], point[0]);
		Max[1] = std::max(Max[1], point[1]);
		Max[2] = std::max(Max[2], point[2]);
	}

	constexpr void ExpandToInclude(const Aabb3& other)
	{
		Min[0] = std::min(Min[0], other.Min[0]);
		Min[1] = std::min(Min[1], other.Min[1]);
		Min[2] = std::min(Min[2], other.Min[2]);
		Max[0] = std::max(Max[0], other.Max[0]);
		Max[1] = std::max(Max[1], other.Max[1]);
		Max[2] = std::max(Max[2], other.Max[2]);
	}

	constexpr Aabb3 ExpandedToInclude(const Vec<3, T>& point) const
	{
		Aabb3 result = *this;
		result.ExpandToInclude(point);
		return result;
	}

	constexpr Aabb3 ExpandedToInclude(const Aabb3& other) const
	{
		Aabb3 result = *this;
		result.ExpandToInclude(other);
		return result;
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Aabb3& other) const
	{
		return Min == other.Min && Max == other.Max;
	}

	// -- Static factories ---------------------------------------------------

	static constexpr Aabb3 FromMinMax(const Vec<3, T>& min, const Vec<3, T>& max)
	{
		return Aabb3(min, max);
	}

	static constexpr Aabb3 FromCenterHalfExtent(const Vec<3, T>& center, const Vec<3, T>& halfExtent)
	{
		return Aabb3(center - halfExtent, center + halfExtent);
	}

	static constexpr Aabb3 Empty()
	{
		return Aabb3(
			Vec<3, T>(
				std::numeric_limits<T>::max(),
				std::numeric_limits<T>::max(),
				std::numeric_limits<T>::max()),
			Vec<3, T>(
				std::numeric_limits<T>::lowest(),
				std::numeric_limits<T>::lowest(),
				std::numeric_limits<T>::lowest())
		);
	}
};

// -- Stream output ----------------------------------------------------------

template <typename T>
std::ostream& operator<<(std::ostream& os, const Aabb3<T>& box)
{
	os << "{Min: " << box.Min << ", Max: " << box.Max << "}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Aabb3f = Aabb3<float>;
using Aabb3d = Aabb3<double>;
using Aabb3i = Aabb3<int>;
