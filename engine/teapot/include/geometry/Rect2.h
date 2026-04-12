#pragma once

#include <algorithm>
#include <ostream>
#include <type_traits>

#include <math/Vec.h>

//=============================================================================
// Rect2<T>
//
// 2D rectangular region defined by position (top-left origin) and size.
//
// Position is the minimum corner. Size is the extent along each axis.
// A rect is valid when Size components are non-negative.
//
// Min = Position, Max = Position + Size.
//
// Common aliases:
//   Rect2f, Rect2d, Rect2i
//=============================================================================
template <typename T = float>
struct Rect2
{
	static_assert(std::is_arithmetic_v<T>, "Rect2 component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	Vec<2, T> Position;
	Vec<2, T> Size;

	// -- Construction -------------------------------------------------------

	constexpr Rect2() = default;

	constexpr Rect2(const Vec<2, T>& position, const Vec<2, T>& size)
		: Position(position), Size(size) {}

	constexpr Rect2(T x, T y, T width, T height)
		: Position(x, y), Size(width, height) {}

	// -- Validation ---------------------------------------------------------

	constexpr bool IsValid() const
	{
		return Size.Data[0] >= T{0} && Size.Data[1] >= T{0};
	}

	// -- Geometry -----------------------------------------------------------

	constexpr Vec<2, T> Min() const
	{
		return Position;
	}

	constexpr Vec<2, T> Max() const
	{
		return Position + Size;
	}

	constexpr Vec<2, T> Center() const
	{
		return Position + Size / T{2};
	}

	constexpr T Width() const { return Size.Data[0]; }
	constexpr T Height() const { return Size.Data[1]; }
	constexpr T Area() const { return Size.Data[0] * Size.Data[1]; }

	// -- Queries ------------------------------------------------------------

	constexpr bool Contains(const Vec<2, T>& point) const
	{
		Vec<2, T> max = Max();
		return point.Data[0] >= Position.Data[0] && point.Data[0] <= max.Data[0]
			&& point.Data[1] >= Position.Data[1] && point.Data[1] <= max.Data[1];
	}

	constexpr bool Intersects(const Rect2& other) const
	{
		Vec<2, T> maxA = Max();
		Vec<2, T> maxB = other.Max();
		return Position.Data[0] <= maxB.Data[0] && maxA.Data[0] >= other.Position.Data[0]
			&& Position.Data[1] <= maxB.Data[1] && maxA.Data[1] >= other.Position.Data[1];
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Rect2& other) const
	{
		return Position == other.Position && Size == other.Size;
	}

	// -- Static factories ---------------------------------------------------

	static constexpr Rect2 FromMinMax(const Vec<2, T>& min, const Vec<2, T>& max)
	{
		return Rect2(min, max - min);
	}

	static constexpr Rect2 FromCenterSize(const Vec<2, T>& center, const Vec<2, T>& size)
	{
		return Rect2(center - size / T{2}, size);
	}
};

// -- Stream output ----------------------------------------------------------

template <typename T>
std::ostream& operator<<(std::ostream& os, const Rect2<T>& rect)
{
	os << "{Position: " << rect.Position << ", Size: " << rect.Size << "}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Rect2f = Rect2<float>;
using Rect2d = Rect2<double>;
using Rect2i = Rect2<int>;
