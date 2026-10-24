#include "brush/BrushBoxFit.h"
#include "brush/BrushOps.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <string>
#include <vector>

// The box a volume brush is, fitted exactly or refused with a reason: what a
// sensor cooked from it is built from, so an approximation would be a lie.

namespace
{
std::vector<Vec3d> Positions(const BrushMesh& mesh, const Quatf& turn = Quatf::Identity(),
                             Vec3d offset = Vec3d::Zero(), Vec3d scale = Vec3d(1.0f, 1.0f, 1.0f))
{
    std::vector<Vec3d> out;
    for (const BrushVertex& vertex : mesh.Vertices)
    {
        const Vec3d scaled(vertex.Position.X * scale.X, vertex.Position.Y * scale.Y, vertex.Position.Z * scale.Z);
        out.push_back(turn.RotateVector(scaled) + offset);
    }
    return out;
}

void ExpectNear(Vec3d actual, Vec3d expected)
{
    EXPECT_NEAR(actual.X, expected.X, 1e-4f);
    EXPECT_NEAR(actual.Y, expected.Y, 1e-4f);
    EXPECT_NEAR(actual.Z, expected.Z, 1e-4f);
}
} // namespace

TEST(BrushBoxFit, ASquareBoxOffItsOriginKeepsItsCentreAndNoTurn)
{
    const BrushMesh box = BrushOps::MakeBox(Vec3d(1.0f, 0.5f, 2.0f));
    std::string reason;
    const auto fit = FitOrientedBox(box, Positions(box, Quatf::Identity(), Vec3d(3.0f, 0.5f, -1.0f)), reason);
    ASSERT_TRUE(fit.has_value()) << reason;
    ExpectNear(fit->Center, Vec3d(3.0f, 0.5f, -1.0f));
    ExpectNear(fit->HalfExtents, Vec3d(1.0f, 0.5f, 2.0f));
    EXPECT_NEAR(std::abs(fit->Orientation.W), 1.0f, 1e-5f);
}

TEST(BrushBoxFit, ATurnedBoxIsFittedTurned)
{
    const BrushMesh box = BrushOps::MakeBox(Vec3d(2.0f, 1.0f, 0.25f));
    const Quatf turn = Quatf::FromAxisAngle(Vec3d(0.0f, 1.0f, 0.0f), 0.4f);
    std::string reason;
    const auto fit = FitOrientedBox(box, Positions(box, turn), reason);
    ASSERT_TRUE(fit.has_value()) << reason;
    ExpectNear(fit->Center, Vec3d::Zero());
    // Every corner, put back through the fit, lands on one the brush has.
    for (const Vec3d& corner : Positions(box, turn))
    {
        const Vec3d local = fit->Orientation.Inverse().RotateVector(corner - fit->Center);
        EXPECT_NEAR(std::abs(local.X), fit->HalfExtents.X, 1e-3f);
        EXPECT_NEAR(std::abs(local.Y), fit->HalfExtents.Y, 1e-3f);
        EXPECT_NEAR(std::abs(local.Z), fit->HalfExtents.Z, 1e-3f);
    }
}

TEST(BrushBoxFit, AShapeThatIsNotABoxIsRefusedWithAReason)
{
    std::string reason;
    const BrushMesh cylinder = BrushOps::MakeCylinder(Vec3d(1.0f, 1.0f, 1.0f), 1, 8);
    EXPECT_FALSE(FitOrientedBox(cylinder, Positions(cylinder), reason).has_value());
    EXPECT_FALSE(reason.empty());

    // A box with one corner dragged off: still six quads, no longer square.
    BrushMesh skewed = BrushOps::MakeBox(Vec3d(1.0f, 1.0f, 1.0f));
    std::vector<Vec3d> positions = Positions(skewed);
    positions[0] = positions[0] + Vec3d(0.3f, 0.0f, 0.0f);
    reason.clear();
    EXPECT_FALSE(FitOrientedBox(skewed, positions, reason).has_value());
    EXPECT_FALSE(reason.empty());

    // Scaled unevenly across a turn: a parallelepiped, not a box.
    const BrushMesh box = BrushOps::MakeBox(Vec3d(1.0f, 1.0f, 1.0f));
    std::vector<Vec3d> sheared;
    const Quatf turn = Quatf::FromAxisAngle(Vec3d(0.0f, 1.0f, 0.0f), 0.6f);
    for (const Vec3d& p : Positions(box, turn))
        sheared.push_back(Vec3d(p.X * 3.0f, p.Y, p.Z));
    reason.clear();
    EXPECT_FALSE(FitOrientedBox(box, sheared, reason).has_value());
}
