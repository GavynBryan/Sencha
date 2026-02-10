#pragma once

#include <math/Vec.h>
#include <cmath>
#include <cassert>

//=============================================================================
// Mat4<T>
//
// 4x4 column-major matrix with arithmetic component type T (default: float).
// Stored in column-major order to match OpenGL's expected layout.
//
// Column-major indexing:
//   [0]  [4]  [8]   [12]
//   [1]  [5]  [9]   [13]
//   [2]  [6]  [10]  [14]
//   [3]  [7]  [11]  [15]
//
// Extensibility:
//   - Any arithmetic component type (float, double).
//   - Static factories for common transforms.
//   - GetData() returns raw pointer for direct upload to OpenGL.
//
// Common aliases:
//   Mat4f (float), Mat4d (double)
//=============================================================================
template <typename T = float>
struct Mat4
{
	static_assert(std::is_arithmetic_v<T>, "Mat4 component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	T Data[16] = {};

	// -- Element access -----------------------------------------------------

	constexpr T& operator()(int row, int col)
	{
		assert(row >= 0 && row < 4 && col >= 0 && col < 4);
		return Data[col * 4 + row];
	}

	constexpr const T& operator()(int row, int col) const
	{
		assert(row >= 0 && row < 4 && col >= 0 && col < 4);
		return Data[col * 4 + row];
	}

	const T* GetData() const { return Data; }

	// -- Multiplication -----------------------------------------------------

	constexpr Mat4 operator*(const Mat4& other) const
	{
		Mat4 result;
		for (int col = 0; col < 4; ++col)
		{
			for (int row = 0; row < 4; ++row)
			{
				T sum = T{0};
				for (int k = 0; k < 4; ++k)
				{
					sum += (*this)(row, k) * other(k, col);
				}
				result(row, col) = sum;
			}
		}
		return result;
	}

	// -- Vector transform ---------------------------------------------------

	constexpr Vec<4, T> operator*(const Vec<4, T>& v) const
	{
		Vec<4, T> result;
		for (int row = 0; row < 4; ++row)
		{
			T sum = T{0};
			for (int k = 0; k < 4; ++k)
			{
				sum += (*this)(row, k) * v.Data[k];
			}
			result.Data[row] = sum;
		}
		return result;
	}

	// -- Static factories ---------------------------------------------------

	static constexpr Mat4 Identity()
	{
		Mat4 m;
		m(0, 0) = T{1};
		m(1, 1) = T{1};
		m(2, 2) = T{1};
		m(3, 3) = T{1};
		return m;
	}

	static constexpr Mat4 Translation(T x, T y, T z)
	{
		Mat4 m = Identity();
		m(0, 3) = x;
		m(1, 3) = y;
		m(2, 3) = z;
		return m;
	}

	static constexpr Mat4 Scale(T x, T y, T z)
	{
		Mat4 m;
		m(0, 0) = x;
		m(1, 1) = y;
		m(2, 2) = z;
		m(3, 3) = T{1};
		return m;
	}

	static Mat4 RotationX(T radians) requires std::floating_point<T>
	{
		T c = std::cos(radians);
		T s = std::sin(radians);
		Mat4 m = Identity();
		m(1, 1) = c;
		m(1, 2) = -s;
		m(2, 1) = s;
		m(2, 2) = c;
		return m;
	}

	static Mat4 RotationY(T radians) requires std::floating_point<T>
	{
		T c = std::cos(radians);
		T s = std::sin(radians);
		Mat4 m = Identity();
		m(0, 0) = c;
		m(0, 2) = s;
		m(2, 0) = -s;
		m(2, 2) = c;
		return m;
	}

	static Mat4 RotationZ(T radians) requires std::floating_point<T>
	{
		T c = std::cos(radians);
		T s = std::sin(radians);
		Mat4 m = Identity();
		m(0, 0) = c;
		m(0, 1) = -s;
		m(1, 0) = s;
		m(1, 1) = c;
		return m;
	}

	static Mat4 Perspective(T fovRadians, T aspect, T near, T far)
		requires std::floating_point<T>
	{
		assert(aspect != T{0} && "Aspect ratio cannot be zero.");
		assert(near != far && "Near and far planes cannot be equal.");

		T tanHalfFov = std::tan(fovRadians / T{2});
		Mat4 m;
		m(0, 0) = T{1} / (aspect * tanHalfFov);
		m(1, 1) = T{1} / tanHalfFov;
		m(2, 2) = -(far + near) / (far - near);
		m(2, 3) = -(T{2} * far * near) / (far - near);
		m(3, 2) = -T{1};
		return m;
	}

	static Mat4 LookAt(const Vec<3, T>& eye, const Vec<3, T>& target,
	                    const Vec<3, T>& up) requires std::floating_point<T>
	{
		Vec<3, T> f = (target - eye).Normalized();
		Vec<3, T> r = f.Cross(up).Normalized();
		Vec<3, T> u = r.Cross(f);

		Mat4 m = Identity();
		m(0, 0) = r.X();
		m(0, 1) = r.Y();
		m(0, 2) = r.Z();
		m(0, 3) = -r.Dot(eye);
		m(1, 0) = u.X();
		m(1, 1) = u.Y();
		m(1, 2) = u.Z();
		m(1, 3) = -u.Dot(eye);
		m(2, 0) = -f.X();
		m(2, 1) = -f.Y();
		m(2, 2) = -f.Z();
		m(2, 3) = f.Dot(eye);
		return m;
	}
};

// -- Common aliases ---------------------------------------------------------

using Mat4f = Mat4<float>;
using Mat4d = Mat4<double>;
