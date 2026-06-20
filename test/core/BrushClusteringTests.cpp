#include <assets/cook/BrushClustering.h>
#include <assets/cook/BrushGeometryCook.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
    // One axis-aligned triangle centered at `center`, with a tiny extent so the
    // brush's bounds-center is exactly `center`. Only the center drives cell
    // assignment; the explicit vertices let rebasing be checked exactly.
    CookBrushGeometry MakeBrush(const Vec3d& center, const char* material)
    {
        const Vec3d a = center + Vec3d{ -1.0, -1.0, 0.0 };
        const Vec3d b = center + Vec3d{ 1.0, -1.0, 0.0 };
        const Vec3d c = center + Vec3d{ 0.0, 1.0, 0.0 };

        CookFace face;
        face.Material = AssetRef{ .Path = material };
        face.Triangles = {
            StaticMeshVertex{ .Position = a, .Normal = Vec3d{ 0, 0, 1 }, .Uv0 = {}, .Tangent = {} },
            StaticMeshVertex{ .Position = b, .Normal = Vec3d{ 0, 0, 1 }, .Uv0 = {}, .Tangent = {} },
            StaticMeshVertex{ .Position = c, .Normal = Vec3d{ 0, 0, 1 }, .Uv0 = {}, .Tangent = {} },
        };

        CookBrushGeometry brush;
        brush.WorldBounds = Aabb3d::Empty();
        for (const StaticMeshVertex& v : face.Triangles)
            brush.WorldBounds.ExpandToInclude(v.Position);
        brush.Faces.push_back(std::move(face));
        return brush;
    }
}

TEST(BrushClusteringTests, EmptyInputYieldsNoCells)
{
    const std::vector<CookBrushGeometry> brushes;
    EXPECT_TRUE(ClusterBrushesIntoCells(brushes, 16.0).empty());
}

TEST(BrushClusteringTests, EmptyBrushIsSkipped)
{
    CookBrushGeometry empty; // no faces
    empty.WorldBounds = Aabb3d::Empty();
    const std::vector<CookBrushGeometry> brushes = { empty };
    EXPECT_TRUE(ClusterBrushesIntoCells(brushes, 16.0).empty());
}

TEST(BrushClusteringTests, BrushesInSameCellMergeToOneCell)
{
    // Both centers fall in cell (0,0,0) for cellSize 16.
    const std::vector<CookBrushGeometry> brushes = {
        MakeBrush(Vec3d{ 2, 2, 2 }, "asset://a.smat"),
        MakeBrush(Vec3d{ 5, 5, 5 }, "asset://b.smat"),
    };

    const std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, 16.0);
    ASSERT_EQ(cells.size(), 1u);
    EXPECT_EQ(cells[0].Coord, (Vec3i{ 0, 0, 0 }));
    EXPECT_EQ(cells[0].Faces.size(), 2u); // both brushes' single faces
}

TEST(BrushClusteringTests, BrushesInDifferentCellsSeparate)
{
    const std::vector<CookBrushGeometry> brushes = {
        MakeBrush(Vec3d{ 8, 8, 8 }, "asset://a.smat"),    // cell (0,0,0)
        MakeBrush(Vec3d{ 40, 8, 8 }, "asset://b.smat"),   // cell (2,0,0)
    };

    const std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, 16.0);
    ASSERT_EQ(cells.size(), 2u);
    EXPECT_EQ(cells[0].Coord, (Vec3i{ 0, 0, 0 }));
    EXPECT_EQ(cells[1].Coord, (Vec3i{ 2, 0, 0 }));
}

TEST(BrushClusteringTests, CellOriginIsLatticeCornerAndGeometryIsRebased)
{
    // Center (40,8,8), cellSize 16 -> cell (2,0,0), origin (32,0,0). The triangle
    // vertex at world (41,7,8) must come back cell-local at (9,7,8).
    const std::vector<CookBrushGeometry> brushes = {
        MakeBrush(Vec3d{ 40, 8, 8 }, "asset://a.smat"),
    };

    const std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, 16.0);
    ASSERT_EQ(cells.size(), 1u);
    EXPECT_EQ(cells[0].Origin, (Vec3d{ 32, 0, 0 }));

    // Vertex b of MakeBrush is center + (1,-1,0) = (41,7,8) world -> (9,7,8) local.
    ASSERT_EQ(cells[0].Faces.size(), 1u);
    ASSERT_EQ(cells[0].Faces[0].Triangles.size(), 3u);
    EXPECT_EQ(cells[0].Faces[0].Triangles[1].Position, (Vec3d{ 9, 7, 8 }));
}

TEST(BrushClusteringTests, NegativeCentersFloorTowardNegativeInfinity)
{
    // floor, not truncation: center -1 with cellSize 16 -> cell -1, origin -16.
    const std::vector<CookBrushGeometry> brushes = {
        MakeBrush(Vec3d{ -1, -1, -1 }, "asset://a.smat"),
    };

    const std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, 16.0);
    ASSERT_EQ(cells.size(), 1u);
    EXPECT_EQ(cells[0].Coord, (Vec3i{ -1, -1, -1 }));
    EXPECT_EQ(cells[0].Origin, (Vec3d{ -16, -16, -16 }));
}

TEST(BrushClusteringTests, CellsEmergeInAscendingCoordOrderRegardlessOfInput)
{
    // Fed back-to-front and out of axis order; output must be sorted (x,y,z).
    const std::vector<CookBrushGeometry> brushes = {
        MakeBrush(Vec3d{ 8, 8, 40 }, "asset://a.smat"),   // (0,0,2)
        MakeBrush(Vec3d{ 40, 8, 8 }, "asset://b.smat"),   // (2,0,0)
        MakeBrush(Vec3d{ 8, 40, 8 }, "asset://c.smat"),   // (0,2,0)
        MakeBrush(Vec3d{ 8, 8, 8 }, "asset://d.smat"),    // (0,0,0)
    };

    const std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, 16.0);
    ASSERT_EQ(cells.size(), 4u);
    EXPECT_EQ(cells[0].Coord, (Vec3i{ 0, 0, 0 }));
    EXPECT_EQ(cells[1].Coord, (Vec3i{ 0, 0, 2 }));
    EXPECT_EQ(cells[2].Coord, (Vec3i{ 0, 2, 0 }));
    EXPECT_EQ(cells[3].Coord, (Vec3i{ 2, 0, 0 }));
}

TEST(BrushClusteringTests, StraddlingBrushStaysWholeInCenterCell)
{
    // A brush whose bounds-center is in cell 0 but whose geometry pokes past the
    // boundary at x=16 is not split: all its faces stay in cell (0,0,0). Bounds
    // are set so the center stays at x=8 (cell 0) even with the poking vertex.
    CookBrushGeometry wide = MakeBrush(Vec3d{ 8, 8, 8 }, "asset://a.smat");
    wide.Faces[0].Triangles[1].Position = Vec3d{ 30, 7, 8 }; // pokes into cell 1
    wide.WorldBounds = Aabb3d::FromMinMax(Vec3d{ -14, 7, 8 }, Vec3d{ 30, 9, 8 }); // center x=8

    const std::vector<BrushCell> cells = ClusterBrushesIntoCells({ &wide, 1 }, 16.0);
    ASSERT_EQ(cells.size(), 1u);
    EXPECT_EQ(cells[0].Coord, (Vec3i{ 0, 0, 0 }));
    EXPECT_EQ(cells[0].Faces[0].Triangles.size(), 3u);
}

TEST(BrushClusteringTests, NonPositiveCellSizeCollapsesToSingleCell)
{
    const std::vector<CookBrushGeometry> brushes = {
        MakeBrush(Vec3d{ 1000, -500, 250 }, "asset://a.smat"),
        MakeBrush(Vec3d{ -800, 900, -100 }, "asset://b.smat"),
    };

    const std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, 0.0);
    ASSERT_EQ(cells.size(), 1u);
    EXPECT_EQ(cells[0].Coord, (Vec3i{ 0, 0, 0 }));
    EXPECT_EQ(cells[0].Origin, (Vec3d{ 0, 0, 0 }));
    EXPECT_EQ(cells[0].Faces.size(), 2u);
}

// The cluster -> bake seam: each cell's local faces feed the existing per-material
// bake, and the resulting mesh bounds are cell-local (small), not world-scale.
TEST(BrushClusteringTests, EachCellBakesToCellLocalMesh)
{
    const std::vector<CookBrushGeometry> brushes = {
        MakeBrush(Vec3d{ 8, 8, 8 }, "asset://a.smat"),    // cell (0,0,0)
        MakeBrush(Vec3d{ 200, 8, 8 }, "asset://a.smat"),  // cell (12,0,0), far away
    };

    const std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, 16.0);
    ASSERT_EQ(cells.size(), 2u);

    for (const BrushCell& cell : cells)
    {
        const std::vector<AssetRef> order = CollectMaterialOrder(cell.Faces);
        MeshGeometry mesh;
        std::string error;
        ASSERT_TRUE(BakeBrushFacesToStaticMesh(cell.Faces, order, mesh, &error)) << error;
        ASSERT_EQ(mesh.Sections.size(), 1u); // one material -> one section

        // Local bounds stay near the cell origin even for the far cell: the rebase
        // means the second mesh is not authored at world x=200.
        EXPECT_LT(mesh.LocalBounds.Max.X, 16.0);
        EXPECT_GT(mesh.LocalBounds.Min.X, -16.0);
    }
}
