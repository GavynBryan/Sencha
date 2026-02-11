#pragma once

#include <teapot/math/Vec.h>

#include <type_traits>

//=============================================================================
// Transform<N, T>
//
// N-dimensional transform with arithmetic component type T (default: float).
//
// Bundles position, scale, and rotation into a single value type.
//
// Rotation type varies by dimension:
//   - 2D: scalar T (a single angle in radians)
//   - 3D+: Vec<N, T> (e.g. Euler angles)
//
// Common aliases:
//   Transform2D, Transform3D           (float)
//   Transform2Dd, Transform3Dd         (double)
//=============================================================================
template <int N, typename T = float>
struct Transform
{
	static_assert(N >= 2, "Transform dimension must be at least 2.");
	static_assert(std::is_arithmetic_v<T>, "Transform component type must be arithmetic.");

	using RotationType = std::conditional_t<N == 2, T, Vec<N, T>>;

	Vec<N, T> Position{};
	Vec<N, T> Scale = Vec<N, T>::One();
	RotationType Rotation{};
};

// -- Common aliases ---------------------------------------------------------

using Transform2D  = Transform<2>;
using Transform3D  = Transform<3>;

using Transform2Dd = Transform<2, double>;
using Transform3Dd = Transform<3, double>;
