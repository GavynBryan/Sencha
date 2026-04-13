#include <gtest/gtest.h>
#include <geometry/2d/Rect2d.h>
#include <sstream>

// --- Construction ---

TEST(Rect2d, DefaultConstructionAtOriginZeroSize)
{
	Rect2f rect;
	EXPECT_EQ(rect.Position, Vec2d::Zero());
	EXPECT_EQ(rect.Size, Vec2d::Zero());
}

TEST(Rect2d, VecConstruction)
{
	Rect2f rect(Vec2d(1.0f, 2.0f), Vec2d(10.0f, 20.0f));
	EXPECT_EQ(rect.Position, Vec2d(1.0f, 2.0f));
	EXPECT_EQ(rect.Size, Vec2d(10.0f, 20.0f));
}

TEST(Rect2d, ScalarConstruction)
{
	Rect2f rect(1.0f, 2.0f, 10.0f, 20.0f);
	EXPECT_EQ(rect.Position, Vec2d(1.0f, 2.0f));
	EXPECT_EQ(rect.Size, Vec2d(10.0f, 20.0f));
}

// --- Validation ---

TEST(Rect2d, ValidWhenSizeIsNonNegative)
{
	EXPECT_TRUE(Rect2f(Vec2d(0.0f, 0.0f), Vec2d(0.0f, 0.0f)).IsValid());
	EXPECT_TRUE(Rect2f(Vec2d(-5.0f, -3.0f), Vec2d(10.0f, 6.0f)).IsValid());
}

TEST(Rect2d, InvalidWhenAnySizeComponentIsNegative)
{
	EXPECT_FALSE(Rect2f(Vec2d(0.0f, 0.0f), Vec2d(-1.0f, 5.0f)).IsValid());
	EXPECT_FALSE(Rect2f(Vec2d(0.0f, 0.0f), Vec2d(5.0f, -1.0f)).IsValid());
}

// --- Geometry ---

TEST(Rect2d, MinMaxCenterSizeHelpers)
{
	Rect2f rect(Vec2d(2.0f, 3.0f), Vec2d(10.0f, 6.0f));

	EXPECT_EQ(rect.Min(), Vec2d(2.0f, 3.0f));
	EXPECT_EQ(rect.Max(), Vec2d(12.0f, 9.0f));
	EXPECT_EQ(rect.Center(), Vec2d(7.0f, 6.0f));
	EXPECT_FLOAT_EQ(rect.Width(), 10.0f);
	EXPECT_FLOAT_EQ(rect.Height(), 6.0f);
	EXPECT_FLOAT_EQ(rect.Area(), 60.0f);
}

// --- Contains Point ---

TEST(Rect2d, ContainsPointInside)
{
	Rect2f rect(Vec2d(0.0f, 0.0f), Vec2d(10.0f, 10.0f));
	EXPECT_TRUE(rect.Contains(Vec2d(5.0f, 5.0f)));
}

TEST(Rect2d, ContainsPointOnBoundary)
{
	Rect2f rect(Vec2d(0.0f, 0.0f), Vec2d(10.0f, 10.0f));
	EXPECT_TRUE(rect.Contains(Vec2d(0.0f, 0.0f)));
	EXPECT_TRUE(rect.Contains(Vec2d(10.0f, 10.0f)));
	EXPECT_TRUE(rect.Contains(Vec2d(10.0f, 0.0f)));
}

TEST(Rect2d, DoesNotContainPointOutside)
{
	Rect2f rect(Vec2d(0.0f, 0.0f), Vec2d(10.0f, 10.0f));
	EXPECT_FALSE(rect.Contains(Vec2d(-0.1f, 5.0f)));
	EXPECT_FALSE(rect.Contains(Vec2d(5.0f, 10.1f)));
}

// --- Intersects Rect ---

TEST(Rect2d, IntersectsOverlappingRect)
{
	Rect2f a(Vec2d(0.0f, 0.0f), Vec2d(10.0f, 10.0f));
	Rect2f b(Vec2d(5.0f, 5.0f), Vec2d(10.0f, 10.0f));
	EXPECT_TRUE(a.Intersects(b));
	EXPECT_TRUE(b.Intersects(a));
}

TEST(Rect2d, IntersectsTouchingRect)
{
	Rect2f a(Vec2d(0.0f, 0.0f), Vec2d(10.0f, 10.0f));
	Rect2f b(Vec2d(10.0f, 0.0f), Vec2d(10.0f, 10.0f));
	EXPECT_TRUE(a.Intersects(b));
}

TEST(Rect2d, DoesNotIntersectSeparateRect)
{
	Rect2f a(Vec2d(0.0f, 0.0f), Vec2d(10.0f, 10.0f));
	Rect2f b(Vec2d(10.1f, 0.0f), Vec2d(10.0f, 10.0f));
	EXPECT_FALSE(a.Intersects(b));
}

// --- Factories ---

TEST(Rect2d, FromMinMax)
{
	Rect2f rect = Rect2f::FromMinMax(Vec2d(2.0f, 3.0f), Vec2d(12.0f, 9.0f));
	EXPECT_EQ(rect.Position, Vec2d(2.0f, 3.0f));
	EXPECT_EQ(rect.Size, Vec2d(10.0f, 6.0f));
}

TEST(Rect2d, FromCenterSize)
{
	Rect2f rect = Rect2f::FromCenterSize(Vec2d(7.0f, 6.0f), Vec2d(10.0f, 6.0f));
	EXPECT_EQ(rect.Position, Vec2d(2.0f, 3.0f));
	EXPECT_EQ(rect.Size, Vec2d(10.0f, 6.0f));
}

// --- Float Alias ---

TEST(Rect2d, FloatAlias)
{
	Rect2f rect(Vec2d(0.0f, 0.0f), Vec2d(100.0f, 50.0f));
	EXPECT_TRUE(rect.Contains(Vec2d(50.0f, 25.0f)));
	EXPECT_FLOAT_EQ(rect.Area(), 5000.0f);
}

// --- Runtime ---

TEST(Rect2d, RuntimeOperations)
{
	const Rect2f rect(Vec2d(1.0f, 2.0f), Vec2d(10.0f, 20.0f));
	const Vec2d center = rect.Center();
	const bool valid = rect.IsValid();
	const bool contains = rect.Contains(Vec2d(5.0f, 10.0f));

	EXPECT_EQ(center, Vec2d(6.0f, 12.0f));
	EXPECT_TRUE(valid);
	EXPECT_TRUE(contains);
}

// --- Comparison ---

TEST(Rect2d, Equality)
{
	Rect2f a(Vec2d(1.0f, 2.0f), Vec2d(3.0f, 4.0f));
	Rect2f b(Vec2d(1.0f, 2.0f), Vec2d(3.0f, 4.0f));
	Rect2f c(Vec2d(1.0f, 2.0f), Vec2d(3.0f, 5.0f));

	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a == c);
}

// --- Stream Output ---

TEST(Rect2d, StreamOutput)
{
	Rect2f rect(Vec2d(1.0f, 2.0f), Vec2d(10.0f, 20.0f));
	std::ostringstream oss;
	oss << rect;
	EXPECT_EQ(oss.str(), "{Position: (1, 2), Size: (10, 20)}");
}
