#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace
{
    // A concave closed manifold: an L-shaped hexagonal prism. Vertex rings are
    // geometrically valid polygons; per-face winding direction is intentionally
    // left arbitrary so BrushOrientFacesOutward has something to fix.
    BrushMesh MakeLPrism()
    {
        BrushMesh m;
        m.Vertices = {
            BrushVertex{ { 0, 0, 0 } }, BrushVertex{ { 2, 0, 0 } }, BrushVertex{ { 2, 1, 0 } },
            BrushVertex{ { 1, 1, 0 } }, BrushVertex{ { 1, 2, 0 } }, BrushVertex{ { 0, 2, 0 } },
            BrushVertex{ { 0, 0, 1 } }, BrushVertex{ { 2, 0, 1 } }, BrushVertex{ { 2, 1, 1 } },
            BrushVertex{ { 1, 1, 1 } }, BrushVertex{ { 1, 2, 1 } }, BrushVertex{ { 0, 2, 1 } },
        };
        m.Faces = {
            BrushFace{ { 0, 1, 2, 3, 4, 5 }, {} },      // bottom cap
            BrushFace{ { 6, 7, 8, 9, 10, 11 }, {} },    // top cap
            BrushFace{ { 0, 1, 7, 6 }, {} },
            BrushFace{ { 1, 2, 8, 7 }, {} },
            BrushFace{ { 2, 3, 9, 8 }, {} },
            BrushFace{ { 3, 4, 10, 9 }, {} },
            BrushFace{ { 4, 5, 11, 10 }, {} },
            BrushFace{ { 5, 0, 6, 11 }, {} },
        };
        return m;
    }

    // Every undirected edge is shared by exactly two faces that traverse it in
    // opposite directions: the defining property of a consistently wound mesh.
    bool IsConsistentlyWound(const BrushMesh& mesh)
    {
        std::unordered_map<std::uint64_t, int> net;   // +1/-1 per traversal, must cancel
        std::unordered_map<std::uint64_t, int> count; // must be 2
        for (const BrushFace& face : mesh.Faces)
        {
            const std::size_t n = face.Loop.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                const std::uint32_t a = face.Loop[i];
                const std::uint32_t b = face.Loop[(i + 1) % n];
                const std::uint64_t key = a < b ? (static_cast<std::uint64_t>(a) << 32) | b
                                                : (static_cast<std::uint64_t>(b) << 32) | a;
                net[key] += a < b ? 1 : -1;
                count[key] += 1;
            }
        }
        for (const auto& [key, c] : count)
            if (c != 2)
                return false;
        for (const auto& [key, n] : net)
            if (n != 0)
                return false;
        return true;
    }

    // Six times the enclosed signed volume (divergence theorem); positive == outward.
    double SignedVolumeSixth(const BrushMesh& mesh)
    {
        double volume = 0.0;
        for (const BrushFace& face : mesh.Faces)
        {
            const std::size_t n = face.Loop.size();
            if (n < 3)
                continue;
            const Vec3d p0 = mesh.Vertices[face.Loop[0]].Position;
            for (std::size_t i = 1; i + 1 < n; ++i)
            {
                const Vec3d c = mesh.Vertices[face.Loop[i]].Position.Cross(
                    mesh.Vertices[face.Loop[i + 1]].Position);
                volume += static_cast<double>(p0.X) * c.X
                        + static_cast<double>(p0.Y) * c.Y
                        + static_cast<double>(p0.Z) * c.Z;
            }
        }
        return volume;
    }
}

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

TEST(BrushValidation, OrientFacesOutwardFixesInwardWinding)
{
    // Box with every face wound the wrong way. Repair no longer reorients, so the
    // explicit orient pass is what fixes it.
    BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    for (BrushFace& face : box.Faces)
        std::reverse(face.Loop.begin(), face.Loop.end());

    BrushOrientFacesOutward(box);
    const Vec3d center = BrushMeshCentroid(box);
    for (const BrushFace& face : box.Faces)
        EXPECT_GT(BrushComputeFaceNormal(box, face).Dot(BrushFaceCentroid(box, face) - center), 0.0f);
}

TEST(BrushValidation, OrientFacesOutwardHandlesConcave)
{
    BrushMesh l = MakeLPrism();
    // Scramble: flip a subset of loops so the winding starts inconsistent.
    std::reverse(l.Faces[0].Loop.begin(), l.Faces[0].Loop.end());
    std::reverse(l.Faces[3].Loop.begin(), l.Faces[3].Loop.end());
    std::reverse(l.Faces[6].Loop.begin(), l.Faces[6].Loop.end());

    BrushOrientFacesOutward(l);

    // The convex centroid heuristic could not guarantee these for a reflex corner.
    EXPECT_TRUE(IsConsistentlyWound(l));
    EXPECT_GT(SignedVolumeSixth(l), 0.0); // net outward
    for (const BrushFace& face : l.Faces)
        EXPECT_GT(face.Normal.Dot(BrushComputeFaceNormal(l, face)), 0.99f); // cached matches winding
}

TEST(BrushValidation, RepairLeavesOrientedConcaveWindingAlone)
{
    BrushMesh l = MakeLPrism();
    BrushOrientFacesOutward(l); // now consistent + outward

    const BrushMesh before = l;
    const BrushRepairResult result = BrushValidateAndRepair(l);
    EXPECT_TRUE(result.Ok);
    EXPECT_FALSE(result.Changed); // repair recomputes normals but does not re-wind
    for (std::size_t i = 0; i < l.Faces.size(); ++i)
        EXPECT_EQ(l.Faces[i].Loop, before.Faces[i].Loop);
}

TEST(BrushValidation, ExtrudeOnConcaveBrushKeepsNeighboursConsistent)
{
    BrushMesh l = MakeLPrism();
    BrushOrientFacesOutward(l);

    // Extrude the top cap (+Z). The reported bug had the post-edit repair flip
    // unrelated faces inward (backface culling); repair no longer reorients.
    std::uint32_t topCap = 0;
    for (std::uint32_t i = 0; i < l.Faces.size(); ++i)
        if (BrushComputeFaceNormal(l, l.Faces[i]).Z > 0.9f)
            topCap = i;

    const BrushMesh extruded = BrushOps::ExtrudeFace(l, topCap, 1.0f);
    EXPECT_TRUE(IsConsistentlyWound(extruded));
    EXPECT_GT(SignedVolumeSixth(extruded), 0.0);
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
