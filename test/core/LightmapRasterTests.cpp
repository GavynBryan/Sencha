#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <assets/cook/BakeBvh.h>
#include <assets/cook/DirectLightBake.h>
#include <assets/cook/LightmapRaster.h>

#include <algorithm>
#include <array>
#include <cmath>
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

    // The same surface and vertex attributes as MakeQuadChart, split across
    // the other diagonal.
    std::vector<LightmapRasterTriangle> MakeOppositeDiagonalQuadChart()
    {
        LightmapRasterTriangle a{};
        a.Uv[0] = { 0, 0 }; a.Uv[1] = { 4, 0 }; a.Uv[2] = { 0, 4 };
        a.Position[0] = { 0, 0, 0 }; a.Position[1] = { 4, 0, 0 }; a.Position[2] = { 0, 0, 4 };
        LightmapRasterTriangle b = a;
        b.Uv[0] = { 4, 0 }; b.Uv[1] = { 4, 4 }; b.Uv[2] = { 0, 4 };
        b.Position[0] = { 4, 0, 0 }; b.Position[1] = { 4, 0, 4 }; b.Position[2] = { 0, 0, 4 };
        for (int k = 0; k < 3; ++k)
        {
            a.Normal[k] = { 0, 1, 0 };
            b.Normal[k] = { 0, 1, 0 };
        }
        return { a, b };
    }

    BakeBvh MakeCrossingShadowBvh()
    {
        std::vector<BakeTriangle> occluders;
        // This wall splits luxel 3 and its shadow boundary crosses either
        // possible quad diagonal inside the chart.
        occluders.push_back({ Vec3d{ 2.875f, 0.0f, -10 }, Vec3d{ 2.875f, 20.0f, -10 },
                              Vec3d{ 2.875f, 20.0f, 10 } });
        occluders.push_back({ Vec3d{ 2.875f, 0.0f, -10 }, Vec3d{ 2.875f, 20.0f, 10 },
                              Vec3d{ 2.875f, 0.0f, 10 } });
        BakeBvh bvh;
        bvh.Build(std::move(occluders));
        return bvh;
    }

    std::vector<std::uint32_t> BakeQuadAtlas(
        const std::vector<LightmapRasterTriangle>& chart,
        const BakeBvh& occluders, std::span<const BakeDirectLight> lights)
    {
        const LightmapChartRect rect{
            1, 1, 5 + 2 * kLightmapGutter, 5 + 2 * kLightmapGutter
        };
        constexpr std::uint32_t kWidth = 16;
        std::vector<std::uint32_t> atlas(kWidth * 16, 0u);
        BakeChartLuxels(chart, rect, lights, occluders,
                        DirectLightBakeParams{}, kWidth, atlas);
        return atlas;
    }

    // RGB9E5 red channel: 9-bit mantissa scaled by the shared exponent.
    float DecodeRed(std::uint32_t pixel)
    {
        const float scale = std::exp2(static_cast<float>(
            static_cast<int>(pixel >> 27) - 15 - 9));
        return static_cast<float>(pixel & 0x1FFu) * scale;
    }

    std::uint32_t PixelAt(const std::vector<std::uint32_t>& atlas,
                          std::uint32_t width, std::uint32_t x, std::uint32_t y)
    {
        return atlas[static_cast<std::size_t>(y) * width + x];
    }

    void AppendGridQuad(std::vector<LightmapRasterTriangle>& out,
                        float minX, float minY, float maxX, float maxY,
                        bool oppositeDiagonal)
    {
        const auto vertex = [](float x, float y)
        {
            LightmapRasterTriangle tri{};
            tri.Uv[0] = { x, y };
            tri.Position[0] = { x, 0.0f, y };
            tri.Normal[0] = { 0.0f, 1.0f, 0.0f };
            return tri;
        };
        const LightmapRasterTriangle bl = vertex(minX, minY);
        const LightmapRasterTriangle br = vertex(maxX, minY);
        const LightmapRasterTriangle tr = vertex(maxX, maxY);
        const LightmapRasterTriangle tl = vertex(minX, maxY);
        const auto make = [](const LightmapRasterTriangle& a,
                             const LightmapRasterTriangle& b,
                             const LightmapRasterTriangle& c)
        {
            LightmapRasterTriangle result{};
            for (int axis = 0; axis < 3; ++axis)
            {
                const LightmapRasterTriangle* source =
                    axis == 0 ? &a : (axis == 1 ? &b : &c);
                result.Uv[axis] = source->Uv[0];
                result.Position[axis] = source->Position[0];
                result.Normal[axis] = source->Normal[0];
            }
            return result;
        };
        if (oppositeDiagonal)
        {
            out.push_back(make(bl, br, tl));
            out.push_back(make(br, tr, tl));
        }
        else
        {
            out.push_back(make(bl, br, tr));
            out.push_back(make(bl, tr, tl));
        }
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
    // The chart runs through the inside of a closed slab (an overlapping
    // brush straddling the chart plane): every luxel's probe sees a backface
    // first BOTH up (the slab's top from below) and down (the slab's bottom
    // from above), so every sample is invalidated and, with nothing lit to
    // dilate from, the rect stays entirely neutral instead of baking a
    // black-vs-lit patchwork.
    std::vector<BakeTriangle> occluders;
    // Slab top at y = 0.05, outward normal +Y (up).
    occluders.push_back({ Vec3d{ -10, 0.05f, -10 }, Vec3d{ -10, 0.05f, 10 },
                          Vec3d{ 10, 0.05f, 10 } });
    occluders.push_back({ Vec3d{ -10, 0.05f, -10 }, Vec3d{ 10, 0.05f, 10 },
                          Vec3d{ 10, 0.05f, -10 } });
    // Slab bottom at y = -0.05, outward normal -Y (down).
    occluders.push_back({ Vec3d{ -10, -0.05f, -10 }, Vec3d{ 10, -0.05f, 10 },
                          Vec3d{ -10, -0.05f, 10 } });
    occluders.push_back({ Vec3d{ -10, -0.05f, -10 }, Vec3d{ 10, -0.05f, -10 },
                          Vec3d{ 10, -0.05f, 10 } });
    const Vec3d topNormal =
        (occluders[0].V1 - occluders[0].V0).Cross(occluders[0].V2 - occluders[0].V0);
    ASSERT_GT(topNormal.Y, 0.0f);
    const Vec3d bottomNormal =
        (occluders[2].V1 - occluders[2].V0).Cross(occluders[2].V2 - occluders[2].V0);
    ASSERT_LT(bottomNormal.Y, 0.0f);

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

TEST(LightmapRaster, DistantSingleSidedBackfaceDoesNotBury)
{
    // A lit chart facing an open room whose far wall has no second skin
    // (carved interiors and doorway-connected rooms author walls single
    // sided): the chart's probe sees that wall's BACK across open air. One
    // distant backface is shadowing, not burial; the chart must still bake.
    // Regression for whole wall charts baking black in doorway-connected
    // rooms.
    std::vector<BakeTriangle> occluders;
    // A wall 8 units above the chart whose outward normal points UP, so the
    // chart's upward probe hits its back side first.
    occluders.push_back({ Vec3d{ -10, 8.0f, -10 }, Vec3d{ -10, 8.0f, 10 },
                          Vec3d{ 10, 8.0f, 10 } });
    occluders.push_back({ Vec3d{ -10, 8.0f, -10 }, Vec3d{ 10, 8.0f, 10 },
                          Vec3d{ 10, 8.0f, -10 } });
    BakeBvh bvh;
    bvh.Build(std::move(occluders));

    const LightmapChartRect rect{ 1, 1, 5 + 2 * kLightmapGutter, 5 + 2 * kLightmapGutter };
    const std::uint32_t width = 16;
    std::vector<std::uint32_t> atlas(width * 16, 0u);
    const auto lights = MakeLight();
    BakeChartLuxels(MakeQuadChart(), rect, lights, bvh,
                    DirectLightBakeParams{}, width, atlas);

    EXPECT_NE(PixelAt(atlas, width, rect.X + kLightmapGutter + 2,
                      rect.Y + kLightmapGutter + 2), 0u);
}

TEST(LightmapRaster, ShadowBoundaryLuxelBakesPartialBrightness)
{
    // A wall crosses a luxel between its subsample columns: half the luxel's
    // area sees the overhead light, half sits in shadow. The luxel must bake
    // an intermediate value (2x2 supersampling), not the all-or-nothing of a
    // single center sample, so an under-texel light spill reads as a dim
    // patch instead of a full-bright pinhole and shadow lines grade the same
    // way on every chart that crosses them.
    std::vector<BakeTriangle> occluders;
    // Vertical wall at x = 2.875 (between luxel 3's subsamples at 2.75 and
    // 3.25), tall enough to shadow everything past it from the light below.
    occluders.push_back({ Vec3d{ 2.875f, 0.0f, -10 }, Vec3d{ 2.875f, 20.0f, -10 },
                          Vec3d{ 2.875f, 20.0f, 10 } });
    occluders.push_back({ Vec3d{ 2.875f, 0.0f, -10 }, Vec3d{ 2.875f, 20.0f, 10 },
                          Vec3d{ 2.875f, 0.0f, 10 } });
    BakeBvh bvh;
    bvh.Build(std::move(occluders));

    std::array<BakeDirectLight, 1> lights{};
    lights[0].Position = Vec3d{ 1.0f, 8.0f, 2.0f };
    lights[0].Intensity = 30.0f;
    lights[0].Range = 30.0f;

    const LightmapChartRect rect{ 1, 1, 5 + 2 * kLightmapGutter, 5 + 2 * kLightmapGutter };
    const std::uint32_t width = 16;
    std::vector<std::uint32_t> atlas(width * 16, 0u);
    BakeChartLuxels(MakeQuadChart(), rect, lights, bvh,
                    DirectLightBakeParams{}, width, atlas);

    const auto luminance = [&](std::uint32_t gx, std::uint32_t gy)
    {
        const std::uint32_t p = PixelAt(atlas, width, rect.X + kLightmapGutter + gx,
                                        rect.Y + kLightmapGutter + gy);
        return DecodeRed(p);
    };

    const float lit = luminance(2, 2);       // fully in the light
    const float boundary = luminance(3, 2);  // wall splits this luxel
    const float shadow = luminance(4, 2);    // fully behind the wall
    EXPECT_GT(lit, 0.05f);
    EXPECT_LT(shadow, lit * 0.1f);
    EXPECT_GT(boundary, lit * 0.2f);
    EXPECT_LT(boundary, lit * 0.8f);
}

TEST(LightmapRaster, OppositePlanarTriangulationsMatchAcrossShadowBoundary)
{
    const auto lights = MakeLight();
    const BakeBvh bvh = MakeCrossingShadowBvh();

    const std::vector<std::uint32_t> first =
        BakeQuadAtlas(MakeQuadChart(), bvh, lights);
    const std::vector<std::uint32_t> opposite =
        BakeQuadAtlas(MakeOppositeDiagonalQuadChart(), bvh, lights);

    // Includes chart-edge samples and both dilation passes, not just the
    // interior texels around the diagonal.
    EXPECT_EQ(first, opposite);
}

TEST(LightmapRaster, OpenSurfaceCornerTieDoesNotExposeQuadDiagonals)
{
    // Three coplanar charts form an L: target [0,4]^2 plus neighbors to its
    // right and top. At (4.25,4.25), outside the real concave corner, the two
    // boundary edges are equally near. The selected edge (and its 0.0025
    // inset) must come from geometry, not whichever diagonal sorts first.
    const auto make = [](bool opposite)
    {
        std::vector<LightmapRasterTriangle> chart;
        AppendGridQuad(chart, 0, 0, 4, 4, opposite);
        AppendGridQuad(chart, 4, 0, 8, 4, opposite);
        AppendGridQuad(chart, 0, 4, 4, 8, opposite);
        return chart;
    };
    std::array<BakeDirectLight, 1> lights{};
    lights[0].Position = { 7.0f, 2.0f, 1.0f };
    lights[0].Intensity = 30.0f;
    lights[0].Range = 20.0f;
    BakeBvh empty;

    EXPECT_EQ(BakeQuadAtlas(make(false), empty, lights),
              BakeQuadAtlas(make(true), empty, lights));
}

TEST(LightmapRaster, TriangleInputOrderDoesNotChangeAtlas)
{
    const auto lights = MakeLight();
    const BakeBvh bvh = MakeCrossingShadowBvh();
    std::vector<LightmapRasterTriangle> chart = MakeQuadChart();
    for (int k = 0; k < 3; ++k)
    {
        // Make the shared-edge winner observable instead of relying on two
        // triangles whose interpolated attributes happen to be identical.
        chart[0].Normal[k] = { 0, -1, 0 };
        chart[1].Normal[k] = { 0, 1, 0 };
    }
    const std::vector<std::uint32_t> forward = BakeQuadAtlas(chart, bvh, lights);
    std::reverse(chart.begin(), chart.end());
    const std::vector<std::uint32_t> reversed = BakeQuadAtlas(chart, bvh, lights);
    EXPECT_EQ(forward, reversed);
}

TEST(LightmapRaster, SupersampleCrossingInternalEdgeUsesNeighborTriangle)
{
    std::vector<LightmapRasterTriangle> chart = MakeQuadChart();
    for (int k = 0; k < 3; ++k)
    {
        // At grid point (2,2), three deterministic samples resolve to the
        // lower-right triangle while the upper-left offset lies strictly in
        // this lit triangle. A center-owner implementation clamps all four to
        // the first triangle and incorrectly bakes zero.
        chart[0].Normal[k] = { 0, -1, 0 };
        chart[1].Normal[k] = { 0, 1, 0 };
    }
    std::array<BakeDirectLight, 1> lights{};
    lights[0].Position = { 2.0f, 10.0f, 2.0f };
    lights[0].Intensity = 100.0f;
    lights[0].Range = 30.0f;
    BakeBvh empty;
    const std::vector<std::uint32_t> atlas = BakeQuadAtlas(chart, empty, lights);

    constexpr std::uint32_t kWidth = 16;
    const LightmapChartRect rect{
        1, 1, 5 + 2 * kLightmapGutter, 5 + 2 * kLightmapGutter
    };
    const float crossing = DecodeRed(PixelAt(
        atlas, kWidth, rect.X + kLightmapGutter + 2,
        rect.Y + kLightmapGutter + 2));
    const float lit = DecodeRed(PixelAt(
        atlas, kWidth, rect.X + kLightmapGutter + 1,
        rect.Y + kLightmapGutter + 3));
    EXPECT_GT(lit, 0.05f);
    EXPECT_GT(crossing, lit * 0.1f);
    EXPECT_LT(crossing, lit * 0.5f);
}

TEST(LightmapRaster, OpenInwardShellReceivesLightAndOccludesTwoSided)
{
    // Five inward-facing room planes, open at z=0. The receiver is the floor;
    // its own geometry is in the BVH, as it is in a real document cook.
    std::vector<BakeTriangle> shell = {
        // floor, +Y
        { { 0, 0, 0 }, { 4, 0, 4 }, { 4, 0, 0 } },
        { { 0, 0, 0 }, { 0, 0, 4 }, { 4, 0, 4 } },
        // ceiling, -Y
        { { 0, 4, 0 }, { 4, 4, 0 }, { 4, 4, 4 } },
        { { 0, 4, 0 }, { 4, 4, 4 }, { 0, 4, 4 } },
        // x=0 wall, +X
        { { 0, 0, 0 }, { 0, 4, 4 }, { 0, 0, 4 } },
        { { 0, 0, 0 }, { 0, 4, 0 }, { 0, 4, 4 } },
        // x=4 wall, -X
        { { 4, 0, 0 }, { 4, 0, 4 }, { 4, 4, 4 } },
        { { 4, 0, 0 }, { 4, 4, 4 }, { 4, 4, 0 } },
        // z=4 wall, -Z
        { { 0, 0, 4 }, { 0, 4, 4 }, { 4, 4, 4 } },
        { { 0, 0, 4 }, { 4, 4, 4 }, { 4, 0, 4 } },
    };
    BakeBvh openShell;
    openShell.Build(shell);
    std::array<BakeDirectLight, 1> lights{};
    lights[0].Position = { 2.0f, 3.0f, 2.0f };
    lights[0].Intensity = 30.0f;
    lights[0].Range = 20.0f;

    const std::vector<std::uint32_t> unblocked =
        BakeQuadAtlas(MakeQuadChart(), openShell, lights);
    constexpr std::uint32_t kWidth = 16;
    const LightmapChartRect rect{
        1, 1, 5 + 2 * kLightmapGutter, 5 + 2 * kLightmapGutter
    };
    const auto centerPixel = [&](const std::vector<std::uint32_t>& atlas)
    {
        return PixelAt(atlas, kWidth, rect.X + kLightmapGutter + 2,
                       rect.Y + kLightmapGutter + 2);
    };
    ASSERT_NE(centerPixel(unblocked), 0u);

    // A one-sided sheet between the floor and light blocks the segment in
    // either winding, while the shell/receiver is still not classified as a
    // buried overlap.
    const BakeTriangle blockerA{
        { -10, 1, -10 }, { -10, 1, 10 }, { 10, 1, 10 }
    };
    const BakeTriangle blockerB{
        { -10, 1, -10 }, { 10, 1, 10 }, { 10, 1, -10 }
    };
    for (const bool reverseWinding : { false, true })
    {
        std::vector<BakeTriangle> blockedGeometry = shell;
        if (reverseWinding)
        {
            blockedGeometry.push_back({ blockerA.V0, blockerA.V2, blockerA.V1 });
            blockedGeometry.push_back({ blockerB.V0, blockerB.V2, blockerB.V1 });
        }
        else
        {
            blockedGeometry.push_back(blockerA);
            blockedGeometry.push_back(blockerB);
        }
        BakeBvh blockedShell;
        blockedShell.Build(std::move(blockedGeometry));
        const std::vector<std::uint32_t> blocked =
            BakeQuadAtlas(MakeQuadChart(), blockedShell, lights);
        EXPECT_EQ(centerPixel(blocked), 0u);
    }
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
