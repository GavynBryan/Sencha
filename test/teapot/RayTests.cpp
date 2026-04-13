#include <gtest/gtest.h>
#include <geometry/2d/Ray2.h>
#include <geometry/3d/Ray3.h>
#include <cmath>
#include <sstream>

// --- Construction ---

TEST(Ray, DefaultConstructionAtOriginForward)
{
	Ray3f ray;
	EXPECT_EQ(ray.Origin, Vec3::Zero());
	EXPECT_EQ(ray.Direction, Vec3::Forward());
}

TEST(Ray, ValueConstruction)
{
	Ray3f ray(Vec3(1.0f, 2.0f, 3.0f), Vec3(0.0f, 1.0f, 0.0f));
	EXPECT_EQ(ray.Origin, Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(ray.Direction, Vec3(0.0f, 1.0f, 0.0f));
}

// --- PointAt ---

TEST(Ray, PointAtZeroReturnsOrigin)
{
	Ray3f ray(Vec3(1.0f, 2.0f, 3.0f), Vec3(0.0f, 1.0f, 0.0f));
	EXPECT_EQ(ray.PointAt(0.0f), Vec3(1.0f, 2.0f, 3.0f));
}

TEST(Ray, PointAtPositiveDistance)
{
	Ray3f ray(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f));
	Vec3 p = ray.PointAt(5.0f);
	EXPECT_FLOAT_EQ(p.X, 5.0f);
	EXPECT_FLOAT_EQ(p.Y, 0.0f);
	EXPECT_FLOAT_EQ(p.Z, 0.0f);
}

TEST(Ray, PointAtNegativeDistance)
{
	Ray3f ray(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f));
	Vec3 p = ray.PointAt(-3.0f);
	EXPECT_FLOAT_EQ(p.X, 0.0f);
	EXPECT_FLOAT_EQ(p.Y, 0.0f);
	EXPECT_FLOAT_EQ(p.Z, 3.0f);
}

TEST(Ray, PointAtWithDiagonalDirection)
{
	Vec3 dir = Vec3(1.0f, 1.0f, 0.0f).Normalized();
	Ray3f ray(Vec3(0.0f, 0.0f, 0.0f), dir);
	Vec3 p = ray.PointAt(std::sqrt(2.0f));

	EXPECT_NEAR(p.X, 1.0f, 1e-5f);
	EXPECT_NEAR(p.Y, 1.0f, 1e-5f);
	EXPECT_NEAR(p.Z, 0.0f, 1e-5f);
}

// --- Normalization ---

TEST(Ray, NormalizedProducesUnitDirection)
{
	Ray3f ray(Vec3(1.0f, 2.0f, 3.0f), Vec3(3.0f, 0.0f, 4.0f));
	Ray3f normalized = ray.Normalized();

	float mag = normalized.Direction.Magnitude();
	EXPECT_NEAR(mag, 1.0f, 1e-6f);
	EXPECT_EQ(normalized.Origin, ray.Origin);
}

// --- Comparison ---

TEST(Ray, EqualityAndNearlyEquals)
{
	Ray3f a(Vec3(1.0f, 2.0f, 3.0f), Vec3(0.0f, 1.0f, 0.0f));
	Ray3f b(Vec3(1.0f, 2.0f, 3.0f), Vec3(0.0f, 1.0f, 0.0f));
	Ray3f c(Vec3(1.0f, 2.0f, 3.0001f), Vec3(0.0f, 1.0f, 0.0f));

	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a == c);
	EXPECT_TRUE(a.NearlyEquals(c, 0.001f));
}

// --- Stream Output ---

TEST(Ray, StreamOutput)
{
	Ray3f ray(Vec3(1.0f, 2.0f, 3.0f), Vec3(0.0f, 1.0f, 0.0f));
	std::ostringstream oss;
	oss << ray;
	EXPECT_EQ(oss.str(), "{Origin: (1, 2, 3), Direction: (0, 1, 0)}");
}

// --- 2D Ray Alias ---

TEST(Ray2, DefaultConstructionAtOriginRight)
{
	Ray2f ray;
	EXPECT_EQ(ray.Origin, Vec2::Zero());
	EXPECT_EQ(ray.Direction, Vec2::Right());
}

TEST(Ray2, PointAtUses2DOriginAndDirection)
{
	Ray2f ray(Vec2(2.0f, 3.0f), Vec2(0.0f, 1.0f));
	EXPECT_EQ(ray.PointAt(4.0f), Vec2(2.0f, 7.0f));
}
