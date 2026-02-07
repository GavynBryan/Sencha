#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <ostream>
#include <type_traits>

//=============================================================================
// Vec<N, T>
//
// N-dimensional vector with arithmetic component type T (default: float).
// Dimension-agnostic — works for 2D, 3D, 4D, or any positive dimension.
//
// Named accessors X(), Y(), Z(), W() are available when the dimension
// count supports them (enforced at compile time via requires clauses).
//
// Cross() is restricted to 3-dimensional vectors at compile time.
//
// Extensibility:
//   - Any positive dimension count is supported.
//   - Any arithmetic component type (float, double, int, etc.).
//   - Add new dimension-gated operations with requires (N == K).
//
// Common aliases:
//   Vec2, Vec3, Vec4           (float)
//   Vec2d, Vec3d, Vec4d        (double)
//   Vec2i, Vec3i, Vec4i        (int)
//=============================================================================
template <int N, typename T = float>
struct Vec
{
	static_assert(N > 0, "Vec dimension must be at least 1.");
	static_assert(std::is_arithmetic_v<T>, "Vec component type must be arithmetic.");

	// -- Data ---------------------------------------------------------------

	static constexpr int Dimensions = N;
	T Data[N] = {};

	// -- Construction -------------------------------------------------------

	constexpr Vec() = default;

	template <typename... Args>
		requires (sizeof...(Args) == N && (std::convertible_to<Args, T> && ...))
	constexpr Vec(Args... args) : Data{ static_cast<T>(args)... } {}

	// -- Named accessors (dimension-gated) ----------------------------------

	constexpr T& X() { return Data[0]; }
	constexpr const T& X() const { return Data[0]; }

	constexpr T& Y() requires (N >= 2) { return Data[1]; }
	constexpr const T& Y() const requires (N >= 2) { return Data[1]; }

	constexpr T& Z() requires (N >= 3) { return Data[2]; }
	constexpr const T& Z() const requires (N >= 3) { return Data[2]; }

	constexpr T& W() requires (N >= 4) { return Data[3]; }
	constexpr const T& W() const requires (N >= 4) { return Data[3]; }

	// -- Element access -----------------------------------------------------

	constexpr T& operator[](int index)
	{
		assert(index >= 0 && index < N && "Vec index out of range.");
		return Data[index];
	}

	constexpr const T& operator[](int index) const
	{
		assert(index >= 0 && index < N && "Vec index out of range.");
		return Data[index];
	}

	// -- Arithmetic operators -----------------------------------------------

	constexpr Vec operator+(const Vec& other) const
	{
		Vec result;
		for (int i = 0; i < N; ++i)
			result.Data[i] = Data[i] + other.Data[i];
		return result;
	}

	constexpr Vec operator-(const Vec& other) const
	{
		Vec result;
		for (int i = 0; i < N; ++i)
			result.Data[i] = Data[i] - other.Data[i];
		return result;
	}

	constexpr Vec operator*(T scalar) const
	{
		Vec result;
		for (int i = 0; i < N; ++i)
			result.Data[i] = Data[i] * scalar;
		return result;
	}

	constexpr Vec operator/(T scalar) const
	{
		assert(scalar != T{0} && "Vec division by zero.");
		Vec result;
		for (int i = 0; i < N; ++i)
			result.Data[i] = Data[i] / scalar;
		return result;
	}

	constexpr Vec& operator+=(const Vec& other)
	{
		for (int i = 0; i < N; ++i)
			Data[i] += other.Data[i];
		return *this;
	}

	constexpr Vec& operator-=(const Vec& other)
	{
		for (int i = 0; i < N; ++i)
			Data[i] -= other.Data[i];
		return *this;
	}

	constexpr Vec& operator*=(T scalar)
	{
		for (int i = 0; i < N; ++i)
			Data[i] *= scalar;
		return *this;
	}

	constexpr Vec& operator/=(T scalar)
	{
		assert(scalar != T{0} && "Vec division by zero.");
		for (int i = 0; i < N; ++i)
			Data[i] /= scalar;
		return *this;
	}

	constexpr Vec operator-() const
	{
		Vec result;
		for (int i = 0; i < N; ++i)
			result.Data[i] = -Data[i];
		return result;
	}

	// -- Comparison ---------------------------------------------------------

	constexpr bool operator==(const Vec& other) const
	{
		for (int i = 0; i < N; ++i)
		{
			if (Data[i] != other.Data[i]) return false;
		}
		return true;
	}

	// -- Vector operations --------------------------------------------------

	constexpr T Dot(const Vec& other) const
	{
		T sum = T{0};
		for (int i = 0; i < N; ++i)
			sum += Data[i] * other.Data[i];
		return sum;
	}

	constexpr T SqrMagnitude() const
	{
		return Dot(*this);
	}

	T Magnitude() const requires std::floating_point<T>
	{
		return std::sqrt(SqrMagnitude());
	}

	Vec Normalized() const requires std::floating_point<T>
	{
		T mag = Magnitude();
		assert(mag > T{0} && "Cannot normalize a zero-length Vec.");
		return *this / mag;
	}

	// -- 3D-only operations -------------------------------------------------

	constexpr Vec Cross(const Vec& other) const requires (N == 3)
	{
		return Vec{
			Data[1] * other.Data[2] - Data[2] * other.Data[1],
			Data[2] * other.Data[0] - Data[0] * other.Data[2],
			Data[0] * other.Data[1] - Data[1] * other.Data[0]
		};
	}

	// -- Static factories ---------------------------------------------------

	static constexpr Vec Zero()
	{
		return Vec{};
	}

	static constexpr Vec One()
	{
		Vec result;
		for (int i = 0; i < N; ++i)
			result.Data[i] = T{1};
		return result;
	}

	// -- Static utilities ---------------------------------------------------

	static constexpr Vec Lerp(const Vec& a, const Vec& b, T t)
	{
		Vec result;
		for (int i = 0; i < N; ++i)
			result.Data[i] = a.Data[i] + t * (b.Data[i] - a.Data[i]);
		return result;
	}

	static T Distance(const Vec& a, const Vec& b) requires std::floating_point<T>
	{
		return (a - b).Magnitude();
	}

	static constexpr T SqrDistance(const Vec& a, const Vec& b)
	{
		return (a - b).SqrMagnitude();
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
		os << v.Data[i];
	}
	os << ")";
	return os;
}

// -- Common aliases ---------------------------------------------------------

using Vec2  = Vec<2>;
using Vec3  = Vec<3>;
using Vec4  = Vec<4>;

using Vec2d = Vec<2, double>;
using Vec3d = Vec<3, double>;
using Vec4d = Vec<4, double>;

using Vec2i = Vec<2, int>;
using Vec3i = Vec<3, int>;
using Vec4i = Vec<4, int>;
