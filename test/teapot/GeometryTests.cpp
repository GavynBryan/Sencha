#include <gtest/gtest.h>
#include <teapot/geometry/IGeometry.h>
#include <cmath>

//=============================================================================
// EuclideanGeometry2D Tests
//=============================================================================

class EuclideanGeometry2DTest : public ::testing::Test
{
protected:
	EuclideanGeometry2D geo;
};

TEST_F(EuclideanGeometry2DTest, DistanceBetweenSamePointIsZero)
{
	Vec2 p(3.0f, 4.0f);
	EXPECT_FLOAT_EQ(geo.Distance(p, p), 0.0f);
}

TEST_F(EuclideanGeometry2DTest, DistanceIsL2Norm)
{
	Vec2 a(0.0f, 0.0f);
	Vec2 b(3.0f, 4.0f);
	EXPECT_FLOAT_EQ(geo.Distance(a, b), 5.0f);
}

TEST_F(EuclideanGeometry2DTest, DistanceIsSymmetric)
{
	Vec2 a(1.0f, 2.0f);
	Vec2 b(4.0f, 6.0f);
	EXPECT_FLOAT_EQ(geo.Distance(a, b), geo.Distance(b, a));
}

TEST_F(EuclideanGeometry2DTest, SqrDistanceAvoidsSqrt)
{
	Vec2 a(0.0f, 0.0f);
	Vec2 b(3.0f, 4.0f);
	EXPECT_FLOAT_EQ(geo.SqrDistance(a, b), 25.0f);
}

TEST_F(EuclideanGeometry2DTest, TranslateAddsOffset)
{
	Vec2 point(1.0f, 2.0f);
	Vec2 offset(3.0f, -1.0f);
	Vec2 result = geo.Translate(point, offset);
	EXPECT_FLOAT_EQ(result.X(), 4.0f);
	EXPECT_FLOAT_EQ(result.Y(), 1.0f);
}

TEST_F(EuclideanGeometry2DTest, DirectionIsUnitVector)
{
	Vec2 a(0.0f, 0.0f);
	Vec2 b(3.0f, 4.0f);
	Vec2 dir = geo.Direction(a, b);
	EXPECT_NEAR(dir.Magnitude(), 1.0f, 1e-5f);
	EXPECT_NEAR(dir.X(), 0.6f, 1e-5f);
	EXPECT_NEAR(dir.Y(), 0.8f, 1e-5f);
}

TEST_F(EuclideanGeometry2DTest, DirectionFromSamePointIsZero)
{
	Vec2 p(5.0f, 5.0f);
	Vec2 dir = geo.Direction(p, p);
	EXPECT_FLOAT_EQ(dir.X(), 0.0f);
	EXPECT_FLOAT_EQ(dir.Y(), 0.0f);
}

TEST_F(EuclideanGeometry2DTest, MoveTowardReachesTargetWhenCloseEnough)
{
	Vec2 a(0.0f, 0.0f);
	Vec2 b(1.0f, 0.0f);
	Vec2 result = geo.MoveToward(a, b, 5.0f);
	EXPECT_FLOAT_EQ(result.X(), b.X());
	EXPECT_FLOAT_EQ(result.Y(), b.Y());
}

TEST_F(EuclideanGeometry2DTest, MoveTowardStopsAtMaxDistance)
{
	Vec2 a(0.0f, 0.0f);
	Vec2 b(10.0f, 0.0f);
	Vec2 result = geo.MoveToward(a, b, 3.0f);
	EXPECT_NEAR(result.X(), 3.0f, 1e-5f);
	EXPECT_NEAR(result.Y(), 0.0f, 1e-5f);
}

TEST_F(EuclideanGeometry2DTest, InterpolateMidpoint)
{
	Vec2 a(0.0f, 0.0f);
	Vec2 b(10.0f, 20.0f);
	Vec2 mid = geo.Interpolate(a, b, 0.5f);
	EXPECT_FLOAT_EQ(mid.X(), 5.0f);
	EXPECT_FLOAT_EQ(mid.Y(), 10.0f);
}

TEST_F(EuclideanGeometry2DTest, InterpolateAtZeroReturnsStart)
{
	Vec2 a(1.0f, 2.0f);
	Vec2 b(5.0f, 6.0f);
	Vec2 result = geo.Interpolate(a, b, 0.0f);
	EXPECT_FLOAT_EQ(result.X(), a.X());
	EXPECT_FLOAT_EQ(result.Y(), a.Y());
}

TEST_F(EuclideanGeometry2DTest, InterpolateAtOneReturnsEnd)
{
	Vec2 a(1.0f, 2.0f);
	Vec2 b(5.0f, 6.0f);
	Vec2 result = geo.Interpolate(a, b, 1.0f);
	EXPECT_FLOAT_EQ(result.X(), b.X());
	EXPECT_FLOAT_EQ(result.Y(), b.Y());
}

//=============================================================================
// EuclideanGeometry3D Tests
//=============================================================================

class EuclideanGeometry3DTest : public ::testing::Test
{
protected:
	EuclideanGeometry3D geo;
};

TEST_F(EuclideanGeometry3DTest, DistanceIsL2Norm3D)
{
	Vec3 a(0.0f, 0.0f, 0.0f);
	Vec3 b(1.0f, 2.0f, 2.0f);
	EXPECT_FLOAT_EQ(geo.Distance(a, b), 3.0f);
}

TEST_F(EuclideanGeometry3DTest, SqrDistance3D)
{
	Vec3 a(0.0f, 0.0f, 0.0f);
	Vec3 b(1.0f, 2.0f, 2.0f);
	EXPECT_FLOAT_EQ(geo.SqrDistance(a, b), 9.0f);
}

TEST_F(EuclideanGeometry3DTest, Translate3D)
{
	Vec3 point(1.0f, 2.0f, 3.0f);
	Vec3 offset(10.0f, 20.0f, 30.0f);
	Vec3 result = geo.Translate(point, offset);
	EXPECT_FLOAT_EQ(result.X(), 11.0f);
	EXPECT_FLOAT_EQ(result.Y(), 22.0f);
	EXPECT_FLOAT_EQ(result.Z(), 33.0f);
}

TEST_F(EuclideanGeometry3DTest, Direction3DIsUnit)
{
	Vec3 a(0.0f, 0.0f, 0.0f);
	Vec3 b(0.0f, 0.0f, 5.0f);
	Vec3 dir = geo.Direction(a, b);
	EXPECT_NEAR(dir.Magnitude(), 1.0f, 1e-5f);
	EXPECT_NEAR(dir.Z(), 1.0f, 1e-5f);
}

TEST_F(EuclideanGeometry3DTest, MoveToward3D)
{
	Vec3 a(0.0f, 0.0f, 0.0f);
	Vec3 b(0.0f, 0.0f, 10.0f);
	Vec3 result = geo.MoveToward(a, b, 4.0f);
	EXPECT_NEAR(result.Z(), 4.0f, 1e-5f);
}

TEST_F(EuclideanGeometry3DTest, Interpolate3D)
{
	Vec3 a(0.0f, 0.0f, 0.0f);
	Vec3 b(10.0f, 20.0f, 30.0f);
	Vec3 mid = geo.Interpolate(a, b, 0.25f);
	EXPECT_FLOAT_EQ(mid.X(), 2.5f);
	EXPECT_FLOAT_EQ(mid.Y(), 5.0f);
	EXPECT_FLOAT_EQ(mid.Z(), 7.5f);
}

//=============================================================================
// IGeometry polymorphism — ensure interface is usable via base pointer
//=============================================================================

TEST(GeometryPolymorphism, CanUseViaBasePointer)
{
	EuclideanGeometry2D concrete;
	IGeometry2D& geo = concrete;

	Vec2 a(0.0f, 0.0f);
	Vec2 b(3.0f, 4.0f);
	EXPECT_FLOAT_EQ(geo.Distance(a, b), 5.0f);
}

TEST(GeometryPolymorphism, DifferentImplementationsCanBeSwapped)
{
	// Both EuclideanGeometry2D and EuclideanGeometry3D derive from
	// IGeometry<N> which derives from IService, so they can be
	// registered and swapped in a ServiceHost.
	EuclideanGeometry2D geo2d;
	EuclideanGeometry3D geo3d;

	IService* service2d = &geo2d;
	IService* service3d = &geo3d;

	EXPECT_NE(service2d, nullptr);
	EXPECT_NE(service3d, nullptr);
}

//=============================================================================
// Double-precision tests
//=============================================================================

TEST(EuclideanGeometryDouble, Distance2Dd)
{
	EuclideanGeometry2Dd geo;
	Vec2d a(0.0, 0.0);
	Vec2d b(3.0, 4.0);
	EXPECT_DOUBLE_EQ(geo.Distance(a, b), 5.0);
}

TEST(EuclideanGeometryDouble, Distance3Dd)
{
	EuclideanGeometry3Dd geo;
	Vec3d a(0.0, 0.0, 0.0);
	Vec3d b(1.0, 2.0, 2.0);
	EXPECT_DOUBLE_EQ(geo.Distance(a, b), 3.0);
}
