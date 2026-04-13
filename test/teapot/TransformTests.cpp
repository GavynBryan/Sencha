#include <gtest/gtest.h>
#include <math/Mat.h>
#include <math/Quat.h>
#include <math/Transform2.h>
#include <math/Transform3.h>
#include <math/Vec.h>
#include <cmath>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;

	void ExpectVecNear(const Vec2& actual, const Vec2& expected, float epsilon = 1e-5f)
	{
		EXPECT_NEAR(actual.X, expected.X, epsilon);
		EXPECT_NEAR(actual.Y, expected.Y, epsilon);
	}

	void ExpectVecNear(const Vec3& actual, const Vec3& expected, float epsilon = 1e-5f)
	{
		EXPECT_NEAR(actual.X, expected.X, epsilon);
		EXPECT_NEAR(actual.Y, expected.Y, epsilon);
		EXPECT_NEAR(actual.Z, expected.Z, epsilon);
	}

	Vec2 TransformPoint(const Mat3& m, const Vec2& point)
	{
		Vec3 h(point.X, point.Y, 1.0f);
		Vec3 r = m * h;
		return Vec2(r.X, r.Y);
	}

	Vec2 TransformVector(const Mat3& m, const Vec2& vector)
	{
		Vec3 h(vector.X, vector.Y, 0.0f);
		Vec3 r = m * h;
		return Vec2(r.X, r.Y);
	}
}

// --- Transform2 core ---

TEST(Transform2, DefaultConstructionIsIdentity)
{
	Transform2f t;

	EXPECT_EQ(t.Position, Vec2::Zero());
	EXPECT_FLOAT_EQ(t.Rotation, 0.0f);
	EXPECT_EQ(t.Scale, Vec2::One());
}

TEST(Transform2, ValueConstruction)
{
	Transform2f t(Vec2(1.0f, 2.0f), Pi / 2.0f, Vec2(3.0f, 4.0f));

	EXPECT_EQ(t.Position, Vec2(1.0f, 2.0f));
	EXPECT_FLOAT_EQ(t.Rotation, Pi / 2.0f);
	EXPECT_EQ(t.Scale, Vec2(3.0f, 4.0f));
}

TEST(Transform2, IdentityFactory)
{
	EXPECT_EQ(Transform2f::Identity(), Transform2f());
}

TEST(Transform2, Equality)
{
	EXPECT_EQ(Transform2f(Vec2(1.0f, 2.0f), 3.0f, Vec2(4.0f, 5.0f)),
		Transform2f(Vec2(1.0f, 2.0f), 3.0f, Vec2(4.0f, 5.0f)));
	EXPECT_NE(Transform2f(Vec2(1.0f, 2.0f), 3.0f, Vec2(4.0f, 5.0f)),
		Transform2f(Vec2(1.0f, 2.0f), 6.0f, Vec2(4.0f, 5.0f)));
}

TEST(Transform2, NearlyEquals)
{
	Transform2f a(Vec2(1.0f, 2.0f), 3.0f, Vec2(4.0f, 5.0f));
	Transform2f b(Vec2(1.0f + 1e-7f, 2.0f), 3.0f, Vec2(4.0f, 5.0f));

	EXPECT_TRUE(a.NearlyEquals(b));
	EXPECT_FALSE(a.NearlyEquals(Transform2f(Vec2(1.1f, 2.0f), 3.0f, Vec2(4.0f, 5.0f))));
}

// --- Transform2 math ---

TEST(Transform2, ToMat3Identity)
{
	EXPECT_TRUE(Transform2f::Identity().ToMat3().NearlyEquals(Mat3::Identity()));
}

TEST(Transform2, TranslationRotationScaleEffects)
{
	Transform2f t(Vec2(10.0f, 20.0f), Pi / 2.0f, Vec2(2.0f, 3.0f));
	Vec2 result = t.TransformPoint(Vec2(1.0f, 1.0f));

	// Scale to (2, 3), rotate to (-3, 2), then translate.
	ExpectVecNear(result, Vec2(7.0f, 22.0f));
}

TEST(Transform2, PointVsVectorTransformBehavior)
{
	Transform2f t(Vec2(10.0f, 20.0f), Pi / 2.0f, Vec2(2.0f, 2.0f));

	ExpectVecNear(t.TransformPoint(Vec2(1.0f, 0.0f)), Vec2(10.0f, 22.0f));
	ExpectVecNear(t.TransformVector(Vec2(1.0f, 0.0f)), Vec2(0.0f, 2.0f));
}

TEST(Transform2, DirectionHelpersUseRotationOnly)
{
	Transform2f t(Vec2(10.0f, 20.0f), Pi / 2.0f, Vec2(5.0f, 7.0f));

	ExpectVecNear(t.Forward(), Vec2(-1.0f, 0.0f));
	ExpectVecNear(t.Right(), Vec2(0.0f, 1.0f));
}

TEST(Transform2, DirectionHelpersMatchMatrixRotation)
{
	Transform2f t(Vec2(10.0f, 20.0f), Pi / 2.0f, Vec2(5.0f, 7.0f));
	Mat3 rotation = Mat3::MakeRotationZ(t.Rotation);

	ExpectVecNear(t.Forward(), TransformVector(rotation, Vec2::Up()));
	ExpectVecNear(t.Right(), TransformVector(rotation, Vec2::Right()));
}

TEST(Transform2, ToMat3MatchesHelpers)
{
	Transform2f t(Vec2(3.0f, -2.0f), Pi / 2.0f, Vec2(2.0f, 3.0f));
	Mat3 m = t.ToMat3();
	Vec2 p(1.0f, 2.0f);
	Vec2 v(1.0f, 2.0f);

	ExpectVecNear(TransformPoint(m, p), t.TransformPoint(p));
	ExpectVecNear(TransformVector(m, v), t.TransformVector(v));
}

TEST(Transform2, CompositionAppliesRightThenLeft)
{
	Transform2f a(Vec2(5.0f, 0.0f), Pi / 2.0f, Vec2(2.0f, 2.0f));
	Transform2f b(Vec2(1.0f, 0.0f), 0.0f, Vec2(3.0f, 3.0f));
	Transform2f composed = a * b;
	Vec2 point(1.0f, 0.0f);

	ExpectVecNear(composed.TransformPoint(point), a.TransformPoint(b.TransformPoint(point)));
	EXPECT_TRUE(composed.ToMat3().NearlyEquals(a.ToMat3() * b.ToMat3()));
}

// --- Transform3 core ---

TEST(Transform3, DefaultConstructionIsIdentity)
{
	Transform3f t;

	EXPECT_EQ(t.Position, Vec3::Zero());
	EXPECT_EQ(t.Rotation, Quatf::Identity());
	EXPECT_EQ(t.Scale, Vec3::One());
}

TEST(Transform3, ValueConstruction)
{
	Quatf rotation = Quatf::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), Pi / 2.0f);
	Transform3f t(Vec3(1.0f, 2.0f, 3.0f), rotation, Vec3(4.0f, 5.0f, 6.0f));

	EXPECT_EQ(t.Position, Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(t.Rotation, rotation);
	EXPECT_EQ(t.Scale, Vec3(4.0f, 5.0f, 6.0f));
}

TEST(Transform3, IdentityFactory)
{
	EXPECT_EQ(Transform3f::Identity(), Transform3f());
}

TEST(Transform3, Equality)
{
	Transform3f a(Vec3(1.0f, 2.0f, 3.0f), Quatf::Identity(), Vec3(4.0f, 5.0f, 6.0f));
	Transform3f b(Vec3(1.0f, 2.0f, 3.0f), Quatf::Identity(), Vec3(4.0f, 5.0f, 6.0f));
	Transform3f c(Vec3(1.0f, 2.0f, 3.0f), Quatf::Identity(), Vec3(4.0f, 5.0f, 7.0f));

	EXPECT_EQ(a, b);
	EXPECT_NE(a, c);
}

TEST(Transform3, NearlyEquals)
{
	Transform3f a(Vec3(1.0f, 2.0f, 3.0f), Quatf::Identity(), Vec3(4.0f, 5.0f, 6.0f));
	Transform3f b(Vec3(1.0f, 2.0f, 3.0f + 1e-7f), Quatf::Identity(), Vec3(4.0f, 5.0f, 6.0f));

	EXPECT_TRUE(a.NearlyEquals(b));
	EXPECT_FALSE(a.NearlyEquals(Transform3f(Vec3(1.0f, 2.0f, 3.1f), Quatf::Identity(), Vec3(4.0f, 5.0f, 6.0f))));
}

// --- Transform3 math ---

TEST(Transform3, ToMat4Identity)
{
	EXPECT_TRUE(Transform3f::Identity().ToMat4().NearlyEquals(Mat4::Identity()));
}

TEST(Transform3, QuaternionRotationEffect)
{
	Transform3f t(Vec3::Zero(), Quatf::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), Pi / 2.0f), Vec3::One());

	ExpectVecNear(t.TransformVector(Vec3(1.0f, 0.0f, 0.0f)), Vec3(0.0f, 1.0f, 0.0f));
}

TEST(Transform3, TranslationRotationScaleEffects)
{
	Transform3f t(
		Vec3(10.0f, 20.0f, 30.0f),
		Quatf::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3(2.0f, 3.0f, 4.0f));
	Vec3 result = t.TransformPoint(Vec3(1.0f, 1.0f, 1.0f));

	// Scale to (2, 3, 4), rotate to (-3, 2, 4), then translate.
	ExpectVecNear(result, Vec3(7.0f, 22.0f, 34.0f));
}

TEST(Transform3, PointVsVectorTransformBehavior)
{
	Transform3f t(
		Vec3(10.0f, 20.0f, 30.0f),
		Quatf::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3(2.0f, 2.0f, 2.0f));

	ExpectVecNear(t.TransformPoint(Vec3(1.0f, 0.0f, 0.0f)), Vec3(10.0f, 22.0f, 30.0f));
	ExpectVecNear(t.TransformVector(Vec3(1.0f, 0.0f, 0.0f)), Vec3(0.0f, 2.0f, 0.0f));
}

TEST(Transform3, DirectionHelpersUseRotationOnly)
{
	Transform3f t(
		Vec3(10.0f, 20.0f, 30.0f),
		Quatf::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3(5.0f, 7.0f, 9.0f));

	ExpectVecNear(t.Forward(), Vec3(0.0f, 0.0f, -1.0f));
	ExpectVecNear(t.Right(), Vec3(0.0f, 1.0f, 0.0f));
	ExpectVecNear(t.Up(), Vec3(-1.0f, 0.0f, 0.0f));
}

TEST(Transform3, DirectionHelpersMatchQuaternionAndMatrixRotation)
{
	Transform3f t(
		Vec3(10.0f, 20.0f, 30.0f),
		Quatf::FromAxisAngle(Vec3(0.0f, 1.0f, 0.0f), Pi / 2.0f),
		Vec3(5.0f, 7.0f, 9.0f));
	Mat3 rotation = t.Rotation.ToMat3();

	ExpectVecNear(t.Forward(), t.Rotation.RotateVector(Vec3::Forward()));
	ExpectVecNear(t.Right(), t.Rotation.RotateVector(Vec3::Right()));
	ExpectVecNear(t.Up(), t.Rotation.RotateVector(Vec3::Up()));

	ExpectVecNear(t.Forward(), rotation * Vec3::Forward());
	ExpectVecNear(t.Right(), rotation * Vec3::Right());
	ExpectVecNear(t.Up(), rotation * Vec3::Up());
}

TEST(Transform3, ToMat4MatchesHelpers)
{
	Transform3f t(
		Vec3(3.0f, -2.0f, 5.0f),
		Quatf::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3(2.0f, 3.0f, 4.0f));
	Mat4 m = t.ToMat4();
	Vec3 p(1.0f, 2.0f, 3.0f);
	Vec3 v(1.0f, 2.0f, 3.0f);

	ExpectVecNear(m.TransformPoint(p), t.TransformPoint(p));
	ExpectVecNear(m.TransformVector(v), t.TransformVector(v));
}

TEST(Transform3, ToMat3MatchesLinearPart)
{
	Transform3f t(
		Vec3(3.0f, -2.0f, 5.0f),
		Quatf::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3(2.0f, 3.0f, 4.0f));
	Vec3 v(1.0f, 2.0f, 3.0f);

	ExpectVecNear(t.ToMat3() * v, t.TransformVector(v));
}

TEST(Transform3, CompositionAppliesRightThenLeft)
{
	Transform3f a(
		Vec3(5.0f, 0.0f, 0.0f),
		Quatf::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), Pi / 2.0f),
		Vec3(2.0f, 2.0f, 2.0f));
	Transform3f b(
		Vec3(1.0f, 0.0f, 0.0f),
		Quatf::Identity(),
		Vec3(3.0f, 3.0f, 3.0f));
	Transform3f composed = a * b;
	Vec3 point(1.0f, 0.0f, 0.0f);

	ExpectVecNear(composed.TransformPoint(point), a.TransformPoint(b.TransformPoint(point)));
	EXPECT_TRUE(composed.ToMat4().NearlyEquals(a.ToMat4() * b.ToMat4()));
}
