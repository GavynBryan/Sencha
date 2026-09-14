#include "viewport/Picking.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

namespace
{
// An L in the z = 0 plane, wound counter-clockwise seen from +Z, with the
// square notch at (1..2, 1..2) outside the face but inside its bounding box.
//
// The loop starts beside the reflex corner because that is what a flush carve
// leaves behind: the neighbour's first vertex is wherever the cut ended, not a
// corner anyone chose. Fanning from there covers part of the notch, which is
// how the old picking reported hits on empty space.
const std::vector<Vec3d> kConcaveFace = {
    Vec3d{ 2, 1, 0 }, Vec3d{ 1, 1, 0 }, Vec3d{ 1, 2, 0 },
    Vec3d{ 0, 2, 0 }, Vec3d{ 0, 0, 0 }, Vec3d{ 2, 0, 0 },
};

Ray3d DownAt(float x, float y)
{
    return Ray3d{ Vec3d{ x, y, 5.0f }, Vec3d{ 0, 0, -1 } };
}
}

TEST(PickingPolygon, ARayThroughAConcaveNotchMisses)
{
    // The bug this replaces: picking fanned the loop from corner zero, so the
    // notch read as solid and clicking empty space selected the face.
    float distance = 0.0f;
    EXPECT_FALSE(IntersectRayFacePolygon(DownAt(1.7f, 1.2f), kConcaveFace, distance));
    EXPECT_FALSE(IntersectRayFacePolygon(DownAt(1.2f, 1.7f), kConcaveFace, distance));
    EXPECT_FALSE(IntersectRayFacePolygon(DownAt(1.9f, 1.9f), kConcaveFace, distance));
}

TEST(PickingPolygon, ARayThroughTheSolidPartHits)
{
    float distance = 0.0f;
    ASSERT_TRUE(IntersectRayFacePolygon(DownAt(0.5f, 0.5f), kConcaveFace, distance));
    EXPECT_NEAR(distance, 5.0f, 1e-4f);

    // Both arms of the L, including the one a fan from corner zero covers only
    // partially.
    EXPECT_TRUE(IntersectRayFacePolygon(DownAt(1.5f, 0.5f), kConcaveFace, distance));
    EXPECT_TRUE(IntersectRayFacePolygon(DownAt(0.5f, 1.5f), kConcaveFace, distance));
}

TEST(PickingPolygon, TheBoundaryCounts)
{
    // A click landing exactly on an edge shared with the neighbouring face
    // should still select something rather than falling through the brush.
    float distance = 0.0f;
    EXPECT_TRUE(IntersectRayFacePolygon(DownAt(1.0f, 0.5f), kConcaveFace, distance));
    EXPECT_TRUE(IntersectRayFacePolygon(DownAt(2.0f, 0.0f), kConcaveFace, distance));
}

TEST(PickingPolygon, OutsideTheFaceEntirelyMisses)
{
    float distance = 0.0f;
    EXPECT_FALSE(IntersectRayFacePolygon(DownAt(3.0f, 3.0f), kConcaveFace, distance));
    EXPECT_FALSE(IntersectRayFacePolygon(DownAt(-0.5f, 1.0f), kConcaveFace, distance));
}

TEST(PickingPolygon, RefusesAGrazingOrBackwardRay)
{
    float distance = 0.0f;
    // In the plane: no single intersection to report.
    EXPECT_FALSE(IntersectRayFacePolygon(Ray3d{ Vec3d{ -1, 0.5f, 0 }, Vec3d{ 1, 0, 0 } },
                                         kConcaveFace, distance));
    // Pointing away from the face.
    EXPECT_FALSE(IntersectRayFacePolygon(Ray3d{ Vec3d{ 0.5f, 0.5f, 5 }, Vec3d{ 0, 0, 1 } },
                                         kConcaveFace, distance));
}

TEST(PickingPolygon, TheAnswerDoesNotDependOnWhichCornerComesFirst)
{
    // A carve renumbers the loops it touches. Picking must not change its mind
    // about a face because of where the loop happens to start.
    std::vector<Vec3d> rotated = kConcaveFace;
    for (std::size_t shift = 0; shift < kConcaveFace.size(); ++shift)
    {
        std::rotate(rotated.begin(), rotated.begin() + 1, rotated.end());
        float distance = 0.0f;
        EXPECT_FALSE(IntersectRayFacePolygon(DownAt(1.7f, 1.2f), rotated, distance))
            << "shift " << shift;
        EXPECT_TRUE(IntersectRayFacePolygon(DownAt(0.5f, 0.5f), rotated, distance)) << "shift " << shift;
    }
}
