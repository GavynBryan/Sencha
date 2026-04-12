#pragma once

#include <algorithm>
#include <limits>
#include <ostream>
#include <type_traits>

#include <math/Vec.h>

//=============================================================================
// Aabb<Dimensions, T>
//
// Axis-aligned bounding box in an unspecified coordinate space.
//
// Min and Max are inclusive bounds. A box is valid when Min <= Max on every
// axis. The type does not imply world space, local space, rendering, collision,
// or ownership of any transform.
//
// Common aliases:
//   Aabb2, Aabb3           (float)
//   Aabb2d, Aabb3d        (double)
//   Aabb2i, Aabb3i        (int)
//=============================================================================
template <int Dimensions, typename T = float>
struct Aabb
{
	static_assert(Dimensions > 0, "Aabb dimension must be at least 1.");
	static_assert(std::is_arithmetic_v<T>, "Aabb component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	static constexpr int DimensionCount = Dimensions;
	Vec<Dimensions, T> Min;
	Vec<Dimensions, T> Max;

	// -- Construction -------------------------------------------------------

	constexpr Aabb() = default;

	constexpr Aabb(const Vec<Dimensions, T>& min, const Vec<Dimensions, T>& max)
		: Min(min), Max(max) {}

	// -- Validation ---------------------------------------------------------

	constexpr bool IsValid() const
	{
		for (int i = 0; i < Dimensions; ++i)
		{
			if (Min[i] > Max[i]) return false;
		}
		return true;
	}

	// -- Geometry -----------------------------------------------------------

	constexpr Vec<Dimensions, T> Center() const
	{
		return (Min + Max) / T{2};
	}

	constexpr Vec<Dimensions, T> Size() const
	{
		return Max - Min;
	}

	constexpr Vec<Dimensions, T> HalfExtent() const
	{
		return Size() / T{2};
	}

	constexpr Vec<Dimensions, T> Extent() const
	{
		return HalfExtent();
	}

	// -- Queries ------------------------------------------------------------

	constexpr bool Contains(const Vec<Dimensions, T>& point) const
	{
		for (int i = 0; i < Dimensions; ++i)
		{
			if (point[i] < Min[i] || point[i] > Max[i]) return false;
		}
		return true;
	}

	constexpr bool Intersects(const Aabb& other) const
	{
		for (int i = 0; i < Dimensions; ++i)
		{
			if (Max[i] < other.Min[i] || Min[i] > other.Max[i]) return false;
		}
		return true;
	}

	// -- Expansion ----------------------------------------------------------

	constexpr void ExpandToInclude(const Vec<Dimensions, T>& point)
	{
		for (int i = 0; i < Dimensions; ++i)
		{
			Min[i] = std::min(Min[i], point[i]);
			Max[i] = std::max(Max[i], point[i]);
		}
	}

	constexpr void ExpandToInclude(const Aabb& other)
	{
		for (int i = 0; i < Dimensions; ++i)
		{
			Min[i] = std::min(Min[i], other.Min[i]);
			Max[i] = std::max(Max[i], other.Max[i]);
		}
	}

	constexpr Aabb ExpandedToInclude(const Vec<Dimensions, T>& point) const
	{
		Aabb result = *this;
		result.ExpandToInclude(point);
		return result;
	}

	constexpr Aabb ExpandedToInclude(const Aabb& other) const
	{
		Aabb result = *this;
		result.ExpandToInclude(other);
		return result;
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Aabb& other) const
	{
		return Min == other.Min && Max == other.Max;
	}

	// -- Static factories ---------------------------------------------------

	static constexpr Aabb FromMinMax(const Vec<Dimensions, T>& min, const Vec<Dimensions, T>& max)
	{
		return Aabb(min, max);
	}

	static constexpr Aabb FromCenterHalfExtent(const Vec<Dimensions, T>& center, const Vec<Dimensions, T>& halfExtent)
	{
		return Aabb(center - halfExtent, center + halfExtent);
	}

	static constexpr Aabb Empty()
	{
		Aabb result;
		for (int i = 0; i < Dimensions; ++i)
		{
			result.Min[i] = std::numeric_limits<T>::max();
			result.Max[i] = std::numeric_limits<T>::lowest();
		}
		return result;
	}
};

// -- Stream output ----------------------------------------------------------

template <int Dimensions, typename T>
std::ostream& operator<<(std::ostream& os, const Aabb<Dimensions, T>& box)
{
	os << "{Min: " << box.Min << ", Max: " << box.Max << "}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Aabb2  = Aabb<2>;
using Aabb3  = Aabb<3>;

using Aabb2d = Aabb<2, double>;
using Aabb3d = Aabb<3, double>;

using Aabb2i = Aabb<2, int>;
using Aabb3i = Aabb<3, int>;
