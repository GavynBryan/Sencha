#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <assets/cook/BakeBvh.h>
#include <assets/cook/DirectLightBake.h>
#include <assets/cook/LightmapRaster.h>

#include <array>
#include <vector>

namespace
{
    // One light straight above the chart, far enough to light it evenly.
    std::array<BakeDirectLight, 1> MakeLight()
    {
        BakeDirectLight light{};
        light.Position = Vec3d{ 2.0f, 3.0f, 2.0f };
        light.Intensity = 20.0f;
        light.Range = 20.0f;
        return { light };
    }

    // A flat XZ quad chart covering grid [0,4]x[0,4] (two triangles).
    std::vector<LightmapRasterTriangle> MakeQuadChart()
    {
        LightmapRasterTriangle a{};
        a.Uv[0] = { 0, 0 }; a.Uv[1] = { 4, 0 }; a.Uv[2] = { 4, 4 };
        a.Position[0] = { 0, 0, 0 }; a.Position[1] = { 4, 0, 0 }; a.Position[2] = { 4, 0, 4 };
        LightmapRasterTriangle b = a;
        b.Uv[1] = { 4, 4 }; b.Uv[2] = { 0, 4 };
        b.Position[1] = { 4, 0, 4 }; b.Position[2] = { 0, 0, 4 };
        for (int k = 0; k < 3; ++k)
        {
            a.Normal[k] = { 0, 1, 0 };
            b.Normal[k] = { 0, 1, 0 };
        }
        return { a, b };
    }

    std::uint32_t PixelAt(const std::vector<std::uint32_t>& atlas,
                          std::uint32_t width, std::uint32_t x, std::uint32_t y)
    {
        return atlas[static_cast<std::size_t>(y) * width + x];
    }
}

TEST(LightmapRaster, CoversInteriorAndDilatesGutter)
{
    const LightmapChartRect rect{ 1, 1, 5 + 2 * kLightmapGutter, 5 + 2 * kLightmapGutter };
    const std::uint32_t width = 16;
    std::vector<std::uint32_t> atlas(width * 16, 0u);
    BakeBvh empty;
    const auto lights = MakeLight();
    BakeChartLuxels(MakeQuadChart(), rect, lights, empty,
                    DirectLightBakeParams{}, width, atlas);

    // Chart interior grid point (2,2) baked nonzero.
    EXPECT_NE(PixelAt(atlas, width, rect.X + kLightmapGutter + 2,
                      rect.Y + kLightmapGutter + 2), 0u);
    // The gutter corner (one texel outside the grid) was dilated nonzero.
    EXPECT_NE(PixelAt(atlas, width, rect.X + kLightmapGutter - 1,
                      rect.Y + kLightmapGutter - 1), 0u);
    // Outside the padded rect stays untouched (the reserved border row).
    EXPECT_EQ(PixelAt(atlas, width, 0, 0), 0u);
}

TEST(LightmapRaster, SliverTriangleStillCoversItsGridPoints)
{
    // A near-degenerate 0.4-luxel-tall sliver crossing grid row 0: the edge
    // reach must still light the points it passes near.
    LightmapRasterTriangle tri{};
    tri.Uv[0] = { 0.0f, 0.3f }; tri.Uv[1] = { 4.0f, 0.3f }; tri.Uv[2] = { 2.0f, 0.7f };
    tri.Position[0] = { 0, 0, 0 }; tri.Position[1] = { 4, 0, 0 }; tri.Position[2] = { 2, 0, 1 };
    for (int k = 0; k < 3; ++k)
        tri.Normal[k] = { 0, 1, 0 };

    const LightmapChartRect rect{ 1, 1, 5 + 2 * kLightmapGutter, 3 + 2 * kLightmapGutter };
    const std::uint32_t width = 16;
    std::vector<std::uint32_t> atlas(width * 16, 0u);
    BakeBvh empty;
    const auto lights = MakeLight();
    BakeChartLuxels({ &tri, 1 }, rect, lights, empty,
                    DirectLightBakeParams{}, width, atlas);

    EXPECT_NE(PixelAt(atlas, width, rect.X + kLightmapGutter + 2,
                      rect.Y + kLightmapGutter + 0), 0u);
}

TEST(LightmapRaster, BuriedSamplesAreDilatedNotBlack)
{
    // A solid slab hovering just above the whole chart: every luxel's
    // upward probe first hits the slab's underside (a backface of the
    // downward-wound quad is its top side... wind it so the ray sees a
    // backface), so every sample is invalidated and, with nothing lit to
    // dilate from, the rect stays entirely neutral instead of baking a
    // black-vs-lit patchwork.
    std::vector<BakeTriangle> occluders;
    // Wind the slab so its normal points UP (away from the chart below):
    // the chart's upward probe then sees a backface first = buried.
    occluders.push_back({ Vec3d{ -10, 0.05f, -10 }, Vec3d{ -10, 0.05f, 10 },
                          Vec3d{ 10, 0.05f, 10 } });
    occluders.push_back({ Vec3d{ -10, 0.05f, -10 }, Vec3d{ 10, 0.05f, 10 },
                          Vec3d{ 10, 0.05f, -10 } });
    const Vec3d normal =
        (occluders[0].V1 - occluders[0].V0).Cross(occluders[0].V2 - occluders[0].V0);
    ASSERT_GT(normal.Y, 0.0f);

    BakeBvh bvh;
    bvh.Build(std::move(occluders));

    const LightmapChartRect rect{ 1, 1, 5 + 2 * kLightmapGutter, 5 + 2 * kLightmapGutter };
    const std::uint32_t width = 16;
    std::vector<std::uint32_t> atlas(width * 16, 0xFFFFFFFFu);
    const auto lights = MakeLight();
    BakeChartLuxels(MakeQuadChart(), rect, lights, bvh,
                    DirectLightBakeParams{}, width, atlas);

    for (std::uint32_t y = 0; y < rect.Height; ++y)
        for (std::uint32_t x = 0; x < rect.Width; ++x)
            EXPECT_EQ(PixelAt(atlas, width, rect.X + x, rect.Y + y), 0u);
}

TEST(LightmapRaster, IsDeterministic)
{
    const LightmapChartRect rect{ 1, 1, 5 + 2 * kLightmapGutter, 5 + 2 * kLightmapGutter };
    const std::uint32_t width = 16;
    BakeBvh empty;
    const auto lights = MakeLight();

    std::vector<std::uint32_t> a(width * 16, 0u);
    std::vector<std::uint32_t> b(width * 16, 0u);
    BakeChartLuxels(MakeQuadChart(), rect, lights, empty,
                    DirectLightBakeParams{}, width, a);
    BakeChartLuxels(MakeQuadChart(), rect, lights, empty,
                    DirectLightBakeParams{}, width, b);
    EXPECT_EQ(a, b);
}

#endif // SENCHA_ENABLE_COOK
