#include <gtest/gtest.h>
#include <geometry/2d/Rect2.h>
#include <sstream>

// --- Construction ---

TEST(Rect2, DefaultConstructionAtOriginZeroSize)
{
	Rect2f rect;
	EXPECT_EQ(rect.Position, Vec2::Zero());
	EXPECT_EQ(rect.Size, Vec2::Zero());
}

TEST(Rect2, VecConstruction)
{
	Rect2f rect(Vec2(1.0f, 2.0f), Vec2(10.0f, 20.0f));
	EXPECT_EQ(rect.Position, Vec2(1.0f, 2.0f));
	EXPECT_EQ(rect.Size, Vec2(10.0f, 20.0f));
}

TEST(Rect2, ScalarConstruction)
{
	Rect2f rect(1.0f, 2.0f, 10.0f, 20.0f);
	EXPECT_EQ(rect.Position, Vec2(1.0f, 2.0f));
	EXPECT_EQ(rect.Size, Vec2(10.0f, 20.0f));
}

// --- Validation ---

TEST(Rect2, ValidWhenSizeIsNonNegative)
{
	EXPECT_TRUE(Rect2f(Vec2(0.0f, 0.0f), Vec2(0.0f, 0.0f)).IsValid());
	EXPECT_TRUE(Rect2f(Vec2(-5.0f, -3.0f), Vec2(10.0f, 6.0f)).IsValid());
}

TEST(Rect2, InvalidWhenAnySizeComponentIsNegative)
{
	EXPECT_FALSE(Rect2f(Vec2(0.0f, 0.0f), Vec2(-1.0f, 5.0f)).IsValid());
	EXPECT_FALSE(Rect2f(Vec2(0.0f, 0.0f), Vec2(5.0f, -1.0f)).IsValid());
}

// --- Geometry ---

TEST(Rect2, MinMaxCenterSizeHelpers)
{
	Rect2f rect(Vec2(2.0f, 3.0f), Vec2(10.0f, 6.0f));

	EXPECT_EQ(rect.Min(), Vec2(2.0f, 3.0f));
	EXPECT_EQ(rect.Max(), Vec2(12.0f, 9.0f));
	EXPECT_EQ(rect.Center(), Vec2(7.0f, 6.0f));
	EXPECT_FLOAT_EQ(rect.Width(), 10.0f);
	EXPECT_FLOAT_EQ(rect.Height(), 6.0f);
	EXPECT_FLOAT_EQ(rect.Area(), 60.0f);
}

// --- Contains Point ---

TEST(Rect2, ContainsPointInside)
{
	Rect2f rect(Vec2(0.0f, 0.0f), Vec2(10.0f, 10.0f));
	EXPECT_TRUE(rect.Contains(Vec2(5.0f, 5.0f)));
}

TEST(Rect2, ContainsPointOnBoundary)
{
	Rect2f rect(Vec2(0.0f, 0.0f), Vec2(10.0f, 10.0f));
	EXPECT_TRUE(rect.Contains(Vec2(0.0f, 0.0f)));
	EXPECT_TRUE(rect.Contains(Vec2(10.0f, 10.0f)));
	EXPECT_TRUE(rect.Contains(Vec2(10.0f, 0.0f)));
}

TEST(Rect2, DoesNotContainPointOutside)
{
	Rect2f rect(Vec2(0.0f, 0.0f), Vec2(10.0f, 10.0f));
	EXPECT_FALSE(rect.Contains(Vec2(-0.1f, 5.0f)));
	EXPECT_FALSE(rect.Contains(Vec2(5.0f, 10.1f)));
}

// --- Intersects Rect ---

TEST(Rect2, IntersectsOverlappingRect)
{
	Rect2f a(Vec2(0.0f, 0.0f), Vec2(10.0f, 10.0f));
	Rect2f b(Vec2(5.0f, 5.0f), Vec2(10.0f, 10.0f));
	EXPECT_TRUE(a.Intersects(b));
	EXPECT_TRUE(b.Intersects(a));
}

TEST(Rect2, IntersectsTouchingRect)
{
	Rect2f a(Vec2(0.0f, 0.0f), Vec2(10.0f, 10.0f));
	Rect2f b(Vec2(10.0f, 0.0f), Vec2(10.0f, 10.0f));
	EXPECT_TRUE(a.Intersects(b));
}

TEST(Rect2, DoesNotIntersectSeparateRect)
{
	Rect2f a(Vec2(0.0f, 0.0f), Vec2(10.0f, 10.0f));
	Rect2f b(Vec2(10.1f, 0.0f), Vec2(10.0f, 10.0f));
	EXPECT_FALSE(a.Intersects(b));
}

// --- Factories ---

TEST(Rect2, FromMinMax)
{
	Rect2f rect = Rect2f::FromMinMax(Vec2(2.0f, 3.0f), Vec2(12.0f, 9.0f));
	EXPECT_EQ(rect.Position, Vec2(2.0f, 3.0f));
	EXPECT_EQ(rect.Size, Vec2(10.0f, 6.0f));
}

TEST(Rect2, FromCenterSize)
{
	Rect2f rect = Rect2f::FromCenterSize(Vec2(7.0f, 6.0f), Vec2(10.0f, 6.0f));
	EXPECT_EQ(rect.Position, Vec2(2.0f, 3.0f));
	EXPECT_EQ(rect.Size, Vec2(10.0f, 6.0f));
}

// --- Float Alias ---

TEST(Rect2, FloatAlias)
{
	Rect2f rect(Vec2(0.0f, 0.0f), Vec2(100.0f, 50.0f));
	EXPECT_TRUE(rect.Contains(Vec2(50.0f, 25.0f)));
	EXPECT_FLOAT_EQ(rect.Area(), 5000.0f);
}

// --- Runtime ---

TEST(Rect2, RuntimeOperations)
{
	const Rect2f rect(Vec2(1.0f, 2.0f), Vec2(10.0f, 20.0f));
	const Vec2 center = rect.Center();
	const bool valid = rect.IsValid();
	const bool contains = rect.Contains(Vec2(5.0f, 10.0f));

	EXPECT_EQ(center, Vec2(6.0f, 12.0f));
	EXPECT_TRUE(valid);
	EXPECT_TRUE(contains);
}

// --- Comparison ---

TEST(Rect2, Equality)
{
	Rect2f a(Vec2(1.0f, 2.0f), Vec2(3.0f, 4.0f));
	Rect2f b(Vec2(1.0f, 2.0f), Vec2(3.0f, 4.0f));
	Rect2f c(Vec2(1.0f, 2.0f), Vec2(3.0f, 5.0f));

	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a == c);
}

// --- Stream Output ---

TEST(Rect2, StreamOutput)
{
	Rect2f rect(Vec2(1.0f, 2.0f), Vec2(10.0f, 20.0f));
	std::ostringstream oss;
	oss << rect;
	EXPECT_EQ(oss.str(), "{Position: (1, 2), Size: (10, 20)}");
}
