#include <gtest/gtest.h>
#include <math/geometry/3d/Rect3d.h>
#include <sstream>

// --- Construction ---

TEST(Rect3d, DefaultConstructionAtOriginZeroSize)
{
	Rect3f rect;
	EXPECT_EQ(rect.Position, Vec3d::Zero());
	EXPECT_EQ(rect.Size, Vec3d::Zero());
}

TEST(Rect3d, VecConstruction)
{
	Rect3f rect(Vec3d(1.0f, 2.0f, 3.0f), Vec3d(10.0f, 20.0f, 30.0f));
	EXPECT_EQ(rect.Position, Vec3d(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(rect.Size, Vec3d(10.0f, 20.0f, 30.0f));
}

TEST(Rect3d, ScalarConstruction)
{
	Rect3f rect(1.0f, 2.0f, 3.0f, 10.0f, 20.0f, 30.0f);
	EXPECT_EQ(rect.Position, Vec3d(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(rect.Size, Vec3d(10.0f, 20.0f, 30.0f));
}

// --- Validation ---

TEST(Rect3d, ValidWhenSizeIsNonNegative)
{
	EXPECT_TRUE(Rect3f(Vec3d::Zero(), Vec3d::Zero()).IsValid());
	EXPECT_TRUE(Rect3f(Vec3d(-5.0f, -3.0f, -1.0f), Vec3d(10.0f, 6.0f, 2.0f)).IsValid());
}

TEST(Rect3d, InvalidWhenAnySizeComponentIsNegative)
{
	EXPECT_FALSE(Rect3f(Vec3d::Zero(), Vec3d(-1.0f, 5.0f, 5.0f)).IsValid());
	EXPECT_FALSE(Rect3f(Vec3d::Zero(), Vec3d(5.0f, -1.0f, 5.0f)).IsValid());
	EXPECT_FALSE(Rect3f(Vec3d::Zero(), Vec3d(5.0f, 5.0f, -1.0f)).IsValid());
}

// --- Geometry ---

TEST(Rect3d, MinMaxCenterSizeHelpers)
{
	Rect3f rect(Vec3d(2.0f, 3.0f, 4.0f), Vec3d(10.0f, 6.0f, 8.0f));

	EXPECT_EQ(rect.Min(), Vec3d(2.0f, 3.0f, 4.0f));
	EXPECT_EQ(rect.Max(), Vec3d(12.0f, 9.0f, 12.0f));
	EXPECT_EQ(rect.Center(), Vec3d(7.0f, 6.0f, 8.0f));
	EXPECT_FLOAT_EQ(rect.Width(), 10.0f);
	EXPECT_FLOAT_EQ(rect.Height(), 6.0f);
	EXPECT_FLOAT_EQ(rect.Depth(), 8.0f);
	EXPECT_FLOAT_EQ(rect.Volume(), 480.0f);
}

// --- Contains Point ---

TEST(Rect3d, ContainsPointInside)
{
	Rect3f rect(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(10.0f, 10.0f, 10.0f));
	EXPECT_TRUE(rect.Contains(Vec3d(5.0f, 5.0f, 5.0f)));
}

TEST(Rect3d, ContainsPointOnBoundary)
{
	Rect3f rect(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(10.0f, 10.0f, 10.0f));
	EXPECT_TRUE(rect.Contains(Vec3d(0.0f, 0.0f, 0.0f)));
	EXPECT_TRUE(rect.Contains(Vec3d(10.0f, 10.0f, 10.0f)));
}

TEST(Rect3d, DoesNotContainPointOutside)
{
	Rect3f rect(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(10.0f, 10.0f, 10.0f));
	EXPECT_FALSE(rect.Contains(Vec3d(-0.1f, 5.0f, 5.0f)));
	EXPECT_FALSE(rect.Contains(Vec3d(5.0f, 10.1f, 5.0f)));
	EXPECT_FALSE(rect.Contains(Vec3d(5.0f, 5.0f, -0.1f)));
}

// --- Intersects Rect ---

TEST(Rect3d, IntersectsOverlappingRect)
{
	Rect3f a(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(10.0f, 10.0f, 10.0f));
	Rect3f b(Vec3d(5.0f, 5.0f, 5.0f), Vec3d(10.0f, 10.0f, 10.0f));
	EXPECT_TRUE(a.Intersects(b));
	EXPECT_TRUE(b.Intersects(a));
}

TEST(Rect3d, IntersectsTouchingRect)
{
	Rect3f a(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(10.0f, 10.0f, 10.0f));
	Rect3f b(Vec3d(10.0f, 0.0f, 0.0f), Vec3d(10.0f, 10.0f, 10.0f));
	EXPECT_TRUE(a.Intersects(b));
}

TEST(Rect3d, DoesNotIntersectSeparateRect)
{
	Rect3f a(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(10.0f, 10.0f, 10.0f));
	Rect3f b(Vec3d(10.1f, 0.0f, 0.0f), Vec3d(10.0f, 10.0f, 10.0f));
	EXPECT_FALSE(a.Intersects(b));
}

// --- Factories ---

TEST(Rect3d, FromMinMax)
{
	Rect3f rect = Rect3f::FromMinMax(Vec3d(2.0f, 3.0f, 4.0f), Vec3d(12.0f, 9.0f, 12.0f));
	EXPECT_EQ(rect.Position, Vec3d(2.0f, 3.0f, 4.0f));
	EXPECT_EQ(rect.Size, Vec3d(10.0f, 6.0f, 8.0f));
}

TEST(Rect3d, FromCenterSize)
{
	Rect3f rect = Rect3f::FromCenterSize(Vec3d(7.0f, 6.0f, 8.0f), Vec3d(10.0f, 6.0f, 8.0f));
	EXPECT_EQ(rect.Position, Vec3d(2.0f, 3.0f, 4.0f));
	EXPECT_EQ(rect.Size, Vec3d(10.0f, 6.0f, 8.0f));
}

// --- Float Alias ---

TEST(Rect3d, FloatAlias)
{
	Rect3f rect(Vec3d(0.0f, 0.0f, 0.0f), Vec3d(10.0f, 20.0f, 30.0f));
	EXPECT_TRUE(rect.Contains(Vec3d(5.0f, 10.0f, 15.0f)));
	EXPECT_FLOAT_EQ(rect.Volume(), 6000.0f);
}

// --- Runtime ---

TEST(Rect3d, RuntimeOperations)
{
	const Rect3f rect(Vec3d(1.0f, 2.0f, 3.0f), Vec3d(10.0f, 20.0f, 30.0f));
	const Vec3d center = rect.Center();
	const bool valid = rect.IsValid();
	const bool contains = rect.Contains(Vec3d(5.0f, 10.0f, 15.0f));

	EXPECT_EQ(center, Vec3d(6.0f, 12.0f, 18.0f));
	EXPECT_TRUE(valid);
	EXPECT_TRUE(contains);
}

// --- Comparison ---

TEST(Rect3d, Equality)
{
	Rect3f a(Vec3d(1.0f, 2.0f, 3.0f), Vec3d(4.0f, 5.0f, 6.0f));
	Rect3f b(Vec3d(1.0f, 2.0f, 3.0f), Vec3d(4.0f, 5.0f, 6.0f));
	Rect3f c(Vec3d(1.0f, 2.0f, 3.0f), Vec3d(4.0f, 5.0f, 7.0f));

	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a == c);
}

// --- Stream Output ---

TEST(Rect3d, StreamOutput)
{
	Rect3f rect(Vec3d(1.0f, 2.0f, 3.0f), Vec3d(10.0f, 20.0f, 30.0f));
	std::ostringstream oss;
	oss << rect;
	EXPECT_EQ(oss.str(), "{Position: (1, 2, 3), Size: (10, 20, 30)}");
}
