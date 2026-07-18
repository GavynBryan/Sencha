#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <array>
#include <cstdint>
#include <vector>

#include <assets/cook/BakeBvh.h>
#include <assets/cook/DirectLightBake.h>
#include <assets/cook/DirectLightTessellate.h>
#include <math/Mat.h>
#include <render/static_mesh/MeshGeometry.h>
#include <render/static_mesh/MeshValidation.h>

namespace
{
    // A flat quad in XZ (two triangles, four vertices, one section), normal +Y.
    MeshGeometry FlatQuad(float half)
    {
        MeshGeometry g;
        const auto vertex = [](float x, float z) {
            StaticMeshVertex v{};
            v.Position = Vec3d(x, 0.0f, z);
            v.Normal = Vec3d(0.0f, 1.0f, 0.0f);
            v.Tangent = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
            return v;
        };
        g.Vertices = { vertex(-half, -half), vertex(half, -half),
                       vertex(half, half), vertex(-half, half) };
        g.Indices = { 0, 2, 1, 0, 3, 2 };
        g.Sections.push_back(StaticMeshSection{
            .IndexOffset = 0, .IndexCount = 6,
            .VertexOffset = 0, .VertexCount = 4, .MaterialSlot = 0 });
        RecomputeMeshBounds(g);
        return g;
    }

    BakeDirectLight PointAbove(const Vec3d& position, float range)
    {
        BakeDirectLight light{};
        light.Kind = BakeLightKind::Point;
        light.Position = position;
        light.Color = Vec3d(1.0f, 1.0f, 1.0f);
        light.Intensity = 30.0f;
        light.Range = range;
        return light;
    }
}

TEST(DirectLightTessellate, RefinesNearLight)
{
    MeshGeometry mesh = FlatQuad(6.0f);
    const std::size_t before = mesh.Vertices.size();

    const std::array<BakeDirectLight, 1> lights{ PointAbove(Vec3d(0, 1.5f, 0), 4.0f) };
    BakeBvh empty;
    const std::size_t added = TessellateForDirectBake(
        mesh, Mat4::Identity(), lights, empty, DirectLightBakeParams{},
        DirectTessellationParams{});

    EXPECT_GT(added, 0u);
    EXPECT_EQ(mesh.Vertices.size(), before + added);
    EXPECT_TRUE(ValidateMeshGeometry(mesh).IsValid());
}

TEST(DirectLightTessellate, LeavesGeometryFarFromLightsUntouched)
{
    MeshGeometry mesh = FlatQuad(6.0f);
    const std::size_t before = mesh.Vertices.size();

    // Light range 4 well away from a quad at the origin: no edge is near it.
    const std::array<BakeDirectLight, 1> lights{ PointAbove(Vec3d(100, 1.5f, 0), 4.0f) };
    BakeBvh empty;
    const std::size_t added = TessellateForDirectBake(
        mesh, Mat4::Identity(), lights, empty, DirectLightBakeParams{},
        DirectTessellationParams{});

    EXPECT_EQ(added, 0u);
    EXPECT_EQ(mesh.Vertices.size(), before);
}

TEST(DirectLightTessellate, RespectsVertexGrowthCap)
{
    MeshGeometry mesh = FlatQuad(6.0f);
    const std::size_t before = mesh.Vertices.size();

    const std::array<BakeDirectLight, 1> lights{ PointAbove(Vec3d(0, 1.5f, 0), 8.0f) };
    BakeBvh empty;
    DirectTessellationParams params{};
    params.MaxDepth = 8;             // would explode without the cap
    params.ErrorTolerance = 0.0f;    // split everything that is near a light
    params.MinEdgeLength = 0.01f;
    params.MaxVertexGrowth = 3.0f;
    TessellateForDirectBake(mesh, Mat4::Identity(), lights, empty,
                            DirectLightBakeParams{}, params);

    EXPECT_LE(mesh.Vertices.size(), before * 3 + 8);  // cap plus one level's slack
    EXPECT_TRUE(ValidateMeshGeometry(mesh).IsValid());
}

TEST(DirectLightTessellate, IsDeterministic)
{
    const std::array<BakeDirectLight, 1> lights{ PointAbove(Vec3d(1, 1.5f, -0.5f), 5.0f) };
    BakeBvh empty;

    MeshGeometry a = FlatQuad(6.0f);
    MeshGeometry b = FlatQuad(6.0f);
    TessellateForDirectBake(a, Mat4::Identity(), lights, empty,
                            DirectLightBakeParams{}, DirectTessellationParams{});
    TessellateForDirectBake(b, Mat4::Identity(), lights, empty,
                            DirectLightBakeParams{}, DirectTessellationParams{});

    ASSERT_EQ(a.Vertices.size(), b.Vertices.size());
    ASSERT_EQ(a.Indices.size(), b.Indices.size());
    for (std::size_t i = 0; i < a.Vertices.size(); ++i)
    {
        EXPECT_EQ(a.Vertices[i].Position.X, b.Vertices[i].Position.X);
        EXPECT_EQ(a.Vertices[i].Position.Z, b.Vertices[i].Position.Z);
    }
    EXPECT_EQ(a.Indices, b.Indices);
}

TEST(DirectLightTessellate, RefinedMeshBakesMoreDetailThanCoarse)
{
    const std::array<BakeDirectLight, 1> lights{ PointAbove(Vec3d(0, 1.2f, 0), 3.0f) };
    BakeBvh empty;

    // A small-range light over a big quad: at coarse (corner-only) density the
    // light falls between the four corners and bakes to nothing; after
    // refinement, interior vertices capture it.
    MeshGeometry coarse = FlatQuad(6.0f);
    BakeDirectLighting(coarse, Mat4::Identity(), lights, empty, DirectLightBakeParams{});
    std::size_t coarseLit = 0;
    for (const StaticMeshVertex& v : coarse.Vertices)
        coarseLit += v.BakedDirect != 0 ? 1 : 0;

    MeshGeometry refined = FlatQuad(6.0f);
    TessellateForDirectBake(refined, Mat4::Identity(), lights, empty,
                            DirectLightBakeParams{}, DirectTessellationParams{});
    BakeDirectLighting(refined, Mat4::Identity(), lights, empty, DirectLightBakeParams{});
    std::size_t refinedLit = 0;
    for (const StaticMeshVertex& v : refined.Vertices)
        refinedLit += v.BakedDirect != 0 ? 1 : 0;

    EXPECT_EQ(coarseLit, 0u);
    EXPECT_GT(refinedLit, 0u);
}

#endif  // SENCHA_ENABLE_COOK
