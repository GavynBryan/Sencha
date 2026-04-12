#pragma once

#include <algorithm>
#include <ostream>
#include <type_traits>

#include <math/Vec.h>

//=============================================================================
// Rect3<T>
//
// 3D rectangular region defined by position and size.
//
// Position is the minimum corner. Size is the extent along each axis.
// A rect is valid when Size components are non-negative.
//
// Min = Position, Max = Position + Size.
//
// Note: This is a plain region primitive. Unlike Aabb, it does not
// provide accumulation helpers (Empty/ExpandToInclude). Use Aabb when
// you need bounding-box semantics.
//
// Common aliases:
//   Rect3f, Rect3d, Rect3i
//=============================================================================
template <typename T = float>
struct Rect3
{
	static_assert(std::is_arithmetic_v<T>, "Rect3 component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	Vec<3, T> Position;
	Vec<3, T> Size;

	// -- Construction -------------------------------------------------------

	constexpr Rect3() = default;

	constexpr Rect3(const Vec<3, T>& position, const Vec<3, T>& size)
		: Position(position), Size(size) {}

	constexpr Rect3(T x, T y, T z, T width, T height, T depth)
		: Position(x, y, z), Size(width, height, depth) {}

	// -- Validation ---------------------------------------------------------

	constexpr bool IsValid() const
	{
		return Size.Data[0] >= T{0} && Size.Data[1] >= T{0} && Size.Data[2] >= T{0};
	}

	// -- Geometry -----------------------------------------------------------

	constexpr Vec<3, T> Min() const
	{
		return Position;
	}

	constexpr Vec<3, T> Max() const
	{
		return Position + Size;
	}

	constexpr Vec<3, T> Center() const
	{
		return Position + Size / T{2};
	}

	constexpr T Width() const { return Size.Data[0]; }
	constexpr T Height() const { return Size.Data[1]; }
	constexpr T Depth() const { return Size.Data[2]; }
	constexpr T Volume() const { return Size.Data[0] * Size.Data[1] * Size.Data[2]; }

	// -- Queries ------------------------------------------------------------

	constexpr bool Contains(const Vec<3, T>& point) const
	{
		Vec<3, T> max = Max();
		return point.Data[0] >= Position.Data[0] && point.Data[0] <= max.Data[0]
			&& point.Data[1] >= Position.Data[1] && point.Data[1] <= max.Data[1]
			&& point.Data[2] >= Position.Data[2] && point.Data[2] <= max.Data[2];
	}

	constexpr bool Intersects(const Rect3& other) const
	{
		Vec<3, T> maxA = Max();
		Vec<3, T> maxB = other.Max();
		return Position.Data[0] <= maxB.Data[0] && maxA.Data[0] >= other.Position.Data[0]
			&& Position.Data[1] <= maxB.Data[1] && maxA.Data[1] >= other.Position.Data[1]
			&& Position.Data[2] <= maxB.Data[2] && maxA.Data[2] >= other.Position.Data[2];
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Rect3& other) const
	{
		return Position == other.Position && Size == other.Size;
	}

	// -- Static factories ---------------------------------------------------

	static constexpr Rect3 FromMinMax(const Vec<3, T>& min, const Vec<3, T>& max)
	{
		return Rect3(min, max - min);
	}

	static constexpr Rect3 FromCenterSize(const Vec<3, T>& center, const Vec<3, T>& size)
	{
		return Rect3(center - size / T{2}, size);
	}
};

// -- Stream output ----------------------------------------------------------

template <typename T>
std::ostream& operator<<(std::ostream& os, const Rect3<T>& rect)
{
	os << "{Position: " << rect.Position << ", Size: " << rect.Size << "}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Rect3f = Rect3<float>;
using Rect3d = Rect3<double>;
using Rect3i = Rect3<int>;
