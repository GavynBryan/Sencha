#include <gtest/gtest.h>
#include <geometry/Circle.h>
#include <geometry/Sphere.h>
#include <sstream>

// --- Construction ---

TEST(Sphere, DefaultConstructionAtOriginZeroRadius)
{
	Spheref sphere;
	EXPECT_EQ(sphere.Center, Vec3::Zero());
	EXPECT_FLOAT_EQ(sphere.Radius, 0.0f);
}

TEST(Sphere, ValueConstruction)
{
	Spheref sphere(Vec3(1.0f, 2.0f, 3.0f), 5.0f);
	EXPECT_EQ(sphere.Center, Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_FLOAT_EQ(sphere.Radius, 5.0f);
}

// --- Validation ---

TEST(Sphere, ValidWhenRadiusIsNonNegative)
{
	EXPECT_TRUE(Spheref(Vec3::Zero(), 0.0f).IsValid());
	EXPECT_TRUE(Spheref(Vec3::Zero(), 5.0f).IsValid());
}

TEST(Sphere, InvalidWhenRadiusIsNegative)
{
	EXPECT_FALSE(Spheref(Vec3::Zero(), -1.0f).IsValid());
}

// --- Contains Point ---

TEST(Sphere, ContainsPointAtCenter)
{
	Spheref sphere(Vec3(1.0f, 2.0f, 3.0f), 5.0f);
	EXPECT_TRUE(sphere.Contains(Vec3(1.0f, 2.0f, 3.0f)));
}

TEST(Sphere, ContainsPointOnSurface)
{
	Spheref sphere(Vec3(0.0f, 0.0f, 0.0f), 5.0f);
	EXPECT_TRUE(sphere.Contains(Vec3(5.0f, 0.0f, 0.0f)));
	EXPECT_TRUE(sphere.Contains(Vec3(0.0f, -5.0f, 0.0f)));
}

TEST(Sphere, ContainsPointInside)
{
	Spheref sphere(Vec3(0.0f, 0.0f, 0.0f), 5.0f);
	EXPECT_TRUE(sphere.Contains(Vec3(3.0f, 0.0f, 0.0f)));
}

TEST(Sphere, DoesNotContainPointOutside)
{
	Spheref sphere(Vec3(0.0f, 0.0f, 0.0f), 5.0f);
	EXPECT_FALSE(sphere.Contains(Vec3(5.1f, 0.0f, 0.0f)));
	EXPECT_FALSE(sphere.Contains(Vec3(3.0f, 4.0f, 1.0f)));
}

// --- Intersects Sphere ---

TEST(Sphere, IntersectsOverlappingSphere)
{
	Spheref a(Vec3(0.0f, 0.0f, 0.0f), 3.0f);
	Spheref b(Vec3(4.0f, 0.0f, 0.0f), 3.0f);
	EXPECT_TRUE(a.Intersects(b));
}

TEST(Sphere, IntersectsTouchingSphere)
{
	Spheref a(Vec3(0.0f, 0.0f, 0.0f), 3.0f);
	Spheref b(Vec3(6.0f, 0.0f, 0.0f), 3.0f);
	EXPECT_TRUE(a.Intersects(b));
}

TEST(Sphere, DoesNotIntersectSeparateSphere)
{
	Spheref a(Vec3(0.0f, 0.0f, 0.0f), 3.0f);
	Spheref b(Vec3(6.1f, 0.0f, 0.0f), 3.0f);
	EXPECT_FALSE(a.Intersects(b));
}

TEST(Sphere, IntersectsContainedSphere)
{
	Spheref a(Vec3(0.0f, 0.0f, 0.0f), 10.0f);
	Spheref b(Vec3(1.0f, 0.0f, 0.0f), 2.0f);
	EXPECT_TRUE(a.Intersects(b));
}

// --- Expansion ---

TEST(Sphere, ExpandToIncludePointOutside)
{
	Spheref sphere(Vec3(0.0f, 0.0f, 0.0f), 3.0f);
	sphere.ExpandToInclude(Vec3(5.0f, 0.0f, 0.0f));
	EXPECT_FLOAT_EQ(sphere.Radius, 5.0f);
}

TEST(Sphere, ExpandToIncludePointInsideDoesNotShrink)
{
	Spheref sphere(Vec3(0.0f, 0.0f, 0.0f), 5.0f);
	sphere.ExpandToInclude(Vec3(1.0f, 0.0f, 0.0f));
	EXPECT_FLOAT_EQ(sphere.Radius, 5.0f);
}

TEST(Sphere, ExpandToIncludeSphere)
{
	Spheref sphere(Vec3(0.0f, 0.0f, 0.0f), 3.0f);
	sphere.ExpandToInclude(Spheref(Vec3(5.0f, 0.0f, 0.0f), 2.0f));
	EXPECT_FLOAT_EQ(sphere.Radius, 7.0f);
}

// --- Comparison ---

TEST(Sphere, EqualityAndNearlyEquals)
{
	Spheref a(Vec3(1.0f, 2.0f, 3.0f), 5.0f);
	Spheref b(Vec3(1.0f, 2.0f, 3.0f), 5.0f);
	Spheref c(Vec3(1.0f, 2.0f, 3.0f), 5.0001f);

	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a == c);
	EXPECT_TRUE(a.NearlyEquals(c, 0.001f));
}

// --- Stream Output ---

TEST(Sphere, StreamOutput)
{
	Spheref sphere(Vec3(1.0f, 2.0f, 3.0f), 5.0f);
	std::ostringstream oss;
	oss << sphere;
	EXPECT_EQ(oss.str(), "{Center: (1, 2, 3), Radius: 5}");
}

// --- 2D Circle Alias ---

TEST(Circle, ContainsPoint)
{
	Circlef circle(Vec2(1.0f, 2.0f), 3.0f);

	EXPECT_TRUE(circle.Contains(Vec2(4.0f, 2.0f)));
	EXPECT_TRUE(circle.Contains(Vec2(2.0f, 4.0f)));
	EXPECT_FALSE(circle.Contains(Vec2(5.0f, 2.0f)));
}

TEST(Circle, IntersectsCircle)
{
	Circlef a(Vec2(0.0f, 0.0f), 2.0f);
	Circlef b(Vec2(3.0f, 0.0f), 2.0f);
	Circlef c(Vec2(5.0f, 0.0f), 2.0f);

	EXPECT_TRUE(a.Intersects(b));
	EXPECT_FALSE(a.Intersects(c));
}
