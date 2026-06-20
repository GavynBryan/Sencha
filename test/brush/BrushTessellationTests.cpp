#include "level/brush/BrushOps.h"
#include "level/brush/BrushTessellation.h"
#include "level/brush/FaceMaterial.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
    struct CollectedFace
    {
        FaceMaterial Material;
        std::vector<BrushTriVertex> Vertices;
    };

    std::vector<CollectedFace> Collect(const BrushMesh& mesh, const Transform3f& transform)
    {
        std::vector<CollectedFace> faces;
        BrushTessellate(mesh, transform,
            [&](const FaceMaterial& material, std::span<const BrushTriVertex> tris) {
                faces.push_back(CollectedFace{ material, { tris.begin(), tris.end() } });
            });
        return faces;
    }
}

TEST(BrushTessellate, BoxEmitsSixQuadsAsTwelveTriangles)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::vector<CollectedFace> faces = Collect(box, Transform3f::Identity());

    ASSERT_EQ(faces.size(), 6u);
    std::size_t totalVertices = 0;
    for (const CollectedFace& face : faces)
    {
        EXPECT_EQ(face.Vertices.size(), 6u); // a quad -> 2 tris -> 6 verts
        totalVertices += face.Vertices.size();
    }
    EXPECT_EQ(totalVertices, 36u); // 12 triangles
}

TEST(BrushTessellate, NormalsAreUnitAndMatchTheFace)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::vector<CollectedFace> faces = Collect(box, Transform3f::Identity());

    for (std::size_t f = 0; f < faces.size(); ++f)
        for (const BrushTriVertex& v : faces[f].Vertices)
        {
            EXPECT_NEAR(v.Normal.Magnitude(), 1.0f, 1e-5f);
            // Identity transform: world normal == the face's cached normal.
            EXPECT_NEAR((v.Normal - box.Faces[f].Normal).Magnitude(), 0.0f, 1e-5f);
        }
}

TEST(BrushTessellate, UvsComeFromTheProjectionInLocalSpace)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    // Tessellate untransformed and translated; UVs must be identical (projection
    // is evaluated in local space, so they are transform-invariant).
    const std::vector<CollectedFace> at0 = Collect(box, Transform3f::Identity());

    Transform3f moved = Transform3f::Identity();
    moved.Position = Vec3d{ 10.0f, -4.0f, 7.0f };
    const std::vector<CollectedFace> atMoved = Collect(box, moved);

    ASSERT_EQ(at0.size(), atMoved.size());
    for (std::size_t f = 0; f < at0.size(); ++f)
    {
        ASSERT_EQ(at0[f].Vertices.size(), atMoved[f].Vertices.size());
        for (std::size_t i = 0; i < at0[f].Vertices.size(); ++i)
        {
            const BrushTriVertex& a = at0[f].Vertices[i];
            const BrushTriVertex& b = atMoved[f].Vertices[i];
            EXPECT_FLOAT_EQ(a.Uv.X, b.Uv.X);
            EXPECT_FLOAT_EQ(a.Uv.Y, b.Uv.Y);
            // Positions, in contrast, shift by the translation.
            EXPECT_NEAR((b.Position - a.Position - moved.Position).Magnitude(), 0.0f, 1e-4f);
        }
    }
}

TEST(BrushTessellate, MaterialIsCarriedToEachEmittedFace)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    box.Faces[2].Material.Material = AssetRef{ AssetType::Material, "asset://m.smat" };

    const std::vector<CollectedFace> faces = Collect(box, Transform3f::Identity());
    ASSERT_EQ(faces.size(), 6u);
    EXPECT_EQ(faces[2].Material.Material.Path, "asset://m.smat");
    EXPECT_TRUE(faces[0].Material.Material.Path.empty());
}
