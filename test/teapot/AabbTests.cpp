#include <gtest/gtest.h>
#include <geometry/2d/Aabb2.h>
#include <geometry/3d/Aabb3.h>
#include <sstream>

// --- Construction ---

TEST(Aabb, DefaultConstructionIsDegenerateAtOrigin)
{
	Aabb3f box;
	EXPECT_EQ(box.Min, Vec3::Zero());
	EXPECT_EQ(box.Max, Vec3::Zero());
	EXPECT_TRUE(box.IsValid());
}

TEST(Aabb, ValueConstruction)
{
	Aabb3f box(Vec3(-1.0f, -2.0f, -3.0f), Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(box.Min, Vec3(-1.0f, -2.0f, -3.0f));
	EXPECT_EQ(box.Max, Vec3(1.0f, 2.0f, 3.0f));
}

// --- Validation ---

TEST(Aabb, ValidWhenMinIsLessThanOrEqualToMaxOnEveryAxis)
{
	Aabb3f box(Vec3(-1.0f, 0.0f, 2.0f), Vec3(1.0f, 0.0f, 5.0f));
	EXPECT_TRUE(box.IsValid());
}

TEST(Aabb, InvalidWhenMinExceedsMaxOnAnyAxis)
{
	Aabb3f box(Vec3(-1.0f, 3.0f, 2.0f), Vec3(1.0f, 2.0f, 5.0f));
	EXPECT_FALSE(box.IsValid());
}

TEST(Aabb, EmptyFactoryCreatesInvalidAccumulationBox)
{
	Aabb3f box = Aabb3f::Empty();
	EXPECT_FALSE(box.IsValid());

	box.ExpandToInclude(Vec3(2.0f, -1.0f, 4.0f));
	EXPECT_TRUE(box.IsValid());
	EXPECT_EQ(box.Min, Vec3(2.0f, -1.0f, 4.0f));
	EXPECT_EQ(box.Max, Vec3(2.0f, -1.0f, 4.0f));
}

// --- Geometry ---

TEST(Aabb, CenterSizeAndHalfExtent)
{
	Aabb3f box(Vec3(-2.0f, 1.0f, 4.0f), Vec3(6.0f, 5.0f, 10.0f));

	EXPECT_EQ(box.Center(), Vec3(2.0f, 3.0f, 7.0f));
	EXPECT_EQ(box.Size(), Vec3(8.0f, 4.0f, 6.0f));
	EXPECT_EQ(box.HalfExtent(), Vec3(4.0f, 2.0f, 3.0f));
	EXPECT_EQ(box.Extent(), box.HalfExtent());
}

// --- Queries ---

TEST(Aabb, ContainsPointInsideAndOnBoundary)
{
	Aabb3f box(Vec3(-1.0f, -2.0f, -3.0f), Vec3(1.0f, 2.0f, 3.0f));

	EXPECT_TRUE(box.Contains(Vec3(0.0f, 0.0f, 0.0f)));
	EXPECT_TRUE(box.Contains(Vec3(-1.0f, -2.0f, -3.0f)));
	EXPECT_TRUE(box.Contains(Vec3(1.0f, 2.0f, 3.0f)));
}

TEST(Aabb, DoesNotContainPointOutside)
{
	Aabb3f box(Vec3(-1.0f, -2.0f, -3.0f), Vec3(1.0f, 2.0f, 3.0f));

	EXPECT_FALSE(box.Contains(Vec3(1.1f, 0.0f, 0.0f)));
	EXPECT_FALSE(box.Contains(Vec3(0.0f, -2.1f, 0.0f)));
	EXPECT_FALSE(box.Contains(Vec3(0.0f, 0.0f, 3.1f)));
}

TEST(Aabb, IntersectsOverlappingAndTouchingBoxes)
{
	Aabb3f box(Vec3(0.0f, 0.0f, 0.0f), Vec3(2.0f, 2.0f, 2.0f));

	EXPECT_TRUE(box.Intersects(Aabb3f(Vec3(1.0f, 1.0f, 1.0f), Vec3(3.0f, 3.0f, 3.0f))));
	EXPECT_TRUE(box.Intersects(Aabb3f(Vec3(2.0f, 2.0f, 2.0f), Vec3(4.0f, 4.0f, 4.0f))));
}

TEST(Aabb, DoesNotIntersectSeparatedBoxes)
{
	Aabb3f box(Vec3(0.0f, 0.0f, 0.0f), Vec3(2.0f, 2.0f, 2.0f));

	EXPECT_FALSE(box.Intersects(Aabb3f(Vec3(2.1f, 0.0f, 0.0f), Vec3(4.0f, 2.0f, 2.0f))));
	EXPECT_FALSE(box.Intersects(Aabb3f(Vec3(0.0f, -4.0f, 0.0f), Vec3(2.0f, -0.1f, 2.0f))));
}

// --- Expansion ---

TEST(Aabb, ExpandsByPoint)
{
	Aabb3f box(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));

	box.ExpandToInclude(Vec3(-2.0f, 0.5f, 3.0f));

	EXPECT_EQ(box.Min, Vec3(-2.0f, 0.0f, 0.0f));
	EXPECT_EQ(box.Max, Vec3(1.0f, 1.0f, 3.0f));
}

TEST(Aabb, ExpandedToIncludePointReturnsExpandedCopy)
{
	Aabb3f box(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));

	Aabb3f expanded = box.ExpandedToInclude(Vec3(-2.0f, 0.5f, 3.0f));

	EXPECT_EQ(box.Min, Vec3(0.0f, 0.0f, 0.0f));
	EXPECT_EQ(box.Max, Vec3(1.0f, 1.0f, 1.0f));
	EXPECT_EQ(expanded.Min, Vec3(-2.0f, 0.0f, 0.0f));
	EXPECT_EQ(expanded.Max, Vec3(1.0f, 1.0f, 3.0f));
}

TEST(Aabb, ExpandsByAabb)
{
	Aabb3f box(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
	Aabb3f other(Vec3(-2.0f, 0.5f, -1.0f), Vec3(0.5f, 4.0f, 2.0f));

	box.ExpandToInclude(other);

	EXPECT_EQ(box.Min, Vec3(-2.0f, 0.0f, -1.0f));
	EXPECT_EQ(box.Max, Vec3(1.0f, 4.0f, 2.0f));
}

TEST(Aabb, ExpandedToIncludeAabbReturnsExpandedCopy)
{
	Aabb3f box(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
	Aabb3f other(Vec3(-2.0f, 0.5f, -1.0f), Vec3(0.5f, 4.0f, 2.0f));

	Aabb3f expanded = box.ExpandedToInclude(other);

	EXPECT_EQ(box.Min, Vec3(0.0f, 0.0f, 0.0f));
	EXPECT_EQ(box.Max, Vec3(1.0f, 1.0f, 1.0f));
	EXPECT_EQ(expanded.Min, Vec3(-2.0f, 0.0f, -1.0f));
	EXPECT_EQ(expanded.Max, Vec3(1.0f, 4.0f, 2.0f));
}

// --- Static Factories ---

TEST(Aabb, FromMinMaxFactory)
{
	Aabb3f box = Aabb3f::FromMinMax(Vec3(-1.0f, -2.0f, -3.0f), Vec3(1.0f, 2.0f, 3.0f));

	EXPECT_EQ(box.Min, Vec3(-1.0f, -2.0f, -3.0f));
	EXPECT_EQ(box.Max, Vec3(1.0f, 2.0f, 3.0f));
}

TEST(Aabb, FromCenterHalfExtentFactory)
{
	Aabb3f box = Aabb3f::FromCenterHalfExtent(Vec3(10.0f, 20.0f, 30.0f), Vec3(1.0f, 2.0f, 3.0f));

	EXPECT_EQ(box.Min, Vec3(9.0f, 18.0f, 27.0f));
	EXPECT_EQ(box.Max, Vec3(11.0f, 22.0f, 33.0f));
}

// --- Aliases ---

TEST(Aabb, FloatAliases)
{
	Aabb2f box2(Vec2(-1.0f, -2.0f), Vec2(1.0f, 2.0f));
	EXPECT_TRUE(box2.Contains(Vec2(0.5f, -1.5f)));

	Aabb3f box3(Vec3(0.0f, 0.0f, 0.0f), Vec3(10.0f, 20.0f, 30.0f));
	EXPECT_EQ(box3.Center(), Vec3(5.0f, 10.0f, 15.0f));
	EXPECT_TRUE(box3.Contains(Vec3(10.0f, 20.0f, 30.0f)));
}

// --- Runtime Validation ---

TEST(Aabb, RuntimeOperations)
{
	const Aabb3f box(Vec3(-2.0f, 1.0f, 4.0f), Vec3(6.0f, 5.0f, 10.0f));
	const Vec3 center = box.Center();
	const Vec3 size = box.Size();
	const bool contains = box.Contains(Vec3(0.0f, 3.0f, 7.0f));
	const Aabb3f expanded = box.ExpandedToInclude(Vec3(8.0f, 0.0f, 12.0f));

	EXPECT_EQ(center, Vec3(2.0f, 3.0f, 7.0f));
	EXPECT_EQ(size, Vec3(8.0f, 4.0f, 6.0f));
	EXPECT_TRUE(contains);
	EXPECT_EQ(expanded.Max, Vec3(8.0f, 5.0f, 12.0f));
}

// --- Stream Output ---

TEST(Aabb, StreamOutput)
{
	Aabb2f box(Vec2(-1.0f, -2.0f), Vec2(3.0f, 4.0f));
	std::ostringstream oss;
	oss << box;
	EXPECT_EQ(oss.str(), "{Min: (-1, -2), Max: (3, 4)}");
}
