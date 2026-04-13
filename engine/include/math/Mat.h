#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <ostream>
#include <type_traits>

#include "Vec.h"

//=============================================================================
// Mat<Rows, Cols, T>
//
// Row-major matrix with arithmetic component type T (default: float).
// Supports arbitrary dimensions; common factories are dimension-gated
// at compile time via requires clauses.
//
// Storage is row-major: Data[row][col].
//
// Common aliases:
//   Mat2d, Mat3d, Mat4           (float)
//   Mat2dd, Mat3dd, Mat4d        (double)
//   Mat2i, Mat3i, Mat4i        (int)
//=============================================================================
template <int Rows, int Cols, typename T = float>
struct Mat
{
	static_assert(Rows > 0 && Cols > 0, "Mat dimensions must be at least 1.");
	static_assert(std::is_arithmetic_v<T>, "Mat component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	static constexpr int RowCount = Rows;
	static constexpr int ColCount = Cols;
	T Data[Rows][Cols] = {};

	// -- Construction -------------------------------------------------------

	constexpr Mat() = default;

	// -- Element access -----------------------------------------------------

	constexpr T* operator[](int row)
	{
		assert(row >= 0 && row < Rows && "Mat row index out of range.");
		return Data[row];
	}

	constexpr const T* operator[](int row) const
	{
		assert(row >= 0 && row < Rows && "Mat row index out of range.");
		return Data[row];
	}

	constexpr T& At(int row, int col)
	{
		assert(row >= 0 && row < Rows && "Mat row index out of range.");
		assert(col >= 0 && col < Cols && "Mat col index out of range.");
		return Data[row][col];
	}

	constexpr const T& At(int row, int col) const
	{
		assert(row >= 0 && row < Rows && "Mat row index out of range.");
		assert(col >= 0 && col < Cols && "Mat col index out of range.");
		return Data[row][col];
	}

	// -- Arithmetic operators -----------------------------------------------

	constexpr Mat operator+(const Mat& other) const
	{
		Mat result;
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				result.Data[r][c] = Data[r][c] + other.Data[r][c];
		return result;
	}

	constexpr Mat operator-(const Mat& other) const
	{
		Mat result;
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				result.Data[r][c] = Data[r][c] - other.Data[r][c];
		return result;
	}

	constexpr Mat operator*(T scalar) const
	{
		Mat result;
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				result.Data[r][c] = Data[r][c] * scalar;
		return result;
	}

	constexpr Mat operator/(T scalar) const
	{
		assert(scalar != T{0} && "Mat division by zero.");
		Mat result;
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				result.Data[r][c] = Data[r][c] / scalar;
		return result;
	}

	constexpr Mat& operator+=(const Mat& other)
	{
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				Data[r][c] += other.Data[r][c];
		return *this;
	}

	constexpr Mat& operator-=(const Mat& other)
	{
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				Data[r][c] -= other.Data[r][c];
		return *this;
	}

	constexpr Mat& operator*=(T scalar)
	{
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				Data[r][c] *= scalar;
		return *this;
	}

	constexpr Mat& operator/=(T scalar)
	{
		assert(scalar != T{0} && "Mat division by zero.");
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				Data[r][c] /= scalar;
		return *this;
	}

	constexpr Mat operator-() const
	{
		Mat result;
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				result.Data[r][c] = -Data[r][c];
		return result;
	}

	// -- Matrix multiplication ----------------------------------------------

	template <int OtherCols>
	constexpr Mat<Rows, OtherCols, T> operator*(const Mat<Cols, OtherCols, T>& other) const
	{
		Mat<Rows, OtherCols, T> result;
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < OtherCols; ++c)
				for (int k = 0; k < Cols; ++k)
					result.Data[r][c] += Data[r][k] * other.Data[k][c];
		return result;
	}

	// Square matrix self-multiply
	constexpr Mat& operator*=(const Mat& other) requires (Rows == Cols)
	{
		*this = *this * other;
		return *this;
	}

	// -- Matrix-vector multiplication ---------------------------------------

	constexpr Vec<Rows, T> operator*(const Vec<Cols, T>& v) const
	{
		Vec<Rows, T> result;
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				result[r] += Data[r][c] * v[c];
		return result;
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Mat& other) const
	{
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				if (Data[r][c] != other.Data[r][c]) return false;
		return true;
	}

	// -- Transpose ----------------------------------------------------------

	constexpr Mat<Cols, Rows, T> Transposed() const
	{
		Mat<Cols, Rows, T> result;
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				result.Data[c][r] = Data[r][c];
		return result;
	}

	// -- Determinant (square only) ------------------------------------------

	constexpr T Determinant() const requires (Rows == Cols && Rows == 2)
	{
		return Data[0][0] * Data[1][1] - Data[0][1] * Data[1][0];
	}

	constexpr T Determinant() const requires (Rows == Cols && Rows == 3)
	{
		return Data[0][0] * (Data[1][1] * Data[2][2] - Data[1][2] * Data[2][1])
		     - Data[0][1] * (Data[1][0] * Data[2][2] - Data[1][2] * Data[2][0])
		     + Data[0][2] * (Data[1][0] * Data[2][1] - Data[1][1] * Data[2][0]);
	}

	constexpr T Determinant() const requires (Rows == Cols && Rows == 4)
	{
		T a00 = Data[0][0], a01 = Data[0][1], a02 = Data[0][2], a03 = Data[0][3];
		T a10 = Data[1][0], a11 = Data[1][1], a12 = Data[1][2], a13 = Data[1][3];
		T a20 = Data[2][0], a21 = Data[2][1], a22 = Data[2][2], a23 = Data[2][3];
		T a30 = Data[3][0], a31 = Data[3][1], a32 = Data[3][2], a33 = Data[3][3];

		T sub00 = a22 * a33 - a23 * a32;
		T sub01 = a21 * a33 - a23 * a31;
		T sub02 = a21 * a32 - a22 * a31;
		T sub03 = a20 * a33 - a23 * a30;
		T sub04 = a20 * a32 - a22 * a30;
		T sub05 = a20 * a31 - a21 * a30;

		return a00 * (a11 * sub00 - a12 * sub01 + a13 * sub02)
		     - a01 * (a10 * sub00 - a12 * sub03 + a13 * sub04)
		     + a02 * (a10 * sub01 - a11 * sub03 + a13 * sub05)
		     - a03 * (a10 * sub02 - a11 * sub04 + a12 * sub05);
	}

	// -- Inverse (square, floating-point only) ------------------------------

	Mat Inverse() const requires (Rows == Cols && std::floating_point<T> && Rows >= 2 && Rows <= 4)
	{
		T det = Determinant();
		assert(det != T{0} && "Mat is singular; cannot invert.");

		if constexpr (Rows == 2)
		{
			Mat result;
			T inv = T{1} / det;
			result.Data[0][0] =  Data[1][1] * inv;
			result.Data[0][1] = -Data[0][1] * inv;
			result.Data[1][0] = -Data[1][0] * inv;
			result.Data[1][1] =  Data[0][0] * inv;
			return result;
		}
		else if constexpr (Rows == 3)
		{
			Mat result;
			T inv = T{1} / det;
			result.Data[0][0] = (Data[1][1] * Data[2][2] - Data[1][2] * Data[2][1]) * inv;
			result.Data[0][1] = (Data[0][2] * Data[2][1] - Data[0][1] * Data[2][2]) * inv;
			result.Data[0][2] = (Data[0][1] * Data[1][2] - Data[0][2] * Data[1][1]) * inv;
			result.Data[1][0] = (Data[1][2] * Data[2][0] - Data[1][0] * Data[2][2]) * inv;
			result.Data[1][1] = (Data[0][0] * Data[2][2] - Data[0][2] * Data[2][0]) * inv;
			result.Data[1][2] = (Data[0][2] * Data[1][0] - Data[0][0] * Data[1][2]) * inv;
			result.Data[2][0] = (Data[1][0] * Data[2][1] - Data[1][1] * Data[2][0]) * inv;
			result.Data[2][1] = (Data[0][1] * Data[2][0] - Data[0][0] * Data[2][1]) * inv;
			result.Data[2][2] = (Data[0][0] * Data[1][1] - Data[0][1] * Data[1][0]) * inv;
			return result;
		}
		else // Rows == 4
		{
			T a00 = Data[0][0], a01 = Data[0][1], a02 = Data[0][2], a03 = Data[0][3];
			T a10 = Data[1][0], a11 = Data[1][1], a12 = Data[1][2], a13 = Data[1][3];
			T a20 = Data[2][0], a21 = Data[2][1], a22 = Data[2][2], a23 = Data[2][3];
			T a30 = Data[3][0], a31 = Data[3][1], a32 = Data[3][2], a33 = Data[3][3];

			T c00 = a11*(a22*a33 - a23*a32) - a12*(a21*a33 - a23*a31) + a13*(a21*a32 - a22*a31);
			T c01 = -(a10*(a22*a33 - a23*a32) - a12*(a20*a33 - a23*a30) + a13*(a20*a32 - a22*a30));
			T c02 = a10*(a21*a33 - a23*a31) - a11*(a20*a33 - a23*a30) + a13*(a20*a31 - a21*a30);
			T c03 = -(a10*(a21*a32 - a22*a31) - a11*(a20*a32 - a22*a30) + a12*(a20*a31 - a21*a30));

			T c10 = -(a01*(a22*a33 - a23*a32) - a02*(a21*a33 - a23*a31) + a03*(a21*a32 - a22*a31));
			T c11 = a00*(a22*a33 - a23*a32) - a02*(a20*a33 - a23*a30) + a03*(a20*a32 - a22*a30);
			T c12 = -(a00*(a21*a33 - a23*a31) - a01*(a20*a33 - a23*a30) + a03*(a20*a31 - a21*a30));
			T c13 = a00*(a21*a32 - a22*a31) - a01*(a20*a32 - a22*a30) + a02*(a20*a31 - a21*a30);

			T c20 = a01*(a12*a33 - a13*a32) - a02*(a11*a33 - a13*a31) + a03*(a11*a32 - a12*a31);
			T c21 = -(a00*(a12*a33 - a13*a32) - a02*(a10*a33 - a13*a30) + a03*(a10*a32 - a12*a30));
			T c22 = a00*(a11*a33 - a13*a31) - a01*(a10*a33 - a13*a30) + a03*(a10*a31 - a11*a30);
			T c23 = -(a00*(a11*a32 - a12*a31) - a01*(a10*a32 - a12*a30) + a02*(a10*a31 - a11*a30));

			T c30 = -(a01*(a12*a23 - a13*a22) - a02*(a11*a23 - a13*a21) + a03*(a11*a22 - a12*a21));
			T c31 = a00*(a12*a23 - a13*a22) - a02*(a10*a23 - a13*a20) + a03*(a10*a22 - a12*a20);
			T c32 = -(a00*(a11*a23 - a13*a21) - a01*(a10*a23 - a13*a20) + a03*(a10*a21 - a11*a20));
			T c33 = a00*(a11*a22 - a12*a21) - a01*(a10*a22 - a12*a20) + a02*(a10*a21 - a11*a20);

			T inv = T{1} / det;
			Mat result;
			result.Data[0][0] = c00*inv; result.Data[0][1] = c10*inv; result.Data[0][2] = c20*inv; result.Data[0][3] = c30*inv;
			result.Data[1][0] = c01*inv; result.Data[1][1] = c11*inv; result.Data[1][2] = c21*inv; result.Data[1][3] = c31*inv;
			result.Data[2][0] = c02*inv; result.Data[2][1] = c12*inv; result.Data[2][2] = c22*inv; result.Data[2][3] = c32*inv;
			result.Data[3][0] = c03*inv; result.Data[3][1] = c13*inv; result.Data[3][2] = c23*inv; result.Data[3][3] = c33*inv;
			return result;
		}
	}

	// -- Approximate comparison ---------------------------------------------

	bool NearlyEquals(const Mat& other, T epsilon = T{1e-6}) const
		requires std::floating_point<T>
	{
		for (int r = 0; r < Rows; ++r)
			for (int c = 0; c < Cols; ++c)
				if (std::abs(Data[r][c] - other.Data[r][c]) > epsilon)
					return false;
		return true;
	}

	// -- Affine transform helpers (Mat4 only) -------------------------------

	Vec<3, T> TransformPoint(const Vec<3, T>& p) const requires (Rows == 4 && Cols == 4)
	{
		Vec<4, T> h(p.X, p.Y, p.Z, T{1});
		Vec<4, T> r = *this * h;
		return Vec<3, T>(r.X, r.Y, r.Z);
	}

	Vec<3, T> TransformVector(const Vec<3, T>& v) const requires (Rows == 4 && Cols == 4)
	{
		Vec<4, T> h(v.X, v.Y, v.Z, T{0});
		Vec<4, T> r = *this * h;
		return Vec<3, T>(r.X, r.Y, r.Z);
	}

	// -- Affine inverse (Mat4, floating-point) ------------------------------
	// Cheaper than full Inverse() for matrices composed only of rotation,
	// scale, and translation (upper-left 3x3 is orthonormal after uniform
	// scale is factored out).

	Mat AffineInverse() const requires (Rows == 4 && Cols == 4 && std::floating_point<T>)
	{
		// Upper-left 3x3
		Mat<3, 3, T> upper;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				upper.Data[r][c] = Data[r][c];

		Mat<3, 3, T> invUpper = upper.Inverse();

		// Translation column
		Vec<3, T> t(Data[0][3], Data[1][3], Data[2][3]);

		// -invUpper * t
		Vec<3, T> invT;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				invT[r] -= invUpper.Data[r][c] * t[c];

		Mat result;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				result.Data[r][c] = invUpper.Data[r][c];

		result.Data[0][3] = invT.X;
		result.Data[1][3] = invT.Y;
		result.Data[2][3] = invT.Z;
		result.Data[3][3] = T{1};
		return result;
	}

	// -- Static factories ---------------------------------------------------

	static constexpr Mat Zero()
	{
		return Mat{};
	}

	static constexpr Mat Identity() requires (Rows == Cols)
	{
		Mat result;
		for (int i = 0; i < Rows; ++i)
			result.Data[i][i] = T{1};
		return result;
	}

	// -- Translation (Mat4 only) --------------------------------------------

	static constexpr Mat MakeTranslation(T x, T y, T z) requires (Rows == 4 && Cols == 4)
	{
		Mat result = Identity();
		result.Data[0][3] = x;
		result.Data[1][3] = y;
		result.Data[2][3] = z;
		return result;
	}

	static constexpr Mat MakeTranslation(const Vec<3, T>& v) requires (Rows == 4 && Cols == 4)
	{
		return MakeTranslation(v.X, v.Y, v.Z);
	}

	// -- Scale --------------------------------------------------------------

	static constexpr Mat MakeScale(T x, T y) requires (Rows == 3 && Cols == 3)
	{
		Mat result = Identity();
		result.Data[0][0] = x;
		result.Data[1][1] = y;
		return result;
	}

	static constexpr Mat MakeScale(T x, T y, T z) requires (Rows == Cols && (Rows == 3 || Rows == 4))
	{
		Mat result = Identity();
		result.Data[0][0] = x;
		result.Data[1][1] = y;
		result.Data[2][2] = z;
		return result;
	}

	static constexpr Mat MakeScale(const Vec<3, T>& v) requires (Rows == Cols && (Rows == 3 || Rows == 4))
	{
		return MakeScale(v.X, v.Y, v.Z);
	}

	// -- TRS (Translation * Rotation * Scale) -------------------------------

	static Mat MakeTRS(const Vec<3, T>& translation, const Vec<3, T>& eulerRadians, const Vec<3, T>& scale)
		requires (Rows == 4 && Cols == 4 && std::floating_point<T>)
	{
		Mat t = MakeTranslation(translation);
		Mat rx = MakeRotationX(eulerRadians.X);
		Mat ry = MakeRotationY(eulerRadians.Y);
		Mat rz = MakeRotationZ(eulerRadians.Z);
		Mat s = MakeScale(scale);
		return t * rz * ry * rx * s;
	}

	// -- Rotation around Z axis ---------------------------------------------

	static Mat MakeRotationZ(T angleRadians) requires (Rows == Cols && (Rows == 3 || Rows == 4) && std::floating_point<T>)
	{
		T c = std::cos(angleRadians);
		T s = std::sin(angleRadians);
		Mat result = Identity();
		result.Data[0][0] =  c;
		result.Data[0][1] = -s;
		result.Data[1][0] =  s;
		result.Data[1][1] =  c;
		return result;
	}

	// -- Rotation around X axis ---------------------------------------------

	static Mat MakeRotationX(T angleRadians) requires (Rows == Cols && (Rows == 3 || Rows == 4) && std::floating_point<T>)
	{
		T c = std::cos(angleRadians);
		T s = std::sin(angleRadians);
		Mat result = Identity();
		result.Data[1][1] =  c;
		result.Data[1][2] = -s;
		result.Data[2][1] =  s;
		result.Data[2][2] =  c;
		return result;
	}

	// -- Rotation around Y axis ---------------------------------------------

	static Mat MakeRotationY(T angleRadians) requires (Rows == Cols && (Rows == 3 || Rows == 4) && std::floating_point<T>)
	{
		T c = std::cos(angleRadians);
		T s = std::sin(angleRadians);
		Mat result = Identity();
		result.Data[0][0] =  c;
		result.Data[0][2] =  s;
		result.Data[2][0] = -s;
		result.Data[2][2] =  c;
		return result;
	}

	// -- Perspective projection (Mat4, floating-point) ----------------------

	static Mat MakePerspective(T fovYRadians, T aspectRatio, T nearPlane, T farPlane)
		requires (Rows == 4 && Cols == 4 && std::floating_point<T>)
	{
		assert(aspectRatio != T{0} && "Aspect ratio must be non-zero.");
		assert(nearPlane != farPlane && "Near and far planes must differ.");

		T tanHalfFov = std::tan(fovYRadians / T{2});

		Mat result; // zero-initialized
		result.Data[0][0] = T{1} / (aspectRatio * tanHalfFov);
		result.Data[1][1] = T{1} / tanHalfFov;
		result.Data[2][2] = -(farPlane + nearPlane) / (farPlane - nearPlane);
		result.Data[2][3] = -(T{2} * farPlane * nearPlane) / (farPlane - nearPlane);
		result.Data[3][2] = T{-1};
		return result;
	}

	// -- Orthographic projection (Mat4, floating-point) ---------------------

	static constexpr Mat MakeOrthographic(T left, T right, T bottom, T top, T nearPlane, T farPlane)
		requires (Rows == 4 && Cols == 4 && std::floating_point<T>)
	{
		assert(left != right  && "Left and right must differ.");
		assert(bottom != top  && "Bottom and top must differ.");
		assert(nearPlane != farPlane && "Near and far planes must differ.");

		Mat result; // zero-initialized
		result.Data[0][0] = T{2} / (right - left);
		result.Data[1][1] = T{2} / (top - bottom);
		result.Data[2][2] = T{-2} / (farPlane - nearPlane);
		result.Data[0][3] = -(right + left) / (right - left);
		result.Data[1][3] = -(top + bottom) / (top - bottom);
		result.Data[2][3] = -(farPlane + nearPlane) / (farPlane - nearPlane);
		result.Data[3][3] = T{1};
		return result;
	}

	// -- LookAt (Mat4, floating-point) --------------------------------------

	static Mat MakeLookAt(const Vec<3, T>& eye, const Vec<3, T>& target, const Vec<3, T>& up)
		requires (Rows == 4 && Cols == 4 && std::floating_point<T>)
	{
		Vec<3, T> f = (target - eye).Normalized();
		Vec<3, T> r = f.Cross(up).Normalized();
		Vec<3, T> u = r.Cross(f);

		Mat result = Identity();
		result.Data[0][0] =  r.X;
		result.Data[0][1] =  r.Y;
		result.Data[0][2] =  r.Z;
		result.Data[0][3] = -r.Dot(eye);
		result.Data[1][0] =  u.X;
		result.Data[1][1] =  u.Y;
		result.Data[1][2] =  u.Z;
		result.Data[1][3] = -u.Dot(eye);
		result.Data[2][0] = -f.X;
		result.Data[2][1] = -f.Y;
		result.Data[2][2] = -f.Z;
		result.Data[2][3] =  f.Dot(eye);
		result.Data[3][3] = T{1};
		return result;
	}
};

// -- Free function: scalar * mat -----------------------------------------------

template <int Rows, int Cols, typename T>
constexpr Mat<Rows, Cols, T> operator*(T scalar, const Mat<Rows, Cols, T>& m)
{
	return m * scalar;
}

// -- Stream output --------------------------------------------------------------

template <int Rows, int Cols, typename T>
std::ostream& operator<<(std::ostream& os, const Mat<Rows, Cols, T>& m)
{
	os << "[";
	for (int r = 0; r < Rows; ++r)
	{
		if (r > 0) os << " ";
		os << "(";
		for (int c = 0; c < Cols; ++c)
		{
			if (c > 0) os << ", ";
			os << m.Data[r][c];
		}
		os << ")";
		if (r < Rows - 1) os << "\n";
	}
	os << "]";
	return os;
}

// -- Common aliases -------------------------------------------------------------

using Mat2d  = Mat<2, 2>;
using Mat3d  = Mat<3, 3>;
using Mat4  = Mat<4, 4>;

using Mat2dd = Mat<2, 2, double>;
using Mat3dd = Mat<3, 3, double>;
using Mat4d = Mat<4, 4, double>;

using Mat2i = Mat<2, 2, int>;
using Mat3i = Mat<3, 3, int>;
using Mat4i = Mat<4, 4, int>;
