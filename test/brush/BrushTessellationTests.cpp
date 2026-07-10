#include "brush/BrushOps.h"
#include "brush/BrushTessellation.h"
#include "brush/FaceMaterial.h"

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

    std::vector<CollectedFace> CollectFace(const BrushMesh& mesh, const Transform3f& transform,
                                           std::uint32_t faceIndex)
    {
        std::vector<CollectedFace> faces;
        BrushTessellateFace(mesh, transform, faceIndex,
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

TEST(BrushTessellate, SingleFaceMatchesWholeMeshFace)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    box.Faces[3].Material.Material = AssetRef{ AssetType::Material, "asset://single.smat" };

    Transform3f transform = Transform3f::Identity();
    transform.Position = Vec3d{ 3.0f, 2.0f, -1.0f };

    const std::vector<CollectedFace> all = Collect(box, transform);
    const std::vector<CollectedFace> one = CollectFace(box, transform, 3);

    ASSERT_EQ(one.size(), 1u);
    ASSERT_EQ(all.size(), 6u);
    EXPECT_EQ(one[0].Material.Material.Path, all[3].Material.Material.Path);
    ASSERT_EQ(one[0].Vertices.size(), all[3].Vertices.size());
    for (std::size_t i = 0; i < one[0].Vertices.size(); ++i)
    {
        EXPECT_NEAR((one[0].Vertices[i].Position - all[3].Vertices[i].Position).Magnitude(), 0.0f, 1e-5f);
        EXPECT_NEAR((one[0].Vertices[i].Normal - all[3].Vertices[i].Normal).Magnitude(), 0.0f, 1e-5f);
        EXPECT_FLOAT_EQ(one[0].Vertices[i].Uv.X, all[3].Vertices[i].Uv.X);
        EXPECT_FLOAT_EQ(one[0].Vertices[i].Uv.Y, all[3].Vertices[i].Uv.Y);
    }
}

TEST(BrushTessellate, SingleFaceInvalidIndexEmitsNothing)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::vector<CollectedFace> one = CollectFace(box, Transform3f::Identity(), 99);
    EXPECT_TRUE(one.empty());
}

TEST(BrushTessellate, ConcaveFaceEarClipsWithFullAreaAndConsistentWinding)
{
    // L-shaped hexagon in the XZ plane, normal +Y. A fan from Loop[0] would
    // sweep outside the L; ear clipping must cover exactly the L's area.
    BrushMesh mesh;
    const Vec3d positions[6] = {
        { 0.0f, 0.0f, 0.0f }, { 4.0f, 0.0f, 0.0f }, { 4.0f, 0.0f, 1.0f },
        { 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 3.0f }, { 0.0f, 0.0f, 3.0f },
    };
    for (const Vec3d& p : positions)
        mesh.Vertices.push_back(BrushVertex{ p });
    BrushFace face;
    face.Loop = { 5, 4, 3, 2, 1, 0 }; // wound so the +Y normal follows the right-hand rule
    face.Normal = { 0.0f, 1.0f, 0.0f };
    mesh.Faces.push_back(face);

    const std::vector<CollectedFace> faces = Collect(mesh, Transform3f::Identity());
    ASSERT_EQ(faces.size(), 1u);
    const std::vector<BrushTriVertex>& tris = faces[0].Vertices;
    ASSERT_EQ(tris.size() % 3, 0u);

    float area = 0.0f;
    for (std::size_t i = 0; i < tris.size(); i += 3)
    {
        const Vec3d cross = (tris[i + 1].Position - tris[i].Position)
                                .Cross(tris[i + 2].Position - tris[i].Position);
        // Consistent winding: every triangle's geometric normal matches the face.
        EXPECT_GT(cross.Dot(face.Normal), 0.0f);
        area += 0.5f * cross.Magnitude();
    }
    EXPECT_NEAR(area, 6.0f, 1e-4f); // 4x1 bar + 1x2 stem
}

TEST(BrushTessellate, CollinearLoopVerticesEmitNoInvertedTriangles)
{
    // A quad with a split vertex mid-edge (the carve's flush-side hexagon
    // shape). All triangles must still point along the face normal.
    BrushMesh mesh;
    const Vec3d positions[5] = {
        { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f },
        { 2.0f, 0.0f, 2.0f }, { 0.0f, 0.0f, 2.0f },
    };
    for (const Vec3d& p : positions)
        mesh.Vertices.push_back(BrushVertex{ p });
    BrushFace face;
    face.Loop = { 4, 3, 2, 1, 0 }; // wound so the +Y normal follows the right-hand rule
    face.Normal = { 0.0f, 1.0f, 0.0f };
    mesh.Faces.push_back(face);

    const std::vector<CollectedFace> faces = Collect(mesh, Transform3f::Identity());
    ASSERT_EQ(faces.size(), 1u);
    const std::vector<BrushTriVertex>& tris = faces[0].Vertices;

    float area = 0.0f;
    for (std::size_t i = 0; i < tris.size(); i += 3)
    {
        const Vec3d cross = (tris[i + 1].Position - tris[i].Position)
                                .Cross(tris[i + 2].Position - tris[i].Position);
        EXPECT_GE(cross.Dot(face.Normal), 0.0f);
        area += 0.5f * cross.Magnitude();
    }
    EXPECT_NEAR(area, 4.0f, 1e-4f);
}

TEST(BrushTessellate, SoftEdgeSharesVertexNormalsAcrossItsFaces)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    // Soften one edge: the two faces meeting there must emit the averaged
    // normal at that edge's vertices, and the hard faces stay flat.
    const std::uint32_t a = box.Faces[0].Loop[0];
    const std::uint32_t b = box.Faces[0].Loop[1];
    BrushSetEdgeSoft(box, a, b, true);

    const Vec3d pa = box.Vertices[a].Position;
    const Vec3d pb = box.Vertices[b].Position;
    bool sawBlended = false;
    for (const CollectedFace& face : Collect(box, Transform3f::Identity()))
        for (const BrushTriVertex& v : face.Vertices)
        {
            if ((v.Position - pa).SqrMagnitude() > 1e-8f
                && (v.Position - pb).SqrMagnitude() > 1e-8f)
                continue;
            const bool axisAligned = std::abs(v.Normal.X) > 0.99f
                                  || std::abs(v.Normal.Y) > 0.99f
                                  || std::abs(v.Normal.Z) > 0.99f;
            if (!axisAligned)
            {
                EXPECT_NEAR(v.Normal.Magnitude(), 1.0f, 1e-4f);
                sawBlended = true;
            }
        }
    EXPECT_TRUE(sawBlended);

    // Hard mesh control: every emitted normal stays axis-aligned.
    const BrushMesh hard = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    for (const CollectedFace& face : Collect(hard, Transform3f::Identity()))
        for (const BrushTriVertex& v : face.Vertices)
            EXPECT_TRUE(std::abs(v.Normal.X) > 0.99f || std::abs(v.Normal.Y) > 0.99f
                        || std::abs(v.Normal.Z) > 0.99f);
}
