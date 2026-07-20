#include <gtest/gtest.h>

#include "brush/BrushLightmapCharts.h"
#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"

#include <array>
#include <cmath>

namespace
{
    // A unit box: 8 vertices, 6 quads, every edge hard.
    BrushMesh MakeBox()
    {
        BrushMesh mesh;
        mesh.Vertices = {
            BrushVertex{ { -1, -1, -1 } }, BrushVertex{ { 1, -1, -1 } },
            BrushVertex{ { 1, 1, -1 } },   BrushVertex{ { -1, 1, -1 } },
            BrushVertex{ { -1, -1, 1 } },  BrushVertex{ { 1, -1, 1 } },
            BrushVertex{ { 1, 1, 1 } },    BrushVertex{ { -1, 1, 1 } },
        };
        mesh.Faces = {
            BrushFace{ { 3, 2, 1, 0 }, { 0, 0, -1 }, {} },
            BrushFace{ { 4, 5, 6, 7 }, { 0, 0, 1 }, {} },
            BrushFace{ { 0, 1, 5, 4 }, { 0, -1, 0 }, {} },
            BrushFace{ { 7, 6, 2, 3 }, { 0, 1, 0 }, {} },
            BrushFace{ { 0, 4, 7, 3 }, { -1, 0, 0 }, {} },
            BrushFace{ { 1, 2, 6, 5 }, { 1, 0, 0 }, {} },
        };
        return mesh;
    }

    // A gentle 3-quad strip: an open sheet folded 15 degrees per seam, all
    // seams soft, well inside a 45 degree cone.
    BrushMesh MakeSoftStrip()
    {
        BrushMesh mesh;
        const float a = 15.0f * 3.14159265f / 180.0f;
        Vec3d p{ 0, 0, 0 };
        mesh.Vertices.push_back(BrushVertex{ { 0, 0, 0 } });
        mesh.Vertices.push_back(BrushVertex{ { 0, 0, 1 } });
        float angle = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            p += Vec3d{ std::cos(angle), std::sin(angle), 0 };
            mesh.Vertices.push_back(BrushVertex{ p });
            mesh.Vertices.push_back(BrushVertex{ { p.X, p.Y, 1 } });
            angle += a;
        }
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            const std::uint32_t b = i * 2;
            mesh.Faces.push_back(BrushFace{ { b, b + 2, b + 3, b + 1 }, {}, {} });
            BrushFace& face = mesh.Faces.back();
            face.Normal = BrushComputeFaceNormal(mesh, face);
        }
        BrushSetEdgeSoft(mesh, 2, 3, true);
        BrushSetEdgeSoft(mesh, 4, 5, true);
        return mesh;
    }
}

TEST(BrushLightmapCharts, HardEdgedBoxIsOneChartPerFace)
{
    const BrushChartSet set =
        BuildBrushLightmapCharts(MakeBox(), Transform3f{}, 45.0f, 0.25f);
    EXPECT_EQ(set.Charts.size(), 6u);
    for (std::size_t a = 0; a < set.FaceChart.size(); ++a)
        for (std::size_t b = a + 1; b < set.FaceChart.size(); ++b)
            EXPECT_NE(set.FaceChart[a], set.FaceChart[b]);
}

TEST(BrushLightmapCharts, SoftStripGrowsOneChart)
{
    const BrushChartSet set =
        BuildBrushLightmapCharts(MakeSoftStrip(), Transform3f{}, 45.0f, 0.25f);
    EXPECT_EQ(set.Charts.size(), 1u);
    EXPECT_EQ(set.FaceChart[0], set.FaceChart[1]);
    EXPECT_EQ(set.FaceChart[1], set.FaceChart[2]);
}

TEST(BrushLightmapCharts, ConeSplitsAnOverCurvedRun)
{
    // The same strip judged with a cone tighter than its 15 degree folds
    // still merges neighbors, but a 5 degree cone splits every seam.
    const BrushChartSet tight =
        BuildBrushLightmapCharts(MakeSoftStrip(), Transform3f{}, 5.0f, 0.25f);
    EXPECT_EQ(tight.Charts.size(), 3u);
}

TEST(BrushLightmapCharts, ChartUvsAreWorldScaled)
{
    // One 2x2 box face projects to a 2x2 world-unit chart extent.
    const BrushChartSet set =
        BuildBrushLightmapCharts(MakeBox(), Transform3f{}, 45.0f, 0.25f);
    for (const BrushLightmapChart& chart : set.Charts)
    {
        EXPECT_NEAR(chart.UvMax.X - chart.UvMin.X, 2.0f, 0.26f);
        EXPECT_NEAR(chart.UvMax.Y - chart.UvMin.Y, 2.0f, 0.26f);
    }
}

TEST(BrushLightmapCharts, IsDeterministic)
{
    const BrushChartSet a =
        BuildBrushLightmapCharts(MakeSoftStrip(), Transform3f{}, 45.0f, 0.25f);
    const BrushChartSet b =
        BuildBrushLightmapCharts(MakeSoftStrip(), Transform3f{}, 45.0f, 0.25f);
    ASSERT_EQ(a.Charts.size(), b.Charts.size());
    EXPECT_EQ(a.FaceChart, b.FaceChart);
    for (std::size_t i = 0; i < a.Charts.size(); ++i)
    {
        EXPECT_EQ(a.Charts[i].UvMin.X, b.Charts[i].UvMin.X);
        EXPECT_EQ(a.Charts[i].UvMax.Y, b.Charts[i].UvMax.Y);
    }
}

TEST(BrushLightmapCharts, CyclicQuadRotationDoesNotChangeChartProjection)
{
    BrushMesh original;
    original.Vertices = {
        BrushVertex{ { 4.0f, 0.5000001192092896f, -1.999999761581421f } },
        BrushVertex{ { 4.0f, -3.5f, -1.999999761581421f } },
        BrushVertex{ { 5.0f, -2.5f, -1.999999761581421f } },
        BrushVertex{ { 5.0f, -0.5f, -1.999999761581421f } },
    };
    original.Faces = { BrushFace{ { 0, 1, 2, 3 }, {}, {} } };

    BrushMesh rotated = original;
    rotated.Faces[0].Loop = { 1, 2, 3, 0 };

    const BrushChartSet a =
        BuildBrushLightmapCharts(original, Transform3f{}, 45.0f, 0.25f);
    const BrushChartSet b =
        BuildBrushLightmapCharts(rotated, Transform3f{}, 45.0f, 0.25f);
    ASSERT_EQ(a.Charts.size(), 1u);
    ASSERT_EQ(b.Charts.size(), 1u);
    EXPECT_EQ(a.Charts[0].Normal, b.Charts[0].Normal);
    EXPECT_EQ(a.Charts[0].TangentU, b.Charts[0].TangentU);
    EXPECT_EQ(a.Charts[0].TangentV, b.Charts[0].TangentV);
    EXPECT_EQ(a.Charts[0].UvMin, b.Charts[0].UvMin);
    EXPECT_EQ(a.Charts[0].UvMax, b.Charts[0].UvMax);

    // Constant-Z input must stay exactly axis-aligned. In float Newell
    // accumulation this real Benchmark-shaped quad left a ~8e-8 X component,
    // enough to select a different seed axis than its coplanar neighbors.
    EXPECT_EQ(a.Charts[0].Normal, (Vec3d{ 0.0f, 0.0f, 1.0f }));
    EXPECT_EQ(a.Charts[0].TangentU, (Vec3d{ 0.0f, -1.0f, 0.0f }));
    EXPECT_EQ(BrushComputeFaceNormal(original, original.Faces[0]),
              BrushComputeFaceNormal(rotated, rotated.Faces[0]));
    EXPECT_EQ(BrushComputeFaceNormal(original, original.Faces[0]),
              (Vec3d{ 0.0f, 0.0f, 1.0f }));
}
