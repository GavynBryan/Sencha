// The exact kernel input captured from a refused clip in Kyusu, replayed
// unchanged: a 5 x 5 x 5 block with an arch niche one unit deep on its +Z
// face, open along the doorway's floor edge, and a plane through the crown.
#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace
{
// kyusu clip capture: mode=2 (Split) capped=1
// front kernel refused | back kernel refused | original closed=0
// world plane normal={ -1, 0, 0 } D=0.5; transform position={ 0.5, 0.5, -12.5 }, identity rotation, unit scale
const Plane kCapturedPlane{ { -1.000000000e+00f, -0.000000000e+00f, -0.000000000e+00f }, -0.000000000e+00f };
const std::vector<Vec3d> kCapturedVertices = {
    { -2.500000000e+00f, -5.000000000e-01f, -2.500000000e+00f },
    { 2.500000000e+00f, -5.000000000e-01f, -2.500000000e+00f },
    { 2.500000000e+00f, 4.500000000e+00f, -2.500000000e+00f },
    { -2.500000000e+00f, 4.500000000e+00f, -2.500000000e+00f },
    { -2.500000000e+00f, -5.000000000e-01f, 2.500000000e+00f },
    { 2.500000000e+00f, -5.000000000e-01f, 2.500000000e+00f },
    { 2.500000000e+00f, 4.500000000e+00f, 2.500000000e+00f },
    { -2.500000000e+00f, 4.500000000e+00f, 2.500000000e+00f },
    { -1.500000000e+00f, -5.000000000e-01f, 2.500000000e+00f },
    { 1.500000000e+00f, -5.000000000e-01f, 2.500000000e+00f },
    { 1.500000000e+00f, 1.000000000e+00f, 2.500000000e+00f },
    { 1.385819197e+00f, 1.574025154e+00f, 2.500000000e+00f },
    { 1.060660124e+00f, 2.060660124e+00f, 2.500000000e+00f },
    { 5.740251541e-01f, 2.385819197e+00f, 2.500000000e+00f },
    { 0.000000000e+00f, 2.500000000e+00f, 2.500000000e+00f },
    { -5.740252733e-01f, 2.385819197e+00f, 2.500000000e+00f },
    { -1.060660124e+00f, 2.060660124e+00f, 2.500000000e+00f },
    { -1.385819435e+00f, 1.574024916e+00f, 2.500000000e+00f },
    { -1.500000000e+00f, 9.999998808e-01f, 2.500000000e+00f },
    { -1.500000000e+00f, -5.000000000e-01f, 1.500000000e+00f },
    { 1.500000000e+00f, -5.000000000e-01f, 1.500000000e+00f },
    { 1.500000000e+00f, 1.000000000e+00f, 1.500000000e+00f },
    { 1.385819197e+00f, 1.574025154e+00f, 1.500000000e+00f },
    { 1.060660124e+00f, 2.060660124e+00f, 1.500000000e+00f },
    { 5.740251541e-01f, 2.385819197e+00f, 1.500000000e+00f },
    { 0.000000000e+00f, 2.500000000e+00f, 1.500000000e+00f },
    { -5.740252733e-01f, 2.385819197e+00f, 1.500000000e+00f },
    { -1.060660124e+00f, 2.060660124e+00f, 1.500000000e+00f },
    { -1.385819435e+00f, 1.574024916e+00f, 1.500000000e+00f },
    { -1.500000000e+00f, 9.999998808e-01f, 1.500000000e+00f },
};
const std::vector<std::vector<std::uint32_t>> kCapturedFaces = {
    { 3, 2, 1, 0 },
    { 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29 },
    { 7, 6, 2, 3 },
    { 4, 7, 3, 0 },
    { 1, 2, 6, 5 },
    { 4, 8, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 5, 6, 7 },
    { 9, 10, 21, 20 },
    { 10, 11, 22, 21 },
    { 11, 12, 23, 22 },
    { 12, 13, 24, 23 },
    { 13, 14, 25, 24 },
    { 14, 15, 26, 25 },
    { 15, 16, 27, 26 },
    { 16, 17, 28, 27 },
    { 17, 18, 29, 28 },
    { 18, 8, 19, 29 },
};

BrushMesh Captured()
{
    BrushMesh mesh;
    for (const Vec3d& p : kCapturedVertices)
        mesh.Vertices.push_back(BrushVertex{ p });
    for (const std::vector<std::uint32_t>& loop : kCapturedFaces)
    {
        BrushFace face;
        face.Loop = loop;
        face.Normal = BrushComputeFaceNormal(mesh, face);
        mesh.Faces.push_back(std::move(face));
    }
    return mesh;
}
}

namespace
{
using Edge = std::pair<std::uint32_t, std::uint32_t>;

// Edges by how many faces use them.
std::map<Edge, int> EdgeUses(const BrushMesh& mesh)
{
    std::map<Edge, int> uses;
    for (const BrushFace& face : mesh.Faces)
        for (std::size_t i = 0; i < face.Loop.size(); ++i)
        {
            const std::uint32_t a = face.Loop[i], b = face.Loop[(i + 1) % face.Loop.size()];
            ++uses[{ std::min(a, b), std::max(a, b) }];
        }
    return uses;
}

// The boundary edges as position pairs, so meshes with different numbering compare.
std::vector<std::pair<Vec3d, Vec3d>> BoundaryEdges(const BrushMesh& mesh)
{
    std::vector<std::pair<Vec3d, Vec3d>> edges;
    for (const auto& [edge, uses] : EdgeUses(mesh))
        if (uses == 1)
            edges.emplace_back(mesh.Vertices[edge.first].Position, mesh.Vertices[edge.second].Position);
    return edges;
}

// A usable manifold with boundary: the repair had nothing to do, every face is
// a polygon, and no edge is used by more than two faces.
void ExpectManifoldWithBoundary(const BrushMesh& mesh, const char* label)
{
    BrushMesh copy = mesh;
    const BrushRepairResult report = BrushValidateAndRepair(copy);
    EXPECT_TRUE(report.Ok) << label;
    EXPECT_FALSE(report.Changed) << label;
    for (const BrushFace& face : mesh.Faces)
        EXPECT_GE(face.Loop.size(), 3u) << label;
    for (const auto& [edge, uses] : EdgeUses(mesh))
        EXPECT_LE(uses, 2) << label << ": a non-manifold edge";
}
}

TEST(ClipCaptured, CarvedDoorwayRegression)
{
    const BrushMesh captured = Captured();
    BrushMesh check = captured;
    const BrushRepairResult original = BrushValidateAndRepair(check);
    EXPECT_TRUE(original.Ok);
    EXPECT_FALSE(original.Closed);

    // The source is open along exactly the boundary of its missing bottom: a
    // U around the doorway's floor, eight edges, all at y = -0.5.
    const std::vector<std::pair<Vec3d, Vec3d>> boundary = BoundaryEdges(captured);
    ASSERT_EQ(boundary.size(), 8u);
    for (const auto& [a, b] : boundary)
    {
        EXPECT_NEAR(a.Y, -0.5f, 1e-6f);
        EXPECT_NEAR(b.Y, -0.5f, 1e-6f);
    }

    // The x = 0 section is one chain from the niche's floor edge to the
    // block's back bottom edge -- both ends on that boundary -- so neither
    // half is capped there and both stay open exactly as the source was.
    const auto cutEdges = [](const BrushMesh& half) {
        std::vector<std::pair<Vec3d, Vec3d>> cut;
        for (const auto& [a, b] : BoundaryEdges(half))
            if (std::abs(a.X) < 1e-5f && std::abs(b.X) < 1e-5f)
                cut.push_back({ a, b });
        return cut;
    };
    for (const bool keep : { true, false })
    {
        const char* label = keep ? "front" : "back";
        const BrushMesh half = BrushOps::Clip(captured, kCapturedPlane, keep);
        ASSERT_FALSE(half.Faces.empty()) << label;
        ExpectManifoldWithBoundary(half, label);
        BrushMesh halfCheck = half;
        EXPECT_FALSE(BrushValidateAndRepair(halfCheck).Closed) << label << ": an open source stays open";

        // Every open edge of the half is either the source's own boundary
        // (y = -0.5) or the uncapped chain in the plane.
        int chainEdges = 0;
        std::map<std::pair<int, int>, int> chainEnds; // coarse position key -> degree along the chain
        for (const auto& [a, b] : BoundaryEdges(half))
        {
            const bool onSourceBoundary = std::abs(a.Y + 0.5f) < 1e-5f && std::abs(b.Y + 0.5f) < 1e-5f;
            const bool onCut = std::abs(a.X) < 1e-5f && std::abs(b.X) < 1e-5f;
            EXPECT_TRUE(onSourceBoundary || onCut) << label << ": an open edge that is neither the source's nor the cut's";
            if (onCut && !onSourceBoundary)
            {
                ++chainEdges;
                for (const Vec3d& v : { a, b })
                    ++chainEnds[{ static_cast<int>(std::lround(v.Y * 1000.0f)), static_cast<int>(std::lround(v.Z * 1000.0f)) }];
            }
        }
        EXPECT_GE(chainEdges, 4) << label << ": top, front, crown edge, niche back, back face";
        // One chain: exactly two points of the cut used once, both at y = -0.5.
        int ends = 0;
        for (const auto& [key, degree] : chainEnds)
            if (degree == 1)
            {
                ++ends;
                EXPECT_EQ(key.first, -500) << label << ": a chain end off the source boundary";
            }
        EXPECT_EQ(ends, 2) << label;
        EXPECT_EQ(cutEdges(half).size(), static_cast<std::size_t>(chainEdges)) << label;
    }
    // Split is both keeps; a second call is the same computation, so the two
    // results above are its halves.
}

TEST(ClipCaptured, AnOpenBoxClipsAcrossItsOpening)
{
    // A box with its top deleted, cut across the opening: general support for
    // a manifold with boundary, not a doorway special case. Each half keeps
    // the opening it had and takes an uncapped cut whose chain ends on it.
    BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    std::uint32_t top = 0;
    for (std::uint32_t f = 0; f < box.Faces.size(); ++f)
        if (BrushComputeFaceNormal(box, box.Faces[f]).Y > 0.99f)
            top = f;
    const BrushMesh open = BrushOps::DeleteFace(box, top);
    ASSERT_EQ(BoundaryEdges(open).size(), 4u);
    const Plane plane = Plane::FromNormalAndPoint({ 1, 0, 0 }, { 0, 0, 0 });
    for (const bool keep : { true, false })
    {
        const char* label = keep ? "front" : "back";
        const BrushMesh half = BrushOps::Clip(open, plane, keep);
        ASSERT_FALSE(half.Faces.empty()) << label;
        ExpectManifoldWithBoundary(half, label);
        EXPECT_EQ(half.Faces.size(), 4u) << label << ": bottom, three sides, no cap";
        for (const auto& [a, b] : BoundaryEdges(half))
        {
            const bool onOpening = std::abs(a.Y - 1.0f) < 1e-5f && std::abs(b.Y - 1.0f) < 1e-5f;
            const bool onCut = std::abs(a.X) < 1e-5f && std::abs(b.X) < 1e-5f;
            EXPECT_TRUE(onOpening || onCut) << label;
        }
    }
    // The closed box with the same cut is capped as ever.
    const BrushMesh whole = BrushOps::Clip(box, plane, true);
    BrushMesh wholeCheck = whole;
    EXPECT_TRUE(BrushValidateAndRepair(wholeCheck).Closed);
    EXPECT_EQ(whole.Faces.size(), 6u);
}

TEST(ClipCaptured, ASectionEndNoBoundaryExplainsIsRefused)
{
    // The section graph's own rule, at the seam the kernel uses: a cycle and a
    // chain whose ends are on the boundary classify; the same chain with one
    // end unexplained, or a vertex of degree three, refuse.
    using Seg = std::pair<std::uint32_t, std::uint32_t>;
    const std::vector<Seg> cycleAndChain{ { 0, 1 }, { 1, 2 }, { 2, 0 }, { 3, 4 }, { 4, 5 } };
    const std::vector<std::uint8_t> bothEnds{ 0, 0, 0, 1, 0, 1 };
    const std::optional<BrushOps::SectionComponents> ok = BrushOps::ClassifySection(6, cycleAndChain, bothEnds);
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok->Cycles.size(), 1u);
    EXPECT_EQ(ok->Chains.size(), 1u);
    EXPECT_EQ(ok->Chains.front().size(), 3u);

    const std::vector<std::uint8_t> oneEnd{ 0, 0, 0, 1, 0, 0 };
    EXPECT_FALSE(BrushOps::ClassifySection(6, cycleAndChain, oneEnd).has_value())
        << "a chain ending in the middle of closed surface";

    const std::vector<Seg> junction{ { 0, 1 }, { 1, 2 }, { 2, 0 }, { 1, 3 } };
    const std::vector<std::uint8_t> anchored{ 0, 1, 0, 1 };
    EXPECT_FALSE(BrushOps::ClassifySection(4, junction, anchored).has_value()) << "degree three";

    const std::vector<Seg> closedOnly{ { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } };
    const std::vector<std::uint8_t> none(4, 0);
    const std::optional<BrushOps::SectionComponents> cycle = BrushOps::ClassifySection(4, closedOnly, none);
    ASSERT_TRUE(cycle.has_value());
    EXPECT_EQ(cycle->Cycles.size(), 1u);
    EXPECT_TRUE(cycle->Chains.empty());
}
