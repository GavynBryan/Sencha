#include <gtest/gtest.h>
#include <geometry/Line.h>
#include <geometry/Plane.h>
#include <cmath>
#include <sstream>

// --- Construction ---

TEST(Plane, DefaultConstructionIsXYPlane)
{
	Planef plane;
	EXPECT_EQ(plane.Normal, Vec3::Up());
	EXPECT_FLOAT_EQ(plane.D, 0.0f);
}

TEST(Plane, ValueConstruction)
{
	Planef plane(Vec3(0.0f, 1.0f, 0.0f), -5.0f);
	EXPECT_EQ(plane.Normal, Vec3(0.0f, 1.0f, 0.0f));
	EXPECT_FLOAT_EQ(plane.D, -5.0f);
}

TEST(Plane, FromNormalAndDistance)
{
	Planef plane = Planef::FromNormalAndDistance(Vec3(0.0f, 0.0f, 1.0f), 3.0f);
	EXPECT_EQ(plane.Normal, Vec3(0.0f, 0.0f, 1.0f));
	EXPECT_FLOAT_EQ(plane.D, 3.0f);
}

TEST(Plane, FromNormalAndPoint)
{
	// Plane at y=5, normal pointing +Y
	Planef plane = Planef::FromNormalAndPoint(Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 5.0f, 0.0f));
	EXPECT_EQ(plane.Normal, Vec3(0.0f, 1.0f, 0.0f));
	EXPECT_FLOAT_EQ(plane.D, -5.0f);
}

// --- Normalization ---

TEST(Plane, NormalizationScalesNormalAndD)
{
	// Non-unit normal
	Planef plane(Vec3(0.0f, 3.0f, 0.0f), -15.0f);
	Planef normalized = plane.Normalized();

	EXPECT_NEAR(normalized.Normal.Data[0], 0.0f, 1e-6f);
	EXPECT_NEAR(normalized.Normal.Data[1], 1.0f, 1e-6f);
	EXPECT_NEAR(normalized.Normal.Data[2], 0.0f, 1e-6f);
	EXPECT_NEAR(normalized.D, -5.0f, 1e-6f);
}

TEST(Plane, NormalizedPlaneGivesSameSignedDistance)
{
	Planef plane(Vec3(0.0f, 2.0f, 0.0f), -6.0f);
	Planef normalized = plane.Normalized();

	Vec3 point(0.0f, 4.0f, 0.0f);

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
	Planef plane(Vec3(0.0f, 1.0f, 0.0f), 0.0f); // y=0 plane
	EXPECT_GT(plane.SignedDistanceTo(Vec3(0.0f, 5.0f, 0.0f)), 0.0f);
}

TEST(Plane, SignedDistanceNegativeHalfSpace)
{
	Planef plane(Vec3(0.0f, 1.0f, 0.0f), 0.0f);
	EXPECT_LT(plane.SignedDistanceTo(Vec3(0.0f, -3.0f, 0.0f)), 0.0f);
}

TEST(Plane, SignedDistanceOnPlane)
{
	Planef plane(Vec3(0.0f, 1.0f, 0.0f), -5.0f); // y=5 plane
	EXPECT_NEAR(plane.SignedDistanceTo(Vec3(10.0f, 5.0f, -7.0f)), 0.0f, 1e-6f);
}

TEST(Plane, SignedDistanceValue)
{
	Planef plane(Vec3(0.0f, 1.0f, 0.0f), 0.0f); // y=0 plane, unit normal
	EXPECT_FLOAT_EQ(plane.SignedDistanceTo(Vec3(0.0f, 7.0f, 0.0f)), 7.0f);
	EXPECT_FLOAT_EQ(plane.SignedDistanceTo(Vec3(0.0f, -3.0f, 0.0f)), -3.0f);
}

// --- Closest Point ---

TEST(Plane, ClosestPointProjectsOntoPlane)
{
	Planef plane(Vec3(0.0f, 1.0f, 0.0f), 0.0f); // y=0 plane
	Vec3 point(3.0f, 7.0f, -2.0f);
	Vec3 closest = plane.ClosestPoint(point);

	EXPECT_NEAR(closest.Data[0], 3.0f, 1e-6f);
	EXPECT_NEAR(closest.Data[1], 0.0f, 1e-6f);
	EXPECT_NEAR(closest.Data[2], -2.0f, 1e-6f);
}

// --- Comparison ---

TEST(Plane, EqualityAndNearlyEquals)
{
	Planef a(Vec3(0.0f, 1.0f, 0.0f), -5.0f);
	Planef b(Vec3(0.0f, 1.0f, 0.0f), -5.0f);
	Planef c(Vec3(0.0f, 1.0f, 0.0f), -5.0001f);

	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a == c);
	EXPECT_TRUE(a.NearlyEquals(c, 0.001f));
}

// --- Stream Output ---

TEST(Plane, StreamOutput)
{
	Planef plane(Vec3(0.0f, 1.0f, 0.0f), -5.0f);
	std::ostringstream oss;
	oss << plane;
	EXPECT_EQ(oss.str(), "{Normal: (0, 1, 0), D: -5}");
}

// --- Line ---

TEST(Line, SignedDistanceAndClosestPoint)
{
	Linef line = Linef::FromNormalAndPoint(Vec2(1.0f, 0.0f), Vec2(3.0f, 0.0f));

	EXPECT_FLOAT_EQ(line.SignedDistanceTo(Vec2(5.0f, 2.0f)), 2.0f);
	EXPECT_FLOAT_EQ(line.SignedDistanceTo(Vec2(1.0f, 2.0f)), -2.0f);

	Vec2 closest = line.ClosestPoint(Vec2(5.0f, 2.0f));
	EXPECT_NEAR(closest.Data[0], 3.0f, 1e-6f);
	EXPECT_NEAR(closest.Data[1], 2.0f, 1e-6f);
}
