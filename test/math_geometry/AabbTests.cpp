#include <gtest/gtest.h>
#include <geometry/2d/Aabb2d.h>
#include <geometry/3d/Aabb3d.h>
#include <sstream>

// --- Construction ---

TEST(Aabb, DefaultConstructionIsDegenerateAtOrigin)
{
	Aabb3f box;
	EXPECT_EQ(box.Min, Vec3d::Zero());
	EXPECT_EQ(box.Max, Vec3d::Zero());
	EXPECT_TRUE(box.IsValid());
}

TEST(Aabb, ValueConstruction)
{
	Aabb3f box(Vec3d(-1.0f, -2.0f, -3.0f), Vec3d(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(box.Min, Vec3d(-1.0f, -2.0f, -3.0f));
	EXPECT_EQ(box.Max, Vec3d(1.0f, 2.0f, 3.0f));
}

// --- Validation ---

TEST(Aabb, ValidWhenMinIsLessThanOrEqualToMaxOnEveryAxis)
{
	Aabb3f box(Vec3d(-1.0f, 0.0f, 2.0f), Vec3d(1.0f, 0.0f, 5.0f));
	EXPECT_TRUE(box.IsValid());
}

TEST(Aabb, InvalidWhenMinExceedsMaxOnAnyAxis)
{
	Aabb3f box(Vec3d(-1.0f, 3.0f, 2.0f), Vec3d(1.0f, 2.0f, 5.0f));
	EXPECT_FALSE(box.IsValid());
}

TEST(Aabb, EmptyFactoryCreatesInvalidAccumulationBox)
{
	Aabb3f box = Aabb3f::Empty();
	EXPECT_FALSE(box.IsValid());

	box.ExpandToInclude(Vec3d(2.0f, -1.0f, 4.0f));
	EXPECT_TRUE(box.IsValid());
	EXPECT_EQ(box.Min, Vec3d(2.0f, -1.0f, 4.0f));
	EXPECT_EQ(box.Max, Vec3d(2.0f, -1.0f, 4.0f));
}

// --- Geometry ---

TEST(Aabb, CenterSizeAndHalfExtent)
{
	Aabb3f box(Vec3d(-2.0f, 1.0f, 4.0f), Vec3d(6.0f, 5.0f, 10.0f));

	EXPECT_EQ(box.Center(), Vec3d(2.0f, 3.0f, 7.0f));
	EXPECT_EQ(box.Size(), Vec3d(8.0f, 4.0f, 6.0f));
	EXPECT_EQ(box.HalfExtent(), Vec3d(4.0f, 2.0f, 3.0f));
	EXPECT_EQ(box.Extent(), box.HalfExtent());
}

// --- Queries ---

TEST(Aabb, ContainsPointInsideAndOnBoundary)
{
	Aabb3f box(Vec3d(-1.0f, -2.0f, -3.0f), Vec3d(1.0f, 2.0f, 3.0f));

	EXPECT_TRUE(box.Contains(Vec3d(0.0f, 0.0f, 0.0f)));
	EXPECT_TRUE(box.Contains(Vec3d(-1.0f, -2.0f, -3.0f)));
	EXPECT_TRUE(box.Contains(Vec3d(1.0f, 2.0f, 3.0f)));
}

TEST(Aabb, DoesNotContainPointOutside)
{
	Aabb3f box(Vec3d(-1.0f, -2.0f, -3.0f), Vec3d(1.0f, 2.0f, 3.0f));

	EXPECT_FALSE(box.Contains(Vec3d(1.1f, 0.0f, 0.0f)));
	EXPECT_FALSE(box.Contains(Vec3d(0.0f, -2.1f, 0.0f)));
	EXPECT_FALSE(box.Contains(Vec3d(0.0f, 0.0f, 3.1f)));
}

TEST(Aabb, IntersectsOverlappingAndTouchingBoxes)
{
	Aabb3f box(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(2.0f, 2.0f, 2.0f));

	EXPECT_TRUE(box.Intersects(Aabb3f(Vec3d(1.0f, 1.0f, 1.0f), Vec3d(3.0f, 3.0f, 3.0f))));
	EXPECT_TRUE(box.Intersects(Aabb3f(Vec3d(2.0f, 2.0f, 2.0f), Vec3d(4.0f, 4.0f, 4.0f))));
}

TEST(Aabb, DoesNotIntersectSeparatedBoxes)
{
	Aabb3f box(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(2.0f, 2.0f, 2.0f));

	EXPECT_FALSE(box.Intersects(Aabb3f(Vec3d(2.1f, 0.0f, 0.0f), Vec3d(4.0f, 2.0f, 2.0f))));
	EXPECT_FALSE(box.Intersects(Aabb3f(Vec3d(0.0f, -4.0f, 0.0f), Vec3d(2.0f, -0.1f, 2.0f))));
}

// --- Expansion ---

TEST(Aabb, ExpandsByPoint)
{
	Aabb3f box(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(1.0f, 1.0f, 1.0f));

	box.ExpandToInclude(Vec3d(-2.0f, 0.5f, 3.0f));

	EXPECT_EQ(box.Min, Vec3d(-2.0f, 0.0f, 0.0f));
	EXPECT_EQ(box.Max, Vec3d(1.0f, 1.0f, 3.0f));
}

TEST(Aabb, ExpandedToIncludePointReturnsExpandedCopy)
{
	Aabb3f box(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(1.0f, 1.0f, 1.0f));

	Aabb3f expanded = box.ExpandedToInclude(Vec3d(-2.0f, 0.5f, 3.0f));

	EXPECT_EQ(box.Min, Vec3d(0.0f, 0.0f, 0.0f));
	EXPECT_EQ(box.Max, Vec3d(1.0f, 1.0f, 1.0f));
	EXPECT_EQ(expanded.Min, Vec3d(-2.0f, 0.0f, 0.0f));
	EXPECT_EQ(expanded.Max, Vec3d(1.0f, 1.0f, 3.0f));
}

TEST(Aabb, ExpandsByAabb)
{
	Aabb3f box(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(1.0f, 1.0f, 1.0f));
	Aabb3f other(Vec3d(-2.0f, 0.5f, -1.0f), Vec3d(0.5f, 4.0f, 2.0f));

	box.ExpandToInclude(other);

	EXPECT_EQ(box.Min, Vec3d(-2.0f, 0.0f, -1.0f));
	EXPECT_EQ(box.Max, Vec3d(1.0f, 4.0f, 2.0f));
}

TEST(Aabb, ExpandedToIncludeAabbReturnsExpandedCopy)
{
	Aabb3f box(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(1.0f, 1.0f, 1.0f));
	Aabb3f other(Vec3d(-2.0f, 0.5f, -1.0f), Vec3d(0.5f, 4.0f, 2.0f));

	Aabb3f expanded = box.ExpandedToInclude(other);

	EXPECT_EQ(box.Min, Vec3d(0.0f, 0.0f, 0.0f));
	EXPECT_EQ(box.Max, Vec3d(1.0f, 1.0f, 1.0f));
	EXPECT_EQ(expanded.Min, Vec3d(-2.0f, 0.0f, -1.0f));
	EXPECT_EQ(expanded.Max, Vec3d(1.0f, 4.0f, 2.0f));
}

// --- Static Factories ---

TEST(Aabb, FromMinMaxFactory)
{
	Aabb3f box = Aabb3f::FromMinMax(Vec3d(-1.0f, -2.0f, -3.0f), Vec3d(1.0f, 2.0f, 3.0f));

	EXPECT_EQ(box.Min, Vec3d(-1.0f, -2.0f, -3.0f));
	EXPECT_EQ(box.Max, Vec3d(1.0f, 2.0f, 3.0f));
}

TEST(Aabb, FromCenterHalfExtentFactory)
{
	Aabb3f box = Aabb3f::FromCenterHalfExtent(Vec3d(10.0f, 20.0f, 30.0f), Vec3d(1.0f, 2.0f, 3.0f));

	EXPECT_EQ(box.Min, Vec3d(9.0f, 18.0f, 27.0f));
	EXPECT_EQ(box.Max, Vec3d(11.0f, 22.0f, 33.0f));
}

// --- Aliases ---

TEST(Aabb, FloatAliases)
{
	Aabb2f box2(Vec2d(-1.0f, -2.0f), Vec2d(1.0f, 2.0f));
	EXPECT_TRUE(box2.Contains(Vec2d(0.5f, -1.5f)));

	Aabb3f box3(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(10.0f, 20.0f, 30.0f));
	EXPECT_EQ(box3.Center(), Vec3d(5.0f, 10.0f, 15.0f));
	EXPECT_TRUE(box3.Contains(Vec3d(10.0f, 20.0f, 30.0f)));
}

// --- Runtime Validation ---

TEST(Aabb, RuntimeOperations)
{
	const Aabb3f box(Vec3d(-2.0f, 1.0f, 4.0f), Vec3d(6.0f, 5.0f, 10.0f));
	const Vec3d center = box.Center();
	const Vec3d size = box.Size();
	const bool contains = box.Contains(Vec3d(0.0f, 3.0f, 7.0f));
	const Aabb3f expanded = box.ExpandedToInclude(Vec3d(8.0f, 0.0f, 12.0f));

	EXPECT_EQ(center, Vec3d(2.0f, 3.0f, 7.0f));
	EXPECT_EQ(size, Vec3d(8.0f, 4.0f, 6.0f));
	EXPECT_TRUE(contains);
	EXPECT_EQ(expanded.Max, Vec3d(8.0f, 5.0f, 12.0f));
}

// --- Stream Output ---

TEST(Aabb, StreamOutput)
{
	Aabb2f box(Vec2d(-1.0f, -2.0f), Vec2d(3.0f, 4.0f));
	std::ostringstream oss;
	oss << box;
	EXPECT_EQ(oss.str(), "{Min: (-1, -2), Max: (3, 4)}");
}
