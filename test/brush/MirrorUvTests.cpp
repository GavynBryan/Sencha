#include "brush/BrushOps.h"
#include "brush/FaceMaterial.h"
#include "brush/MirrorModifier.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

// The product rule under test: a mirrored face shows its source's texture
// image un-reflected, seen from its own front, with the same up direction.

namespace
{
    std::vector<Vec3d> LoopPositions(const BrushMesh& mesh, const BrushFace& face)
    {
        std::vector<Vec3d> out;
        for (const std::uint32_t i : face.Loop)
            out.push_back(mesh.Vertices[i].Position);
        return out;
    }

    // Shoelace area of the face's UV outline walked in loop order (outward
    // CCW). Its sign is the image's handedness as seen from the front: equal
    // signs mean the texture is not reflected.
    float SignedUvArea(const BrushMesh& mesh, const BrushFace& face)
    {
        std::vector<Vec2d> uv;
        for (const Vec3d p : LoopPositions(mesh, face))
            uv.push_back(ProjectUv(face.Material.Uv, p));
        float area = 0.0f;
        for (std::size_t i = 0; i < uv.size(); ++i)
        {
            const Vec2d a = uv[i];
            const Vec2d b = uv[(i + 1) % uv.size()];
            area += a.X * b.Y - b.X * a.Y;
        }
        return area * 0.5f;
    }

    struct UvBounds
    {
        Vec2d Min{ 1e9f, 1e9f };
        Vec2d Max{ -1e9f, -1e9f };
    };

    UvBounds BoundsOf(const BrushMesh& mesh, const BrushFace& face)
    {
        UvBounds b;
        for (const Vec3d p : LoopPositions(mesh, face))
        {
            const Vec2d uv = ProjectUv(face.Material.Uv, p);
            b.Min.X = std::min(b.Min.X, uv.X);
            b.Min.Y = std::min(b.Min.Y, uv.Y);
            b.Max.X = std::max(b.Max.X, uv.X);
            b.Max.Y = std::max(b.Max.Y, uv.Y);
        }
        return b;
    }

    std::uint32_t FaceFacing(const BrushMesh& mesh, Vec3d direction)
    {
        for (std::uint32_t i = 0; i < mesh.Faces.size(); ++i)
            if (mesh.Faces[i].Normal.Dot(direction) > 0.9f)
                return i;
        ADD_FAILURE() << "no face facing the requested direction";
        return 0;
    }

    const Vec3d kPlanes[] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0.7071f, 0, 0.7071f } };

    void ExpectHandednessAndUpPreserved(const BrushMesh& source)
    {
        for (const Vec3d normal : kPlanes)
        {
            const BrushMesh mirrored = MirrorBrushMesh(source, Plane::FromNormalAndDistance(normal, 0.0f));
            ASSERT_EQ(mirrored.Faces.size(), source.Faces.size());
            for (std::size_t i = 0; i < source.Faces.size(); ++i)
            {
                const float before = SignedUvArea(source, source.Faces[i]);
                const float after = SignedUvArea(mirrored, mirrored.Faces[i]);
                ASSERT_NE(before, 0.0f);
                EXPECT_GT(before * after, 0.0f) << "face " << i << " reflected across ("
                                                << normal.X << "," << normal.Y << "," << normal.Z << ")";
                const UvProjection a = UvProjectionFoldRotation(source.Faces[i].Material.Uv);
                const UvProjection b = UvProjectionFoldRotation(mirrored.Faces[i].Material.Uv);
                EXPECT_NEAR(a.AxisV.X, b.AxisV.X, 1e-5f);
                EXPECT_NEAR(a.AxisV.Y, b.AxisV.Y, 1e-5f);
                EXPECT_NEAR(a.AxisV.Z, b.AxisV.Z, 1e-5f);
            }
        }
    }
}

TEST(MirrorUv, NoFaceIsEverReflectedAndUpIsKept)
{
    ExpectHandednessAndUpPreserved(BrushOps::MakeBox({ 1.0f, 1.5f, 2.0f }));
}

TEST(MirrorUv, RotatedProjectionsObeyTheSameInvariants)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.5f, 2.0f });
    for (BrushFace& face : box.Faces)
    {
        face.Material.Uv.Rotation = 30.0f;
        face.Material.Uv.Scale = { 1.5f, 0.75f };
        face.Material.Uv.Offset = { 0.3f, -0.2f };
    }
    ExpectHandednessAndUpPreserved(box);
}

TEST(MirrorUv, FittedWallStaysFittedAndReadsTheSameWay)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 2.0f });
    const std::uint32_t wall = FaceFacing(box, { 1, 0, 0 });
    BrushFace& face = box.Faces[wall];
    face.Material.Uv = UvProjectionFit(face.Material.Uv, LoopPositions(box, face));

    const BrushMesh mirrored = MirrorBrushMesh(box, Plane::FromNormalAndDistance({ 1, 0, 0 }, 0.0f));
    const BrushFace& copy = mirrored.Faces[wall];
    EXPECT_LT(copy.Normal.X, -0.9f);

    const UvBounds b = BoundsOf(mirrored, copy);
    EXPECT_NEAR(b.Min.X, 0.0f, 1e-4f);
    EXPECT_NEAR(b.Max.X, 1.0f, 1e-4f);
    EXPECT_NEAR(b.Min.Y, 0.0f, 1e-4f);
    EXPECT_NEAR(b.Max.Y, 1.0f, 1e-4f);

    // U grows toward the viewer's right on both faces: right = forward x up
    // for a viewer facing the face with world up.
    const Vec3d up{ 0, 1, 0 };
    const Vec3d sourceRight = (-face.Normal).Cross(up);
    const Vec3d copyRight = (-copy.Normal).Cross(up);
    const float sourceReads = UvProjectionFoldRotation(face.Material.Uv).AxisU.Dot(sourceRight);
    const float copyReads = UvProjectionFoldRotation(copy.Material.Uv).AxisU.Dot(copyRight);
    EXPECT_GT(sourceReads * copyReads, 0.0f);
}

TEST(MirrorUv, FloorTouchingThePlaneIsSeamlessAcrossIt)
{
    const BrushMesh box = BrushOps::Translate(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }), { 1.0f, 0, 0 }); // x in [0,2]
    const std::uint32_t floor = FaceFacing(box, { 0, -1, 0 });
    const BrushMesh mirrored = MirrorBrushMesh(box, Plane::FromNormalAndDistance({ 1, 0, 0 }, 0.0f));
    int seamVertices = 0;
    for (const std::uint32_t i : box.Faces[floor].Loop)
    {
        const Vec3d p = box.Vertices[i].Position;
        if (std::abs(p.X) > 1e-5f)
            continue;
        ++seamVertices;
        const Vec2d a = ProjectUv(box.Faces[floor].Material.Uv, p);
        const Vec2d b = ProjectUv(mirrored.Faces[floor].Material.Uv, mirrored.Vertices[i].Position);
        EXPECT_NEAR(a.X, b.X, 1e-5f);
        EXPECT_NEAR(a.Y, b.Y, 1e-5f);
    }
    EXPECT_EQ(seamVertices, 2);
}

TEST(MirrorUv, CrossingFacesKeepTheirProjectionUntouched)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::uint32_t floor = FaceFacing(box, { 0, -1, 0 });
    const BrushMesh mirrored = MirrorBrushMesh(box, Plane::FromNormalAndDistance({ 1, 0, 0 }, 0.0f));
    const UvProjection& a = box.Faces[floor].Material.Uv;
    const UvProjection& b = mirrored.Faces[floor].Material.Uv;
    EXPECT_EQ(a.AxisU.X, b.AxisU.X);
    EXPECT_EQ(a.Offset.X, b.Offset.X);
    EXPECT_EQ(a.Rotation, b.Rotation);
}
