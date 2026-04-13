#pragma once

#include <cmath>
#include <concepts>
#include <type_traits>

#include "Mat.h"
#include "Quat.h"
#include "Vec.h"

//=============================================================================
// Transform3d<T>
//
// 3D translation, quaternion rotation, and scale for Sencha's column-vector math.
//
// Conventions:
//   - Local Forward is -Z, local Right is +X, and local Up is +Y.
//   - Points and vectors are transformed as Translation * Rotation * Scale.
//   - A * B means "apply B first, then A", matching Mat and Quat composition.
//   - ToMat4() emits a row-major Mat compatible with Mat * Vec.
//
// Note:
//   TRS values cannot exactly represent every composition of rotated
//   non-uniform scales because that can introduce shear. Transform composition
//   keeps explicit TRS components; use matrix multiplication for exact arbitrary
//   affine composition when shear matters.
//=============================================================================
template <typename T = float>
struct Transform3d
{
	static_assert(std::is_arithmetic_v<T>, "Transform3d component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	Vec<3, T> Position = Vec<3, T>::Zero();
	Quat<T> Rotation = Quat<T>::Identity();
	Vec<3, T> Scale = Vec<3, T>::One();

	// -- Construction -------------------------------------------------------

	constexpr Transform3d() = default;

	constexpr Transform3d(const Vec<3, T>& position, const Quat<T>& rotation, const Vec<3, T>& scale)
		: Position(position), Rotation(rotation), Scale(scale)
	{
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Transform3d& other) const
	{
		return Position == other.Position
			&& Rotation == other.Rotation
			&& Scale == other.Scale;
	}

	bool NearlyEquals(const Transform3d& other, T epsilon = T{1e-6}) const
		requires std::floating_point<T>
	{
		return std::abs(Position.X - other.Position.X) <= epsilon
			&& std::abs(Position.Y - other.Position.Y) <= epsilon
			&& std::abs(Position.Z - other.Position.Z) <= epsilon
			&& Rotation.NearlyEquals(other.Rotation, epsilon)
			&& std::abs(Scale.X - other.Scale.X) <= epsilon
			&& std::abs(Scale.Y - other.Scale.Y) <= epsilon
			&& std::abs(Scale.Z - other.Scale.Z) <= epsilon;
	}

	// -- Transform operations ----------------------------------------------

	Vec<3, T> TransformPoint(const Vec<3, T>& point) const
		requires std::floating_point<T>
	{
		return Position + TransformVector(point);
	}

	Vec<3, T> TransformVector(const Vec<3, T>& vector) const
		requires std::floating_point<T>
	{
		return Rotation.RotateVector(ComponentScale(vector, Scale));
	}

	Vec<3, T> Forward() const
		requires std::floating_point<T>
	{
		return Rotation.RotateVector(Vec<3, T>::Forward());
	}

	Vec<3, T> Right() const
		requires std::floating_point<T>
	{
		return Rotation.RotateVector(Vec<3, T>::Right());
	}

	Vec<3, T> Up() const
		requires std::floating_point<T>
	{
		return Rotation.RotateVector(Vec<3, T>::Up());
	}

	Mat<3, 3, T> ToMat3() const
		requires std::floating_point<T>
	{
		Mat<3, 3, T> rotation = Rotation.ToMat3();
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				rotation.Data[r][c] *= Scale[c];
		return rotation;
	}

	Mat<4, 4, T> ToMat4() const
		requires std::floating_point<T>
	{
		Mat<3, 3, T> linear = ToMat3();
		Mat<4, 4, T> result = Mat<4, 4, T>::Identity();
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				result.Data[r][c] = linear.Data[r][c];

		result.Data[0][3] = Position.X;
		result.Data[1][3] = Position.Y;
		result.Data[2][3] = Position.Z;
		return result;
	}

	Transform3d operator*(const Transform3d& other) const
		requires std::floating_point<T>
	{
		return Transform3d{
			TransformPoint(other.Position),
			Rotation * other.Rotation,
			ComponentScale(Scale, other.Scale)
		};
	}

	Transform3d& operator*=(const Transform3d& other)
		requires std::floating_point<T>
	{
		*this = *this * other;
		return *this;
	}

	// -- Static factories ---------------------------------------------------

	static constexpr Transform3d Identity()
	{
		return Transform3d{};
	}

private:
	static constexpr Vec<3, T> ComponentScale(const Vec<3, T>& a, const Vec<3, T>& b)
	{
		return Vec<3, T>(
			a.X * b.X,
			a.Y * b.Y,
			a.Z * b.Z
		);
	}
};

// -- Common aliases ---------------------------------------------------------

using Transform3f = Transform3d<float>;
using Transform3dd = Transform3d<double>;
