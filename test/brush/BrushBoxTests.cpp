#include "brush/BrushBox.h"
#include "brush/BrushOps.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
void ExpectExactBox(const BrushMesh& mesh, const Transform3f& transform)
{
    std::string error = "previous error";
    const auto box = ExtractBrushBox(mesh, transform, error);
    ASSERT_TRUE(box.has_value()) << error;
    EXPECT_TRUE(error.empty());
    for (const BrushVertex& vertex : mesh.Vertices)
    {
        const Vec3d local = box->Rotation.Conjugate().RotateVector(
            transform.TransformPoint(vertex.Position) - box->Center);
        for (int axis = 0; axis < 3; ++axis)
            EXPECT_NEAR(std::abs(local[axis]), box->HalfExtents[axis], 2e-4f);
    }
}

void ExpectRejected(const BrushMesh& mesh, std::string_view diagnostic)
{
    std::string error;
    EXPECT_FALSE(ExtractBrushBox(mesh, Transform3f::Identity(), error).has_value());
    EXPECT_NE(error.find(diagnostic), std::string::npos) << error;
}
}

TEST(BrushBox, PreservesOffsetDimensionsAndArbitraryEntityRotation)
{
    const BrushMesh mesh = BrushOps::Translate(BrushOps::MakeBox({ 2, 3, 4 }), { 7, -3, 5 });
    const Transform3f transform{ { 13, 8, -21 },
        Quatf::FromAxisAngle(Vec3d{ 1, 2, 3 }.Normalized(), 1.3f), { 2, 3, 0.5f } };
    ExpectExactBox(mesh, transform);
    std::string error;
    const auto box = ExtractBrushBox(mesh, transform, error);
    ASSERT_TRUE(box.has_value());
    EXPECT_NEAR((box->Center - transform.TransformPoint({ 7, -3, 5 })).Magnitude(), 0, 1e-4f);
}

TEST(BrushBox, RecognizesRotatedLocalGeometryAndReflections)
{
    BrushMesh mesh = BrushOps::MakeBox({ 2, 3, 4 });
    const Quatf rotation = Quatf::FromAxisAngle(Vec3d{ 2, 1, -3 }.Normalized(), 0.7f);
    for (BrushVertex& vertex : mesh.Vertices)
        vertex.Position = rotation.RotateVector(vertex.Position) + Vec3d{ 4, -8, 6 };
    ExpectExactBox(mesh, Transform3f::Identity());
    ExpectExactBox(mesh, Transform3f{ {}, Quatf::Identity(), { -2, -2, -2 } });
}

TEST(BrushBox, RejectsShearedGeometryInsteadOfApproximatingBounds)
{
    BrushMesh mesh = BrushOps::MakeBox({ 2, 3, 4 });
    for (BrushVertex& vertex : mesh.Vertices)
        vertex.Position.X += vertex.Position.Y * 0.25f;
    ExpectRejected(mesh, "box");
}

TEST(BrushBox, RejectsNonUniformScalingOfRotatedLocalGeometry)
{
    BrushMesh mesh = BrushOps::MakeBox({ 2, 3, 4 });
    const Quatf rotation = Quatf::FromAxisAngle(Vec3d::Up(), 0.7f);
    for (BrushVertex& vertex : mesh.Vertices)
        vertex.Position = rotation.RotateVector(vertex.Position);
    std::string error;
    EXPECT_FALSE(ExtractBrushBox(mesh, Transform3f{ {}, Quatf::Identity(), { 1, 1, 2 } }, error));
    EXPECT_FALSE(error.empty());
}

TEST(BrushBox, RejectsOpenAndUnsupportedShapes)
{
    BrushMesh mesh = BrushOps::MakeBox({ 2, 3, 4 });
    mesh.Faces.pop_back();
    ExpectRejected(mesh, "closed box");
    ExpectRejected(BrushOps::MakeCylinder({ 2, 3, 4 }, 1, 12), "closed box");
    ExpectRejected(BrushOps::MakePlane({ 2, 3, 4 }, 1), "closed box");
}

TEST(BrushBox, RejectsMissingFacesAndCrossedFaceLoops)
{
    BrushMesh mesh = BrushOps::MakeBox({ 2, 3, 4 });
    mesh.Faces.back() = mesh.Faces.front();
    ExpectRejected(mesh, "not closed");
    mesh = BrushOps::MakeBox({ 2, 3, 4 });
    std::swap(mesh.Faces.back().Loop[1], mesh.Faces.back().Loop[2]);
    ExpectRejected(mesh, "adjacent corners");
}

TEST(BrushBox, RejectsInvalidIndicesDegeneracyAndNonFiniteVertices)
{
    BrushMesh mesh = BrushOps::MakeBox({ 2, 3, 4 });
    mesh.Faces.front().Loop.front() = 8;
    ExpectRejected(mesh, "vertex index");
    mesh = BrushOps::MakeBox({ 2, 3, 4 });
    for (BrushVertex& vertex : mesh.Vertices)
        vertex.Position.X = 0;
    ExpectRejected(mesh, "dimensions");
    mesh = BrushOps::MakeBox({ 2, 3, 4 });
    mesh.Vertices.back().Position.X = std::numeric_limits<float>::infinity();
    ExpectRejected(mesh, "non-finite");
}

TEST(BrushBox, RejectsMovedCornerRatherThanTakingAnAabb)
{
    BrushMesh mesh = BrushOps::MakeBox({ 2, 3, 4 });
    mesh.Vertices.back().Position.X += 0.2f;
    ExpectRejected(mesh, "box");
}

TEST(BrushBox, SupportsThinBoxesWithoutMergingTheirCorners)
{
    ExpectExactBox(BrushOps::MakeBox({ 0.0001f, 10, 100 }), Transform3f::Identity());
}

TEST(BrushBox, MatrixCompositionRejectsShearInsteadOfDroppingItDuringTrsComposition)
{
    const Transform3f scale{ {}, Quatf::Identity(), { 2, 1, 1 } };
    const Transform3f placement{ {}, Quatf::FromAxisAngle(Vec3d::Up(), 0.7f), Vec3d::One() };
    std::string error;
    EXPECT_FALSE(ExtractBrushBox(BrushOps::MakeBox({ 1, 1, 1 }), scale.ToMat4() * placement.ToMat4(), error));
    EXPECT_FALSE(error.empty());
}
