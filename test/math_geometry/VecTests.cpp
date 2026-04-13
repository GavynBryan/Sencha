#include <gtest/gtest.h>
#include <math/Vec.h>
#include <sstream>

// --- Construction ---

TEST(Vec, DefaultConstructionIsZero)
{
	Vec3d v;
	EXPECT_FLOAT_EQ(v.X, 0.0f);
	EXPECT_FLOAT_EQ(v.Y, 0.0f);
	EXPECT_FLOAT_EQ(v.Z, 0.0f);
}

TEST(Vec, ValueConstruction)
{
	Vec3d v(1.0f, 2.0f, 3.0f);
	EXPECT_FLOAT_EQ(v.X, 1.0f);
	EXPECT_FLOAT_EQ(v.Y, 2.0f);
	EXPECT_FLOAT_EQ(v.Z, 3.0f);
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

// --- Direction Factories ---

TEST(Vec, DirectionFactories2D)
{
	EXPECT_EQ(Vec2d::Right(), Vec2d(1.0f, 0.0f));
	EXPECT_EQ(Vec2d::Left(), Vec2d(-1.0f, 0.0f));
	EXPECT_EQ(Vec2d::Up(), Vec2d(0.0f, 1.0f));
	EXPECT_EQ(Vec2d::Down(), Vec2d(0.0f, -1.0f));
}

TEST(Vec, DirectionFactories3D)
{
	EXPECT_EQ(Vec3d::Forward(), Vec3d(0.0f, 0.0f, -1.0f));
	EXPECT_EQ(Vec3d::Backward(), Vec3d(0.0f, 0.0f, 1.0f));
	EXPECT_EQ(Vec3d::Right(), Vec3d(1.0f, 0.0f, 0.0f));
	EXPECT_EQ(Vec3d::Left(), Vec3d(-1.0f, 0.0f, 0.0f));
	EXPECT_EQ(Vec3d::Up(), Vec3d(0.0f, 1.0f, 0.0f));
	EXPECT_EQ(Vec3d::Down(), Vec3d(0.0f, -1.0f, 0.0f));
}

TEST(Vec, DirectionConventionIsRightHanded)
{
	EXPECT_EQ(Vec3d::Right().Cross(Vec3d::Up()), Vec3d::Backward());
	EXPECT_EQ(Vec3d::Up().Cross(Vec3d::Forward()), Vec3d::Left());
}

// --- Named Fields ---

TEST(Vec, NamedFields2D)
{
	Vec2d v(3.0f, 4.0f);
	EXPECT_FLOAT_EQ(v.X, 3.0f);
	EXPECT_FLOAT_EQ(v.Y, 4.0f);
}

TEST(Vec, NamedFields4D)
{
	Vec4 v(1.0f, 2.0f, 3.0f, 4.0f);
	EXPECT_FLOAT_EQ(v.X, 1.0f);
	EXPECT_FLOAT_EQ(v.Y, 2.0f);
	EXPECT_FLOAT_EQ(v.Z, 3.0f);
	EXPECT_FLOAT_EQ(v.W, 4.0f);
}

TEST(Vec, NamedFieldsMutate)
{
	Vec3d v;
	v.X = 10.0f;
	v.Y = 20.0f;
	v.Z = 30.0f;
	EXPECT_FLOAT_EQ(v[0], 10.0f);
	EXPECT_FLOAT_EQ(v[1], 20.0f);
	EXPECT_FLOAT_EQ(v[2], 30.0f);
}

TEST(Vec, CommonVectorSizesHaveTightFieldStorage)
{
	static_assert(sizeof(Vec2d) == sizeof(float) * 2);
	static_assert(sizeof(Vec3d) == sizeof(float) * 3);
	static_assert(sizeof(Vec4) == sizeof(float) * 4);
	static_assert(sizeof(Vec3dd) == sizeof(double) * 3);
	static_assert(sizeof(Vec3i) == sizeof(int) * 3);
}

// --- Element Access ---

TEST(Vec, IndexOperator)
{
	Vec3d v(5.0f, 10.0f, 15.0f);
	EXPECT_FLOAT_EQ(v[0], 5.0f);
	EXPECT_FLOAT_EQ(v[1], 10.0f);
	EXPECT_FLOAT_EQ(v[2], 15.0f);
}

TEST(Vec, IndexOperatorMutate)
{
	Vec3d v;
	v[0] = 1.0f;
	v[1] = 2.0f;
	v[2] = 3.0f;
	EXPECT_FLOAT_EQ(v.X, 1.0f);
	EXPECT_FLOAT_EQ(v.Y, 2.0f);
	EXPECT_FLOAT_EQ(v.Z, 3.0f);
}

// --- Arithmetic ---

TEST(Vec, Addition)
{
	Vec3d a(1.0f, 2.0f, 3.0f);
	Vec3d b(4.0f, 5.0f, 6.0f);
	Vec3d c = a + b;
	EXPECT_FLOAT_EQ(c.X, 5.0f);
	EXPECT_FLOAT_EQ(c.Y, 7.0f);
	EXPECT_FLOAT_EQ(c.Z, 9.0f);
}

TEST(Vec, Subtraction)
{
	Vec3d a(4.0f, 5.0f, 6.0f);
	Vec3d b(1.0f, 2.0f, 3.0f);
	Vec3d c = a - b;
	EXPECT_FLOAT_EQ(c.X, 3.0f);
	EXPECT_FLOAT_EQ(c.Y, 3.0f);
	EXPECT_FLOAT_EQ(c.Z, 3.0f);
}

TEST(Vec, ScalarMultiplication)
{
	Vec3d v(1.0f, 2.0f, 3.0f);
	Vec3d r = v * 2.0f;
	EXPECT_FLOAT_EQ(r.X, 2.0f);
	EXPECT_FLOAT_EQ(r.Y, 4.0f);
	EXPECT_FLOAT_EQ(r.Z, 6.0f);
}

TEST(Vec, ScalarMultiplicationLeftHand)
{
	Vec3d v(1.0f, 2.0f, 3.0f);
	Vec3d r = 2.0f * v;
	EXPECT_FLOAT_EQ(r.X, 2.0f);
	EXPECT_FLOAT_EQ(r.Y, 4.0f);
	EXPECT_FLOAT_EQ(r.Z, 6.0f);
}

TEST(Vec, ScalarDivision)
{
	Vec3d v(2.0f, 4.0f, 6.0f);
	Vec3d r = v / 2.0f;
	EXPECT_FLOAT_EQ(r.X, 1.0f);
	EXPECT_FLOAT_EQ(r.Y, 2.0f);
	EXPECT_FLOAT_EQ(r.Z, 3.0f);
}

TEST(Vec, CompoundAddition)
{
	Vec3d a(1.0f, 2.0f, 3.0f);
	a += Vec3d(4.0f, 5.0f, 6.0f);
	EXPECT_FLOAT_EQ(a.X, 5.0f);
	EXPECT_FLOAT_EQ(a.Y, 7.0f);
	EXPECT_FLOAT_EQ(a.Z, 9.0f);
}

TEST(Vec, CompoundSubtraction)
{
	Vec3d a(4.0f, 5.0f, 6.0f);
	a -= Vec3d(1.0f, 2.0f, 3.0f);
	EXPECT_FLOAT_EQ(a.X, 3.0f);
	EXPECT_FLOAT_EQ(a.Y, 3.0f);
	EXPECT_FLOAT_EQ(a.Z, 3.0f);
}

TEST(Vec, CompoundScalarMultiplication)
{
	Vec3d v(1.0f, 2.0f, 3.0f);
	v *= 3.0f;
	EXPECT_FLOAT_EQ(v.X, 3.0f);
	EXPECT_FLOAT_EQ(v.Y, 6.0f);
	EXPECT_FLOAT_EQ(v.Z, 9.0f);
}

TEST(Vec, CompoundScalarDivision)
{
	Vec3d v(4.0f, 8.0f, 12.0f);
	v /= 4.0f;
	EXPECT_FLOAT_EQ(v.X, 1.0f);
	EXPECT_FLOAT_EQ(v.Y, 2.0f);
	EXPECT_FLOAT_EQ(v.Z, 3.0f);
}

TEST(Vec, UnaryNegation)
{
	Vec3d v(1.0f, -2.0f, 3.0f);
	Vec3d r = -v;
	EXPECT_FLOAT_EQ(r.X, -1.0f);
	EXPECT_FLOAT_EQ(r.Y, 2.0f);
	EXPECT_FLOAT_EQ(r.Z, -3.0f);
}

// --- Comparison ---

TEST(Vec, Equality)
{
	Vec3d a(1.0f, 2.0f, 3.0f);
	Vec3d b(1.0f, 2.0f, 3.0f);
	EXPECT_TRUE(a == b);
}

TEST(Vec, Inequality)
{
	Vec3d a(1.0f, 2.0f, 3.0f);
	Vec3d b(1.0f, 2.0f, 4.0f);
	EXPECT_TRUE(a != b);
}

// --- Vector Operations ---

TEST(Vec, DotProduct)
{
	Vec3d a(1.0f, 2.0f, 3.0f);
	Vec3d b(4.0f, 5.0f, 6.0f);
	EXPECT_FLOAT_EQ(a.Dot(b), 32.0f);
}

TEST(Vec, SqrMagnitude)
{
	Vec3d v(3.0f, 4.0f, 0.0f);
	EXPECT_FLOAT_EQ(v.SqrMagnitude(), 25.0f);
}

TEST(Vec, Magnitude)
{
	Vec3d v(3.0f, 4.0f, 0.0f);
	EXPECT_FLOAT_EQ(v.Magnitude(), 5.0f);
}

TEST(Vec, Normalized)
{
	Vec3d v(0.0f, 3.0f, 4.0f);
	Vec3d n = v.Normalized();
	EXPECT_NEAR(n.Magnitude(), 1.0f, 1e-6f);
	EXPECT_NEAR(n.Y, 0.6f, 1e-6f);
	EXPECT_NEAR(n.Z, 0.8f, 1e-6f);
}

// --- 3D Cross Product ---

TEST(Vec, CrossProduct)
{
	Vec3d x(1.0f, 0.0f, 0.0f);
	Vec3d y(0.0f, 1.0f, 0.0f);
	Vec3d z = x.Cross(y);
	EXPECT_FLOAT_EQ(z.X, 0.0f);
	EXPECT_FLOAT_EQ(z.Y, 0.0f);
	EXPECT_FLOAT_EQ(z.Z, 1.0f);
}

TEST(Vec, CrossProductAntiCommutative)
{
	Vec3d a(1.0f, 2.0f, 3.0f);
	Vec3d b(4.0f, 5.0f, 6.0f);
	Vec3d ab = a.Cross(b);
	Vec3d ba = b.Cross(a);
	EXPECT_FLOAT_EQ(ab.X, -ba.X);
	EXPECT_FLOAT_EQ(ab.Y, -ba.Y);
	EXPECT_FLOAT_EQ(ab.Z, -ba.Z);
}

// --- Static Utilities ---

TEST(Vec, Lerp)
{
	Vec3d a(0.0f, 0.0f, 0.0f);
	Vec3d b(10.0f, 20.0f, 30.0f);
	Vec3d mid = Vec3d::Lerp(a, b, 0.5f);
	EXPECT_FLOAT_EQ(mid.X, 5.0f);
	EXPECT_FLOAT_EQ(mid.Y, 10.0f);
	EXPECT_FLOAT_EQ(mid.Z, 15.0f);
}

TEST(Vec, LerpEndpoints)
{
	Vec3d a(1.0f, 2.0f, 3.0f);
	Vec3d b(4.0f, 5.0f, 6.0f);
	EXPECT_EQ(Vec3d::Lerp(a, b, 0.0f), a);
	EXPECT_EQ(Vec3d::Lerp(a, b, 1.0f), b);
}

TEST(Vec, Distance)
{
	Vec3d a(0.0f, 0.0f, 0.0f);
	Vec3d b(3.0f, 4.0f, 0.0f);
	EXPECT_FLOAT_EQ(Vec3d::Distance(a, b), 5.0f);
}

TEST(Vec, SqrDistance)
{
	Vec3d a(0.0f, 0.0f, 0.0f);
	Vec3d b(3.0f, 4.0f, 0.0f);
	EXPECT_FLOAT_EQ(Vec3d::SqrDistance(a, b), 25.0f);
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
	EXPECT_EQ(sum.X, 5);
	EXPECT_EQ(sum.Y, 7);
	EXPECT_EQ(sum.Z, 9);
}

TEST(Vec, DoubleVec)
{
	Vec3dd v(3.0, 4.0, 0.0);
	EXPECT_DOUBLE_EQ(v.Magnitude(), 5.0);
}

// --- Constexpr Validation ---

TEST(Vec, ConstexprOperations)
{
	constexpr Vec3d a(1.0f, 2.0f, 3.0f);
	constexpr Vec3d b(4.0f, 5.0f, 6.0f);
	constexpr Vec3d sum  = a + b;
	constexpr float dot = a.Dot(b);
	constexpr Vec3d zero = Vec3d::Zero();
	constexpr Vec3d one  = Vec3d::One();
	constexpr Vec3d forward = Vec3d::Forward();
	constexpr Vec2d right = Vec2d::Right();

	EXPECT_FLOAT_EQ(sum.X, 5.0f);
	EXPECT_FLOAT_EQ(dot, 32.0f);
	EXPECT_FLOAT_EQ(zero.X, 0.0f);
	EXPECT_FLOAT_EQ(one.X, 1.0f);
	EXPECT_FLOAT_EQ(forward.Z, -1.0f);
	EXPECT_FLOAT_EQ(right.X, 1.0f);
}

// --- Stream Output ---

TEST(Vec, StreamOutput)
{
	Vec3i v(1, 2, 3);
	std::ostringstream oss;
	oss << v;
	EXPECT_EQ(oss.str(), "(1, 2, 3)");
}
