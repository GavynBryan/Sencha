#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <ostream>
#include <type_traits>

#include "Mat.h"
#include "Vec.h"

//=============================================================================
// Quat<T>
//
// Quaternion rotation representation with arithmetic component type T
// (default: float).
//
// Conventions:
//   - Component layout is vector part first, then scalar part: X, Y, Z, W.
//   - Multiplication is the Hamilton product. For rotation quaternions,
//     a * b composes rotations in matrix order for Sencha's column-vector math:
//     b is applied first, then a.
//   - Vector rotation uses active rotation: q * (v, 0) * inverse(q).
//   - Matrix conversion emits row-major Mat values compatible with Mat * Vec,
//     so ToMat3() * v matches RotateVector(v).
//
// Common aliases:
//   Quatf, Quatd
//=============================================================================
template <typename T = float>
struct Quat
{
	static_assert(std::is_arithmetic_v<T>, "Quat component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	T X = T{0};
	T Y = T{0};
	T Z = T{0};
	T W = T{1};

	// -- Construction -------------------------------------------------------

	constexpr Quat() = default;

	constexpr Quat(T x, T y, T z, T w)
		: X(x), Y(y), Z(z), W(w)
	{
	}

	// -- Arithmetic operators -----------------------------------------------

	constexpr Quat operator*(const Quat& other) const
	{
		return Quat{
			W * other.X + X * other.W + Y * other.Z - Z * other.Y,
			W * other.Y - X * other.Z + Y * other.W + Z * other.X,
			W * other.Z + X * other.Y - Y * other.X + Z * other.W,
			W * other.W - X * other.X - Y * other.Y - Z * other.Z
		};
	}

	constexpr Quat& operator*=(const Quat& other)
	{
		*this = *this * other;
		return *this;
	}

	constexpr Quat operator*(T scalar) const
	{
		return Quat{ X * scalar, Y * scalar, Z * scalar, W * scalar };
	}

	constexpr Quat operator/(T scalar) const
	{
		assert(scalar != T{0} && "Quat division by zero.");
		return Quat{ X / scalar, Y / scalar, Z / scalar, W / scalar };
	}

	constexpr Quat& operator*=(T scalar)
	{
		X *= scalar;
		Y *= scalar;
		Z *= scalar;
		W *= scalar;
		return *this;
	}

	constexpr Quat& operator/=(T scalar)
	{
		assert(scalar != T{0} && "Quat division by zero.");
		X /= scalar;
		Y /= scalar;
		Z /= scalar;
		W /= scalar;
		return *this;
	}

	constexpr Quat operator-() const
	{
		return Quat{ -X, -Y, -Z, -W };
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Quat& other) const
	{
		return X == other.X && Y == other.Y && Z == other.Z && W == other.W;
	}

	// -- Quaternion operations ---------------------------------------------

	constexpr T Dot(const Quat& other) const
	{
		return X * other.X + Y * other.Y + Z * other.Z + W * other.W;
	}

	constexpr T LengthSquared() const
	{
		return Dot(*this);
	}

	T Length() const requires std::floating_point<T>
	{
		return std::sqrt(LengthSquared());
	}

	Quat& Normalize() requires std::floating_point<T>
	{
		T length = Length();
		assert(length > T{0} && "Cannot normalize a zero-length Quat.");
		*this /= length;
		return *this;
	}

	Quat Normalized() const requires std::floating_point<T>
	{
		Quat result = *this;
		result.Normalize();
		return result;
	}

	constexpr Quat Conjugate() const
	{
		return Quat{ -X, -Y, -Z, W };
	}

	constexpr Quat Inverse() const requires std::floating_point<T>
	{
		T lengthSquared = LengthSquared();
		assert(lengthSquared != T{0} && "Cannot invert a zero-length Quat.");
		return Conjugate() / lengthSquared;
	}

	bool NearlyEquals(const Quat& other, T epsilon = T{1e-6}) const
		requires std::floating_point<T>
	{
		return std::abs(X - other.X) <= epsilon
			&& std::abs(Y - other.Y) <= epsilon
			&& std::abs(Z - other.Z) <= epsilon
			&& std::abs(W - other.W) <= epsilon;
	}

	Vec<3, T> RotateVector(const Vec<3, T>& v) const
		requires std::floating_point<T>
	{
		Quat p(v.Data[0], v.Data[1], v.Data[2], T{0});
		Quat rotated = *this * p * Inverse();
		return Vec<3, T>(rotated.X, rotated.Y, rotated.Z);
	}

	Mat<3, 3, T> ToMat3() const requires std::floating_point<T>
	{
		Quat q = Normalized();

		T xx = q.X * q.X;
		T yy = q.Y * q.Y;
		T zz = q.Z * q.Z;
		T xy = q.X * q.Y;
		T xz = q.X * q.Z;
		T yz = q.Y * q.Z;
		T wx = q.W * q.X;
		T wy = q.W * q.Y;
		T wz = q.W * q.Z;

		Mat<3, 3, T> result;
		result.Data[0][0] = T{1} - T{2} * (yy + zz);
		result.Data[0][1] = T{2} * (xy - wz);
		result.Data[0][2] = T{2} * (xz + wy);

		result.Data[1][0] = T{2} * (xy + wz);
		result.Data[1][1] = T{1} - T{2} * (xx + zz);
		result.Data[1][2] = T{2} * (yz - wx);

		result.Data[2][0] = T{2} * (xz - wy);
		result.Data[2][1] = T{2} * (yz + wx);
		result.Data[2][2] = T{1} - T{2} * (xx + yy);
		return result;
	}

	Mat<4, 4, T> ToMat4() const requires std::floating_point<T>
	{
		Mat<3, 3, T> rotation = ToMat3();
		Mat<4, 4, T> result = Mat<4, 4, T>::Identity();
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				result.Data[r][c] = rotation.Data[r][c];
		return result;
	}

	// -- Static factories ---------------------------------------------------

	static constexpr Quat Identity()
	{
		return Quat{};
	}

	static Quat FromAxisAngle(const Vec<3, T>& axis, T angleRadians)
		requires std::floating_point<T>
	{
		Vec<3, T> normalizedAxis = axis.Normalized();
		T halfAngle = angleRadians / T{2};
		T s = std::sin(halfAngle);
		T c = std::cos(halfAngle);
		return Quat{
			normalizedAxis.Data[0] * s,
			normalizedAxis.Data[1] * s,
			normalizedAxis.Data[2] * s,
			c
		};
	}
};

// -- Free function: scalar * quat -------------------------------------------

template <typename T>
constexpr Quat<T> operator*(T scalar, const Quat<T>& q)
{
	return q * scalar;
}

// -- Stream output ----------------------------------------------------------

template <typename T>
std::ostream& operator<<(std::ostream& os, const Quat<T>& q)
{
	os << "(" << q.X << ", " << q.Y << ", " << q.Z << ", " << q.W << ")";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Quatf = Quat<float>;
using Quatd = Quat<double>;
