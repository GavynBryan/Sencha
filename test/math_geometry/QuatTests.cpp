#include <gtest/gtest.h>
#include <math/Mat.h>
#include <math/Quat.h>
#include <math/Vec.h>
#include <cmath>
#include <sstream>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;

	void ExpectVecNear(const Vec3d& actual, const Vec3d& expected, float epsilon = 1e-5f)
	{
		EXPECT_NEAR(actual.X, expected.X, epsilon);
		EXPECT_NEAR(actual.Y, expected.Y, epsilon);
		EXPECT_NEAR(actual.Z, expected.Z, epsilon);
	}
}

// --- Construction ---

TEST(Quat, DefaultConstructionIsIdentity)
{
	Quatf q;
	EXPECT_FLOAT_EQ(q.X, 0.0f);
	EXPECT_FLOAT_EQ(q.Y, 0.0f);
	EXPECT_FLOAT_EQ(q.Z, 0.0f);
	EXPECT_FLOAT_EQ(q.W, 1.0f);
}

TEST(Quat, ValueConstruction)
{
	Quatf q(1.0f, 2.0f, 3.0f, 4.0f);
	EXPECT_FLOAT_EQ(q.X, 1.0f);
	EXPECT_FLOAT_EQ(q.Y, 2.0f);
	EXPECT_FLOAT_EQ(q.Z, 3.0f);
	EXPECT_FLOAT_EQ(q.W, 4.0f);
}

TEST(Quat, IdentityFactory)
{
	Quatf q = Quatf::Identity();
	EXPECT_EQ(q, Quatf());
}

// --- Quaternion Operations ---

TEST(Quat, DotProduct)
{
	Quatf a(1.0f, 2.0f, 3.0f, 4.0f);
	Quatf b(5.0f, 6.0f, 7.0f, 8.0f);
	EXPECT_FLOAT_EQ(a.Dot(b), 70.0f);
}

TEST(Quat, LengthSquared)
{
	Quatf q(1.0f, 2.0f, 3.0f, 4.0f);
	EXPECT_FLOAT_EQ(q.LengthSquared(), 30.0f);
}

TEST(Quat, Length)
{
	Quatf q(0.0f, 3.0f, 4.0f, 0.0f);
	EXPECT_FLOAT_EQ(q.Length(), 5.0f);
}

TEST(Quat, NormalizeInPlace)
{
	Quatf q(0.0f, 3.0f, 4.0f, 0.0f);
	Quatf& result = q.Normalize();

	EXPECT_EQ(&result, &q);
	EXPECT_NEAR(q.Length(), 1.0f, 1e-6f);
	EXPECT_NEAR(q.Y, 0.6f, 1e-6f);
	EXPECT_NEAR(q.Z, 0.8f, 1e-6f);
}

TEST(Quat, NormalizedCopy)
{
	Quatf q(0.0f, 0.0f, 0.0f, 2.0f);
	Quatf n = q.Normalized();

	EXPECT_FLOAT_EQ(q.W, 2.0f);
	EXPECT_FLOAT_EQ(n.W, 1.0f);
	EXPECT_NEAR(n.Length(), 1.0f, 1e-6f);
}

TEST(Quat, Conjugate)
{
	Quatf q(1.0f, -2.0f, 3.0f, -4.0f);
	Quatf c = q.Conjugate();

	EXPECT_FLOAT_EQ(c.X, -1.0f);
	EXPECT_FLOAT_EQ(c.Y, 2.0f);
	EXPECT_FLOAT_EQ(c.Z, -3.0f);
	EXPECT_FLOAT_EQ(c.W, -4.0f);
}

TEST(Quat, Inverse)
{
	Quatf q = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 3.0f);
	Quatf product = q * q.Inverse();

	EXPECT_TRUE(product.NearlyEquals(Quatf::Identity(), 1e-6f));
}

TEST(Quat, InverseHandlesNonUnitQuaternion)
{
	Quatf q(0.0f, 0.0f, 2.0f, 2.0f);
	Quatf product = q * q.Inverse();

	EXPECT_TRUE(product.NearlyEquals(Quatf::Identity(), 1e-6f));
}

// --- Arithmetic ---

TEST(Quat, MultiplicationIdentity)
{
	Quatf q = Quatf::FromAxisAngle(Vec3d(1.0f, 0.0f, 0.0f), Pi / 2.0f);

	EXPECT_TRUE((Quatf::Identity() * q).NearlyEquals(q));
	EXPECT_TRUE((q * Quatf::Identity()).NearlyEquals(q));
}

TEST(Quat, MultiplicationComposesInMatrixOrder)
{
	Quatf rotateX = Quatf::FromAxisAngle(Vec3d(1.0f, 0.0f, 0.0f), Pi / 2.0f);
	Quatf rotateZ = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f);
	Quatf composed = rotateZ * rotateX;

	Vec3d result = composed.RotateVector(Vec3d(0.0f, 1.0f, 0.0f));

	// rotateX first: +Y -> +Z, then rotateZ leaves +Z unchanged.
	ExpectVecNear(result, Vec3d(0.0f, 0.0f, 1.0f));
}

TEST(Quat, CompoundMultiplication)
{
	Quatf q = Quatf::FromAxisAngle(Vec3d(1.0f, 0.0f, 0.0f), Pi / 2.0f);
	Quatf r = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f);

	Quatf expected = r * q;
	r *= q;

	EXPECT_TRUE(r.NearlyEquals(expected));
}

TEST(Quat, ScalarMultiplicationAndDivision)
{
	Quatf q(1.0f, 2.0f, 3.0f, 4.0f);

	EXPECT_EQ(q * 2.0f, Quatf(2.0f, 4.0f, 6.0f, 8.0f));
	EXPECT_EQ(2.0f * q, Quatf(2.0f, 4.0f, 6.0f, 8.0f));
	EXPECT_EQ(q / 2.0f, Quatf(0.5f, 1.0f, 1.5f, 2.0f));
}

TEST(Quat, CompoundScalarArithmetic)
{
	Quatf q(1.0f, 2.0f, 3.0f, 4.0f);
	q *= 2.0f;
	EXPECT_EQ(q, Quatf(2.0f, 4.0f, 6.0f, 8.0f));

	q /= 2.0f;
	EXPECT_EQ(q, Quatf(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST(Quat, UnaryNegation)
{
	Quatf q(1.0f, -2.0f, 3.0f, -4.0f);
	EXPECT_EQ(-q, Quatf(-1.0f, 2.0f, -3.0f, 4.0f));
}

TEST(Quat, Equality)
{
	EXPECT_EQ(Quatf(1.0f, 2.0f, 3.0f, 4.0f), Quatf(1.0f, 2.0f, 3.0f, 4.0f));
	EXPECT_NE(Quatf(1.0f, 2.0f, 3.0f, 4.0f), Quatf(1.0f, 2.0f, 3.0f, 5.0f));
}

// --- Axis-Angle and Vector Rotation ---

TEST(Quat, FromAxisAngleCreatesExpectedRotation)
{
	Quatf q = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f);

	EXPECT_NEAR(q.X, 0.0f, 1e-6f);
	EXPECT_NEAR(q.Y, 0.0f, 1e-6f);
	EXPECT_NEAR(q.Z, std::sin(Pi / 4.0f), 1e-6f);
	EXPECT_NEAR(q.W, std::cos(Pi / 4.0f), 1e-6f);
	EXPECT_NEAR(q.Length(), 1.0f, 1e-6f);
}

TEST(Quat, FromAxisAngleNormalizesAxis)
{
	Quatf unitAxis = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f);
	Quatf scaledAxis = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 5.0f), Pi / 2.0f);

	EXPECT_TRUE(scaledAxis.NearlyEquals(unitAxis));
}

TEST(Quat, RotateVectorIdentity)
{
	Vec3d v(1.0f, 2.0f, 3.0f);
	ExpectVecNear(Quatf::Identity().RotateVector(v), v);
}

TEST(Quat, RotateVectorAroundZ)
{
	Quatf q = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f);
	Vec3d result = q.RotateVector(Vec3d(1.0f, 0.0f, 0.0f));

	ExpectVecNear(result, Vec3d(0.0f, 1.0f, 0.0f));
}

TEST(Quat, RotateVectorHandlesNonUnitQuaternion)
{
	Quatf q = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f) * 3.0f;
	Vec3d result = q.RotateVector(Vec3d(1.0f, 0.0f, 0.0f));

	ExpectVecNear(result, Vec3d(0.0f, 1.0f, 0.0f));
}

// --- Matrix Interop ---

TEST(Quat, ToMat3Identity)
{
	EXPECT_TRUE(Quatf::Identity().ToMat3().NearlyEquals(Mat3d::Identity()));
}

TEST(Quat, ToMat3MatchesRotateVector)
{
	Quatf q = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f);
	Vec3d v(1.0f, 0.0f, 0.0f);

	ExpectVecNear(q.ToMat3() * v, q.RotateVector(v));
}

TEST(Quat, ToMat3MatchesExistingRotationFactories)
{
	Quatf x = Quatf::FromAxisAngle(Vec3d(1.0f, 0.0f, 0.0f), Pi / 2.0f);
	Quatf y = Quatf::FromAxisAngle(Vec3d(0.0f, 1.0f, 0.0f), Pi / 2.0f);
	Quatf z = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f);

	EXPECT_TRUE(x.ToMat3().NearlyEquals(Mat3d::MakeRotationX(Pi / 2.0f)));
	EXPECT_TRUE(y.ToMat3().NearlyEquals(Mat3d::MakeRotationY(Pi / 2.0f)));
	EXPECT_TRUE(z.ToMat3().NearlyEquals(Mat3d::MakeRotationZ(Pi / 2.0f)));
}

TEST(Quat, ToMat4EmbedsRotation)
{
	Quatf q = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f);
	Mat4 m = q.ToMat4();
	Vec3d result = m.TransformVector(Vec3d(1.0f, 0.0f, 0.0f));

	ExpectVecNear(result, q.RotateVector(Vec3d(1.0f, 0.0f, 0.0f)));
	EXPECT_FLOAT_EQ(m[0][3], 0.0f);
	EXPECT_FLOAT_EQ(m[1][3], 0.0f);
	EXPECT_FLOAT_EQ(m[2][3], 0.0f);
	EXPECT_FLOAT_EQ(m[3][3], 1.0f);
}

TEST(Quat, ToMat3NormalizesBeforeConversion)
{
	Quatf unit = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f);
	Quatf scaled = unit * 5.0f;

	EXPECT_TRUE(scaled.ToMat3().NearlyEquals(unit.ToMat3()));
}

// --- NearlyEquals ---

TEST(Quat, NearlyEqualsIdentical)
{
	EXPECT_TRUE(Quatf::Identity().NearlyEquals(Quatf::Identity()));
}

TEST(Quat, NearlyEqualsWithinEpsilon)
{
	Quatf a = Quatf::Identity();
	Quatf b(0.0f, 0.0f, 0.0f, 1.0f + 1e-7f);

	EXPECT_TRUE(a.NearlyEquals(b));
}

TEST(Quat, NearlyEqualsOutsideEpsilon)
{
	Quatf a = Quatf::Identity();
	Quatf b(0.0f, 0.0f, 0.0f, 1.1f);

	EXPECT_FALSE(a.NearlyEquals(b));
}

TEST(Quat, NearlyEqualsCustomEpsilon)
{
	Quatf a = Quatf::Identity();
	Quatf b(0.0f, 0.0f, 0.0f, 1.05f);

	EXPECT_FALSE(a.NearlyEquals(b, 0.01f));
	EXPECT_TRUE(a.NearlyEquals(b, 0.1f));
}

// --- Aliases and Component Types ---

TEST(Quat, DoubleAlias)
{
	Quatd q = Quatd::FromAxisAngle(Vec3dd(0.0, 0.0, 1.0), 3.14159265358979323846 / 2.0);
	Vec3dd result = q.RotateVector(Vec3dd(1.0, 0.0, 0.0));

	EXPECT_NEAR(result.X, 0.0, 1e-12);
	EXPECT_NEAR(result.Y, 1.0, 1e-12);
	EXPECT_NEAR(result.Z, 0.0, 1e-12);
}

// --- Constexpr Validation ---

TEST(Quat, ConstexprOperations)
{
	constexpr Quatf id = Quatf::Identity();
	constexpr Quatf q(1.0f, 2.0f, 3.0f, 4.0f);
	constexpr Quatf c = q.Conjugate();
	constexpr float dot = q.Dot(q);
	constexpr Quatf scaled = q * 2.0f;

	EXPECT_FLOAT_EQ(id.W, 1.0f);
	EXPECT_EQ(c, Quatf(-1.0f, -2.0f, -3.0f, 4.0f));
	EXPECT_FLOAT_EQ(dot, 30.0f);
	EXPECT_EQ(scaled, Quatf(2.0f, 4.0f, 6.0f, 8.0f));
}

// --- Stream Output ---

TEST(Quat, StreamOutput)
{
	Quat<int> q(1, 2, 3, 4);
	std::ostringstream oss;
	oss << q;
	EXPECT_EQ(oss.str(), "(1, 2, 3, 4)");
}
