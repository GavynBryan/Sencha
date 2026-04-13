#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <ostream>
#include <type_traits>

template <int N, typename T = float>
struct Vec;

//=============================================================================
// VecOps<TDerived, N, T>
//
// Shared vector behavior for both generic N-dimensional vectors and the common
// field-backed Vec2d/Vec3d/Vec4 specializations.
//=============================================================================
template <typename TDerived, int N, typename T>
struct VecOps
{
	static_assert(N > 0, "Vec dimension must be at least 1.");
	static_assert(std::is_arithmetic_v<T>, "Vec component type must be arithmetic.");

	constexpr T& operator[](int index)
	{
		assert(index >= 0 && index < N && "Vec index out of range.");
		return Self().AtUnchecked(index);
	}

	constexpr const T& operator[](int index) const
	{
		assert(index >= 0 && index < N && "Vec index out of range.");
		return Self().AtUnchecked(index);
	}

	constexpr TDerived operator+(const TDerived& other) const
	{
		TDerived result;
		for (int i = 0; i < N; ++i)
			result[i] = (*this)[i] + other[i];
		return result;
	}

	constexpr TDerived operator-(const TDerived& other) const
	{
		TDerived result;
		for (int i = 0; i < N; ++i)
			result[i] = (*this)[i] - other[i];
		return result;
	}

	constexpr TDerived operator*(T scalar) const
	{
		TDerived result;
		for (int i = 0; i < N; ++i)
			result[i] = (*this)[i] * scalar;
		return result;
	}

	constexpr TDerived operator/(T scalar) const
	{
		assert(scalar != T{0} && "Vec division by zero.");
		TDerived result;
		for (int i = 0; i < N; ++i)
			result[i] = (*this)[i] / scalar;
		return result;
	}

	constexpr TDerived& operator+=(const TDerived& other)
	{
		for (int i = 0; i < N; ++i)
			(*this)[i] += other[i];
		return Self();
	}

	constexpr TDerived& operator-=(const TDerived& other)
	{
		for (int i = 0; i < N; ++i)
			(*this)[i] -= other[i];
		return Self();
	}

	constexpr TDerived& operator*=(T scalar)
	{
		for (int i = 0; i < N; ++i)
			(*this)[i] *= scalar;
		return Self();
	}

	constexpr TDerived& operator/=(T scalar)
	{
		assert(scalar != T{0} && "Vec division by zero.");
		for (int i = 0; i < N; ++i)
			(*this)[i] /= scalar;
		return Self();
	}

	constexpr TDerived operator-() const
	{
		TDerived result;
		for (int i = 0; i < N; ++i)
			result[i] = -(*this)[i];
		return result;
	}

	constexpr T Dot(const TDerived& other) const
	{
		T sum = T{0};
		for (int i = 0; i < N; ++i)
			sum += (*this)[i] * other[i];
		return sum;
	}

	constexpr T SqrMagnitude() const
	{
		return Dot(Self());
	}

	T Magnitude() const requires std::floating_point<T>
	{
		return std::sqrt(SqrMagnitude());
	}

	TDerived Normalized() const requires std::floating_point<T>
	{
		T mag = Magnitude();
		assert(mag > T{0} && "Cannot normalize a zero-length Vec.");
		return Self() / mag;
	}

	constexpr TDerived Cross(const TDerived& other) const requires (N == 3)
	{
		return TDerived{
			(*this)[1] * other[2] - (*this)[2] * other[1],
			(*this)[2] * other[0] - (*this)[0] * other[2],
			(*this)[0] * other[1] - (*this)[1] * other[0]
		};
	}

	static constexpr TDerived Zero()
	{
		return TDerived{};
	}

	static constexpr TDerived One()
	{
		TDerived result;
		for (int i = 0; i < N; ++i)
			result[i] = T{1};
		return result;
	}

	static constexpr TDerived Right() requires (N == 2 || N == 3)
	{
		TDerived result;
		result[0] = T{1};
		return result;
	}

	static constexpr TDerived Left() requires (N == 2 || N == 3)
	{
		TDerived result;
		result[0] = T{-1};
		return result;
	}

	static constexpr TDerived Up() requires (N == 2 || N == 3)
	{
		TDerived result;
		result[1] = T{1};
		return result;
	}

	static constexpr TDerived Down() requires (N == 2 || N == 3)
	{
		TDerived result;
		result[1] = T{-1};
		return result;
	}

	static constexpr TDerived Forward() requires (N == 3)
	{
		TDerived result;
		result[2] = T{-1};
		return result;
	}

	static constexpr TDerived Backward() requires (N == 3)
	{
		TDerived result;
		result[2] = T{1};
		return result;
	}

	static constexpr TDerived Lerp(const TDerived& a, const TDerived& b, T t)
	{
		TDerived result;
		for (int i = 0; i < N; ++i)
			result[i] = a[i] + t * (b[i] - a[i]);
		return result;
	}

	static T Distance(const TDerived& a, const TDerived& b) requires std::floating_point<T>
	{
		return (a - b).Magnitude();
	}

	static constexpr T SqrDistance(const TDerived& a, const TDerived& b)
	{
		return (a - b).SqrMagnitude();
	}

private:
	constexpr TDerived& Self()
	{
		return static_cast<TDerived&>(*this);
	}

	constexpr const TDerived& Self() const
	{
		return static_cast<const TDerived&>(*this);
	}
};

//=============================================================================
// Vec<N, T>
//
// N-dimensional vector with arithmetic component type T (default: float).
// Generic dimensions use indexed storage. Vec2d, Vec3d, and Vec4 are specialized
// below with first-class X/Y/Z/W fields.
//
// Direction conventions:
//   - Right is +X and Up is +Y for 2D and 3D.
//   - 3D Forward is -Z, matching Sencha's view/look-at convention.
//
// Common aliases:
//   Vec2d, Vec3d, Vec4           (float)
//   Vec2dd, Vec3dd, Vec4d        (double)
//   Vec2i, Vec3i, Vec4i        (int)
//=============================================================================
template <int N, typename T>
struct Vec : VecOps<Vec<N, T>, N, T>
{
	static_assert(N > 0, "Vec dimension must be at least 1.");
	static_assert(std::is_arithmetic_v<T>, "Vec component type must be arithmetic.");

	static constexpr int Dimensions = N;
	T Data[N] = {};

	constexpr Vec() = default;

	template <typename... Args>
		requires (sizeof...(Args) == N && (std::convertible_to<Args, T> && ...))
	constexpr Vec(Args... args) : Data{ static_cast<T>(args)... } {}

	constexpr T& AtUnchecked(int index) { return Data[index]; }
	constexpr const T& AtUnchecked(int index) const { return Data[index]; }

	constexpr bool operator==(const Vec& other) const
	{
		for (int i = 0; i < N; ++i)
		{
			if (Data[i] != other.Data[i]) return false;
		}
		return true;
	}
};

template <typename T>
struct Vec<2, T> : VecOps<Vec<2, T>, 2, T>
{
	static_assert(std::is_arithmetic_v<T>, "Vec component type must be arithmetic.");

	static constexpr int Dimensions = 2;
	T X = T{};
	T Y = T{};

	constexpr Vec() = default;
	constexpr Vec(T x, T y) : X(x), Y(y) {}

	constexpr T& AtUnchecked(int index)
	{
		return index == 0 ? X : Y;
	}

	constexpr const T& AtUnchecked(int index) const
	{
		return index == 0 ? X : Y;
	}

	constexpr bool operator==(const Vec& other) const
	{
		return X == other.X && Y == other.Y;
	}
};

template <typename T>
struct Vec<3, T> : VecOps<Vec<3, T>, 3, T>
{
	static_assert(std::is_arithmetic_v<T>, "Vec component type must be arithmetic.");

	static constexpr int Dimensions = 3;
	T X = T{};
	T Y = T{};
	T Z = T{};

	constexpr Vec() = default;
	constexpr Vec(T x, T y, T z) : X(x), Y(y), Z(z) {}

	constexpr T& AtUnchecked(int index)
	{
		return index == 0 ? X : (index == 1 ? Y : Z);
	}

	constexpr const T& AtUnchecked(int index) const
	{
		return index == 0 ? X : (index == 1 ? Y : Z);
	}

	constexpr bool operator==(const Vec& other) const
	{
		return X == other.X && Y == other.Y && Z == other.Z;
	}
};

template <typename T>
struct Vec<4, T> : VecOps<Vec<4, T>, 4, T>
{
	static_assert(std::is_arithmetic_v<T>, "Vec component type must be arithmetic.");

	static constexpr int Dimensions = 4;
	T X = T{};
	T Y = T{};
	T Z = T{};
	T W = T{};

	constexpr Vec() = default;
	constexpr Vec(T x, T y, T z, T w) : X(x), Y(y), Z(z), W(w) {}

	constexpr T& AtUnchecked(int index)
	{
		return index == 0 ? X : (index == 1 ? Y : (index == 2 ? Z : W));
	}

	constexpr const T& AtUnchecked(int index) const
	{
		return index == 0 ? X : (index == 1 ? Y : (index == 2 ? Z : W));
	}

	constexpr bool operator==(const Vec& other) const
	{
		return X == other.X && Y == other.Y && Z == other.Z && W == other.W;
	}
};

// -- Free function: scalar * vec --------------------------------------------

template <int N, typename T>
constexpr Vec<N, T> operator*(T scalar, const Vec<N, T>& v)
{
	return v * scalar;
}

// -- Stream output ----------------------------------------------------------

template <int N, typename T>
std::ostream& operator<<(std::ostream& os, const Vec<N, T>& v)
{
	os << "(";
	for (int i = 0; i < N; ++i)
	{
		if (i > 0) os << ", ";
		os << v[i];
	}
	os << ")";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Vec2d  = Vec<2>;
using Vec3d  = Vec<3>;
using Vec4  = Vec<4>;

using Vec2dd = Vec<2, double>;
using Vec3dd = Vec<3, double>;
using Vec4d = Vec<4, double>;

using Vec2i = Vec<2, int>;
using Vec3i = Vec<3, int>;
using Vec4i = Vec<4, int>;
