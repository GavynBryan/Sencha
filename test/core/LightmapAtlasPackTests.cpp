#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <assets/cook/LightmapAtlasPack.h>

#include <algorithm>
#include <vector>

namespace
{
    bool Overlaps(const LightmapChartRect& a, const LightmapChartRect& b)
    {
        return a.X < b.X + b.Width && b.X < a.X + a.Width
            && a.Y < b.Y + b.Height && b.Y < a.Y + a.Height;
    }
}

TEST(LightmapAtlasPack, RectsAreDisjointAndInsideTheReservedBorder)
{
    std::vector<Vec2d> extents;
    for (int i = 0; i < 20; ++i)
        extents.push_back(Vec2d{ 1.0f + 0.3f * static_cast<float>(i % 7),
                                 0.5f + 0.2f * static_cast<float>(i % 5) });
    const LightmapAtlasLayout layout = PackLightmapAtlas(extents, 0.25f, 2048);

    ASSERT_EQ(layout.Rects.size(), extents.size());
    for (std::size_t i = 0; i < layout.Rects.size(); ++i)
    {
        const LightmapChartRect& rect = layout.Rects[i];
        EXPECT_GE(rect.X, 1u);
        EXPECT_GE(rect.Y, 1u);
        EXPECT_LT(rect.X + rect.Width, layout.Width);
        EXPECT_LT(rect.Y + rect.Height, layout.Height);
        for (std::size_t j = i + 1; j < layout.Rects.size(); ++j)
            EXPECT_FALSE(Overlaps(rect, layout.Rects[j])) << i << " vs " << j;
    }
}

TEST(LightmapAtlasPack, GridPointConventionSizesRects)
{
    // 1.0 world units at 0.25 luxel = 5 grid points (+ 2*gutter padding).
    const std::vector<Vec2d> extents{ Vec2d{ 1.0f, 1.0f } };
    const LightmapAtlasLayout layout = PackLightmapAtlas(extents, 0.25f, 2048);
    EXPECT_EQ(layout.Rects[0].Width, 5u + 2 * kLightmapGutter);
    EXPECT_EQ(layout.Rects[0].Height, 5u + 2 * kLightmapGutter);
}

TEST(LightmapAtlasPack, DensityClampsInsteadOfOverflowing)
{
    // One chart that cannot fit a 128 cap at the requested density.
    const std::vector<Vec2d> extents{ Vec2d{ 100.0f, 100.0f } };
    const LightmapAtlasLayout layout = PackLightmapAtlas(extents, 0.25f, 128);
    EXPECT_LE(layout.Width, 128u);
    EXPECT_LE(layout.Height, 128u);
    EXPECT_GT(layout.EffectiveLuxelSize, 0.25f);
    EXPECT_LT(layout.Rects[0].X + layout.Rects[0].Width, layout.Width + 1);
}

TEST(LightmapAtlasPack, IsDeterministicUnderEqualSizeTies)
{
    // Many identical extents: placement must be reproducible (ties break by
    // chart index).
    const std::vector<Vec2d> extents(16, Vec2d{ 2.0f, 2.0f });
    const LightmapAtlasLayout a = PackLightmapAtlas(extents, 0.25f, 2048);
    const LightmapAtlasLayout b = PackLightmapAtlas(extents, 0.25f, 2048);
    ASSERT_EQ(a.Rects.size(), b.Rects.size());
    for (std::size_t i = 0; i < a.Rects.size(); ++i)
    {
        EXPECT_EQ(a.Rects[i].X, b.Rects[i].X);
        EXPECT_EQ(a.Rects[i].Y, b.Rects[i].Y);
    }
}

#endif // SENCHA_ENABLE_COOK
