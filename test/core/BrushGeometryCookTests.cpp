// docs/plans/sencha-level-editor/05-level-cook.md §2 / §9 — the pure brush bake.
// Zero threads, no filesystem: faces are built in memory and baked.

#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <assets/cook/BrushGeometryCook.h>

#include <cmath>
#include <string>
#include <vector>

namespace
{
    AssetRef Mat(std::string path)
    {
        return AssetRef{ AssetType::Material, std::move(path) };
    }

    StaticMeshVertex MakeVertex(Vec3d pos, Vec3d normal, Vec2d uv)
    {
        return StaticMeshVertex{ .Position = pos, .Normal = normal, .Uv0 = uv, .Tangent = Vec4{} };
    }

    // A unit quad in the plane z = 0, wound CCW so its geometric normal is +Z.
    // Two fan triangles (0,1,2)(0,2,3) → six raw vertices; UV = (x, y).
    CookFace MakeQuadZ(const AssetRef& material, float z = 0.0f)
    {
        const Vec3d n{ 0.0f, 0.0f, 1.0f };
        const Vec3d c0{ 0.0f, 0.0f, z };
        const Vec3d c1{ 1.0f, 0.0f, z };
        const Vec3d c2{ 1.0f, 1.0f, z };
        const Vec3d c3{ 0.0f, 1.0f, z };

        CookFace face;
        face.Material = material;
        face.Triangles = {
            MakeVertex(c0, n, { 0.0f, 0.0f }), MakeVertex(c1, n, { 1.0f, 0.0f }),
            MakeVertex(c2, n, { 1.0f, 1.0f }), MakeVertex(c0, n, { 0.0f, 0.0f }),
            MakeVertex(c2, n, { 1.0f, 1.0f }), MakeVertex(c3, n, { 0.0f, 1.0f }),
        };
        return face;
    }

    Vec3d GeometricNormal(const StaticMeshVertex& a, const StaticMeshVertex& b, const StaticMeshVertex& c)
    {
        return (b.Position - a.Position).Cross(c.Position - a.Position).Normalized();
    }
}

TEST(BrushGeometryCook, CollectMaterialOrderIsFirstSeen)
{
    std::vector<CookFace> faces = {
        MakeQuadZ(Mat("b")), MakeQuadZ(Mat("a")), MakeQuadZ(Mat("b")),
    };
    const std::vector<AssetRef> order = CollectMaterialOrder(faces);
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0].Path, "b");
    EXPECT_EQ(order[1].Path, "a");
}

TEST(BrushGeometryCook, EmptyInputBakesEmptyMesh)
{
    MeshGeometry mesh;
    std::string error;
    ASSERT_TRUE(BakeBrushFacesToStaticMesh({}, {}, mesh, &error)) << error;
    EXPECT_TRUE(mesh.Vertices.empty());
    EXPECT_TRUE(mesh.Indices.empty());
    EXPECT_TRUE(mesh.Sections.empty());
}

TEST(BrushGeometryCook, FaceMaterialAbsentFromOrderFails)
{
    std::vector<CookFace> faces = { MakeQuadZ(Mat("a")) };
    const std::vector<AssetRef> order = { Mat("b") }; // wrong material
    MeshGeometry mesh;
    std::string error;
    EXPECT_FALSE(BakeBrushFacesToStaticMesh(faces, order, mesh, &error));
    EXPECT_FALSE(error.empty());
}

TEST(BrushGeometryCook, SingleQuadWeldsToFourVerticesSixIndices)
{
    std::vector<CookFace> faces = { MakeQuadZ(Mat("a")) };
    const std::vector<AssetRef> order = CollectMaterialOrder(faces);
    MeshGeometry mesh;
    std::string error;
    ASSERT_TRUE(BakeBrushFacesToStaticMesh(faces, order, mesh, &error)) << error;

    ASSERT_EQ(mesh.Sections.size(), 1u);
    EXPECT_EQ(mesh.Sections[0].MaterialSlot, 0u);
    EXPECT_EQ(mesh.Sections[0].IndexCount, 6u);   // two triangles
    EXPECT_EQ(mesh.Sections[0].VertexCount, 4u);  // six corners welded to four
    EXPECT_EQ(mesh.Vertices.size(), 4u);
    EXPECT_EQ(mesh.Indices.size(), 6u);
}

TEST(BrushGeometryCook, SectionPerMaterialWithSlotsInOrder)
{
    std::vector<CookFace> faces = {
        MakeQuadZ(Mat("asset://materials/a.smat")),
        MakeQuadZ(Mat("asset://materials/b.smat")),
        MakeQuadZ(Mat("asset://materials/a.smat")),
    };
    const std::vector<AssetRef> order = CollectMaterialOrder(faces);
    MeshGeometry mesh;
    std::string error;
    ASSERT_TRUE(BakeBrushFacesToStaticMesh(faces, order, mesh, &error)) << error;

    ASSERT_EQ(mesh.Sections.size(), 2u);
    // Slot 0 = material "a" (first seen), gathering its two quads → 12 indices.
    EXPECT_EQ(mesh.Sections[0].MaterialSlot, 0u);
    EXPECT_EQ(mesh.Sections[0].IndexCount, 12u);
    // Slot 1 = material "b", one quad → 6 indices.
    EXPECT_EQ(mesh.Sections[1].MaterialSlot, 1u);
    EXPECT_EQ(mesh.Sections[1].IndexCount, 6u);

    // Sections partition the shared streams without overlap.
    EXPECT_EQ(mesh.Sections[1].VertexOffset, mesh.Sections[0].VertexCount);
    EXPECT_EQ(mesh.Sections[1].IndexOffset, mesh.Sections[0].IndexCount);
}

TEST(BrushGeometryCook, BakePreservesWindingAndComputesBounds)
{
    std::vector<CookFace> faces = { MakeQuadZ(Mat("a")) };
    const std::vector<AssetRef> order = CollectMaterialOrder(faces);
    MeshGeometry mesh;
    std::string error;
    ASSERT_TRUE(BakeBrushFacesToStaticMesh(faces, order, mesh, &error)) << error;

    // Winding survives the bake: the first triangle's geometric normal faces +Z.
    const Vec3d n = GeometricNormal(mesh.Vertices[mesh.Indices[0]],
                                    mesh.Vertices[mesh.Indices[1]],
                                    mesh.Vertices[mesh.Indices[2]]);
    EXPECT_NEAR(n.Z, 1.0f, 1e-4f);

    // Bounds cover the unit quad in z = 0.
    EXPECT_NEAR(mesh.LocalBounds.Min.X, 0.0f, 1e-5f);
    EXPECT_NEAR(mesh.LocalBounds.Min.Y, 0.0f, 1e-5f);
    EXPECT_NEAR(mesh.LocalBounds.Max.X, 1.0f, 1e-5f);
    EXPECT_NEAR(mesh.LocalBounds.Max.Y, 1.0f, 1e-5f);
}

TEST(BrushGeometryCook, EveryVertexHasUnitHandednessTangent)
{
    std::vector<CookFace> faces = { MakeQuadZ(Mat("a")) };
    const std::vector<AssetRef> order = CollectMaterialOrder(faces);
    MeshGeometry mesh;
    std::string error;
    ASSERT_TRUE(BakeBrushFacesToStaticMesh(faces, order, mesh, &error)) << error;

    for (const StaticMeshVertex& v : mesh.Vertices)
        EXPECT_NEAR(std::abs(v.Tangent.W), 1.0f, 1e-4f);
}

#endif // SENCHA_ENABLE_COOK
