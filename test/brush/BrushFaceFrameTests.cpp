#include "brush/BrushFaceFrame.h"
#include "brush/BrushOps.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
constexpr float kPlanarTol = 1e-3f;

// The +X face of a unit box, which every case here carves into.
std::uint32_t FaceFacing(const BrushMesh& mesh, Vec3d direction)
{
    for (std::uint32_t i = 0; i < mesh.Faces.size(); ++i)
        if (BrushComputeFaceNormal(mesh, mesh.Faces[i]).Dot(direction) > 0.99f)
            return i;
    return 0xFFFFFFFFu;
}

void ExpectOrthonormal(const BrushFaceFrame& frame)
{
    EXPECT_NEAR(frame.AxisU.Magnitude(), 1.0f, 1e-5f);
    EXPECT_NEAR(frame.AxisV.Magnitude(), 1.0f, 1e-5f);
    EXPECT_NEAR(frame.AxisU.Dot(frame.AxisV), 0.0f, 1e-5f);
    const Vec3d cross = frame.AxisU.Cross(frame.AxisV);
    EXPECT_NEAR((cross - frame.Normal).Magnitude(), 0.0f, 1e-5f);
}
}

TEST(BrushFaceFrame, AcceptsAnNgonAndRoundTripsThroughItsBasis)
{
    BrushMesh mesh = BrushOps::MakeCylinder(Vec3d{ 1, 1, 1 }, 1, 7);
    const std::uint32_t cap = FaceFacing(mesh, Vec3d{ 0, 1, 0 });
    ASSERT_NE(cap, 0xFFFFFFFFu);
    ASSERT_GT(mesh.Faces[cap].Loop.size(), 4u) << "the cap should be an n-gon";

    const BrushFaceFrameResult result = FaceFrame(mesh, cap, kPlanarTol);
    ASSERT_TRUE(result.Frame.has_value()) << CarveStatusText(result.Status);
    ExpectOrthonormal(*result.Frame);
    ASSERT_EQ(result.Frame->Outline.size(), mesh.Faces[cap].Loop.size());
    for (std::size_t i = 0; i < result.Frame->Outline.size(); ++i)
    {
        const Vec3d world = result.Frame->ToWorld(result.Frame->Outline[i]);
        EXPECT_NEAR((world - mesh.Vertices[mesh.Faces[cap].Loop[i]].Position).Magnitude(), 0.0f, 1e-4f);
    }
}

TEST(BrushFaceFrame, AcceptsAShearedFaceThatTheRectangleGateRefused)
{
    // The second complaint: the old gate wanted opposite edges to cancel and
    // the axes to be square within about a twentieth of a degree, so a face a
    // single degree out of true could not be carved at all.
    BrushMesh mesh = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    const std::uint32_t face = FaceFacing(mesh, Vec3d{ 1, 0, 0 });
    ASSERT_NE(face, 0xFFFFFFFFu);
    const float shear = std::tan(1.0f * std::numbers::pi_v<float> / 180.0f);
    for (std::uint32_t index : mesh.Faces[face].Loop)
        mesh.Vertices[index].Position.Z += mesh.Vertices[index].Position.Y * shear;

    EXPECT_FALSE(BrushOps::RectFaceFrame(mesh, face).has_value()) << "the old gate accepted this after all";
    const BrushFaceFrameResult result = FaceFrame(mesh, face, kPlanarTol);
    EXPECT_TRUE(result.Frame.has_value()) << CarveStatusText(result.Status);
}

TEST(BrushFaceFrame, RefusesAFaceThatIsNotFlat)
{
    BrushMesh mesh = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    const std::uint32_t face = FaceFacing(mesh, Vec3d{ 1, 0, 0 });
    ASSERT_NE(face, 0xFFFFFFFFu);
    mesh.Vertices[mesh.Faces[face].Loop[0]].Position.X += 0.2f;

    const BrushFaceFrameResult result = FaceFrame(mesh, face, kPlanarTol);
    EXPECT_FALSE(result.Frame.has_value());
    EXPECT_EQ(result.Status, CarveStatus::NonPlanarFace);
}

TEST(BrushFaceFrame, TheBasisIgnoresWhichVertexTheLoopStartsAt)
{
    // A flush carve leaves the neighbour's first edge as whatever stub it cut,
    // so a basis read off the loop would rotate the carving lattice once per
    // carve and recreate the flush problem as drift.
    BrushMesh mesh = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    const std::uint32_t face = FaceFacing(mesh, Vec3d{ 1, 0, 0 });
    ASSERT_NE(face, 0xFFFFFFFFu);
    const BrushFaceFrameResult before = FaceFrame(mesh, face, kPlanarTol);
    ASSERT_TRUE(before.Frame.has_value());

    std::rotate(mesh.Faces[face].Loop.begin(), mesh.Faces[face].Loop.begin() + 1,
                mesh.Faces[face].Loop.end());
    const BrushFaceFrameResult after = FaceFrame(mesh, face, kPlanarTol);
    ASSERT_TRUE(after.Frame.has_value());

    EXPECT_NEAR((after.Frame->AxisU - before.Frame->AxisU).Magnitude(), 0.0f, 1e-6f);
    EXPECT_NEAR((after.Frame->AxisV - before.Frame->AxisV).Magnitude(), 0.0f, 1e-6f);
}

TEST(BrushFaceFrame, RotatingTheMaterialDoesNotMoveTheLattice)
{
    // The frame is editor geometry state, not material state. Rotating a
    // texture must not rotate the grid a carve snaps to.
    BrushMesh mesh = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    const std::uint32_t face = FaceFacing(mesh, Vec3d{ 1, 0, 0 });
    ASSERT_NE(face, 0xFFFFFFFFu);
    const BrushFaceFrameResult before = FaceFrame(mesh, face, kPlanarTol);
    ASSERT_TRUE(before.Frame.has_value());

    mesh.Faces[face].Material.Uv.Rotation = 37.0f;
    mesh.Faces[face].Material.Uv.Scale = Vec2d{ 0.25f, 3.0f };
    mesh.Faces[face].Material.Uv.Offset = Vec2d{ 1.5f, -0.5f };
    const BrushFaceFrameResult after = FaceFrame(mesh, face, kPlanarTol);
    ASSERT_TRUE(after.Frame.has_value());

    EXPECT_NEAR((after.Frame->AxisU - before.Frame->AxisU).Magnitude(), 0.0f, 1e-6f);
    EXPECT_NEAR((after.Frame->AxisV - before.Frame->AxisV).Magnitude(), 0.0f, 1e-6f);
}

TEST(BrushFaceFrame, AnAxisAlignedWallGetsAWorldAlignedBasis)
{
    // Why the canonical basis is the world-axis one: on a wall the carve
    // lattice then agrees with the world grid the user is snapping to.
    BrushMesh mesh = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    for (Vec3d direction : { Vec3d{ 1, 0, 0 }, Vec3d{ 0, 1, 0 }, Vec3d{ 0, 0, 1 },
                             Vec3d{ -1, 0, 0 }, Vec3d{ 0, -1, 0 }, Vec3d{ 0, 0, -1 } })
    {
        const std::uint32_t face = FaceFacing(mesh, direction);
        ASSERT_NE(face, 0xFFFFFFFFu);
        const BrushFaceFrameResult result = FaceFrame(mesh, face, kPlanarTol);
        ASSERT_TRUE(result.Frame.has_value());
        ExpectOrthonormal(*result.Frame);
        for (const Vec3d& axis : { result.Frame->AxisU, result.Frame->AxisV })
        {
            const float largest = std::max({ std::abs(axis.X), std::abs(axis.Y), std::abs(axis.Z) });
            EXPECT_NEAR(largest, 1.0f, 1e-5f) << "the basis left the world axes";
        }
    }
}

TEST(BrushFaceFrame, TheOutlineIsCounterClockwiseSeenFromOutside)
{
    BrushMesh mesh = BrushOps::MakeBox(Vec3d{ 1, 2, 3 });
    for (std::uint32_t face = 0; face < mesh.Faces.size(); ++face)
    {
        const BrushFaceFrameResult result = FaceFrame(mesh, face, kPlanarTol);
        ASSERT_TRUE(result.Frame.has_value());
        float area = 0.0f;
        const std::vector<Vec2d>& outline = result.Frame->Outline;
        for (std::size_t i = 0; i < outline.size(); ++i)
        {
            const Vec2d& a = outline[i];
            const Vec2d& b = outline[(i + 1) % outline.size()];
            area += a.X * b.Y - b.X * a.Y;
        }
        EXPECT_GT(area, 0.0f) << "face " << face << " came out wound clockwise";
    }
}

TEST(BrushFaceFrame, EveryWallPutsTheRiseAxisOnWorldUp)
{
    // A shape authored with its rise along +V has to stand upright on all four
    // walls. With the basis read off the least-aligned world axis, only one of
    // them did: the others came out sideways or upside down.
    const BrushMesh box = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    for (Vec3d direction : { Vec3d{ 1, 0, 0 }, Vec3d{ -1, 0, 0 },
                             Vec3d{ 0, 0, 1 }, Vec3d{ 0, 0, -1 } })
    {
        const std::uint32_t face = FaceFacing(box, direction);
        ASSERT_NE(face, 0xFFFFFFFFu);
        const BrushFaceFrameResult result = FaceFrame(box, face, kPlanarTol);
        ASSERT_TRUE(result.Frame.has_value()) << CarveStatusText(result.Status);
        ExpectOrthonormal(*result.Frame);
        EXPECT_GT(result.Frame->AxisV.Dot(Vec3d::Up()), 0.99f)
            << "the wall facing " << direction.X << ", " << direction.Y << ", " << direction.Z;
    }
}

TEST(BrushFaceFrame, AHorizontalFaceKeepsADeterministicFallback)
{
    // A floor has no up inside its own plane, so the fallback's promise is that
    // it is stable and world-aligned, not that it points anywhere in particular.
    BrushMesh box = BrushOps::MakeBox(Vec3d{ 1, 1, 1 });
    for (Vec3d direction : { Vec3d{ 0, 1, 0 }, Vec3d{ 0, -1, 0 } })
    {
        const std::uint32_t face = FaceFacing(box, direction);
        ASSERT_NE(face, 0xFFFFFFFFu);
        const BrushFaceFrameResult first = FaceFrame(box, face, kPlanarTol);
        ASSERT_TRUE(first.Frame.has_value());
        ExpectOrthonormal(*first.Frame);

        const BrushFaceFrameResult again = FaceFrame(box, face, kPlanarTol);
        ASSERT_TRUE(again.Frame.has_value());
        EXPECT_NEAR((again.Frame->AxisU - first.Frame->AxisU).Magnitude(), 0.0f, 1e-6f);
        EXPECT_NEAR((again.Frame->AxisV - first.Frame->AxisV).Magnitude(), 0.0f, 1e-6f);

        BrushMesh rotated = box;
        std::rotate(rotated.Faces[face].Loop.begin(), rotated.Faces[face].Loop.begin() + 1,
                    rotated.Faces[face].Loop.end());
        const BrushFaceFrameResult moved = FaceFrame(rotated, face, kPlanarTol);
        ASSERT_TRUE(moved.Frame.has_value());
        EXPECT_NEAR((moved.Frame->AxisU - first.Frame->AxisU).Magnitude(), 0.0f, 1e-6f);
        EXPECT_NEAR((moved.Frame->AxisV - first.Frame->AxisV).Magnitude(), 0.0f, 1e-6f);

        for (const Vec3d& axis : { first.Frame->AxisU, first.Frame->AxisV })
        {
            const float largest = std::max({ std::abs(axis.X), std::abs(axis.Y), std::abs(axis.Z) });
            EXPECT_NEAR(largest, 1.0f, 1e-5f) << "the fallback left the world axes";
        }
    }
}
