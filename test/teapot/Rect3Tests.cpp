#include <gtest/gtest.h>
#include <geometry/Rect3.h>
#include <sstream>

// --- Construction ---

TEST(Rect3, DefaultConstructionAtOriginZeroSize)
{
	Rect3f rect;
	EXPECT_EQ(rect.Position, Vec3::Zero());
	EXPECT_EQ(rect.Size, Vec3::Zero());
}

TEST(Rect3, VecConstruction)
{
	Rect3f rect(Vec3(1.0f, 2.0f, 3.0f), Vec3(10.0f, 20.0f, 30.0f));
	EXPECT_EQ(rect.Position, Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(rect.Size, Vec3(10.0f, 20.0f, 30.0f));
}

TEST(Rect3, ScalarConstruction)
{
	Rect3f rect(1.0f, 2.0f, 3.0f, 10.0f, 20.0f, 30.0f);
	EXPECT_EQ(rect.Position, Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(rect.Size, Vec3(10.0f, 20.0f, 30.0f));
}

// --- Validation ---

TEST(Rect3, ValidWhenSizeIsNonNegative)
{
	EXPECT_TRUE(Rect3f(Vec3::Zero(), Vec3::Zero()).IsValid());
	EXPECT_TRUE(Rect3f(Vec3(-5.0f, -3.0f, -1.0f), Vec3(10.0f, 6.0f, 2.0f)).IsValid());
}

TEST(Rect3, InvalidWhenAnySizeComponentIsNegative)
{
	EXPECT_FALSE(Rect3f(Vec3::Zero(), Vec3(-1.0f, 5.0f, 5.0f)).IsValid());
	EXPECT_FALSE(Rect3f(Vec3::Zero(), Vec3(5.0f, -1.0f, 5.0f)).IsValid());
	EXPECT_FALSE(Rect3f(Vec3::Zero(), Vec3(5.0f, 5.0f, -1.0f)).IsValid());
}

// --- Geometry ---

TEST(Rect3, MinMaxCenterSizeHelpers)
{
	Rect3f rect(Vec3(2.0f, 3.0f, 4.0f), Vec3(10.0f, 6.0f, 8.0f));

	EXPECT_EQ(rect.Min(), Vec3(2.0f, 3.0f, 4.0f));
	EXPECT_EQ(rect.Max(), Vec3(12.0f, 9.0f, 12.0f));
	EXPECT_EQ(rect.Center(), Vec3(7.0f, 6.0f, 8.0f));
	EXPECT_FLOAT_EQ(rect.Width(), 10.0f);
	EXPECT_FLOAT_EQ(rect.Height(), 6.0f);
	EXPECT_FLOAT_EQ(rect.Depth(), 8.0f);
	EXPECT_FLOAT_EQ(rect.Volume(), 480.0f);
}

// --- Contains Point ---

TEST(Rect3, ContainsPointInside)
{
	Rect3f rect(Vec3(0.0f, 0.0f, 0.0f), Vec3(10.0f, 10.0f, 10.0f));
	EXPECT_TRUE(rect.Contains(Vec3(5.0f, 5.0f, 5.0f)));
}

TEST(Rect3, ContainsPointOnBoundary)
{
	Rect3f rect(Vec3(0.0f, 0.0f, 0.0f), Vec3(10.0f, 10.0f, 10.0f));
	EXPECT_TRUE(rect.Contains(Vec3(0.0f, 0.0f, 0.0f)));
	EXPECT_TRUE(rect.Contains(Vec3(10.0f, 10.0f, 10.0f)));
}

TEST(Rect3, DoesNotContainPointOutside)
{
	Rect3f rect(Vec3(0.0f, 0.0f, 0.0f), Vec3(10.0f, 10.0f, 10.0f));
	EXPECT_FALSE(rect.Contains(Vec3(-0.1f, 5.0f, 5.0f)));
	EXPECT_FALSE(rect.Contains(Vec3(5.0f, 10.1f, 5.0f)));
	EXPECT_FALSE(rect.Contains(Vec3(5.0f, 5.0f, -0.1f)));
}

// --- Intersects Rect ---

TEST(Rect3, IntersectsOverlappingRect)
{
	Rect3f a(Vec3(0.0f, 0.0f, 0.0f), Vec3(10.0f, 10.0f, 10.0f));
	Rect3f b(Vec3(5.0f, 5.0f, 5.0f), Vec3(10.0f, 10.0f, 10.0f));
	EXPECT_TRUE(a.Intersects(b));
	EXPECT_TRUE(b.Intersects(a));
}

TEST(Rect3, IntersectsTouchingRect)
{
	Rect3f a(Vec3(0.0f, 0.0f, 0.0f), Vec3(10.0f, 10.0f, 10.0f));
	Rect3f b(Vec3(10.0f, 0.0f, 0.0f), Vec3(10.0f, 10.0f, 10.0f));
	EXPECT_TRUE(a.Intersects(b));
}

TEST(Rect3, DoesNotIntersectSeparateRect)
{
	Rect3f a(Vec3(0.0f, 0.0f, 0.0f), Vec3(10.0f, 10.0f, 10.0f));
	Rect3f b(Vec3(10.1f, 0.0f, 0.0f), Vec3(10.0f, 10.0f, 10.0f));
	EXPECT_FALSE(a.Intersects(b));
}

// --- Factories ---

TEST(Rect3, FromMinMax)
{
	Rect3f rect = Rect3f::FromMinMax(Vec3(2.0f, 3.0f, 4.0f), Vec3(12.0f, 9.0f, 12.0f));
	EXPECT_EQ(rect.Position, Vec3(2.0f, 3.0f, 4.0f));
	EXPECT_EQ(rect.Size, Vec3(10.0f, 6.0f, 8.0f));
}

TEST(Rect3, FromCenterSize)
{
	Rect3f rect = Rect3f::FromCenterSize(Vec3(7.0f, 6.0f, 8.0f), Vec3(10.0f, 6.0f, 8.0f));
	EXPECT_EQ(rect.Position, Vec3(2.0f, 3.0f, 4.0f));
	EXPECT_EQ(rect.Size, Vec3(10.0f, 6.0f, 8.0f));
}

// --- Integer Alias ---

TEST(Rect3, IntegerAlias)
{
	Rect3i rect(Vec3i(0, 0, 0), Vec3i(10, 20, 30));
	EXPECT_TRUE(rect.Contains(Vec3i(5, 10, 15)));
	EXPECT_EQ(rect.Volume(), 6000);
}

// --- Constexpr ---

TEST(Rect3, ConstexprOperations)
{
	constexpr Rect3f rect(Vec3(1.0f, 2.0f, 3.0f), Vec3(10.0f, 20.0f, 30.0f));
	constexpr Vec3 center = rect.Center();
	constexpr bool valid = rect.IsValid();
	constexpr bool contains = rect.Contains(Vec3(5.0f, 10.0f, 15.0f));

	EXPECT_EQ(center, Vec3(6.0f, 12.0f, 18.0f));
	EXPECT_TRUE(valid);
	EXPECT_TRUE(contains);
}

// --- Comparison ---

TEST(Rect3, Equality)
{
	Rect3f a(Vec3(1.0f, 2.0f, 3.0f), Vec3(4.0f, 5.0f, 6.0f));
	Rect3f b(Vec3(1.0f, 2.0f, 3.0f), Vec3(4.0f, 5.0f, 6.0f));
	Rect3f c(Vec3(1.0f, 2.0f, 3.0f), Vec3(4.0f, 5.0f, 7.0f));

	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a == c);
}

// --- Stream Output ---

TEST(Rect3, StreamOutput)
{
	Rect3i rect(Vec3i(1, 2, 3), Vec3i(10, 20, 30));
	std::ostringstream oss;
	oss << rect;
	EXPECT_EQ(oss.str(), "{Position: (1, 2, 3), Size: (10, 20, 30)}");
}
