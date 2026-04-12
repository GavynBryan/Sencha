#pragma once

#include <cmath>
#include <concepts>
#include <type_traits>

#include "Mat.h"
#include "Vec.h"

//=============================================================================
// Transform2<T>
//
// 2D translation, rotation, and scale for Sencha's column-vector math.
//
// Conventions:
//   - Rotation is radians around the positive Z axis.
//   - Points and vectors are transformed as Translation * Rotation * Scale.
//   - A * B means "apply B first, then A", matching Mat and Quat composition.
//   - ToMat3() emits a row-major Mat compatible with Mat * Vec.
//
// Note:
//   TRS values cannot exactly represent every composition of rotated
//   non-uniform scales because that can introduce shear. Transform composition
//   keeps explicit TRS components; use matrix multiplication for exact arbitrary
//   affine composition when shear matters.
//=============================================================================
template <typename T = float>
struct Transform2
{
	static_assert(std::is_arithmetic_v<T>, "Transform2 component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	Vec<2, T> Position = Vec<2, T>::Zero();
	T Rotation = T{0};
	Vec<2, T> Scale = Vec<2, T>::One();

	// -- Construction -------------------------------------------------------

	constexpr Transform2() = default;

	constexpr Transform2(const Vec<2, T>& position, T rotation, const Vec<2, T>& scale)
		: Position(position), Rotation(rotation), Scale(scale)
	{
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Transform2& other) const
	{
		return Position == other.Position
			&& Rotation == other.Rotation
			&& Scale == other.Scale;
	}

	bool NearlyEquals(const Transform2& other, T epsilon = T{1e-6}) const
		requires std::floating_point<T>
	{
		return std::abs(Position.Data[0] - other.Position.Data[0]) <= epsilon
			&& std::abs(Position.Data[1] - other.Position.Data[1]) <= epsilon
			&& std::abs(Rotation - other.Rotation) <= epsilon
			&& std::abs(Scale.Data[0] - other.Scale.Data[0]) <= epsilon
			&& std::abs(Scale.Data[1] - other.Scale.Data[1]) <= epsilon;
	}

	// -- Transform operations ----------------------------------------------

	Vec<2, T> TransformPoint(const Vec<2, T>& point) const
		requires std::floating_point<T>
	{
		return Position + TransformVector(point);
	}

	Vec<2, T> TransformVector(const Vec<2, T>& vector) const
		requires std::floating_point<T>
	{
		T c = std::cos(Rotation);
		T s = std::sin(Rotation);
		T x = vector.Data[0] * Scale.Data[0];
		T y = vector.Data[1] * Scale.Data[1];

		return Vec<2, T>(
			c * x - s * y,
			s * x + c * y
		);
	}

	Mat<3, 3, T> ToMat3() const
		requires std::floating_point<T>
	{
		T c = std::cos(Rotation);
		T s = std::sin(Rotation);

		Mat<3, 3, T> result = Mat<3, 3, T>::Identity();
		result.Data[0][0] = c * Scale.Data[0];
		result.Data[0][1] = -s * Scale.Data[1];
		result.Data[0][2] = Position.Data[0];
		result.Data[1][0] = s * Scale.Data[0];
		result.Data[1][1] = c * Scale.Data[1];
		result.Data[1][2] = Position.Data[1];
		return result;
	}

	Transform2 operator*(const Transform2& other) const
		requires std::floating_point<T>
	{
		return Transform2{
			TransformPoint(other.Position),
			Rotation + other.Rotation,
			ComponentScale(Scale, other.Scale)
		};
	}

	Transform2& operator*=(const Transform2& other)
		requires std::floating_point<T>
	{
		*this = *this * other;
		return *this;
	}

	// -- Static factories ---------------------------------------------------

	static constexpr Transform2 Identity()
	{
		return Transform2{};
	}

private:
	static constexpr Vec<2, T> ComponentScale(const Vec<2, T>& a, const Vec<2, T>& b)
	{
		return Vec<2, T>(a.Data[0] * b.Data[0], a.Data[1] * b.Data[1]);
	}
};

// -- Common aliases ---------------------------------------------------------

using Transform2f = Transform2<float>;
using Transform2d = Transform2<double>;
