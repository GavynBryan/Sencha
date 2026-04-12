#pragma once

#include <cmath>
#include <concepts>
#include <ostream>
#include <type_traits>

#include <math/Mat.h>
#include <math/Vec.h>
#include <geometry/Aabb3.h>
#include <geometry/Plane.h>
#include <geometry/Sphere.h>

//=============================================================================
// Frustum<T>
//
// Six-plane frustum for culling and containment tests.
//
// Plane extraction convention:
//   Planes are extracted from a row-major view-projection matrix using the
//   Griggs/Hartmann method. Each plane normal points inward (toward the
//   interior of the frustum).
//
//   After extraction, planes are normalized so that SignedDistanceTo returns
//   true Euclidean distances.
//
// Plane order:
//   [0] Left   [1] Right   [2] Bottom   [3] Top   [4] Near   [5] Far
//
// Common aliases:
//   Frustumf, Frustumd
//=============================================================================
template <typename T = float>
struct Frustum
{
	static_assert(std::is_arithmetic_v<T>, "Frustum component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	static constexpr int PlaneCount = 6;

	enum Side { Left = 0, Right, Bottom, Top, Near, Far };

	Plane<T> Planes[PlaneCount];

	// -- Construction -------------------------------------------------------

	constexpr Frustum() = default;

	// -- Factory ------------------------------------------------------------

	static Frustum FromViewProjection(const Mat<4, 4, T>& vp)
		requires std::floating_point<T>
	{
		Frustum f;

		// Left:   row3 + row0
		f.Planes[Left] = Plane<T>(
			Vec<3, T>(vp[3][0] + vp[0][0], vp[3][1] + vp[0][1], vp[3][2] + vp[0][2]),
			vp[3][3] + vp[0][3]
		).Normalized();

		// Right:  row3 - row0
		f.Planes[Right] = Plane<T>(
			Vec<3, T>(vp[3][0] - vp[0][0], vp[3][1] - vp[0][1], vp[3][2] - vp[0][2]),
			vp[3][3] - vp[0][3]
		).Normalized();

		// Bottom: row3 + row1
		f.Planes[Bottom] = Plane<T>(
			Vec<3, T>(vp[3][0] + vp[1][0], vp[3][1] + vp[1][1], vp[3][2] + vp[1][2]),
			vp[3][3] + vp[1][3]
		).Normalized();

		// Top:    row3 - row1
		f.Planes[Top] = Plane<T>(
			Vec<3, T>(vp[3][0] - vp[1][0], vp[3][1] - vp[1][1], vp[3][2] - vp[1][2]),
			vp[3][3] - vp[1][3]
		).Normalized();

		// Near:   row3 + row2
		f.Planes[Near] = Plane<T>(
			Vec<3, T>(vp[3][0] + vp[2][0], vp[3][1] + vp[2][1], vp[3][2] + vp[2][2]),
			vp[3][3] + vp[2][3]
		).Normalized();

		// Far:    row3 - row2
		f.Planes[Far] = Plane<T>(
			Vec<3, T>(vp[3][0] - vp[2][0], vp[3][1] - vp[2][1], vp[3][2] - vp[2][2]),
			vp[3][3] - vp[2][3]
		).Normalized();

		return f;
	}

	// -- Queries ------------------------------------------------------------

	bool ContainsPoint(const Vec<3, T>& point) const
	{
		for (int i = 0; i < PlaneCount; ++i)
		{
			if (Planes[i].SignedDistanceTo(point) < T{0})
				return false;
		}
		return true;
	}

	bool IntersectsSphere(const Sphere<T>& sphere) const
	{
		for (int i = 0; i < PlaneCount; ++i)
		{
			if (Planes[i].SignedDistanceTo(sphere.Center) < -sphere.Radius)
				return false;
		}
		return true;
	}

	bool IntersectsAabb(const Aabb3<T>& box) const
	{
		for (int i = 0; i < PlaneCount; ++i)
		{
			// Find the positive vertex (the corner most aligned with the plane normal)
			Vec<3, T> pVertex;
			for (int axis = 0; axis < 3; ++axis)
			{
				pVertex.Data[axis] = (Planes[i].Normal.Data[axis] >= T{0})
					? box.Max.Data[axis]
					: box.Min.Data[axis];
			}

			if (Planes[i].SignedDistanceTo(pVertex) < T{0})
				return false;
		}
		return true;
	}
};

// -- Stream output ----------------------------------------------------------

template <typename T>
std::ostream& operator<<(std::ostream& os, const Frustum<T>& frustum)
{
	os << "{Frustum: [";
	const char* names[] = { "Left", "Right", "Bottom", "Top", "Near", "Far" };
	for (int i = 0; i < Frustum<T>::PlaneCount; ++i)
	{
		if (i > 0) os << ", ";
		os << names[i] << ": " << frustum.Planes[i];
	}
	os << "]}";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Frustumf = Frustum<float>;
using Frustumd = Frustum<double>;
