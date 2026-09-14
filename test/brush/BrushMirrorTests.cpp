#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "brush/MirrorModifier.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace
{
    bool IsClosed(const BrushMesh& mesh)
    {
        BrushMesh copy = mesh;
        return BrushValidateAndRepair(copy).Closed;
    }

    bool AllNormalsOutward(const BrushMesh& mesh)
    {
        const Vec3d center = BrushMeshCentroid(mesh);
        for (const BrushFace& face : mesh.Faces)
        {
            const Vec3d normal = BrushComputeFaceNormal(mesh, face);
            if (normal.Dot(BrushFaceCentroid(mesh, face) - center) <= 0.0f)
                return false;
        }
        return true;
    }

    void ExpectNear(Vec3d a, Vec3d b, float eps = 1e-5f)
    {
        EXPECT_NEAR(a.X, b.X, eps);
        EXPECT_NEAR(a.Y, b.Y, eps);
        EXPECT_NEAR(a.Z, b.Z, eps);
    }

    // A box off the origin on every axis, so a reflection is visible on each.
    BrushMesh OffsetBox()
    {
        return BrushOps::Translate(BrushOps::MakeBox({ 1.0f, 2.0f, 3.0f }), { 0.5f, 0.25f, 0.75f });
    }

    const Vec3d kAxes[] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
}

TEST(MirrorBrushMesh, EachLocalAxisStaysClosedOutwardWithReflectedBounds)
{
    const BrushMesh box = OffsetBox();
    const Aabb3d before = BrushComputeBounds(box);
    for (int axis = 0; axis < 3; ++axis)
    {
        const BrushMesh mirrored = MirrorBrushMesh(box, Plane::FromNormalAndDistance(kAxes[axis], 0.0f));
        EXPECT_EQ(mirrored.Vertices.size(), box.Vertices.size());
        EXPECT_EQ(mirrored.Faces.size(), box.Faces.size());
        EXPECT_TRUE(IsClosed(mirrored)) << "axis " << axis;
        EXPECT_TRUE(AllNormalsOutward(mirrored)) << "axis " << axis;
        const Aabb3d after = BrushComputeBounds(mirrored);
        for (int c = 0; c < 3; ++c)
        {
            if (c == axis)
            {
                EXPECT_NEAR(after.Min[c], -before.Max[c], 1e-5f);
                EXPECT_NEAR(after.Max[c], -before.Min[c], 1e-5f);
            }
            else
            {
                EXPECT_NEAR(after.Min[c], before.Min[c], 1e-5f);
                EXPECT_NEAR(after.Max[c], before.Max[c], 1e-5f);
            }
        }
    }
}

TEST(MirrorBrushMesh, OffsetPlaneReflectsAboutThatPlane)
{
    const BrushMesh box = OffsetBox(); // x in [-0.5, 1.5]
    const BrushMesh mirrored = MirrorBrushMesh(box, Plane::FromNormalAndDistance({ 1, 0, 0 }, -3.0f)); // x = 3
    const Aabb3d after = BrushComputeBounds(mirrored);
    EXPECT_NEAR(after.Min.X, 4.5f, 1e-5f);
    EXPECT_NEAR(after.Max.X, 6.5f, 1e-5f);
    EXPECT_TRUE(IsClosed(mirrored));
    EXPECT_TRUE(AllNormalsOutward(mirrored));
}

TEST(MirrorBrushMesh, PreservesVertexAndFaceIndicesAndReversesLoops)
{
    const BrushMesh box = OffsetBox();
    const Plane plane = Plane::FromNormalAndDistance({ 0, 0, 1 }, 0.0f);
    const BrushMesh mirrored = MirrorBrushMesh(box, plane);
    for (std::size_t i = 0; i < box.Vertices.size(); ++i)
        ExpectNear(mirrored.Vertices[i].Position, ReflectPoint(plane, box.Vertices[i].Position));
    for (std::size_t i = 0; i < box.Faces.size(); ++i)
    {
        std::vector<std::uint32_t> reversed = box.Faces[i].Loop;
        std::reverse(reversed.begin(), reversed.end());
        EXPECT_EQ(mirrored.Faces[i].Loop, reversed);
        EXPECT_EQ(mirrored.Faces[i].Material.Material.Path, box.Faces[i].Material.Material.Path);
    }
}

TEST(MirrorBrushMesh, KeepsSoftEdgesAndOpenness)
{
    BrushMesh box = OffsetBox();
    BrushSetEdgeSoft(box, box.Faces[0].Loop[0], box.Faces[0].Loop[1], true);
    const BrushMesh open = BrushOps::DeleteFace(box, 5);
    const BrushMesh mirrored = MirrorBrushMesh(open, Plane::FromNormalAndDistance({ 1, 0, 0 }, 0.0f));
    EXPECT_EQ(mirrored.SoftEdges, open.SoftEdges);
    EXPECT_EQ(mirrored.Faces.size(), open.Faces.size());
    EXPECT_FALSE(IsClosed(mirrored));
}

TEST(MirrorBrushMesh, MirroringTwiceAcrossOnePlaneRestoresTheSource)
{
    const BrushMesh box = OffsetBox();
    const Plane plane = Plane::FromNormalAndDistance({ 0, 1, 0 }, 1.5f);
    const BrushMesh twice = MirrorBrushMesh(MirrorBrushMesh(box, plane), plane);
    ASSERT_EQ(twice.Vertices.size(), box.Vertices.size());
    for (std::size_t i = 0; i < box.Vertices.size(); ++i)
        ExpectNear(twice.Vertices[i].Position, box.Vertices[i].Position);
    for (std::size_t i = 0; i < box.Faces.size(); ++i)
    {
        EXPECT_EQ(twice.Faces[i].Loop, box.Faces[i].Loop);
        ExpectNear(twice.Faces[i].Material.Uv.AxisU, box.Faces[i].Material.Uv.AxisU);
        EXPECT_NEAR(twice.Faces[i].Material.Uv.Offset.X, box.Faces[i].Material.Uv.Offset.X, 1e-4f);
    }
}

TEST(MirrorBrushMesh, DegeneratePlaneIsIdentity)
{
    const BrushMesh box = OffsetBox();
    const BrushMesh same = MirrorBrushMesh(box, Plane{ Vec3d{ 0, 0, 0 }, 0.0f });
    for (std::size_t i = 0; i < box.Vertices.size(); ++i)
        ExpectNear(same.Vertices[i].Position, box.Vertices[i].Position);
    EXPECT_EQ(same.Faces[0].Loop, box.Faces[0].Loop);
}
