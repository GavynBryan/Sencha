#pragma once

#include <math/Vec.h>
#include <service/IService.h>
#include <cmath>

//=============================================================================
// IGeometry<N, T>
//
// Service interface that defines how spatial operations behave in a given
// N-dimensional space. Game systems that need distance, direction, or
// interpolation resolve an IGeometry from the ServiceProvider rather than
// hard-coding Euclidean math.
//
// This decouples game logic from the underlying spatial model:
//   - EuclideanGeometry  — flat space, standard vector math (default)
//   - (future) Manifold geometries for portals, curved space, etc.
//
// The geometry service is intentionally separate from the render systems.
// Render systems consume positional data from DataBatches; game systems
// use IGeometry to produce and transform that data.
//
// Template parameters:
//   N — number of spatial dimensions (2, 3, or eventually 4)
//   T — component type (float by default)
//
// Common aliases are provided at the bottom of this file.
//=============================================================================
template<int N, typename T = float>
class IGeometry : public IService
{
public:
	using VecType = Vec<N, T>;

	~IGeometry() override = default;

	// -- Core spatial operations ---------------------------------------------

	// Distance between two points following this geometry's metric.
	virtual T Distance(const VecType& a, const VecType& b) const = 0;

	// Squared distance (avoids sqrt when only comparison is needed).
	virtual T SqrDistance(const VecType& a, const VecType& b) const = 0;

	// -- Movement and direction ---------------------------------------------

	// Translate a point by an offset.
	virtual VecType Translate(const VecType& point, const VecType& offset) const = 0;

	// Unit direction vector from one point toward another.
	virtual VecType Direction(const VecType& from, const VecType& to) const = 0;

	// Move from one point toward another, clamped to maxDistance.
	virtual VecType MoveToward(const VecType& from, const VecType& to, T maxDistance) const = 0;

	// -- Interpolation ------------------------------------------------------

	// Interpolate between two points following this geometry's geodesic.
	virtual VecType Interpolate(const VecType& a, const VecType& b, T t) const = 0;
};

//=============================================================================
// EuclideanGeometry<N, T>
//
// Standard flat-space geometry. Distance is the L2 norm, translation is
// vector addition, interpolation is linear. This is the default geometry
// for most games.
//=============================================================================
template<int N, typename T = float>
class EuclideanGeometry : public IGeometry<N, T>
{
public:
	using VecType = typename IGeometry<N, T>::VecType;

	T Distance(const VecType& a, const VecType& b) const override
	{
		return (a - b).Magnitude();
	}

	T SqrDistance(const VecType& a, const VecType& b) const override
	{
		return (a - b).SqrMagnitude();
	}

	VecType Translate(const VecType& point, const VecType& offset) const override
	{
		return point + offset;
	}

	VecType Direction(const VecType& from, const VecType& to) const override
	{
		VecType diff = to - from;
		T sqrMag = diff.SqrMagnitude();
		if (sqrMag < std::numeric_limits<T>::epsilon())
		{
			return VecType::Zero();
		}
		return diff / std::sqrt(sqrMag);
	}

	VecType MoveToward(const VecType& from, const VecType& to, T maxDistance) const override
	{
		VecType diff = to - from;
		T sqrMag = diff.SqrMagnitude();

		if (sqrMag <= maxDistance * maxDistance || sqrMag < std::numeric_limits<T>::epsilon())
		{
			return to;
		}

		return from + diff / std::sqrt(sqrMag) * maxDistance;
	}

	VecType Interpolate(const VecType& a, const VecType& b, T t) const override
	{
		return VecType::Lerp(a, b, t);
	}
};

// -- Common aliases ---------------------------------------------------------

using IGeometry2D  = IGeometry<2>;
using IGeometry3D  = IGeometry<3>;
using IGeometry2Dd = IGeometry<2, double>;
using IGeometry3Dd = IGeometry<3, double>;

using EuclideanGeometry2D  = EuclideanGeometry<2>;
using EuclideanGeometry3D  = EuclideanGeometry<3>;
using EuclideanGeometry2Dd = EuclideanGeometry<2, double>;
using EuclideanGeometry3Dd = EuclideanGeometry<3, double>;
