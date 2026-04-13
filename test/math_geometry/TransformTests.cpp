#include <gtest/gtest.h>
#include <math/Mat.h>
#include <math/Quat.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <math/Vec.h>
#include <cmath>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;

	void ExpectVecNear(const Vec2d& actual, const Vec2d& expected, float epsilon = 1e-5f)
	{
		EXPECT_NEAR(actual.X, expected.X, epsilon);
		EXPECT_NEAR(actual.Y, expected.Y, epsilon);
	}

	void ExpectVecNear(const Vec3d& actual, const Vec3d& expected, float epsilon = 1e-5f)
	{
		EXPECT_NEAR(actual.X, expected.X, epsilon);
		EXPECT_NEAR(actual.Y, expected.Y, epsilon);
		EXPECT_NEAR(actual.Z, expected.Z, epsilon);
	}

	Vec2d TransformPoint(const Mat3d& m, const Vec2d& point)
	{
		Vec3d h(point.X, point.Y, 1.0f);
		Vec3d r = m * h;
		return Vec2d(r.X, r.Y);
	}

	Vec2d TransformVector(const Mat3d& m, const Vec2d& vector)
	{
		Vec3d h(vector.X, vector.Y, 0.0f);
		Vec3d r = m * h;
		return Vec2d(r.X, r.Y);
	}
}

// --- Transform2d core ---

TEST(Transform2d, DefaultConstructionIsIdentity)
{
	Transform2f t;

	EXPECT_EQ(t.Position, Vec2d::Zero());
	EXPECT_FLOAT_EQ(t.Rotation, 0.0f);
	EXPECT_EQ(t.Scale, Vec2d::One());
}

TEST(Transform2d, ValueConstruction)
{
	Transform2f t(Vec2d(1.0f, 2.0f), Pi / 2.0f, Vec2d(3.0f, 4.0f));

	EXPECT_EQ(t.Position, Vec2d(1.0f, 2.0f));
	EXPECT_FLOAT_EQ(t.Rotation, Pi / 2.0f);
	EXPECT_EQ(t.Scale, Vec2d(3.0f, 4.0f));
}

TEST(Transform2d, IdentityFactory)
{
	EXPECT_EQ(Transform2f::Identity(), Transform2f());
}

TEST(Transform2d, Equality)
{
	EXPECT_EQ(Transform2f(Vec2d(1.0f, 2.0f), 3.0f, Vec2d(4.0f, 5.0f)),
		Transform2f(Vec2d(1.0f, 2.0f), 3.0f, Vec2d(4.0f, 5.0f)));
	EXPECT_NE(Transform2f(Vec2d(1.0f, 2.0f), 3.0f, Vec2d(4.0f, 5.0f)),
		Transform2f(Vec2d(1.0f, 2.0f), 6.0f, Vec2d(4.0f, 5.0f)));
}

TEST(Transform2d, NearlyEquals)
{
	Transform2f a(Vec2d(1.0f, 2.0f), 3.0f, Vec2d(4.0f, 5.0f));
	Transform2f b(Vec2d(1.0f + 1e-7f, 2.0f), 3.0f, Vec2d(4.0f, 5.0f));

	EXPECT_TRUE(a.NearlyEquals(b));
	EXPECT_FALSE(a.NearlyEquals(Transform2f(Vec2d(1.1f, 2.0f), 3.0f, Vec2d(4.0f, 5.0f))));
}

// --- Transform2d math ---

TEST(Transform2d, ToMat3Identity)
{
	EXPECT_TRUE(Transform2f::Identity().ToMat3().NearlyEquals(Mat3d::Identity()));
}

TEST(Transform2d, TranslationRotationScaleEffects)
{
	Transform2f t(Vec2d(10.0f, 20.0f), Pi / 2.0f, Vec2d(2.0f, 3.0f));
	Vec2d result = t.TransformPoint(Vec2d(1.0f, 1.0f));

	// Scale to (2, 3), rotate to (-3, 2), then translate.
	ExpectVecNear(result, Vec2d(7.0f, 22.0f));
}

TEST(Transform2d, PointVsVectorTransformBehavior)
{
	Transform2f t(Vec2d(10.0f, 20.0f), Pi / 2.0f, Vec2d(2.0f, 2.0f));

	ExpectVecNear(t.TransformPoint(Vec2d(1.0f, 0.0f)), Vec2d(10.0f, 22.0f));
	ExpectVecNear(t.TransformVector(Vec2d(1.0f, 0.0f)), Vec2d(0.0f, 2.0f));
}

TEST(Transform2d, DirectionHelpersUseRotationOnly)
{
	Transform2f t(Vec2d(10.0f, 20.0f), Pi / 2.0f, Vec2d(5.0f, 7.0f));

	ExpectVecNear(t.Forward(), Vec2d(-1.0f, 0.0f));
	ExpectVecNear(t.Right(), Vec2d(0.0f, 1.0f));
}

TEST(Transform2d, DirectionHelpersMatchMatrixRotation)
{
	Transform2f t(Vec2d(10.0f, 20.0f), Pi / 2.0f, Vec2d(5.0f, 7.0f));
	Mat3d rotation = Mat3d::MakeRotationZ(t.Rotation);

	ExpectVecNear(t.Forward(), TransformVector(rotation, Vec2d::Up()));
	ExpectVecNear(t.Right(), TransformVector(rotation, Vec2d::Right()));
}

TEST(Transform2d, ToMat3MatchesHelpers)
{
	Transform2f t(Vec2d(3.0f, -2.0f), Pi / 2.0f, Vec2d(2.0f, 3.0f));
	Mat3d m = t.ToMat3();
	Vec2d p(1.0f, 2.0f);
	Vec2d v(1.0f, 2.0f);

	ExpectVecNear(TransformPoint(m, p), t.TransformPoint(p));
	ExpectVecNear(TransformVector(m, v), t.TransformVector(v));
}

TEST(Transform2d, CompositionAppliesRightThenLeft)
{
	Transform2f a(Vec2d(5.0f, 0.0f), Pi / 2.0f, Vec2d(2.0f, 2.0f));
	Transform2f b(Vec2d(1.0f, 0.0f), 0.0f, Vec2d(3.0f, 3.0f));
	Transform2f composed = a * b;
	Vec2d point(1.0f, 0.0f);

	ExpectVecNear(composed.TransformPoint(point), a.TransformPoint(b.TransformPoint(point)));
	EXPECT_TRUE(composed.ToMat3().NearlyEquals(a.ToMat3() * b.ToMat3()));
}

// --- Transform3d core ---

TEST(Transform3d, DefaultConstructionIsIdentity)
{
	Transform3f t;

	EXPECT_EQ(t.Position, Vec3d::Zero());
	EXPECT_EQ(t.Rotation, Quatf::Identity());
	EXPECT_EQ(t.Scale, Vec3d::One());
}

TEST(Transform3d, ValueConstruction)
{
	Quatf rotation = Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f);
	Transform3f t(Vec3d(1.0f, 2.0f, 3.0f), rotation, Vec3d(4.0f, 5.0f, 6.0f));

	EXPECT_EQ(t.Position, Vec3d(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(t.Rotation, rotation);
	EXPECT_EQ(t.Scale, Vec3d(4.0f, 5.0f, 6.0f));
}

TEST(Transform3d, IdentityFactory)
{
	EXPECT_EQ(Transform3f::Identity(), Transform3f());
}

TEST(Transform3d, Equality)
{
	Transform3f a(Vec3d(1.0f, 2.0f, 3.0f), Quatf::Identity(), Vec3d(4.0f, 5.0f, 6.0f));
	Transform3f b(Vec3d(1.0f, 2.0f, 3.0f), Quatf::Identity(), Vec3d(4.0f, 5.0f, 6.0f));
	Transform3f c(Vec3d(1.0f, 2.0f, 3.0f), Quatf::Identity(), Vec3d(4.0f, 5.0f, 7.0f));

	EXPECT_EQ(a, b);
	EXPECT_NE(a, c);
}

TEST(Transform3d, NearlyEquals)
{
	Transform3f a(Vec3d(1.0f, 2.0f, 3.0f), Quatf::Identity(), Vec3d(4.0f, 5.0f, 6.0f));
	Transform3f b(Vec3d(1.0f, 2.0f, 3.0f + 1e-7f), Quatf::Identity(), Vec3d(4.0f, 5.0f, 6.0f));

	EXPECT_TRUE(a.NearlyEquals(b));
	EXPECT_FALSE(a.NearlyEquals(Transform3f(Vec3d(1.0f, 2.0f, 3.1f), Quatf::Identity(), Vec3d(4.0f, 5.0f, 6.0f))));
}

// --- Transform3d math ---

TEST(Transform3d, ToMat4Identity)
{
	EXPECT_TRUE(Transform3f::Identity().ToMat4().NearlyEquals(Mat4::Identity()));
}

TEST(Transform3d, QuaternionRotationEffect)
{
	Transform3f t(Vec3d::Zero(), Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f), Vec3d::One());

	ExpectVecNear(t.TransformVector(Vec3d(1.0f, 0.0f, 0.0f)), Vec3d(0.0f, 1.0f, 0.0f));
}

TEST(Transform3d, TranslationRotationScaleEffects)
{
	Transform3f t(
		Vec3d(10.0f, 20.0f, 30.0f),
		Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3d(2.0f, 3.0f, 4.0f));
	Vec3d result = t.TransformPoint(Vec3d(1.0f, 1.0f, 1.0f));

	// Scale to (2, 3, 4), rotate to (-3, 2, 4), then translate.
	ExpectVecNear(result, Vec3d(7.0f, 22.0f, 34.0f));
}

TEST(Transform3d, PointVsVectorTransformBehavior)
{
	Transform3f t(
		Vec3d(10.0f, 20.0f, 30.0f),
		Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3d(2.0f, 2.0f, 2.0f));

	ExpectVecNear(t.TransformPoint(Vec3d(1.0f, 0.0f, 0.0f)), Vec3d(10.0f, 22.0f, 30.0f));
	ExpectVecNear(t.TransformVector(Vec3d(1.0f, 0.0f, 0.0f)), Vec3d(0.0f, 2.0f, 0.0f));
}

TEST(Transform3d, DirectionHelpersUseRotationOnly)
{
	Transform3f t(
		Vec3d(10.0f, 20.0f, 30.0f),
		Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3d(5.0f, 7.0f, 9.0f));

	ExpectVecNear(t.Forward(), Vec3d(0.0f, 0.0f, -1.0f));
	ExpectVecNear(t.Right(), Vec3d(0.0f, 1.0f, 0.0f));
	ExpectVecNear(t.Up(), Vec3d(-1.0f, 0.0f, 0.0f));
}

TEST(Transform3d, DirectionHelpersMatchQuaternionAndMatrixRotation)
{
	Transform3f t(
		Vec3d(10.0f, 20.0f, 30.0f),
		Quatf::FromAxisAngle(Vec3d(0.0f, 1.0f, 0.0f), Pi / 2.0f),
		Vec3d(5.0f, 7.0f, 9.0f));
	Mat3d rotation = t.Rotation.ToMat3();

	ExpectVecNear(t.Forward(), t.Rotation.RotateVector(Vec3d::Forward()));
	ExpectVecNear(t.Right(), t.Rotation.RotateVector(Vec3d::Right()));
	ExpectVecNear(t.Up(), t.Rotation.RotateVector(Vec3d::Up()));

	ExpectVecNear(t.Forward(), rotation * Vec3d::Forward());
	ExpectVecNear(t.Right(), rotation * Vec3d::Right());
	ExpectVecNear(t.Up(), rotation * Vec3d::Up());
}

TEST(Transform3d, ToMat4MatchesHelpers)
{
	Transform3f t(
		Vec3d(3.0f, -2.0f, 5.0f),
		Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3d(2.0f, 3.0f, 4.0f));
	Mat4 m = t.ToMat4();
	Vec3d p(1.0f, 2.0f, 3.0f);
	Vec3d v(1.0f, 2.0f, 3.0f);

	ExpectVecNear(m.TransformPoint(p), t.TransformPoint(p));
	ExpectVecNear(m.TransformVector(v), t.TransformVector(v));
}

TEST(Transform3d, ToMat3MatchesLinearPart)
{
	Transform3f t(
		Vec3d(3.0f, -2.0f, 5.0f),
		Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3d(2.0f, 3.0f, 4.0f));
	Vec3d v(1.0f, 2.0f, 3.0f);

	ExpectVecNear(t.ToMat3() * v, t.TransformVector(v));
}

TEST(Transform3d, CompositionAppliesRightThenLeft)
{
	Transform3f a(
		Vec3d(5.0f, 0.0f, 0.0f),
		Quatf::FromAxisAngle(Vec3d(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3d(2.0f, 2.0f, 2.0f));
	Transform3f b(
		Vec3d(1.0f, 0.0f, 0.0f),
		Quatf::Identity(),
		Vec3d(3.0f, 3.0f, 3.0f));
	Transform3f composed = a * b;
	Vec3d point(1.0f, 0.0f, 0.0f);

	ExpectVecNear(composed.TransformPoint(point), a.TransformPoint(b.TransformPoint(point)));
	EXPECT_TRUE(composed.ToMat4().NearlyEquals(a.ToMat4() * b.ToMat4()));
}
