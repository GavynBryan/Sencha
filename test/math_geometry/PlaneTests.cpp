#include <gtest/gtest.h>
#include <geometry/2d/Line.h>
#include <geometry/3d/Plane.h>
#include <cmath>
#include <sstream>

// --- Construction ---

TEST(Plane, DefaultConstructionIsXYPlane)
{
	Planef plane;
	EXPECT_EQ(plane.Normal, Vec3d::Up());
	EXPECT_FLOAT_EQ(plane.D, 0.0f);
}

TEST(Plane, ValueConstruction)
{
	Planef plane(Vec3d(0.0f, 1.0f, 0.0f), -5.0f);
	EXPECT_EQ(plane.Normal, Vec3d(0.0f, 1.0f, 0.0f));
	EXPECT_FLOAT_EQ(plane.D, -5.0f);
}

TEST(Plane, FromNormalAndDistance)
{
	Planef plane = Planef::FromNormalAndDistance(Vec3d(0.0f, 0.0f, 1.0f), 3.0f);
	EXPECT_EQ(plane.Normal, Vec3d(0.0f, 0.0f, 1.0f));
	EXPECT_FLOAT_EQ(plane.D, 3.0f);
}

TEST(Plane, FromNormalAndPoint)
{
	// Plane at y=5, normal pointing +Y
	Planef plane = Planef::FromNormalAndPoint(Vec3d(0.0f, 1.0f, 0.0f), Vec3d(0.0f, 5.0f, 0.0f));
	EXPECT_EQ(plane.Normal, Vec3d(0.0f, 1.0f, 0.0f));
	EXPECT_FLOAT_EQ(plane.D, -5.0f);
}

// --- Normalization ---

TEST(Plane, NormalizationScalesNormalAndD)
{
	// Non-unit normal
	Planef plane(Vec3d(0.0f, 3.0f, 0.0f), -15.0f);
	Planef normalized = plane.Normalized();

	EXPECT_NEAR(normalized.Normal.X, 0.0f, 1e-6f);
	EXPECT_NEAR(normalized.Normal.Y, 1.0f, 1e-6f);
	EXPECT_NEAR(normalized.Normal.Z, 0.0f, 1e-6f);
	EXPECT_NEAR(normalized.D, -5.0f, 1e-6f);
}

TEST(Plane, NormalizedPlaneGivesSameSignedDistance)
{
	Planef plane(Vec3d(0.0f, 2.0f, 0.0f), -6.0f);
	Planef normalized = plane.Normalized();

	Vec3d point(0.0f, 4.0f, 0.0f);

	// Unnormalized gives scaled distance
	float rawDist = plane.SignedDistanceTo(point);
	float normDist = normalized.SignedDistanceTo(point);

	// Normalized distance should be the true Euclidean distance
	EXPECT_NEAR(normDist, 1.0f, 1e-5f);
	// Raw distance is scaled by normal length (2)
	EXPECT_NEAR(rawDist, 2.0f, 1e-5f);
}

// --- Signed Distance ---

TEST(Plane, SignedDistancePositiveHalfSpace)
{
	Planef plane(Vec3d(0.0f, 1.0f, 0.0f), 0.0f); // y=0 plane
	EXPECT_GT(plane.SignedDistanceTo(Vec3d(0.0f, 5.0f, 0.0f)), 0.0f);
}

TEST(Plane, SignedDistanceNegativeHalfSpace)
{
	Planef plane(Vec3d(0.0f, 1.0f, 0.0f), 0.0f);
	EXPECT_LT(plane.SignedDistanceTo(Vec3d(0.0f, -3.0f, 0.0f)), 0.0f);
}

TEST(Plane, SignedDistanceOnPlane)
{
	Planef plane(Vec3d(0.0f, 1.0f, 0.0f), -5.0f); // y=5 plane
	EXPECT_NEAR(plane.SignedDistanceTo(Vec3d(10.0f, 5.0f, -7.0f)), 0.0f, 1e-6f);
}

TEST(Plane, SignedDistanceValue)
{
	Planef plane(Vec3d(0.0f, 1.0f, 0.0f), 0.0f); // y=0 plane, unit normal
	EXPECT_FLOAT_EQ(plane.SignedDistanceTo(Vec3d(0.0f, 7.0f, 0.0f)), 7.0f);
	EXPECT_FLOAT_EQ(plane.SignedDistanceTo(Vec3d(0.0f, -3.0f, 0.0f)), -3.0f);
}

// --- Closest Point ---

TEST(Plane, ClosestPointProjectsOntoPlane)
{
	Planef plane(Vec3d(0.0f, 1.0f, 0.0f), 0.0f); // y=0 plane
	Vec3d point(3.0f, 7.0f, -2.0f);
	Vec3d closest = plane.ClosestPoint(point);

	EXPECT_NEAR(closest.X, 3.0f, 1e-6f);
	EXPECT_NEAR(closest.Y, 0.0f, 1e-6f);
	EXPECT_NEAR(closest.Z, -2.0f, 1e-6f);
}

// --- Comparison ---

TEST(Plane, EqualityAndNearlyEquals)
{
	Planef a(Vec3d(0.0f, 1.0f, 0.0f), -5.0f);
	Planef b(Vec3d(0.0f, 1.0f, 0.0f), -5.0f);
	Planef c(Vec3d(0.0f, 1.0f, 0.0f), -5.0001f);

	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a == c);
	EXPECT_TRUE(a.NearlyEquals(c, 0.001f));
}

// --- Stream Output ---

TEST(Plane, StreamOutput)
{
	Planef plane(Vec3d(0.0f, 1.0f, 0.0f), -5.0f);
	std::ostringstream oss;
	oss << plane;
	EXPECT_EQ(oss.str(), "{Normal: (0, 1, 0), D: -5}");
}

// --- Line ---

TEST(Line, SignedDistanceAndClosestPoint)
{
	Linef line = Linef::FromNormalAndPoint(Vec2d(1.0f, 0.0f), Vec2d(3.0f, 0.0f));

	EXPECT_FLOAT_EQ(line.SignedDistanceTo(Vec2d(5.0f, 2.0f)), 2.0f);
	EXPECT_FLOAT_EQ(line.SignedDistanceTo(Vec2d(1.0f, 2.0f)), -2.0f);

	Vec2d closest = line.ClosestPoint(Vec2d(5.0f, 2.0f));
	EXPECT_NEAR(closest.X, 3.0f, 1e-6f);
	EXPECT_NEAR(closest.Y, 2.0f, 1e-6f);
}
