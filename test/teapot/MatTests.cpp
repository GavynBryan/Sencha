#include <gtest/gtest.h>
#include <math/Mat.h>
#include <sstream>
#include <cmath>

// --- Construction ---

TEST(Mat, DefaultConstructionIsZero)
{
	Mat4 m;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			EXPECT_FLOAT_EQ(m[r][c], 0.0f);
}

// --- Element Access ---

TEST(Mat, BracketOperator)
{
	Mat3 m;
	m[0][0] = 1.0f;
	m[1][2] = 5.0f;
	EXPECT_FLOAT_EQ(m[0][0], 1.0f);
	EXPECT_FLOAT_EQ(m[1][2], 5.0f);
}

TEST(Mat, AtAccessor)
{
	Mat3 m;
	m.At(2, 1) = 7.0f;
	EXPECT_FLOAT_EQ(m.At(2, 1), 7.0f);
}

// --- Identity ---

TEST(Mat, Identity2)
{
	auto m = Mat2::Identity();
	EXPECT_FLOAT_EQ(m[0][0], 1.0f);
	EXPECT_FLOAT_EQ(m[0][1], 0.0f);
	EXPECT_FLOAT_EQ(m[1][0], 0.0f);
	EXPECT_FLOAT_EQ(m[1][1], 1.0f);
}

TEST(Mat, Identity3)
{
	auto m = Mat3::Identity();
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			EXPECT_FLOAT_EQ(m[r][c], r == c ? 1.0f : 0.0f);
}

TEST(Mat, Identity4)
{
	auto m = Mat4::Identity();
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			EXPECT_FLOAT_EQ(m[r][c], r == c ? 1.0f : 0.0f);
}

// --- Arithmetic ---

TEST(Mat, Addition)
{
	auto a = Mat2::Identity();
	auto b = Mat2::Identity();
	auto c = a + b;
	EXPECT_FLOAT_EQ(c[0][0], 2.0f);
	EXPECT_FLOAT_EQ(c[0][1], 0.0f);
	EXPECT_FLOAT_EQ(c[1][0], 0.0f);
	EXPECT_FLOAT_EQ(c[1][1], 2.0f);
}

TEST(Mat, Subtraction)
{
	auto a = Mat2::Identity();
	Mat2 b;
	auto c = a - b;
	EXPECT_EQ(c, a);
}

TEST(Mat, ScalarMultiplication)
{
	auto m = Mat2::Identity();
	auto r = m * 3.0f;
	EXPECT_FLOAT_EQ(r[0][0], 3.0f);
	EXPECT_FLOAT_EQ(r[1][1], 3.0f);
	EXPECT_FLOAT_EQ(r[0][1], 0.0f);
}

TEST(Mat, ScalarMultiplicationLeftHand)
{
	auto m = Mat2::Identity();
	auto r = 3.0f * m;
	EXPECT_FLOAT_EQ(r[0][0], 3.0f);
	EXPECT_FLOAT_EQ(r[1][1], 3.0f);
}

TEST(Mat, ScalarDivision)
{
	auto m = Mat2::Identity() * 4.0f;
	auto r = m / 2.0f;
	EXPECT_FLOAT_EQ(r[0][0], 2.0f);
	EXPECT_FLOAT_EQ(r[1][1], 2.0f);
}

TEST(Mat, CompoundAddition)
{
	auto m = Mat2::Identity();
	m += Mat2::Identity();
	EXPECT_FLOAT_EQ(m[0][0], 2.0f);
}

TEST(Mat, CompoundSubtraction)
{
	auto m = Mat2::Identity() * 3.0f;
	m -= Mat2::Identity();
	EXPECT_FLOAT_EQ(m[0][0], 2.0f);
}

TEST(Mat, CompoundScalarMultiplication)
{
	auto m = Mat2::Identity();
	m *= 5.0f;
	EXPECT_FLOAT_EQ(m[0][0], 5.0f);
}

TEST(Mat, CompoundScalarDivision)
{
	auto m = Mat2::Identity() * 6.0f;
	m /= 3.0f;
	EXPECT_FLOAT_EQ(m[0][0], 2.0f);
}

TEST(Mat, UnaryNegation)
{
	auto m = Mat2::Identity();
	auto r = -m;
	EXPECT_FLOAT_EQ(r[0][0], -1.0f);
	EXPECT_FLOAT_EQ(r[1][1], -1.0f);
	EXPECT_FLOAT_EQ(r[0][1], 0.0f);
}

// --- Matrix Multiplication ---

TEST(Mat, MatrixMultiplyIdentity)
{
	auto id = Mat4::Identity();
	auto m = Mat4::MakeTranslation(1.0f, 2.0f, 3.0f);
	auto r = id * m;
	EXPECT_EQ(r, m);
}

TEST(Mat, MatrixMultiply2x2)
{
	Mat2 a;
	a[0][0] = 1.0f; a[0][1] = 2.0f;
	a[1][0] = 3.0f; a[1][1] = 4.0f;

	Mat2 b;
	b[0][0] = 5.0f; b[0][1] = 6.0f;
	b[1][0] = 7.0f; b[1][1] = 8.0f;

	auto c = a * b;
	EXPECT_FLOAT_EQ(c[0][0], 19.0f);
	EXPECT_FLOAT_EQ(c[0][1], 22.0f);
	EXPECT_FLOAT_EQ(c[1][0], 43.0f);
	EXPECT_FLOAT_EQ(c[1][1], 50.0f);
}

TEST(Mat, MatrixMultiplyNonSquare)
{
	Mat<2, 3> a;
	a[0][0] = 1.0f; a[0][1] = 2.0f; a[0][2] = 3.0f;
	a[1][0] = 4.0f; a[1][1] = 5.0f; a[1][2] = 6.0f;

	Mat<3, 2> b;
	b[0][0] = 7.0f;  b[0][1] = 8.0f;
	b[1][0] = 9.0f;  b[1][1] = 10.0f;
	b[2][0] = 11.0f; b[2][1] = 12.0f;

	auto c = a * b; // 2x2 result
	EXPECT_FLOAT_EQ(c[0][0], 58.0f);
	EXPECT_FLOAT_EQ(c[0][1], 64.0f);
	EXPECT_FLOAT_EQ(c[1][0], 139.0f);
	EXPECT_FLOAT_EQ(c[1][1], 154.0f);
}

TEST(Mat, CompoundMatrixMultiply)
{
	auto m = Mat3::Identity();
	auto s = Mat3::MakeScale(2.0f, 3.0f, 4.0f);
	m *= s;
	EXPECT_FLOAT_EQ(m[0][0], 2.0f);
	EXPECT_FLOAT_EQ(m[1][1], 3.0f);
	EXPECT_FLOAT_EQ(m[2][2], 4.0f);
}

// --- Matrix-Vector Multiplication ---

TEST(Mat, MatVecMultiplyIdentity)
{
	auto id = Mat4::Identity();
	Vec4 v(1.0f, 2.0f, 3.0f, 1.0f);
	Vec4 r = id * v;
	EXPECT_EQ(r, v);
}

TEST(Mat, MatVecMultiplyTranslation)
{
	auto t = Mat4::MakeTranslation(10.0f, 20.0f, 30.0f);
	Vec4 v(1.0f, 2.0f, 3.0f, 1.0f);
	Vec4 r = t * v;
	EXPECT_FLOAT_EQ(r.X, 11.0f);
	EXPECT_FLOAT_EQ(r.Y, 22.0f);
	EXPECT_FLOAT_EQ(r.Z, 33.0f);
	EXPECT_FLOAT_EQ(r.W, 1.0f);
}

TEST(Mat, MatVecMultiplyScale)
{
	auto s = Mat4::MakeScale(2.0f, 3.0f, 4.0f);
	Vec4 v(1.0f, 1.0f, 1.0f, 1.0f);
	Vec4 r = s * v;
	EXPECT_FLOAT_EQ(r.X, 2.0f);
	EXPECT_FLOAT_EQ(r.Y, 3.0f);
	EXPECT_FLOAT_EQ(r.Z, 4.0f);
	EXPECT_FLOAT_EQ(r.W, 1.0f);
}

// --- Comparison ---

TEST(Mat, Equality)
{
	auto a = Mat3::Identity();
	auto b = Mat3::Identity();
	EXPECT_TRUE(a == b);
}

TEST(Mat, Inequality)
{
	auto a = Mat3::Identity();
	Mat3 b;
	EXPECT_TRUE(a != b);
}

// --- Transpose ---

TEST(Mat, TransposeSquare)
{
	Mat3 m;
	m[0][1] = 2.0f;
	m[1][0] = 5.0f;
	auto t = m.Transposed();
	EXPECT_FLOAT_EQ(t[0][1], 5.0f);
	EXPECT_FLOAT_EQ(t[1][0], 2.0f);
}

TEST(Mat, TransposeNonSquare)
{
	Mat<2, 3> m;
	m[0][0] = 1.0f; m[0][1] = 2.0f; m[0][2] = 3.0f;
	m[1][0] = 4.0f; m[1][1] = 5.0f; m[1][2] = 6.0f;

	auto t = m.Transposed(); // Mat<3, 2>
	EXPECT_EQ(t.RowCount, 3);
	EXPECT_EQ(t.ColCount, 2);
	EXPECT_FLOAT_EQ(t[0][0], 1.0f);
	EXPECT_FLOAT_EQ(t[1][0], 2.0f);
	EXPECT_FLOAT_EQ(t[2][0], 3.0f);
	EXPECT_FLOAT_EQ(t[0][1], 4.0f);
	EXPECT_FLOAT_EQ(t[1][1], 5.0f);
	EXPECT_FLOAT_EQ(t[2][1], 6.0f);
}

TEST(Mat, TransposeIdentity)
{
	auto m = Mat4::Identity();
	EXPECT_EQ(m.Transposed(), m);
}

// --- Determinant ---

TEST(Mat, Determinant2x2)
{
	Mat2 m;
	m[0][0] = 1.0f; m[0][1] = 2.0f;
	m[1][0] = 3.0f; m[1][1] = 4.0f;
	EXPECT_FLOAT_EQ(m.Determinant(), -2.0f);
}

TEST(Mat, Determinant3x3)
{
	Mat3 m;
	m[0][0] = 6.0f; m[0][1] = 1.0f; m[0][2] = 1.0f;
	m[1][0] = 4.0f; m[1][1] = -2.0f; m[1][2] = 5.0f;
	m[2][0] = 2.0f; m[2][1] = 8.0f; m[2][2] = 7.0f;
	EXPECT_FLOAT_EQ(m.Determinant(), -306.0f);
}

TEST(Mat, Determinant4x4Identity)
{
	auto m = Mat4::Identity();
	EXPECT_FLOAT_EQ(m.Determinant(), 1.0f);
}

TEST(Mat, Determinant4x4)
{
	Mat4 m;
	m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 2.0f; m[0][3] = -1.0f;
	m[1][0] = 3.0f; m[1][1] = 0.0f; m[1][2] = 0.0f; m[1][3] = 5.0f;
	m[2][0] = 2.0f; m[2][1] = 1.0f; m[2][2] = 4.0f; m[2][3] = -3.0f;
	m[3][0] = 1.0f; m[3][1] = 0.0f; m[3][2] = 5.0f; m[3][3] = 0.0f;
	EXPECT_FLOAT_EQ(m.Determinant(), 30.0f);
}

// --- Inverse ---

TEST(Mat, Inverse2x2)
{
	Mat2 m;
	m[0][0] = 4.0f; m[0][1] = 7.0f;
	m[1][0] = 2.0f; m[1][1] = 6.0f;

	auto inv = m.Inverse();
	auto product = m * inv;
	auto id = Mat2::Identity();

	for (int r = 0; r < 2; ++r)
		for (int c = 0; c < 2; ++c)
			EXPECT_NEAR(product[r][c], id[r][c], 1e-5f);
}

TEST(Mat, Inverse3x3)
{
	Mat3 m;
	m[0][0] = 1.0f; m[0][1] = 2.0f; m[0][2] = 3.0f;
	m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 4.0f;
	m[2][0] = 5.0f; m[2][1] = 6.0f; m[2][2] = 0.0f;

	auto inv = m.Inverse();
	auto product = m * inv;
	auto id = Mat3::Identity();

	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
			EXPECT_NEAR(product[r][c], id[r][c], 1e-5f);
}

TEST(Mat, Inverse4x4)
{
	auto m = Mat4::MakeTranslation(5.0f, 10.0f, 15.0f);
	auto inv = m.Inverse();
	auto product = m * inv;
	auto id = Mat4::Identity();

	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			EXPECT_NEAR(product[r][c], id[r][c], 1e-5f);
}

TEST(Mat, InverseIdentity)
{
	auto id = Mat4::Identity();
	auto inv = id.Inverse();
	EXPECT_EQ(inv, id);
}

// --- MakeTranslation ---

TEST(Mat, MakeTranslationXYZ)
{
	auto m = Mat4::MakeTranslation(1.0f, 2.0f, 3.0f);
	EXPECT_FLOAT_EQ(m[0][3], 1.0f);
	EXPECT_FLOAT_EQ(m[1][3], 2.0f);
	EXPECT_FLOAT_EQ(m[2][3], 3.0f);
	// Diagonal is identity
	EXPECT_FLOAT_EQ(m[0][0], 1.0f);
	EXPECT_FLOAT_EQ(m[1][1], 1.0f);
	EXPECT_FLOAT_EQ(m[2][2], 1.0f);
	EXPECT_FLOAT_EQ(m[3][3], 1.0f);
}

TEST(Mat, MakeTranslationVec)
{
	Vec3 v(4.0f, 5.0f, 6.0f);
	auto m = Mat4::MakeTranslation(v);
	EXPECT_FLOAT_EQ(m[0][3], 4.0f);
	EXPECT_FLOAT_EQ(m[1][3], 5.0f);
	EXPECT_FLOAT_EQ(m[2][3], 6.0f);
}

// --- MakeScale ---

TEST(Mat, MakeScale3x3_2D)
{
	auto m = Mat3::MakeScale(2.0f, 3.0f);
	EXPECT_FLOAT_EQ(m[0][0], 2.0f);
	EXPECT_FLOAT_EQ(m[1][1], 3.0f);
	EXPECT_FLOAT_EQ(m[2][2], 1.0f);
}

TEST(Mat, MakeScale3x3)
{
	auto m = Mat3::MakeScale(2.0f, 3.0f, 4.0f);
	EXPECT_FLOAT_EQ(m[0][0], 2.0f);
	EXPECT_FLOAT_EQ(m[1][1], 3.0f);
	EXPECT_FLOAT_EQ(m[2][2], 4.0f);
}

TEST(Mat, MakeScale4x4)
{
	auto m = Mat4::MakeScale(2.0f, 3.0f, 4.0f);
	EXPECT_FLOAT_EQ(m[0][0], 2.0f);
	EXPECT_FLOAT_EQ(m[1][1], 3.0f);
	EXPECT_FLOAT_EQ(m[2][2], 4.0f);
	EXPECT_FLOAT_EQ(m[3][3], 1.0f);
}

TEST(Mat, MakeScaleVec)
{
	Vec3 s(5.0f, 6.0f, 7.0f);
	auto m = Mat4::MakeScale(s);
	EXPECT_FLOAT_EQ(m[0][0], 5.0f);
	EXPECT_FLOAT_EQ(m[1][1], 6.0f);
	EXPECT_FLOAT_EQ(m[2][2], 7.0f);
}

// --- MakeRotationZ ---

TEST(Mat, MakeRotationZ_90Degrees)
{
	constexpr float pi = 3.14159265358979323846f;
	auto m = Mat4::MakeRotationZ(pi / 2.0f);

	// Rotating (1, 0, 0, 1) by 90 degrees -> (0, 1, 0, 1)
	Vec4 v(1.0f, 0.0f, 0.0f, 1.0f);
	Vec4 r = m * v;
	EXPECT_NEAR(r.X, 0.0f, 1e-6f);
	EXPECT_NEAR(r.Y, 1.0f, 1e-6f);
	EXPECT_NEAR(r.Z, 0.0f, 1e-6f);
	EXPECT_NEAR(r.W, 1.0f, 1e-6f);
}

TEST(Mat, MakeRotationZ_180Degrees)
{
	constexpr float pi = 3.14159265358979323846f;
	auto m = Mat3::MakeRotationZ(pi);

	Vec3 v(1.0f, 0.0f, 0.0f);
	Vec3 r = m * v;
	EXPECT_NEAR(r.X, -1.0f, 1e-5f);
	EXPECT_NEAR(r.Y, 0.0f, 1e-5f);
}

// --- MakeRotationX ---

TEST(Mat, MakeRotationX_90Degrees)
{
	constexpr float pi = 3.14159265358979323846f;
	auto m = Mat4::MakeRotationX(pi / 2.0f);

	// Rotating (0, 1, 0, 1) by 90 degrees around X -> (0, 0, 1, 1)
	Vec4 v(0.0f, 1.0f, 0.0f, 1.0f);
	Vec4 r = m * v;
	EXPECT_NEAR(r.X, 0.0f, 1e-6f);
	EXPECT_NEAR(r.Y, 0.0f, 1e-6f);
	EXPECT_NEAR(r.Z, 1.0f, 1e-6f);
	EXPECT_NEAR(r.W, 1.0f, 1e-6f);
}

// --- MakeRotationY ---

TEST(Mat, MakeRotationY_90Degrees)
{
	constexpr float pi = 3.14159265358979323846f;
	auto m = Mat4::MakeRotationY(pi / 2.0f);

	// Rotating (0, 0, 1, 1) by 90 degrees around Y -> (1, 0, 0, 1)
	Vec4 v(0.0f, 0.0f, 1.0f, 1.0f);
	Vec4 r = m * v;
	EXPECT_NEAR(r.X, 1.0f, 1e-6f);
	EXPECT_NEAR(r.Y, 0.0f, 1e-6f);
	EXPECT_NEAR(r.Z, 0.0f, 1e-6f);
	EXPECT_NEAR(r.W, 1.0f, 1e-6f);
}

// --- MakePerspective ---

TEST(Mat, MakePerspectiveBasicProperties)
{
	constexpr float pi = 3.14159265358979323846f;
	auto m = Mat4::MakePerspective(pi / 4.0f, 16.0f / 9.0f, 0.1f, 100.0f);

	// Should not be identity
	EXPECT_NE(m, Mat4::Identity());
	// [3][2] should be -1 for standard perspective
	EXPECT_FLOAT_EQ(m[3][2], -1.0f);
	// [3][3] should be 0
	EXPECT_FLOAT_EQ(m[3][3], 0.0f);
	// Diagonal elements should be positive
	EXPECT_GT(m[0][0], 0.0f);
	EXPECT_GT(m[1][1], 0.0f);
}

TEST(Mat, MakePerspectiveAspectRatio)
{
	constexpr float pi = 3.14159265358979323846f;
	auto wide = Mat4::MakePerspective(pi / 4.0f, 2.0f, 0.1f, 100.0f);
	auto narrow = Mat4::MakePerspective(pi / 4.0f, 0.5f, 0.1f, 100.0f);
	// Wider aspect should have smaller [0][0]
	EXPECT_LT(wide[0][0], narrow[0][0]);
	// [1][1] should be the same (same fov)
	EXPECT_FLOAT_EQ(wide[1][1], narrow[1][1]);
}

// --- MakeOrthographic ---

TEST(Mat, MakeOrthographic)
{
	auto m = Mat4::MakeOrthographic(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);

	// With symmetric bounds, translation terms in X and Y should be 0
	EXPECT_FLOAT_EQ(m[0][3], 0.0f);
	EXPECT_FLOAT_EQ(m[1][3], 0.0f);
	// Scale X and Y
	EXPECT_FLOAT_EQ(m[0][0], 1.0f);
	EXPECT_FLOAT_EQ(m[1][1], 1.0f);
	// W row
	EXPECT_FLOAT_EQ(m[3][3], 1.0f);
	EXPECT_FLOAT_EQ(m[3][0], 0.0f);
	EXPECT_FLOAT_EQ(m[3][1], 0.0f);
	EXPECT_FLOAT_EQ(m[3][2], 0.0f);
}

TEST(Mat, MakeOrthographicAsymmetric)
{
	auto m = Mat4::MakeOrthographic(0.0f, 800.0f, 0.0f, 600.0f, -1.0f, 1.0f);

	// Verify that a point at the center maps correctly
	Vec4 center(400.0f, 300.0f, 0.0f, 1.0f);
	Vec4 r = m * center;
	EXPECT_NEAR(r.X, 0.0f, 1e-5f);
	EXPECT_NEAR(r.Y, 0.0f, 1e-5f);
}

// --- MakeLookAt ---

TEST(Mat, MakeLookAtForward)
{
	Vec3 eye(0.0f, 0.0f, 5.0f);
	Vec3 target(0.0f, 0.0f, 0.0f);
	Vec3 up(0.0f, 1.0f, 0.0f);

	auto m = Mat4::MakeLookAt(eye, target, up);

	// The origin (0,0,0) should be at (0,0,-5) in view space
	Vec4 origin(0.0f, 0.0f, 0.0f, 1.0f);
	Vec4 r = m * origin;
	EXPECT_NEAR(r.X, 0.0f, 1e-5f);
	EXPECT_NEAR(r.Y, 0.0f, 1e-5f);
	EXPECT_NEAR(r.Z, -5.0f, 1e-5f);
}

TEST(Mat, MakeLookAtEyeAtOrigin)
{
	Vec3 eye(0.0f, 0.0f, 0.0f);
	Vec3 target(0.0f, 0.0f, -1.0f);
	Vec3 up(0.0f, 1.0f, 0.0f);

	auto m = Mat4::MakeLookAt(eye, target, up);

	// Eye is at origin, looking down -Z. The eye position in view space is origin
	Vec4 eyePos(0.0f, 0.0f, 0.0f, 1.0f);
	Vec4 r = m * eyePos;
	EXPECT_NEAR(r.X, 0.0f, 1e-5f);
	EXPECT_NEAR(r.Y, 0.0f, 1e-5f);
	EXPECT_NEAR(r.Z, 0.0f, 1e-5f);
}

// --- Different Component Types ---

TEST(Mat, IntegerMat)
{
	Mat2i a;
	a[0][0] = 1; a[0][1] = 2;
	a[1][0] = 3; a[1][1] = 4;

	Mat2i b;
	b[0][0] = 5; b[0][1] = 6;
	b[1][0] = 7; b[1][1] = 8;

	auto c = a * b;
	EXPECT_EQ(c[0][0], 19);
	EXPECT_EQ(c[0][1], 22);
	EXPECT_EQ(c[1][0], 43);
	EXPECT_EQ(c[1][1], 50);
}

TEST(Mat, DoubleMat)
{
	auto m = Mat4d::Identity();
	auto inv = m.Inverse();
	EXPECT_EQ(inv, m);
}

// --- Constexpr Validation ---

TEST(Mat, ConstexprOperations)
{
	constexpr auto zero = Mat3::Zero();
	constexpr auto id   = Mat3::Identity();
	constexpr auto sum  = id + id;
	constexpr auto neg  = -id;

	EXPECT_FLOAT_EQ(zero[0][0], 0.0f);
	EXPECT_FLOAT_EQ(id[0][0], 1.0f);
	EXPECT_FLOAT_EQ(sum[0][0], 2.0f);
	EXPECT_FLOAT_EQ(neg[0][0], -1.0f);
}

TEST(Mat, ConstexprTranslation)
{
	constexpr auto t = Mat4::MakeTranslation(1.0f, 2.0f, 3.0f);
	EXPECT_FLOAT_EQ(t[0][3], 1.0f);
	EXPECT_FLOAT_EQ(t[1][3], 2.0f);
	EXPECT_FLOAT_EQ(t[2][3], 3.0f);
}

TEST(Mat, ConstexprScale)
{
	constexpr auto s = Mat4::MakeScale(2.0f, 3.0f, 4.0f);
	EXPECT_FLOAT_EQ(s[0][0], 2.0f);
	EXPECT_FLOAT_EQ(s[1][1], 3.0f);
	EXPECT_FLOAT_EQ(s[2][2], 4.0f);
}

// --- Stream Output ---

TEST(Mat, StreamOutput)
{
	Mat2i m;
	m[0][0] = 1; m[0][1] = 2;
	m[1][0] = 3; m[1][1] = 4;

	std::ostringstream oss;
	oss << m;
	EXPECT_EQ(oss.str(), "[(1, 2)\n (3, 4)]");
}

// --- Rotation Composition ---

TEST(Mat, RotationZThenInverse)
{
	constexpr float pi = 3.14159265358979323846f;
	auto rot = Mat4::MakeRotationZ(pi / 3.0f);
	auto inv = rot.Inverse();
	auto product = rot * inv;
	auto id = Mat4::Identity();

	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			EXPECT_NEAR(product[r][c], id[r][c], 1e-5f);
}

// --- NearlyEquals ---

TEST(Mat, NearlyEqualsIdentical)
{
	auto a = Mat4::Identity();
	auto b = Mat4::Identity();
	EXPECT_TRUE(a.NearlyEquals(b));
}

TEST(Mat, NearlyEqualsWithinEpsilon)
{
	auto a = Mat4::Identity();
	auto b = Mat4::Identity();
	b[0][0] = 1.0f + 1e-7f;
	EXPECT_TRUE(a.NearlyEquals(b));
}

TEST(Mat, NearlyEqualsOutsideEpsilon)
{
	auto a = Mat4::Identity();
	auto b = Mat4::Identity();
	b[0][0] = 1.1f;
	EXPECT_FALSE(a.NearlyEquals(b));
}

TEST(Mat, NearlyEqualsCustomEpsilon)
{
	auto a = Mat4::Identity();
	auto b = Mat4::Identity();
	b[0][0] = 1.05f;
	EXPECT_FALSE(a.NearlyEquals(b, 0.01f));
	EXPECT_TRUE(a.NearlyEquals(b, 0.1f));
}

// --- TransformPoint ---

TEST(Mat, TransformPointIdentity)
{
	auto m = Mat4::Identity();
	Vec3 p(1.0f, 2.0f, 3.0f);
	Vec3 r = m.TransformPoint(p);
	EXPECT_FLOAT_EQ(r.X, 1.0f);
	EXPECT_FLOAT_EQ(r.Y, 2.0f);
	EXPECT_FLOAT_EQ(r.Z, 3.0f);
}

TEST(Mat, TransformPointTranslation)
{
	auto m = Mat4::MakeTranslation(10.0f, 20.0f, 30.0f);
	Vec3 p(1.0f, 2.0f, 3.0f);
	Vec3 r = m.TransformPoint(p);
	EXPECT_FLOAT_EQ(r.X, 11.0f);
	EXPECT_FLOAT_EQ(r.Y, 22.0f);
	EXPECT_FLOAT_EQ(r.Z, 33.0f);
}

TEST(Mat, TransformPointScale)
{
	auto m = Mat4::MakeScale(2.0f, 3.0f, 4.0f);
	Vec3 p(1.0f, 1.0f, 1.0f);
	Vec3 r = m.TransformPoint(p);
	EXPECT_FLOAT_EQ(r.X, 2.0f);
	EXPECT_FLOAT_EQ(r.Y, 3.0f);
	EXPECT_FLOAT_EQ(r.Z, 4.0f);
}

// --- TransformVector ---

TEST(Mat, TransformVectorIgnoresTranslation)
{
	auto m = Mat4::MakeTranslation(100.0f, 200.0f, 300.0f);
	Vec3 v(1.0f, 0.0f, 0.0f);
	Vec3 r = m.TransformVector(v);
	EXPECT_FLOAT_EQ(r.X, 1.0f);
	EXPECT_FLOAT_EQ(r.Y, 0.0f);
	EXPECT_FLOAT_EQ(r.Z, 0.0f);
}

TEST(Mat, TransformVectorAppliesScale)
{
	auto m = Mat4::MakeScale(2.0f, 3.0f, 4.0f);
	Vec3 v(1.0f, 1.0f, 1.0f);
	Vec3 r = m.TransformVector(v);
	EXPECT_FLOAT_EQ(r.X, 2.0f);
	EXPECT_FLOAT_EQ(r.Y, 3.0f);
	EXPECT_FLOAT_EQ(r.Z, 4.0f);
}

TEST(Mat, TransformVectorAppliesRotation)
{
	constexpr float pi = 3.14159265358979323846f;
	auto m = Mat4::MakeRotationZ(pi / 2.0f);
	Vec3 v(1.0f, 0.0f, 0.0f);
	Vec3 r = m.TransformVector(v);
	EXPECT_NEAR(r.X, 0.0f, 1e-6f);
	EXPECT_NEAR(r.Y, 1.0f, 1e-6f);
	EXPECT_NEAR(r.Z, 0.0f, 1e-6f);
}

// --- MakeTRS ---

TEST(Mat, MakeTRSIdentity)
{
	Vec3 t(0.0f, 0.0f, 0.0f);
	Vec3 r(0.0f, 0.0f, 0.0f);
	Vec3 s(1.0f, 1.0f, 1.0f);
	auto m = Mat4::MakeTRS(t, r, s);
	EXPECT_TRUE(m.NearlyEquals(Mat4::Identity()));
}

TEST(Mat, MakeTRSTranslationOnly)
{
	Vec3 t(5.0f, 10.0f, 15.0f);
	Vec3 r(0.0f, 0.0f, 0.0f);
	Vec3 s(1.0f, 1.0f, 1.0f);
	auto m = Mat4::MakeTRS(t, r, s);
	auto expected = Mat4::MakeTranslation(t);
	EXPECT_TRUE(m.NearlyEquals(expected));
}

TEST(Mat, MakeTRSScaleOnly)
{
	Vec3 t(0.0f, 0.0f, 0.0f);
	Vec3 r(0.0f, 0.0f, 0.0f);
	Vec3 s(2.0f, 3.0f, 4.0f);
	auto m = Mat4::MakeTRS(t, r, s);
	auto expected = Mat4::MakeScale(s);
	EXPECT_TRUE(m.NearlyEquals(expected));
}

TEST(Mat, MakeTRSCombined)
{
	Vec3 t(1.0f, 2.0f, 3.0f);
	Vec3 r(0.0f, 0.0f, 0.0f);
	Vec3 s(2.0f, 2.0f, 2.0f);
	auto m = Mat4::MakeTRS(t, r, s);

	// Transform a point: should scale then translate
	Vec3 p(1.0f, 0.0f, 0.0f);
	Vec3 result = m.TransformPoint(p);
	EXPECT_NEAR(result.X, 3.0f, 1e-5f);  // 1*2 + 1
	EXPECT_NEAR(result.Y, 2.0f, 1e-5f);  // 0*2 + 2
	EXPECT_NEAR(result.Z, 3.0f, 1e-5f);  // 0*2 + 3
}

// --- AffineInverse ---

TEST(Mat, AffineInverseTranslation)
{
	auto m = Mat4::MakeTranslation(5.0f, 10.0f, 15.0f);
	auto inv = m.AffineInverse();
	auto product = m * inv;
	EXPECT_TRUE(product.NearlyEquals(Mat4::Identity()));
}

TEST(Mat, AffineInverseScale)
{
	auto m = Mat4::MakeScale(2.0f, 3.0f, 4.0f);
	auto inv = m.AffineInverse();
	auto product = m * inv;
	EXPECT_TRUE(product.NearlyEquals(Mat4::Identity()));
}

TEST(Mat, AffineInverseRotation)
{
	constexpr float pi = 3.14159265358979323846f;
	auto m = Mat4::MakeRotationZ(pi / 3.0f);
	auto inv = m.AffineInverse();
	auto product = m * inv;
	EXPECT_TRUE(product.NearlyEquals(Mat4::Identity()));
}

TEST(Mat, AffineInverseTRS)
{
	constexpr float pi = 3.14159265358979323846f;
	Vec3 t(3.0f, -7.0f, 12.0f);
	Vec3 r(pi / 6.0f, pi / 4.0f, pi / 3.0f);
	Vec3 s(2.0f, 2.0f, 2.0f);
	auto m = Mat4::MakeTRS(t, r, s);
	auto inv = m.AffineInverse();
	auto product = m * inv;
	EXPECT_TRUE(product.NearlyEquals(Mat4::Identity(), 1e-4f));
}

TEST(Mat, AffineInverseMatchesFullInverse)
{
	auto m = Mat4::MakeTranslation(1.0f, 2.0f, 3.0f) * Mat4::MakeScale(2.0f, 2.0f, 2.0f);
	auto affine = m.AffineInverse();
	auto full = m.Inverse();
	EXPECT_TRUE(affine.NearlyEquals(full));
}

// --- Translation does not affect directions (w=0) ---

TEST(Mat, TranslationIgnoresDirections)
{
	auto t = Mat4::MakeTranslation(100.0f, 200.0f, 300.0f);
	Vec4 dir(1.0f, 0.0f, 0.0f, 0.0f); // direction, w=0
	Vec4 r = t * dir;
	EXPECT_FLOAT_EQ(r.X, 1.0f);
	EXPECT_FLOAT_EQ(r.Y, 0.0f);
	EXPECT_FLOAT_EQ(r.Z, 0.0f);
	EXPECT_FLOAT_EQ(r.W, 0.0f);
}
