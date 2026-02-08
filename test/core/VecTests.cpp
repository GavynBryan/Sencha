#include <gtest/gtest.h>
#include <math/Vec.h>
#include <sstream>

// --- Construction ---

TEST(Vec, DefaultConstructionIsZero)
{
	Vec3 v;
	EXPECT_FLOAT_EQ(v.X(), 0.0f);
	EXPECT_FLOAT_EQ(v.Y(), 0.0f);
	EXPECT_FLOAT_EQ(v.Z(), 0.0f);
}

TEST(Vec, ValueConstruction)
{
	Vec3 v(1.0f, 2.0f, 3.0f);
	EXPECT_FLOAT_EQ(v.X(), 1.0f);
	EXPECT_FLOAT_EQ(v.Y(), 2.0f);
	EXPECT_FLOAT_EQ(v.Z(), 3.0f);
}

TEST(Vec, ZeroFactory)
{
	Vec4 v = Vec4::Zero();
	for (int i = 0; i < 4; ++i)
		EXPECT_FLOAT_EQ(v[i], 0.0f);
}

TEST(Vec, OneFactory)
{
	Vec4 v = Vec4::One();
	for (int i = 0; i < 4; ++i)
		EXPECT_FLOAT_EQ(v[i], 1.0f);
}

// --- Named Accessors ---

TEST(Vec, NamedAccessors2D)
{
	Vec2 v(3.0f, 4.0f);
	EXPECT_FLOAT_EQ(v.X(), 3.0f);
	EXPECT_FLOAT_EQ(v.Y(), 4.0f);
}

TEST(Vec, NamedAccessors4D)
{
	Vec4 v(1.0f, 2.0f, 3.0f, 4.0f);
	EXPECT_FLOAT_EQ(v.X(), 1.0f);
	EXPECT_FLOAT_EQ(v.Y(), 2.0f);
	EXPECT_FLOAT_EQ(v.Z(), 3.0f);
	EXPECT_FLOAT_EQ(v.W(), 4.0f);
}

TEST(Vec, NamedAccessorsMutate)
{
	Vec3 v;
	v.X() = 10.0f;
	v.Y() = 20.0f;
	v.Z() = 30.0f;
	EXPECT_FLOAT_EQ(v[0], 10.0f);
	EXPECT_FLOAT_EQ(v[1], 20.0f);
	EXPECT_FLOAT_EQ(v[2], 30.0f);
}

// --- Element Access ---

TEST(Vec, IndexOperator)
{
	Vec3 v(5.0f, 10.0f, 15.0f);
	EXPECT_FLOAT_EQ(v[0], 5.0f);
	EXPECT_FLOAT_EQ(v[1], 10.0f);
	EXPECT_FLOAT_EQ(v[2], 15.0f);
}

TEST(Vec, IndexOperatorMutate)
{
	Vec3 v;
	v[0] = 1.0f;
	v[1] = 2.0f;
	v[2] = 3.0f;
	EXPECT_FLOAT_EQ(v.X(), 1.0f);
	EXPECT_FLOAT_EQ(v.Y(), 2.0f);
	EXPECT_FLOAT_EQ(v.Z(), 3.0f);
}

// --- Arithmetic ---

TEST(Vec, Addition)
{
	Vec3 a(1.0f, 2.0f, 3.0f);
	Vec3 b(4.0f, 5.0f, 6.0f);
	Vec3 c = a + b;
	EXPECT_FLOAT_EQ(c.X(), 5.0f);
	EXPECT_FLOAT_EQ(c.Y(), 7.0f);
	EXPECT_FLOAT_EQ(c.Z(), 9.0f);
}

TEST(Vec, Subtraction)
{
	Vec3 a(4.0f, 5.0f, 6.0f);
	Vec3 b(1.0f, 2.0f, 3.0f);
	Vec3 c = a - b;
	EXPECT_FLOAT_EQ(c.X(), 3.0f);
	EXPECT_FLOAT_EQ(c.Y(), 3.0f);
	EXPECT_FLOAT_EQ(c.Z(), 3.0f);
}

TEST(Vec, ScalarMultiplication)
{
	Vec3 v(1.0f, 2.0f, 3.0f);
	Vec3 r = v * 2.0f;
	EXPECT_FLOAT_EQ(r.X(), 2.0f);
	EXPECT_FLOAT_EQ(r.Y(), 4.0f);
	EXPECT_FLOAT_EQ(r.Z(), 6.0f);
}

TEST(Vec, ScalarMultiplicationLeftHand)
{
	Vec3 v(1.0f, 2.0f, 3.0f);
	Vec3 r = 2.0f * v;
	EXPECT_FLOAT_EQ(r.X(), 2.0f);
	EXPECT_FLOAT_EQ(r.Y(), 4.0f);
	EXPECT_FLOAT_EQ(r.Z(), 6.0f);
}

TEST(Vec, ScalarDivision)
{
	Vec3 v(2.0f, 4.0f, 6.0f);
	Vec3 r = v / 2.0f;
	EXPECT_FLOAT_EQ(r.X(), 1.0f);
	EXPECT_FLOAT_EQ(r.Y(), 2.0f);
	EXPECT_FLOAT_EQ(r.Z(), 3.0f);
}

TEST(Vec, CompoundAddition)
{
	Vec3 a(1.0f, 2.0f, 3.0f);
	a += Vec3(4.0f, 5.0f, 6.0f);
	EXPECT_FLOAT_EQ(a.X(), 5.0f);
	EXPECT_FLOAT_EQ(a.Y(), 7.0f);
	EXPECT_FLOAT_EQ(a.Z(), 9.0f);
}

TEST(Vec, CompoundSubtraction)
{
	Vec3 a(4.0f, 5.0f, 6.0f);
	a -= Vec3(1.0f, 2.0f, 3.0f);
	EXPECT_FLOAT_EQ(a.X(), 3.0f);
	EXPECT_FLOAT_EQ(a.Y(), 3.0f);
	EXPECT_FLOAT_EQ(a.Z(), 3.0f);
}

TEST(Vec, CompoundScalarMultiplication)
{
	Vec3 v(1.0f, 2.0f, 3.0f);
	v *= 3.0f;
	EXPECT_FLOAT_EQ(v.X(), 3.0f);
	EXPECT_FLOAT_EQ(v.Y(), 6.0f);
	EXPECT_FLOAT_EQ(v.Z(), 9.0f);
}

TEST(Vec, CompoundScalarDivision)
{
	Vec3 v(4.0f, 8.0f, 12.0f);
	v /= 4.0f;
	EXPECT_FLOAT_EQ(v.X(), 1.0f);
	EXPECT_FLOAT_EQ(v.Y(), 2.0f);
	EXPECT_FLOAT_EQ(v.Z(), 3.0f);
}

TEST(Vec, UnaryNegation)
{
	Vec3 v(1.0f, -2.0f, 3.0f);
	Vec3 r = -v;
	EXPECT_FLOAT_EQ(r.X(), -1.0f);
	EXPECT_FLOAT_EQ(r.Y(), 2.0f);
	EXPECT_FLOAT_EQ(r.Z(), -3.0f);
}

// --- Comparison ---

TEST(Vec, Equality)
{
	Vec3 a(1.0f, 2.0f, 3.0f);
	Vec3 b(1.0f, 2.0f, 3.0f);
	EXPECT_TRUE(a == b);
}

TEST(Vec, Inequality)
{
	Vec3 a(1.0f, 2.0f, 3.0f);
	Vec3 b(1.0f, 2.0f, 4.0f);
	EXPECT_TRUE(a != b);
}

// --- Vector Operations ---

TEST(Vec, DotProduct)
{
	Vec3 a(1.0f, 2.0f, 3.0f);
	Vec3 b(4.0f, 5.0f, 6.0f);
	EXPECT_FLOAT_EQ(a.Dot(b), 32.0f);
}

TEST(Vec, SqrMagnitude)
{
	Vec3 v(3.0f, 4.0f, 0.0f);
	EXPECT_FLOAT_EQ(v.SqrMagnitude(), 25.0f);
}

TEST(Vec, Magnitude)
{
	Vec3 v(3.0f, 4.0f, 0.0f);
	EXPECT_FLOAT_EQ(v.Magnitude(), 5.0f);
}

TEST(Vec, Normalized)
{
	Vec3 v(0.0f, 3.0f, 4.0f);
	Vec3 n = v.Normalized();
	EXPECT_NEAR(n.Magnitude(), 1.0f, 1e-6f);
	EXPECT_NEAR(n.Y(), 0.6f, 1e-6f);
	EXPECT_NEAR(n.Z(), 0.8f, 1e-6f);
}

// --- 3D Cross Product ---

TEST(Vec, CrossProduct)
{
	Vec3 x(1.0f, 0.0f, 0.0f);
	Vec3 y(0.0f, 1.0f, 0.0f);
	Vec3 z = x.Cross(y);
	EXPECT_FLOAT_EQ(z.X(), 0.0f);
	EXPECT_FLOAT_EQ(z.Y(), 0.0f);
	EXPECT_FLOAT_EQ(z.Z(), 1.0f);
}

TEST(Vec, CrossProductAntiCommutative)
{
	Vec3 a(1.0f, 2.0f, 3.0f);
	Vec3 b(4.0f, 5.0f, 6.0f);
	Vec3 ab = a.Cross(b);
	Vec3 ba = b.Cross(a);
	EXPECT_FLOAT_EQ(ab.X(), -ba.X());
	EXPECT_FLOAT_EQ(ab.Y(), -ba.Y());
	EXPECT_FLOAT_EQ(ab.Z(), -ba.Z());
}

// --- Static Utilities ---

TEST(Vec, Lerp)
{
	Vec3 a(0.0f, 0.0f, 0.0f);
	Vec3 b(10.0f, 20.0f, 30.0f);
	Vec3 mid = Vec3::Lerp(a, b, 0.5f);
	EXPECT_FLOAT_EQ(mid.X(), 5.0f);
	EXPECT_FLOAT_EQ(mid.Y(), 10.0f);
	EXPECT_FLOAT_EQ(mid.Z(), 15.0f);
}

TEST(Vec, LerpEndpoints)
{
	Vec3 a(1.0f, 2.0f, 3.0f);
	Vec3 b(4.0f, 5.0f, 6.0f);
	EXPECT_EQ(Vec3::Lerp(a, b, 0.0f), a);
	EXPECT_EQ(Vec3::Lerp(a, b, 1.0f), b);
}

TEST(Vec, Distance)
{
	Vec3 a(0.0f, 0.0f, 0.0f);
	Vec3 b(3.0f, 4.0f, 0.0f);
	EXPECT_FLOAT_EQ(Vec3::Distance(a, b), 5.0f);
}

TEST(Vec, SqrDistance)
{
	Vec3 a(0.0f, 0.0f, 0.0f);
	Vec3 b(3.0f, 4.0f, 0.0f);
	EXPECT_FLOAT_EQ(Vec3::SqrDistance(a, b), 25.0f);
}

// --- Arbitrary Dimensions ---

TEST(Vec, HighDimensionConstruction)
{
	Vec<5> v(1.0f, 2.0f, 3.0f, 4.0f, 5.0f);
	EXPECT_FLOAT_EQ(v[0], 1.0f);
	EXPECT_FLOAT_EQ(v[4], 5.0f);
	EXPECT_EQ(v.Dimensions, 5);
}

TEST(Vec, HighDimensionDot)
{
	Vec<4> a(1.0f, 2.0f, 3.0f, 4.0f);
	Vec<4> b(5.0f, 6.0f, 7.0f, 8.0f);
	EXPECT_FLOAT_EQ(a.Dot(b), 70.0f);
}

// --- Different Component Types ---

TEST(Vec, IntegerVec)
{
	Vec3i v(1, 2, 3);
	Vec3i w(4, 5, 6);
	Vec3i sum = v + w;
	EXPECT_EQ(sum.X(), 5);
	EXPECT_EQ(sum.Y(), 7);
	EXPECT_EQ(sum.Z(), 9);
}

TEST(Vec, DoubleVec)
{
	Vec3d v(3.0, 4.0, 0.0);
	EXPECT_DOUBLE_EQ(v.Magnitude(), 5.0);
}

// --- Constexpr Validation ---

TEST(Vec, ConstexprOperations)
{
	constexpr Vec3 a(1.0f, 2.0f, 3.0f);
	constexpr Vec3 b(4.0f, 5.0f, 6.0f);
	constexpr Vec3 sum  = a + b;
	constexpr float dot = a.Dot(b);
	constexpr Vec3 zero = Vec3::Zero();
	constexpr Vec3 one  = Vec3::One();

	EXPECT_FLOAT_EQ(sum.X(), 5.0f);
	EXPECT_FLOAT_EQ(dot, 32.0f);
	EXPECT_FLOAT_EQ(zero.X(), 0.0f);
	EXPECT_FLOAT_EQ(one.X(), 1.0f);
}

// --- Stream Output ---

TEST(Vec, StreamOutput)
{
	Vec3i v(1, 2, 3);
	std::ostringstream oss;
	oss << v;
	EXPECT_EQ(oss.str(), "(1, 2, 3)");
}
