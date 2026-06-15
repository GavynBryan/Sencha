#include "level/brush/BrushOps.h"
#include "level/brush/BrushValidation.h"

#include <gtest/gtest.h>

#include <algorithm>

TEST(BrushValidation, WeldMergesCoincidentVertices)
{
    BrushMesh mesh;
    // A quad with a duplicated corner (same position as vertex 0).
    mesh.Vertices = {
        BrushVertex{ { 0, 0, 0 } },
        BrushVertex{ { 1, 0, 0 } },
        BrushVertex{ { 1, 1, 0 } },
        BrushVertex{ { 0, 0, 0 } }, // duplicate of vertex 0
    };
    mesh.Faces = { BrushFace{ { 0, 1, 2, 3 }, {} } };

    BrushWeldVertices(mesh);
    EXPECT_EQ(mesh.Vertices.size(), 3u);
    // Loop collapsed the duplicate (3 → triangle).
    EXPECT_EQ(mesh.Faces[0].Loop.size(), 3u);
}

TEST(BrushValidation, RepairOrientsInwardWindingOutward)
{
    // Box with every face wound the wrong way.
    BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    for (BrushFace& face : box.Faces)
        std::reverse(face.Loop.begin(), face.Loop.end());

    const BrushRepairResult result = BrushValidateAndRepair(box);
    EXPECT_TRUE(result.Ok);
    const Vec3d center = BrushMeshCentroid(box);
    for (const BrushFace& face : box.Faces)
        EXPECT_GT(BrushComputeFaceNormal(box, face).Dot(BrushFaceCentroid(box, face) - center), 0.0f);
}

TEST(BrushValidation, RepairIsIdempotent)
{
    BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 }); // already repaired by MakeBox
    const BrushRepairResult second = BrushValidateAndRepair(box);
    EXPECT_TRUE(second.Ok);
    EXPECT_TRUE(second.Closed);
    EXPECT_FALSE(second.Changed);
}

TEST(BrushValidation, RepairDropsUnreferencedVertices)
{
    BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    box.Vertices.push_back(BrushVertex{ { 9, 9, 9 } }); // orphan
    BrushValidateAndRepair(box);
    EXPECT_EQ(box.Vertices.size(), 8u);
}

TEST(BrushValidation, RepairReportsOpenMesh)
{
    BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    box.Faces.pop_back();
    const BrushRepairResult result = BrushValidateAndRepair(box);
    EXPECT_TRUE(result.Ok);       // usable
    EXPECT_FALSE(result.Closed);  // but open
}

TEST(BrushValidation, RepairRejectsEmptyMesh)
{
    BrushMesh empty;
    const BrushRepairResult result = BrushValidateAndRepair(empty);
    EXPECT_FALSE(result.Ok);
}
